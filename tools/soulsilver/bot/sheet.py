#!/usr/bin/env python3
"""Contact sheets of a run's flags, one incident per tile.

    sheet.py RUN [RUN...]   -> review_<kind>_<n>.png and a summary on stdout

Flags of one kind and side within 120 frames of each other are one incident (the first is
shown). Rules applied here as well as in scan.py (for runs scanned before they were added):
no flat-edge in battles, no pop-in across a transition.
"""
import json
import re
import sys

import numpy as np

from PIL import Image, ImageDraw

incidents = {}
for run in sys.argv[1:]:
    try:
        d = json.load(open(run + '/flags.json'))
    except OSError:
        continue
    last = {}
    for f in d['flags']:
        if f['kind'] == 'flat-edge' and f['scene'].startswith('battle'):
            continue
        if f['kind'] == 'black-edge' and f['scene'] not in ('top', 'battle', 'battle-cmd', 'battle-moves', 'battle-strip'):
            continue
        if f['kind'] == 'flat-edge':
            # (A colour that is also inside the picture is the scene's own: sand, grass.)
            try:
                a = np.asarray(Image.open(f['shot']).convert('RGB')).astype(int)
                c = np.array([int(x) for x in re.findall(r'\d+', f['text'].split('(')[1].split(')')[0])])
                if (np.abs(a[:540, 200:1080] - c).sum(axis=2) < 12).mean() > 0.002:
                    continue
            except OSError:
                pass
        side = f['text'].split(':')[0].split(' ')[0]
        key = (f['kind'], side)
        if key in last and f['frame'] - last[key] <= 120:
            last[key] = f['frame']
            continue
        last[key] = f['frame']
        incidents.setdefault(f['kind'], []).append((run, f))
for kind, items in sorted(incidents.items()):
    print('%-10s %d incidents' % (kind, len(items)))
    for part in range(0, len(items), 12):
        chunk = items[part:part + 12]
        o = Image.new('RGB', (3 * 430, ((len(chunk) + 2) // 3) * 260), (40, 0, 40))
        dr = ImageDraw.Draw(o)
        for i, (run, f) in enumerate(chunk):
            x, y = (i % 3) * 430, (i // 3) * 260
            try:
                im = Image.open(f['shot']).resize((426, 240))
            except OSError:
                continue
            o.paste(im, (x + 2, y + 18))
            dr.text((x + 4, y + 3), '%s %d %s: %s' % (run, f['frame'], f['scene'], f['text'][:48]), fill=(255, 255, 0))
        o.save('review_%s_%d.png' % (kind, part // 12))
