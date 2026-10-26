#!/usr/bin/env python3
"""Input scripts for the SoulSilver bot (lines "first last KEY [KEY...]", frames relative).

    mkinput.py MODE SEED FRAMES OUT [BOOT]

MODE wander: held directions (B held too: running), A now and then (talk, signs, battle
             choices), B taps (close text, back out), X now and then (the menu, then out again).
     grind:  up and down in grass; battles are fought by --battle-mash.
     newgame: A through the intro (START sometimes, to accept a name), then wander.
BOOT: frames of A presses first, to get from the title screen to CONTINUE (when not starting
      from a snapshot).
"""
import random
import sys

mode, seed, frames, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
boot = int(sys.argv[5]) if len(sys.argv) > 5 else 0
r = random.Random(seed)
lines = []
f = 0
if boot:
    f = 600
    while f < boot:
        lines.append('%d %d A' % (f, f + 3))
        f += 45
dirs = ['UP', 'DOWN', 'LEFT', 'RIGHT']


def wander(until):
    global f
    while f < until:
        k = r.random()
        if k < 0.66:
            n = r.randint(30, 150)
            lines.append('%d %d %s%s' % (f, f + n, r.choice(dirs), ' B' if r.random() < 0.6 else ''))
            f += n + 4
        elif k < 0.80:
            lines.append('%d %d A' % (f, f + 3))
            f += 25
        elif k < 0.95:
            for _ in range(3):
                lines.append('%d %d B' % (f, f + 3))
                f += 20
        else:
            # The menu: open, move about in it a little, close.
            lines.append('%d %d X' % (f, f + 3))
            f += r.randint(60, 180)
            for _ in range(r.randint(0, 3)):
                lines.append('%d %d %s' % (f, f + 3, r.choice(dirs)))
                f += 30
            if r.random() < 0.4:
                lines.append('%d %d A' % (f, f + 3))      # open what is chosen
                f += r.randint(200, 600)
            for _ in range(4):
                lines.append('%d %d B' % (f, f + 3))
                f += 40


if mode == 'wander':
    wander(frames)
elif mode == 'grind':
    a, b = ('UP', 'DOWN') if r.random() < 0.5 else ('LEFT', 'RIGHT')
    while f < frames:
        n = r.randint(20, 40)
        lines.append('%d %d %s' % (f, f + n, a))
        f += n + 6
        n = r.randint(20, 40)
        lines.append('%d %d %s' % (f, f + n, b))
        f += n + 6
        if r.random() < 0.05:
            # Text after a battle (a level-up, a new move) that --battle-mash has left.
            for _ in range(6):
                lines.append('%d %d A' % (f, f + 3))
                f += 30
elif mode == 'newgame':
    while f < frames // 2:
        lines.append('%d %d %s' % (f, f + 3, 'START' if r.random() < 0.08 else 'A'))
        f += 50
    wander(frames)
open(out, 'w').write('\n'.join(lines) + '\n')
