#!/usr/bin/env bash
# Builds the HOME Menu entries' program (tools/switch/forwarder) into a folder, for the all-games
# program's romfs: tools/switch/build_forwarder.sh <build folder> <out folder>
# Run with devkitPro's tools (DEVKITPRO set, or inside its Docker image: build_game.sh does that).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPRO
cmake -S "$HERE/tools/switch/forwarder" -B "$1" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" -DSSR_FORWARDER_OUT="$2" >/dev/null
ninja -C "$1" exefs
ls -l "$2"
