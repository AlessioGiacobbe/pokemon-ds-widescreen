#!/bin/bash
# Launches the recompiled SoulSilver. Extra arguments are passed through,
# e.g. ./run.sh --fullscreen --layout side
HERE="$(cd "$(dirname "$0")" && pwd)"
# setup.sh records which ROM to use; otherwise the decompilation's build next to this folder.
ROM="$HERE/../pokeheartgold/build/soulsilver.us/pokesoulsilver.us.nds"
[ -f "$HERE/ssrecomp.rom" ] && ROM="$(head -n1 "$HERE/ssrecomp.rom")"
if [ ! -x "$HERE/build/ssrecomp" ] || [ ! -f "$ROM" ]; then
    echo "Not set up yet: run ./setup.sh first." >&2
    exit 1
fi
exec "$HERE/build/ssrecomp" "$ROM" --save "$HERE/soulsilver.sav" "$@"
