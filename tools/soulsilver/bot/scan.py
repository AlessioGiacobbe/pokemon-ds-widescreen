#!/usr/bin/env python3
"""Scans a bot run's screenshots as they are written (1280x720, every 15 frames) for faults.

    scan.py SHOTDIR LOG NAME

Keeps flagged shots (full size, JPEG) and a thumbnail every 300 frames in NAME/, writes
NAME/flags.json, deletes every PNG once looked at. Ends when LOG has an "exit" line and all
shots are done. Checks:
  stuck      the picture has not changed for 600 frames
  blank      black or white for 300 frames
  flat-edge  a run of one exact colour along an outer edge of the wide view over many rows,
             rare in the DS's own picture (the rear plane showing where nothing was drawn)
  black-edge a large area of pure black beside the DS's picture, absent from the middle
  pop-in     beside the DS's picture, something appearing or vanishing between two shots that
             the camera's movement does not explain (field scenes)
  layout     a fallback layout (spread, battle-menu): a screen without a layout of its own
  battle-ui  battle buttons drawn over what is above their band, or missing from it
"""
import glob
import json
import os
import re
import sys
import time

import numpy as np
from PIL import Image

shotdir, logpath, name = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(name, exist_ok=True)
flags = []
scenes = []          # (frame, name), from the log's [scene] lines
L, R = 160, 1120     # the DS's picture in the 1280-wide view
FIELD = {'top'}      # the overworld (and anything shown as the top screen alone)


def scene_at(fr):
    s = 'top'
    for f, n in scenes:
        if f <= fr:
            s = n
        else:
            break
    return s


def read_log():
    global scenes
    try:
        txt = open(logpath, errors='replace').read()
    except OSError:
        return False
    scenes = [(int(m.group(1)), m.group(2)) for m in re.finditer(r'\[scene\] frame (\d+): (\S+)', txt)]
    return 'exit' in txt.splitlines()[-1] if txt.strip() else False


def keep(img, fr, tag, full=True):
    p = '%s/%s_%06d.jpg' % (name, tag, fr)
    (img if full else img.resize((640, 360))).save(p, quality=88)
    return p


def flag(fr, kind, text, img, extra=None):
    p = keep(img, fr, kind)
    e = {'frame': fr, 'kind': kind, 'scene': scene_at(fr), 'text': text, 'shot': p}
    if extra is not None:
        e['prev'] = keep(extra, fr, kind + '-prev')
    flags.append(e)


def best_shift(a, b):
    """The camera's movement between two half-size greyscale pictures (b = a moved by s):
    the shift along x or along y, in steps of 2 half-size pixels, that best explains the
    DS's own picture."""
    m0, m1 = L // 2 + 70, R // 2 - 70
    best = (1e9, 0, 0)
    for dx in range(-64, 65, 2):
        d = np.abs(a[20:250, m0 - dx:m1 - dx] - b[20:250, m0:m1]).mean()
        if d < best[0]:
            best = (d, dx, 0)
    for dy in range(-20, 21, 2):
        if dy == 0:
            continue
        d = np.abs(a[20 - dy:250 - dy, m0:m1] - b[20:250, m0:m1]).mean()
        if d < best[0]:
            best = (d, 0, dy)
    return best


