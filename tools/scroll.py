#!/usr/bin/env python3
"""Prints the horizontal scroll (pixels, residual error) between consecutive screenshots.
usage: scroll.py <dir> <first frame> <last frame>"""
import sys
import numpy as np
from PIL import Image

d, first, last = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])

def load(f):
    return np.asarray(Image.open("%s/frame_%06d.png" % (d, f)).convert("L"), dtype=np.int32)

def shift(a, b):
    best = None
    for s in range(-6, 7):
        if s >= 0:
            e = np.abs(b[60:190, 8 + s:248] - a[60:190, 8:248 - s]).mean()
        else:
            e = np.abs(b[60:190, 8:248 + s] - a[60:190, 8 - s:248]).mean()
        if best is None or e < best[0]:
            best = (e, s)
    return best[1], round(float(best[0]), 1)

frames = [load(f) for f in range(first, last + 1)]
print([shift(frames[i], frames[i + 1]) for i in range(len(frames) - 1)])
