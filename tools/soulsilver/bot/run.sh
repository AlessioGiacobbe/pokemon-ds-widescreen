#!/bin/bash
# run.sh NAME MODE SEED FRAMES SAVE [STATE] [extra args...]: one bot run, scanned as it goes.
#   MODE: wander, grind (battles fought by --battle-mash) or newgame (see mkinput.py).
#   SAVE is copied (the run never writes it back); STATE "-" or none boots from the title screen.
# Needs SSR_BIN (a build of ssrecomp, the JIT build is fastest) and SSR_ROM. Writes, in the
# current folder: shots_NAME/ (deleted as scanned), NAME/ (flagged shots, thumbnails,
# flags.json), log_NAME.txt. Afterwards: python3 sheet.py NAME... for contact sheets.
HERE=$(cd "$(dirname "$0")" && pwd)
B=${SSR_BIN:?set SSR_BIN to the ssrecomp binary}; ROM=${SSR_ROM:?set SSR_ROM to the ROM}
N=$1; MODE=$2; SEED=$3; FR=$4; SAV=$5; ST=$6; shift 6
rm -rf shots_$N $N; mkdir -p shots_$N
cp "$SAV" run_$N.sav 2>/dev/null || rm -f run_$N.sav
if [ -n "$ST" ] && [ "$ST" != - ]; then
  python3 "$HERE/mkinput.py" $MODE $SEED $FR in_$N.txt
  ARGS=(--state-load $ST --relative)
else
  python3 "$HERE/mkinput.py" $MODE $SEED $FR in_$N.txt 3600
  ARGS=()
fi
MASH=(); [ "$MODE" = grind ] && MASH=(--battle-mash)
( timeout 4000 $B $ROM --headless --jit --save run_$N.sav --no-save "${ARGS[@]}" --input in_$N.txt --frames $FR \
    --compose 1280x720 --shot-dir shots_$N --shot-every 15 --log-sections "${MASH[@]}" "$@" 2>&1 \
    | grep --line-buffered -E "^\[scene\]|\[crash\]|\[exit\]|rror|ssert|\[strip\]"; echo "exit ${PIPESTATUS[0]}" ) > log_$N.txt &
python3 "$HERE/scan.py" shots_$N log_$N.txt $N > scan_$N.out 2>&1
wait
echo "$N finished: $(tail -1 log_$N.txt), $(tail -1 scan_$N.out)"
