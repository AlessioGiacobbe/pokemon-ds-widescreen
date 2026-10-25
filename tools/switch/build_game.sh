#!/usr/bin/env bash
# Builds one of the games for the Nintendo Switch from your own ROM, all steps in one:
#
#   tools/switch/build_game.sh soulsilver your-soulsilver-us.nds
#   tools/switch/build_game.sh black2     your-black2-us.nds
#   tools/switch/build_game.sh white2     your-white2-us.nds
#   tools/switch/build_game.sh bw
#
# bw needs no ROM: one program for Black 2 and White 2 (and Black) that reads what it needs
# from the player's ROM when it starts, packed as build-sd/ssrecomp-black2-white2-switch.zip
# to share (a release's download).
#   tools/switch/build_game.sh black      your-black-us-or-europe.nds
#
# The result is a folder laid out like the SD card, build-sd/switch/<game folder>/, with the
# .nro (and, for Black, Black 2 and White 2, funcs.txt): copy build-sd/switch to the root of the SD card,
# then your ROM into the game's folder (see README, "Nintendo Switch").
#
# Needs Linux or WSL2 (Ubuntu 24.04) with git, CMake, Ninja and Python 3, and devkitPro with
# switch-dev and switch-sdl2. When devkitPro is not installed but Docker is, its official image
# (devkitpro/devkita64) is used instead: their servers refuse some IP addresses (403).
# SSR_SWITCH_ICON=path/to/icon.jpg (256x256 JPEG) gives the game your own Homebrew Menu icon.
#
# Keep what it makes to yourself: SoulSilver's .nro holds parts of the game's code (the images
# its layouts tell its overlays apart by), and the others' funcs.txt is made from your ROM.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
GAME="${1:-}"
ROM="${2:-}"
IMAGE=devkitpro/devkita64
# The decompilation versions the function names come from (only their symbol files are fetched).
BW2_DECOMP=https://github.com/fuddlesworth/pokebw2.git
BW2_COMMIT=753b3fd97683fc619bb98b52e7276afac3d5e32f
# (White 2's symbols came later. Its functions have the same names as Black 2's.)
W2_COMMIT=be2c125a700c23702ba1c5a4ef63e0e970bcc87a
BW1_DECOMP=https://github.com/Project-Holon/aletheia.git
BW1_COMMIT=0d05b6ccc61fcea090d8318a00af9f1634810e18

say() { printf '\n== %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

case "$GAME" in
    soulsilver) FOLDER=ssrecomp; NRO=ssrecomp.nro ;;
    black2) FOLDER=ssrecomp-black2; NRO=ssrecomp-black2.nro ;;
    white2) FOLDER=ssrecomp-white2; NRO=ssrecomp-white2.nro ;;
    black) FOLDER=ssrecomp-black; NRO=ssrecomp-black.nro ;;
    bw) FOLDER=; NRO= ;;
    *) fail "which game? tools/switch/build_game.sh soulsilver|black2|white2|black your-rom.nds (or bw, no ROM)" ;;
esac
if [ "$GAME" != bw ]; then
    [ -n "$ROM" ] || fail "give your ROM: tools/switch/build_game.sh $GAME your-rom.nds"
    [ -f "$ROM" ] || fail "no such file: $ROM"
    ROM="$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")"
fi
for tool in git cmake ninja python3; do
    command -v "$tool" >/dev/null || fail "$tool is not installed (Ubuntu: sudo apt install git cmake ninja-build python3)"
done

# The toolchain: devkitPro installed here, or else its Docker image.
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DOCKER=""
if [ -f "$DEVKITPRO/cmake/Switch.cmake" ] && [ -f "$DEVKITPRO/portlibs/switch/lib/libSDL2.a" ]; then
    export DEVKITPRO
    export PATH="$DEVKITPRO/tools/bin:$PATH"
elif command -v docker >/dev/null 2>&1; then
    if docker info >/dev/null 2>&1; then DOCKER=docker; else DOCKER="sudo docker"; fi
    say "devkitPro is not installed here: using its Docker image ($IMAGE, about 2 GB the first time)"
    $DOCKER pull "$IMAGE"
else
    fail "devkitPro's Switch toolchain is not in $DEVKITPRO. Install devkitPro, then: sudo dkp-pacman -S switch-dev switch-sdl2 (or install Docker)"
