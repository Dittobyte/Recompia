#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The native Linux runtime of release model (b): libpkp_rt.so, the ASTC decoder libraries, the fonts and the shader translator
# (tools/spirv_harness, a self-contained linux-x64 .NET program), built on a Linux x86-64 PC with glibc (any distribution; on Bazzite
# or SteamOS inside a distrobox or toolbox). Copied into the root of an extracted Linux release (next to PokopiaLauncher.AppImage),
# they make the launcher build and run the native `pkp` and translate shaders without Proton (launcher src-tauri/src/host.rs, Game
# and Translator). No game code or data is involved: the game itself is linked on the player's PC by Set up.
#
#   game-build/scripts/build-linux.sh [--out DIR] [--version X.Y.Z] [--clean] [--no-translator] [-- extra CMake arguments]
#
# Needs: cmake (3.25+), ninja, clang (or gcc), python3, git, and the development packages SDL3 and libvpx build against, e.g. on
# Debian / Ubuntu: libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev
#   libwayland-dev wayland-protocols libdecor-0-dev libpulse-dev libpipewire-0.3-dev libasound2-dev libudev-dev libdbus-1-dev
#   libdrm-dev libgbm-dev libegl-dev libvpx-dev
# Fedora: the -devel packages of the same libraries. The translator needs the .NET 10 SDK (Ubuntu 24.04+: dotnet-sdk-10.0; Fedora:
# dotnet-sdk-10.0) and git access to git.ryujinx.app; --no-translator leaves it out (Set up then runs the Windows one through Proton).
# Output: DIR (default game-build/build/linux-native).
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/game-build/build/linux-native"
version="dev"
clean=0
translator=1
extra=()
while [ $# -gt 0 ]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --clean) clean=1; shift ;;
        --no-translator) translator=0; shift ;;
        --) shift; extra=("$@"); break ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ "$(uname -m)" != "x86_64" ]; then echo "build-linux.sh: an x86-64 PC is needed" >&2; exit 1; fi
if ! ldd --version 2>&1 | grep -qiE "glibc|gnu libc"; then
    echo "build-linux.sh: this system's C library is not glibc (e.g. Alpine's musl): build in a glibc distribution (Ubuntu, Fedora, Arch)" >&2
    exit 1
fi
if [ $translator = 1 ] && ! dotnet --list-sdks 2>/dev/null | grep -q '^10\.'; then
    echo "build-linux.sh: the .NET 10 SDK is missing (install dotnet-sdk-10.0, or pass --no-translator)" >&2
    exit 1
fi
for tool in cmake ninja python3 git; do
    command -v "$tool" >/dev/null || { echo "build-linux.sh: $tool is missing" >&2; exit 1; }
done
if command -v clang >/dev/null && command -v clang++ >/dev/null; then
    export CC="${CC:-clang}" CXX="${CXX:-clang++}"
fi

# 1. The ASTC -> BC7 transcoder (tools/textures) and its decoder libraries. Its third-party sources are fetched once (THIRD_PARTY.md).
tex="$root/tools/textures"
tp="$tex/third_party"
mkdir -p "$tp"
if [ ! -d "$tp/astc-encoder" ]; then
    git clone --depth 1 --branch 5.7.0 https://github.com/ARM-software/astc-encoder.git "$tp/astc-encoder"
fi
if [ ! -d "$tp/bc7enc_rdo" ]; then
    git clone https://github.com/richgel999/bc7enc_rdo.git "$tp/bc7enc_rdo"
    git -C "$tp/bc7enc_rdo" checkout -q b9438627eef73a1157e84201b6fa6eb2ffd6d9f0
fi
if [ ! -x "$tp/ispc/ispc-v1.32.0-linux/bin/ispc" ]; then
    mkdir -p "$tp/ispc"
    curl -fL -o "$tp/ispc/ispc-v1.32.0-linux.tar.gz" https://github.com/ispc/ispc/releases/download/v1.32.0/ispc-v1.32.0-linux.tar.gz
    tar -xzf "$tp/ispc/ispc-v1.32.0-linux.tar.gz" -C "$tp/ispc"
    rm -f "$tp/ispc/ispc-v1.32.0-linux.tar.gz"
fi
if [ $clean = 1 ]; then "$tex/scripts/build.sh" --clean; else "$tex/scripts/build.sh"; fi

# 2. The runtime as libpkp_rt.so (PKP_RT_DLL), release settings (no test tooling, every gameplay mod on by default).
build="$root/game-build/build/linux-release"
if [ $clean = 1 ]; then rm -rf "$build"; fi
cmake -S "$root/game-build" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPKP_RT_DLL=ON -DPKP_RELEASE=ON -DPKP_VERSION="$version" "${extra[@]}"
cmake --build "$build"

# 3. The shader translator, the same source and pin as the Windows release (.github/workflows/release.yml RYUBING_COMMIT).
harness_out="$root/game-build/build/linux-harness"
if [ $translator = 1 ]; then
    pin="$(sed -n 's/^ *RYUBING_COMMIT: *\([0-9a-f]\{40\}\).*/\1/p' "$root/.github/workflows/release.yml" | head -1)"
    [ -n "$pin" ] || { echo "build-linux.sh: no RYUBING_COMMIT in .github/workflows/release.yml" >&2; exit 1; }
    ryubing="$root/game-build/build/ryubing"
    if [ ! -d "$ryubing/.git" ]; then
        git clone --filter=blob:none https://git.ryujinx.app/ryubing/ryujinx.git "$ryubing"
    fi
    git -C "$ryubing" fetch -q origin "$pin" 2>/dev/null || true
    git -C "$ryubing" checkout -q "$pin"
    "$root/tools/shaders/scripts/setup_ryujinx.sh" "$ryubing"
    rm -rf "$harness_out"
    dotnet publish -c Release -r linux-x64 --self-contained true "$root/tools/shaders/harness/SpirvHarness.csproj" -o "$harness_out"
fi

# 4. What goes next to the launcher in the release root.
rm -rf "$out"
mkdir -p "$out/fonts"
cp "$build/libpkp_rt.so" "$out/"
cp "$tex/build/release/"libastcdec-*-shared.so "$out/"
cp "$build/fonts/DejaVuSans.ttf" "$out/fonts/"
if [ $translator = 1 ]; then
    mkdir -p "$out/tools/spirv_harness"
    (cd "$harness_out" && tar --exclude='*.pdb' -cf - .) | (cd "$out/tools/spirv_harness" && tar -xf -)
fi
echo "built: $out"
ls -l "$out"
