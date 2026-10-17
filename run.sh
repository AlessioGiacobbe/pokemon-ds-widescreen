#!/bin/bash
# Launches the recompiled SoulSilver. Extra arguments are passed through,
# e.g. ./run.sh --fullscreen --layout side
HERE="$(cd "$(dirname "$0")" && pwd)"
# setup.sh records which ROM to use; otherwise the decompilation's build next to this folder.
ROM="$HERE/../pokeheartgold/build/soulsilver.us/pokesoulsilver.us.nds"
[ -f "$HERE/ssrecomp.rom" ] && ROM="$(head -n1 "$HERE/ssrecomp.rom")"
BUILD="$HERE/build"
[ "$(uname -s)" = Darwin ] && BUILD="$HERE/build-macos-$(uname -m)"
if [ ! -x "$BUILD/ssrecomp" ] || [ ! -f "$ROM" ]; then
    if [ "$(uname -s)" = Darwin ]; then
        echo "Not set up yet: run python3 ssrecomp_setup.py first." >&2
    else
        echo "Not set up yet: run ./setup.sh first." >&2
    fi
    exit 1
fi
exec "$BUILD/ssrecomp" "$ROM" --save "$HERE/soulsilver.sav" "$@"
