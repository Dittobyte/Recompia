#!/usr/bin/env bash
# Configure + build astc2bc7 (library, CLI, astcenc decoder libraries) on Linux with CMake + Ninja.
#   tools/textures/scripts/build.sh [--clean]
# Needs third_party/ (see THIRD_PARTY.md): astc-encoder, bc7enc_rdo, and the Linux ISPC (third_party/ispc/ispc-v1.32.0-linux).
# Output: tools/textures/build/release/
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build/release"
if [ "${1:-}" = "--clean" ]; then rm -rf "$build"; fi
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$build"
echo "built: $build/astc2bc7"
