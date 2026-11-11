#!/bin/sh
# Builds the macOS release: PokopiaLauncher.app with the runtime dylib and the build tools inside it, then a DMG that
# installs into /Applications. The package ships no game code; Set up lifts and compiles pkp from the player's own dump.
#
#   launcher/scripts/make-dmg.sh <play folder with pkp.ini and the dump paths> [version]
#
# Needs a production launcher build (npm run tauri build) and the runtime dylib (PKP_RT_DLL=ON, see docs/macos-port.md).
set -eu
here=$(cd "${1:?usage: make-dmg.sh <play folder> [version]}" && pwd)
version=${2:-0.1.1}
repo=$(cd "$(dirname "$0")/../.." && pwd)
app="$here/PokopiaLauncher.app"
dmg="$here/Recompia-$version-macos-arm64.dmg"

[ -x "$here/PokopiaLauncher" ] || { echo "no launcher binary at $here/PokopiaLauncher (npm run tauri build)" >&2; exit 1; }

"$repo/launcher/scripts/make-app.sh" "$here"

# The bundle carries everything the player needs: the runtime dylib, the lifter, the SDK sources and the builder. The game
# folder (pkp, pkp.ini, shader_cache, saves) is created by Set up in Application Support, never inside the bundle.
res="$app/Contents/MacOS"   # the launcher looks for tools/ next to itself
mkdir -p "$res/tools/build/sdk" "$app/Contents/Frameworks"
cp "$repo/work/build/rtb/libpkp_rt.dylib" "$app/Contents/Frameworks/libpkp_rt.dylib"
cp "$repo/game-build/release_b/pkp_build.py" "$repo/game-build/release_b/dump_hashes.json" "$res/tools/build/"
cp -R "$repo/recomp" "$res/tools/build/recomp"
find "$res/tools/build/recomp" -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || true
# The shader pipeline: the corpus extractor and a self-contained publish of the harness (its own .NET runtime), so a
# player needs neither .NET nor a checkout.
cp "$repo/tools/shaders/ampere/extract_corpus.py" "$res/tools/"
cp -R "$repo/work/harness" "$res/tools/harness"
cp "$repo/game-build/src/pkp_stub.cpp" "$repo/game-build/src/pkp_rt_api.h" "$res/tools/build/sdk/"
# macOS has no Vulkan loader of its own: MoltenVK ships with the package (with its licence) and Set up copies it next to pkp,
# which is where volk looks for libvulkan.1.dylib.
moltenvk=$(brew --prefix molten-vk 2>/dev/null || echo /opt/homebrew/opt/molten-vk)
cp "$moltenvk/lib/libMoltenVK.dylib" "$res/tools/build/sdk/libvulkan.1.dylib"
cp "$moltenvk/LICENSE" "$res/tools/build/sdk/LICENSE-MoltenVK.txt"
# What the game reads next to pkp: the prompt icons (generated here when the work folder does not have them) and the font.
if [ ! -d "$repo/work/assets/kenney_input_prompts" ]; then
    python3 "$repo/tools/kbm_icons.py" >/dev/null
fi
mkdir -p "$res/tools/build/sdk/kbm_icons" "$res/tools/build/sdk/fonts"
cp "$repo/work/assets/kenney_input_prompts/Keyboard & Mouse/Default/"*.png "$res/tools/build/sdk/kbm_icons/"
cp "$repo/runtime/assets/fonts/DejaVuSans.ttf" "$res/tools/build/sdk/fonts/"
cp "$repo/runtime/cmake/guest_segment.py" "$res/tools/build/sdk/"
cp "$repo/work/build/rtb/libpkp_rt.dylib" "$res/tools/build/sdk/"
# No Python in the bundle: Set up fetches python-build-standalone and the lz4 and capstone wheels into tools/macos, as the
# Linux package does (host.rs prepare_tools). macOS compiles with the Apple clang of the Command Line Tools.

# The dylib is loaded from Contents/Frameworks, so the executable looks there first.
install_name_tool -add_rpath "@executable_path/../Frameworks" "$app/Contents/MacOS/PokopiaLauncher" 2>/dev/null || true
codesign --force --deep --sign - "$app" 2>/dev/null || echo "warning: codesign failed; the bundle still runs locally" >&2

# A DMG the way Mac users expect: the app, a shortcut to /Applications to drag it onto, and nothing else.
stage=$(mktemp -d)
cp -R "$app" "$stage/"
ln -s /Applications "$stage/Applications"
rm -f "$dmg"
hdiutil create -quiet -volname "Recompia $version" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$dmg"
rm -rf "$stage"
echo "built $dmg"
