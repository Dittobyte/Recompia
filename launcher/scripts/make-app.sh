#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Assembles PokopiaLauncher.app around the launcher binary. The bundle carries the launcher only: pkp, pkp.ini,
# controls.ini and shader_cache stay beside it (a 26 GB cache does not belong inside a bundle), and the launcher looks
# for the game in the folder the .app sits in.
set -eu
here=$(cd "${1:?usage: make-app.sh <play folder with PokopiaLauncher, pkp and pkp.ini> [version]}" && pwd)
# the release version: the launcher's manifest, unless make-mac.sh names one
version=${2:-$(sed -nE 's/^[[:space:]]*"version": "([^"]+)".*/\1/p' "$(dirname "$0")/../src-tauri/tauri.conf.json" | head -1)}
app="$here/PokopiaLauncher.app"
bin="$here/PokopiaLauncher"

[ -x "$bin" ] || { echo "no launcher binary at $bin (npm run tauri build, then copy it here)" >&2; exit 1; }

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$bin" "$app/Contents/MacOS/PokopiaLauncher"

cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>PokopiaLauncher</string>
    <key>CFBundleIdentifier</key><string>org.pkp-recomp.launcher</string>
    <key>CFBundleName</key><string>Pokopia Launcher</string>
    <key>CFBundleDisplayName</key><string>Pokopia Launcher</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>@VERSION@</string>
    <key>CFBundleVersion</key><string>@VERSION@</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
</dict>
</plist>
PLIST

# the release version, from make-mac.sh (the tag) or the default
sed -i '' -e "s/@VERSION@/$version/g" "$app/Contents/Info.plist"

# Ad-hoc signature: enough for a local double-click. A distribution build signs with a Developer ID and notarizes
# (docs/macos-port.md M6).
codesign --force --sign - "$app" 2>/dev/null || echo "warning: codesign failed; the bundle still runs locally" >&2

echo "built $app"
echo "put it next to pkp, pkp.ini and shader_cache, then double-click it"