prev = None          # previous full-size array
prevg = None         # previous half-size grey, for pop-in
prevfr = None
same = dark = 0
seen_layout = set()
edge_prev = set()    # edge faults in the previous shot: one must last two shots to count
first = None
done = False
n = 0
while True:
    done = read_log()
    fs = sorted(glob.glob(shotdir + '/frame_*.png'))
    # The newest file may still be being written, unless the run has ended.
    ready = fs if done else fs[:-1]
    if not ready:
        if done:
            break
        time.sleep(2)
        continue
    for f in ready:
        fr = int(os.path.basename(f)[6:12])
        try:
            img = Image.open(f).convert('RGB')
        except Exception:
            time.sleep(1)
            continue
        a = np.asarray(img).astype(np.int16)
        sc = scene_at(fr)
        if first is None:
            first = fr
        warm = fr - first < 60   # the wide view starts a few frames after a snapshot is loaded
        edge_now = set()
        n += 1
        if n % 20 == 0:
            keep(img, fr, 'thumb', full=False)
        # stuck / blank
        if prev is not None and np.array_equal(a, prev):
            same += 1
        else:
            same = 0
        if same == 40:
            flag(fr, 'stuck', 'the picture has not changed for 600 frames', img)
        m = a.mean()
        dark = dark + 1 if (m < 6 or m > 250) else 0
        if dark == 20:
            flag(fr, 'blank', 'blank screen for 300 frames (mean %d)' % m, img)
        # (Battle screens that are the bottom screen alone, with bars: battle-submenu, -list, -choice, -menu.)
        wide = (sc in FIELD or sc in ('battle', 'battle-cmd', 'battle-moves', 'battle-strip')) and not warm
        if wide:
            mid = a[:540, L + 40:R - 40]
            for side, strip in (('left', a[:540, :L]), ('right', a[:540, R:][:, ::-1])):
                # flat-edge
                edge = strip[:, 0]
                run = np.all(strip[:, :12] == edge[:, None, :], axis=(1, 2))
                rows = np.where(run)[0]
                if len(rows) >= 40 and sc in FIELD:   # (battle backgrounds are bands of flat colour; sand and grass that run on from the picture have its colour inside it too)
                    cols, counts = np.unique(edge[rows], axis=0, return_counts=True)
                    c = cols[counts.argmax()]
                    if counts.max() >= 40 and np.all(mid == c, axis=2).mean() <= 0.001:
                        edge_now.add(('flat', side))
                        if ('flat', side) in edge_prev: flag(fr, 'flat-edge', '%s edge: one colour %s over %d rows' % (side, tuple(int(x) for x in c), int(counts.max())), img)
                # black-edge
                blk = np.all(strip < 10, axis=2).mean()
                if blk > 0.08 and np.all(mid < 10, axis=2).mean() < 0.01:
                    edge_now.add(('black', side))
                    if ('black', side) in edge_prev: flag(fr, 'black-edge', '%s: %.0f%% of the side is black' % (side, blk * 100), img)
        # pop-in (field): shots 15 frames apart
        g = np.asarray(img.resize((640, 360))).astype(np.int16).sum(axis=2) // 3
        if sc in FIELD and prevg is not None and fr - prevfr == 15 and scene_at(prevfr) in FIELD:
            d, dx, dy = best_shift(prevg, g)
            # (Not across a fade or a transition: a battle's opening wipe, a door, a flash.)
            calm = abs(float(g.mean()) - float(prevg.mean())) < 8 and (g[:, 80:560] < 8).mean() < 0.03 and (prevg[:, 80:560] < 8).mean() < 0.03
            if d < 10 and calm:
                # The overworld is seen through a perspective camera: rows further away move
                # less than near ones as it scrolls sideways. Each band of rows gets its own
                # sideways shift around the overall one.
                moved = np.roll(np.roll(prevg, dx, axis=1), dy, axis=0).copy()
                m0, m1 = L // 2 + 70, R // 2 - 70
                for y0 in range(0, 360, 30):
                    ys = slice(y0, y0 + 30)
                    best = None
                    for ddx in range(-8, 9, 2):
                        cand = np.roll(moved[ys], ddx, axis=1)
                        e = np.abs(g[ys, m0:m1] - cand[:, m0:m1]).mean()
                        if best is None or e < best[0]:
                            best = (e, cand)
                    moved[ys] = best[1]
                diff = np.abs(g - moved) > 40
                diff[:, :abs(dx) + 12] = False        # what scrolls in at the edges
                diff[:, 640 - abs(dx) - 12:] = False
                if dy > 0: diff[:dy + 4] = False
                if dy < 0: diff[360 + dy - 4:] = False
                for side, x0, x1 in (('left', 0, L // 2), ('right', R // 2, 640)):
                    blk = diff[10:265, x0:x1]
                    cells = blk[:248].reshape(31, 8, (x1 - x0) // 8, 8).mean(axis=(1, 3)) > 0.5
                    if cells.sum() >= 10:
                        flag(fr, 'pop-in', '%s: %d changed blocks the camera move (%d,%d) does not explain' % (side, int(cells.sum()), dx * 2, dy * 2), img, Image.fromarray(prev.astype(np.uint8)))
        # layout fallbacks
        if sc in ('spread', 'battle-menu') and (sc, fr // 3000) not in seen_layout:
            seen_layout.add((sc, fr // 3000))
            flag(fr, 'layout', 'fallback layout "%s"' % sc, img)
        # battle buttons
        if sc in ('battle-cmd', 'battle-moves'):
            above = a[520:540, 640:]
            out = (np.all(above == (0xF0, 0x40, 0x40), axis=2) | np.all(above == (0x10, 0x12, 0x18), axis=2)).mean()
            band = a[548:712, 650:1270]
            filled = (~np.all(band == (0x20, 0x20, 0x20), axis=2)).mean()
            if out > 0.002:
                flag(fr, 'battle-ui', 'button outlines above their band (%.1f%%)' % (out * 100), img)
            elif filled < 0.5:
                flag(fr, 'battle-ui', 'buttons missing (%.0f%% of the band drawn)' % (filled * 100), img)
        prev, prevg, prevfr = a, g, fr
        edge_prev = edge_now
        os.remove(f)
    json.dump({'name': name, 'shots': n, 'flags': flags}, open(name + '/flags.json', 'w'), indent=1)
    if done and not glob.glob(shotdir + '/frame_*.png'):
        break
print(name, n, 'shots', len(flags), 'flags')