fi
# Runs a build script with the toolchain (the source folder is mounted at the same path in the
# container, so a build folder's paths stay valid between runs).
build() {
    if [ -z "$DOCKER" ]; then
        env "$@"
    else
        local envs=()
        while [ "$#" -gt 0 ] && [[ "$1" == *=* ]]; do envs+=(-e "$1"); shift; done
        $DOCKER run --rm -v "$HERE:$HERE" -w "$HERE" "${envs[@]}" "$IMAGE" "$@"
        $DOCKER run --rm -v "$HERE:$HERE" "$IMAGE" chown -R "$(id -u):$(id -g)" "$HERE/$BUILD"
    fi
}

# The symbol files of a decompilation at a fixed version, without the rest of it.
fetch_symbols() { # dir url commit paths...
    local dir="$1" url="$2" commit="$3"
    shift 3
    if [ ! -d "$dir/.git" ]; then
        git init -q "$dir"
        git -C "$dir" remote add origin "$url"
    fi
    git -C "$dir" sparse-checkout set --no-cone "$@"
    git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$commit"
    git -C "$dir" checkout -q --force FETCH_HEAD
}

case "$GAME" in
soulsilver)
    BUILD=build-switch-jit
    if [ ! -f gen/sources.cmake ]; then
        say "The game's tables, from $ROM"
        python3 tools/recomp.py --rom "$ROM" --layout layouts/soulsilver-us.json melonDS/src gen
        python3 tools/apps.py --layout layouts/soulsilver-us.json gen
    fi
    # Without the recompiled code: the game runs on melonDS's JIT, about 1.7 times as fast on the
    # Switch, and the build takes minutes instead of most of an hour.
    python3 tools/soulsilver/tables_only.py gen build-gen-tables
    [ -n "${SSR_SWITCH_ICON:-}" ] && install -D -m 644 "$SSR_SWITCH_ICON" "$BUILD/icon.jpg"
    say "Compiling for the Switch"
    build SSR_SWITCH_JIT=1 SSR_SWITCH_GEN="$HERE/build-gen-tables" SSR_SWITCH_BUILD="$BUILD" bash tools/switch/build.sh
    ;;
black2|white2)
    # (Both games are the same program: White 2 reuses Black 2's build folder, so building the
    # second takes only seconds.)
    BUILD=build-switch-bw2
    if [ "$GAME" = black2 ]; then
        NAME="Black 2"; CFG=b2_us; DECOMP_DIR=build-decomp-pokebw2; COMMIT="$BW2_COMMIT"
    else
        NAME="White 2"; CFG=w2_us; DECOMP_DIR=build-decomp-pokew2; COMMIT="$W2_COMMIT"
    fi
    say "$NAME's function table, from the pokebw2 decompilation's symbol files and your ROM"
    fetch_symbols "$DECOMP_DIR" "$BW2_DECOMP" "$COMMIT" \
        "/config/$CFG/arm9/symbols.txt" "/config/$CFG/arm9/*/symbols.txt" "/config/$CFG/arm9/overlays/*/symbols.txt"
    mkdir -p "$BUILD"
    python3 tools/bw2/funcmap.py "$DECOMP_DIR/config/$CFG/arm9" "$ROM" "$BUILD/funcs.txt"
    [ -n "${SSR_SWITCH_ICON:-}" ] && install -D -m 644 "$SSR_SWITCH_ICON" "$BUILD/icon.jpg"
    say "Compiling for the Switch"
    build SSR_SWITCH_BUILD="$BUILD" SSR_NRO_TITLE="Pokemon $NAME" SSR_SWITCH_ROMFS= bash tools/bw2/build_switch.sh
    ;;
