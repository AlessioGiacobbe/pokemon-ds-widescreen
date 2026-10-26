#!/usr/bin/env bash
# Builds Pokemon Black (or White: SSR_NRO_TITLE="Pokemon White") for the Nintendo Switch: build-switch-bw1/ssrecomp.nro. Like Black 2's
# (tools/bw2/build_switch.sh): no recompiled code (tools/bw2/stubgen stands in for gen/), the game
# runs on melonDS's JIT, and its layouts follow the functions listed in funcs.txt, which
# tools/bw1/funcmap.py makes from the aletheia decompilation and your ROM. The same program as
# Black 2's, with Black's title in the Homebrew Menu; it tells the games apart by the ROM
# (frontend/bw1.h). The .nro holds nothing of the game.
#
#   tools/bw1/build_switch.sh
#
# Needs devkitPro with switch-dev and switch-sdl2 (or its devkitpro/devkita64 Docker image),
# CMake and Ninja.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPRO
BUILD=build-switch-bw1

[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || { echo "devkitPro's Switch toolchain is not in $DEVKITPRO" >&2; exit 1; }
# Configured every time, so that an older build folder picks up new options (the JIT).
cmake -S . -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSSR_GENERATED_DIR="$HERE/tools/bw2/stubgen" \
    -DSSR_NRO_TITLE="${SSR_NRO_TITLE:-Pokemon Black}" \
    -DSSR_JIT=ON
ninja -C "$BUILD" ssrecomp_nro
[ -f "$BUILD/ssrecomp.nro" ] || { echo "the build finished but $BUILD/ssrecomp.nro was not made" >&2; exit 1; }
echo "Done: $BUILD/ssrecomp.nro ($(du -h "$BUILD/ssrecomp.nro" | cut -f1))"
