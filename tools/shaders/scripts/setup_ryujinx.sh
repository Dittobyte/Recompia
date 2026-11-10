#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Linux twin of setup_ryujinx.ps1: stage the three Ryujinx projects the translator needs into tools/shaders/third_party/ryubing
# (git-ignored), from a Ryubing checkout (default refs/ryubing). The build writes obj/bin next to the sources, so it builds from
# this copy and never touches the checkout.
#   tools/shaders/scripts/setup_ryujinx.sh [ryubing checkout]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="${1:-$here/../../../refs/ryubing}"
dst="$here/../third_party/ryubing"
mkdir -p "$dst/src"
for p in Ryujinx.Common Spv.Generator Ryujinx.Graphics.Shader; do
    rm -rf "$dst/src/$p"
    mkdir -p "$dst/src/$p"
    (cd "$src/src/$p" && tar --exclude=./obj --exclude=./bin -cf - .) | (cd "$dst/src/$p" && tar -xf -)
done
for f in Directory.Build.props Directory.Packages.props global.json nuget.config LICENSE.txt; do
    cp "$src/$f" "$dst/$f"
done
echo "staged Ryujinx sources in $dst"
