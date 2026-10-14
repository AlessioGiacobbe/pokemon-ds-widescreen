#!/usr/bin/env python3
"""Prints the scroll (dx, dy, residual) between consecutive 256x192 screenshots.
usage: scroll2.py <dir> <first frame> <last frame>"""
import sys
import numpy as np
from PIL import Image
d, first, last = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
def load(f):
    return np.asarray(Image.open("%s/frame_%06d.png" % (d, f)).convert("L"), dtype=np.int32)
def shift(a, b):
    best = None
    for sy in range(-5, 6):
        for sx in range(-5, 6):
            A = a[40 + max(0, -sy):150 - max(0, sy), 20 + max(0, -sx):236 - max(0, sx)]
            B = b[40 + max(0, sy):150 - max(0, -sy), 20 + max(0, sx):236 - max(0, -sx)]
            e = np.abs(B - A).mean()
            if best is None or e < best[0]:
                best = (e, sx, sy)
    return best[1], best[2], round(float(best[0]), 1)
frames = [load(f) for f in range(first, last + 1)]
print(" ".join("%d,%d(%.0f)" % shift(frames[i], frames[i + 1]) for i in range(len(frames) - 1)))
