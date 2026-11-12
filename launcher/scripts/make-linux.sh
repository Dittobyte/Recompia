#!/bin/sh
# Builds the Linux release: the AppImage, the runtime and the build kit, staged from this checkout. Unlike the Windows
# zip, nothing here comes from another platform's build: the Linux package is what Set up needs on a Linux machine
# (the lifter, the SDK sources, the builder, the shader translator), and pkp is compiled by the player from their dump.
#
#   launcher/scripts/make-linux.sh <out dir> [version]
#
# Needs a production launcher build (npm run tauri build -- --bundles appimage), the runtime (PKP_RT_DLL=ON, see
# docs/linux-port.md) and the linux-x64 shader harness (tools/shaders, dotnet publish -r linux-x64) in build/.
set -eu
out=${1:?usage: make-linux.sh <out dir> [version]}
repo=$(cd "$(dirname "$0")/../.." && pwd)
version=${2:-$(sed -nE 's/^[[:space:]]*"version": "([^"]+)".*/\1/p' "$repo/launcher/src-tauri/tauri.conf.json" | head -1)}
pkg="$out/PokopiaRecomp-$version"

rt="$repo/build/rtdll/libpkp_rt.so"
[ -f "$rt" ] || { echo "no runtime at $rt (cmake -DPKP_RT_DLL=ON, target pkp_rt)" >&2; exit 1; }
appimage=$(ls "$repo"/launcher/src-tauri/target/release/bundle/appimage/*.AppImage 2>/dev/null | head -1)
[ -n "$appimage" ] || { echo "no AppImage (npm run tauri build -- --bundles appimage)" >&2; exit 1; }
harness="$repo/build/harness"
[ -x "$harness/spirv_harness" ] || { echo "no shader harness at $harness (dotnet publish -r linux-x64)" >&2; exit 1; }

rm -rf "$pkg"
mkdir -p "$pkg/tools/build/sdk" "$pkg/tools/shaders" "$pkg/assets" "$pkg/licenses"

# the programs
install -m 755 "$appimage" "$pkg/PokopiaLauncher.AppImage"
install -m 755 "$rt" "$pkg/libpkp_rt.so"
install -m 755 "$rt" "$pkg/tools/build/sdk/libpkp_rt.so"

# the build kit: what pkp_build.py runs from tools/build
for f in pkp_build.py dump_hashes.json fetch_dlss.py; do
    cp "$repo/game-build/release_b/$f" "$pkg/tools/build/$f"
done
cp -R "$repo/recomp" "$pkg/tools/build/recomp"
find "$pkg/tools/build/recomp" -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || true
cp "$repo/game-build/src/pkp_stub.cpp" "$repo/game-build/src/pkp_rt_api.h" "$pkg/tools/build/sdk/"

# the shader pipeline: the corpus extractor and a self-contained publish of the harness (its own .NET runtime)
cp "$repo/tools/shaders/ampere/extract_corpus.py" "$pkg/tools/shaders/"
cp -R "$harness" "$pkg/tools/harness"

# what the game reads next to pkp: the keyboard and mouse prompt icons and the shared font (pkp_build.py copies both)
mkdir -p "$pkg/tools/build/sdk/kbm_icons" "$pkg/tools/build/sdk/fonts"
python3 "$repo/tools/kbm_icons.py" >/dev/null
cp "$repo/work/assets/kenney_input_prompts/Keyboard & Mouse/Default/"*.png "$pkg/tools/build/sdk/kbm_icons/"
cp "$repo/runtime/assets/fonts/DejaVuSans.ttf" "$pkg/tools/build/sdk/fonts/"
cp -R "$repo/runtime/assets/prompts" "$pkg/assets/prompts"

# licences and documents
cp "$repo/game-build/release_b/BUNDLED_TOOLS.txt" "$pkg/licenses/"
cp "$repo/LICENSE" "$pkg/LICENSE.txt"
cp "$repo/CHANGELOG.md" "$pkg/CHANGELOG.txt"
sed "s/@VERSION@/$version/g" "$repo/game-build/release_b/README.linux.txt" > "$pkg/README.txt"
for t in "$repo"/game-build/release_b/README.linux.*.txt; do
    [ -f "$t" ] || continue
    lang=${t##*/README.linux.}
    sed "s/@VERSION@/$version/g" "$t" > "$pkg/README.${lang%.txt}.txt"
done

tar -C "$out" -czf "$out/PokopiaRecomp-$version-linux-x64.tar.gz" "PokopiaRecomp-$version"
echo "built $out/PokopiaRecomp-$version-linux-x64.tar.gz"
