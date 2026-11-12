#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the macOS release: PokopiaLauncher.app with the payload make-payload.sh staged and the runtime dylib, signed, and one
# zip that serves both a fresh install (drag the app) and the in-place updater (its Contents/MacOS tree). The package ships no
# game code: Set up lifts and compiles pkp from the player's own dump.
#
#   launcher/scripts/make-mac.sh <play folder with PokopiaLauncher> <payload dir> [version]
#
# Needs a production launcher build (npm run tauri build) and the runtime dylib (PKP_RT_DLL=ON, see docs/macos-port.md).
set -eu
here=$(cd "${1:?usage: make-mac.sh <play folder> <payload dir> [version]}" && pwd)
payload=$(cd "${2:?usage: make-mac.sh <play folder> <payload dir> [version]}" && pwd)
repo=$(cd "$(dirname "$0")/../.." && pwd)
# the release version: the launcher's manifest, unless the caller names one (the release workflow passes the tag's)
version=${3:-$(sed -nE 's/^[[:space:]]*"version": "([^"]+)".*/\1/p' "$repo/launcher/src-tauri/tauri.conf.json" | head -1)}
app="$here/PokopiaLauncher.app"
zip="$here/Recompia-$version-macos-arm64.zip"

[ -x "$here/PokopiaLauncher" ] || { echo "no launcher binary at $here/PokopiaLauncher (npm run tauri build)" >&2; exit 1; }
[ -d "$payload/tools/build" ] || { echo "no payload at $payload (launcher/scripts/make-payload.sh)" >&2; exit 1; }
[ -f "$repo/work/build/rtb/libpkp_rt.dylib" ] || { echo "no runtime dylib at work/build/rtb (cmake --build --target pkp_rt)" >&2; exit 1; }

"$repo/launcher/scripts/make-app.sh" "$here" "$version"

# The bundle carries everything the player needs: the payload the Linux runner staged, and the runtime dylib twice (the
# executable's rpath points at Contents/Frameworks, and Set up copies tools/build/sdk/libpkp_rt.dylib into the game folder).
res="$app/Contents/MacOS"   # the launcher looks for tools/ next to itself
mkdir -p "$app/Contents/Frameworks"
cp -R "$payload/." "$res/"
cp "$repo/work/build/rtb/libpkp_rt.dylib" "$app/Contents/Frameworks/libpkp_rt.dylib"
cp "$repo/work/build/rtb/libpkp_rt.dylib" "$res/tools/build/sdk/"

# The dylib is loaded from Contents/Frameworks, so the executable looks there first.
install_name_tool -add_rpath "@executable_path/../Frameworks" "$app/Contents/MacOS/PokopiaLauncher" 2>/dev/null || true
codesign --force --deep --sign - "$app" 2>/dev/null || echo "warning: codesign failed; the bundle still runs locally" >&2

# Developer ID and notarization, when the caller has them (the release workflow imports the certificate from secrets):
# CODESIGN_IDENTITY names the Developer ID Application certificate, NOTARY_PROFILE a notarytool keychain profile. Without them
# the bundle keeps the ad-hoc signature, which macOS blocks on the first launch (the note in the zip tells the player how to
# allow it).
if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    echo "signing with $CODESIGN_IDENTITY"
    # nested code first (the harness is a self-contained .NET publish with its own dylibs), then the bundle, with the hardened
    # runtime and a timestamp, which notarization requires
    find "$app" -type f \( -perm -111 -o -name '*.dylib' \) | while read -r f; do
        file "$f" | grep -q 'Mach-O' || continue
        codesign --force --options runtime --timestamp --sign "$CODESIGN_IDENTITY" "$f" >/dev/null 2>&1 || echo "warning: cannot sign $f" >&2
    done
    codesign --force --options runtime --timestamp --sign "$CODESIGN_IDENTITY" "$app"
    codesign --verify --deep --strict "$app"
fi

# Notarize and staple the bundle: then the app opens with a plain double click. xcrun notarytool needs an Apple Developer
# account (a Developer ID Application certificate and an App Store Connect API key or an app-specific password). Stapling
# writes into the bundle, so this runs before the zip is made.
if [ -n "${NOTARY_PROFILE:-}" ]; then
    echo "notarizing $app"
    submit=$(mktemp -d)
    ditto -c -k --keepParent "$app" "$submit/notarize.zip"
    xcrun notarytool submit "$submit/notarize.zip" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$app"
    xcrun stapler validate "$app"
    rm -rf "$submit"
fi

# One artifact for both paths: the bundle at the root (a fresh install drags it to /Applications) and, inside it, the
# Contents/MacOS tree the in-place updater replaces (update.rs, find_package), so an update never re-downloads a second zip.
stage=$(mktemp -d)
cp -R "$app" "$stage/"
cp "$repo/launcher/scripts/self-sign.sh" "$stage/Self-sign (optional).command"
cat > "$stage/How to open Recompia.txt" <<'TXT'
Recompia is not notarized by Apple, so macOS refuses to open it the first time. This is expected.

1. Drag PokopiaLauncher.app to the Applications folder.
2. Open it once and let macOS say it cannot be opened. Close that dialog.
3. Open System Settings, Privacy and Security, and scroll down to the Security section.
   There is a line about Pokopia Launcher being blocked: press "Open Anyway", then "Open".
4. From then on it opens like any other app.

On macOS 14 and older you can also right-click the app, choose Open, and confirm.

If System Settings shows no such line, remove the download flag in Terminal instead:

   xattr -dr com.apple.quarantine /Applications/PokopiaLauncher.app

There is also "Self-sign (optional).command" in this zip: double-click it (or right-click and Open) and it removes that flag
and signs the app with a certificate it creates on this Mac. Gatekeeper still only accepts Apple-issued developer IDs by itself,
so the one-time Open Anyway above may be needed either way.
TXT
rm -f "$zip"
ditto -c -k --sequesterRsrc "$stage" "$zip"
rm -rf "$stage"
echo "built $zip"
