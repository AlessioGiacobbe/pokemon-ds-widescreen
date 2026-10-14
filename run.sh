#!/bin/bash
# Launches the recompiled SoulSilver. Extra arguments are passed through,
# e.g. ./run.sh --fullscreen --layout side
HERE="$(cd "$(dirname "$0")" && pwd)"
ROM="$HERE/../pokeheartgold/build/soulsilver.us/pokesoulsilver.us.nds"
if [ ! -f "$ROM" ]; then
    echo "ROM not built: $ROM (run 'make soulsilver' in ../pokeheartgold)" >&2
    exit 1
fi
exec "$HERE/build/ssrecomp" "$ROM" --save "$HERE/soulsilver.sav" "$@"
