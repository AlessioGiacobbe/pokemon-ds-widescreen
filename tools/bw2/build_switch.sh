#!/usr/bin/env bash
# Builds Pokemon Black 2 or White 2 for the Nintendo Switch: build-switch-bw2/ssrecomp.nro.
# SSR_SWITCH_BUILD: another build folder; SSR_NRO_TITLE: the Homebrew Menu's title ("Pokemon
# White 2"); SSR_SWITCH_ROMFS: a folder to put in the .nro's romfs (tools/switch/build_game.sh bw). There is no
# recompiled code for Black 2 (tools/bw2/stubgen stands in for gen/); the game runs on
# melonDS's JIT (or its interpreter, EMULATION in the pause menu) and its layouts follow the
# functions listed in funcs.txt, which
# tools/bw2/funcmap.py makes from the pokebw2 decompilation and your ROM. The .nro holds
# nothing of the game.
#
#   tools/bw2/build_switch.sh
#
# Needs devkitPro with switch-dev and switch-sdl2 (or its devkitpro/devkita64 Docker image),
# CMake and Ninja.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPRO
BUILD="${SSR_SWITCH_BUILD:-build-switch-bw2}"
TITLE="${SSR_NRO_TITLE:-Pokemon Black 2}"
ROMFS="${SSR_SWITCH_ROMFS:-}"

[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || { echo "devkitPro's Switch toolchain is not in $DEVKITPRO" >&2; exit 1; }
# Configured every time, so that an older build folder picks up new options (the JIT).
cmake -S . -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSSR_GENERATED_DIR="$HERE/tools/bw2/stubgen" \
    -DSSR_NRO_TITLE="$TITLE" \
    -DSSR_SWITCH_ROMFS="$ROMFS" \
    -DSSR_JIT=ON
ninja -C "$BUILD" ssrecomp_nro
[ -f "$BUILD/ssrecomp.nro" ] || { echo "the build finished but $BUILD/ssrecomp.nro was not made" >&2; exit 1; }
echo "Done: $BUILD/ssrecomp.nro ($(du -h "$BUILD/ssrecomp.nro" | cut -f1))"
