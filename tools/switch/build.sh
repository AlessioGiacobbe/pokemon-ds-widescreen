#!/usr/bin/env bash
# Builds ssrecomp for the Nintendo Switch (an .nro for the Homebrew Menu) from your own ROM.
#
#   tools/switch/build.sh [your-soulsilver-us.nds]
#
# Needs devkitPro with the switch-dev and switch-sdl2 packages (see README, "Nintendo
# Switch"), plus what the PC build needs to generate the game's code (Python 3, CMake,
# Ninja). The ROM is only needed the first time, or when gen/ is missing: the recompiled
# code in gen/ is the same for every platform and is shared with the PC build.
#
# The result, build-switch/ssrecomp.nro, holds your game's code: keep it to yourself.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPRO
BUILD="${SSR_SWITCH_BUILD:-build-switch}"

say() { printf '\n== %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

# The toolchain.
[ -f "$DEVKITPRO/cmake/Switch.cmake" ] ||
    fail "devkitPro's Switch toolchain is not in $DEVKITPRO. Install devkitPro, then: sudo dkp-pacman -S switch-dev switch-sdl2"
[ -f "$DEVKITPRO/portlibs/switch/lib/libSDL2.a" ] ||
    fail "SDL2 for the Switch is missing: sudo dkp-pacman -S switch-sdl2"
for tool in cmake ninja python3; do
    command -v "$tool" >/dev/null || fail "$tool is not installed (sudo apt install cmake ninja-build python3)"
done

# The game's code, generated from the ROM (once).
if [ ! -f gen/sources.cmake ]; then
    ROM="${1:-}"
    [ -n "$ROM" ] || fail "gen/ is missing: give your SoulSilver (US) ROM, e.g. tools/switch/build.sh ~/soulsilver.nds"
    [ -f "$ROM" ] || fail "no such file: $ROM"
    say "Recompiling the game's code from $ROM"
    python3 tools/recomp.py --rom "$ROM" --layout layouts/soulsilver-us.json melonDS/src gen
    python3 tools/apps.py --layout layouts/soulsilver-us.json gen
fi

# About 1.5 GB of memory per compile job (the generated files are large).
JOBS="${SSR_JOBS:-}"
if [ -z "$JOBS" ]; then
    avail_kb=$(awk '/MemAvailable/ { print $2 }' /proc/meminfo 2>/dev/null || echo 0)
    JOBS=$(( avail_kb / 1572864 ))
    cpus=$(nproc 2>/dev/null || echo 1)
    [ "$JOBS" -gt "$cpus" ] && JOBS=$cpus
    [ "$JOBS" -lt 1 ] && JOBS=1
fi

say "Configuring ($BUILD)"
if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S . -B "$BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSSR_GENERATED_DIR="$HERE/gen"
fi

say "Compiling with $JOBS jobs (the first time takes a while)"
ninja -C "$BUILD" -j "$JOBS" ssrecomp

NRO="$BUILD/ssrecomp.nro"
[ -f "$NRO" ] || fail "the build finished but $NRO was not made"
say "Done: $NRO ($(du -h "$NRO" | cut -f1))"
cat <<EOF

On the Switch's SD card, make the folder /switch/ssrecomp/ and copy into it:
  - $NRO
  - your ROM, renamed to rom.nds
Then hold R while starting any game, and choose ssrecomp in the Homebrew Menu.
The save (soulsilver.sav), settings (ssrecomp.cfg) and game.log are written to that folder.
EOF