bw)
    # One program for Black 2, White 2 and Black, with their function tables inside (made
    # without a ROM: the program reads the few bytes the tables need from the player's ROM).
    # Nothing of the games is in it, so it can be shared (a release's download).
    BUILD=build-switch-bwshared
    ROMFS="$HERE/$BUILD/romfs"
    mkdir -p "$ROMFS/funcs"
    say "The function tables, from the decompilations' symbol files"
    fetch_symbols build-decomp-pokebw2 "$BW2_DECOMP" "$BW2_COMMIT" \
        '/config/b2_us/arm9/symbols.txt' '/config/b2_us/arm9/*/symbols.txt' '/config/b2_us/arm9/overlays/*/symbols.txt'
    fetch_symbols build-decomp-pokew2 "$BW2_DECOMP" "$W2_COMMIT" \
        '/config/w2_us/arm9/symbols.txt' '/config/w2_us/arm9/*/symbols.txt' '/config/w2_us/arm9/overlays/*/symbols.txt'
    fetch_symbols build-decomp-aletheia "$BW1_DECOMP" "$BW1_COMMIT" '/layout/function_addrs.tsv'
    python3 tools/bw2/funcmap.py build-decomp-pokebw2/config/b2_us/arm9 - "$ROMFS/funcs/IREO.txt"
    python3 tools/bw2/funcmap.py build-decomp-pokew2/config/w2_us/arm9 - "$ROMFS/funcs/IRDO.txt"
    python3 tools/bw1/funcmap.py build-decomp-aletheia/layout/function_addrs.tsv - "$ROMFS/funcs/IRBO.txt"
    [ -n "${SSR_SWITCH_ICON:-}" ] && install -D -m 644 "$SSR_SWITCH_ICON" "$BUILD/icon.jpg"
    say "Compiling for the Switch"
    # Packed once per game, with its own title in the Homebrew Menu (the same program).
    for g in black2:"Black 2" white2:"White 2"; do
        build SSR_SWITCH_BUILD="$BUILD" SSR_NRO_TITLE="Pokemon ${g#*:}" SSR_SWITCH_ROMFS="$ROMFS" bash tools/bw2/build_switch.sh
        mkdir -p "build-sd/switch/ssrecomp-${g%%:*}"
        cp "$BUILD/ssrecomp.nro" "build-sd/switch/ssrecomp-${g%%:*}/ssrecomp-${g%%:*}.nro"
    done
    rm -f build-sd/ssrecomp-black2-white2-switch.zip
    (cd build-sd && python3 -c "
import sys, zipfile
with zipfile.ZipFile('ssrecomp-black2-white2-switch.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sys.argv[1:]: z.write(p)
" switch/ssrecomp-black2/ssrecomp-black2.nro switch/ssrecomp-white2/ssrecomp-white2.nro)
    say "Done: $HERE/build-sd/ssrecomp-black2-white2-switch.zip"
    ls -l build-sd/switch/ssrecomp-black2 build-sd/switch/ssrecomp-white2 build-sd/ssrecomp-black2-white2-switch.zip
    cat <<EOF

Copy build-sd/switch to the root of the SD card, then a Black 2 ROM into /switch/ssrecomp-black2/
and a White 2 ROM into /switch/ssrecomp-white2/ (English, US or Europe; any copy of each works).
On the Switch, hold R while starting any game and choose it in the Homebrew Menu.
The zip is the same two files, ready to share: it holds nothing of the games.
EOF
    exit 0
    ;;
black)
    BUILD=build-switch-bw1
    say "Black's function table, from the aletheia decompilation's function list and your ROM"
    fetch_symbols build-decomp-aletheia "$BW1_DECOMP" "$BW1_COMMIT" '/layout/function_addrs.tsv'
    mkdir -p "$BUILD"
    python3 tools/bw1/funcmap.py build-decomp-aletheia/layout/function_addrs.tsv "$ROM" "$BUILD/funcs.txt"
    [ -n "${SSR_SWITCH_ICON:-}" ] && install -D -m 644 "$SSR_SWITCH_ICON" "$BUILD/icon.jpg"
    say "Compiling for the Switch"
    build bash tools/bw1/build_switch.sh
    ;;
esac

OUT="build-sd/switch/$FOLDER"
mkdir -p "$OUT"
cp "$BUILD/ssrecomp.nro" "$OUT/$NRO"
[ -f "$BUILD/funcs.txt" ] && cp "$BUILD/funcs.txt" "$OUT/funcs.txt"
say "Done: $HERE/$OUT"
ls -l "$OUT"
cat <<EOF

Copy build-sd/switch to the root of the SD card, then your ROM into /switch/$FOLDER/
$( [ "$GAME" = soulsilver ] && echo "(renamed rom.nds)" || echo "(as the only .nds file there)" ). On the Switch, hold R while starting any game
and choose it in the Homebrew Menu.
EOF
