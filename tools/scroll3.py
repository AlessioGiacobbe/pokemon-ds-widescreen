#!/usr/bin/env python3
"""Prints the scroll (dx, dy) in output pixels between consecutive screenshots of any size.
usage: scroll3.py <dir> <first frame> <last frame> [search range]"""
import sys
import numpy as np
from PIL import Image
d, first, last = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
R = int(sys.argv[4]) if len(sys.argv) > 4 else 10
def load(f):
    return np.asarray(Image.open("%s/frame_%06d.png" % (d, f)).convert("L"), dtype=np.int32)
def shift(a, b):
    h, w = a.shape
    best = None
    for sy in range(-R, R + 1):
        for sx in range(-R, R + 1):
            A = a[R + max(0, -sy):h // 3 - max(0, sy), R + max(0, -sx):w - R - max(0, sx)]
            B = b[R + max(0, sy):h // 3 - max(0, -sy), R + max(0, sx):w - R - max(0, -sx)]
            e = np.abs(B - A).mean()
            if best is None or e < best[0]:
                best = (e, sx, sy)
    return best[1], best[2]
frames = [load(f) for f in range(first, last + 1)]
print(" ".join("%d,%d" % shift(frames[i], frames[i + 1]) for i in range(len(frames) - 1)))
