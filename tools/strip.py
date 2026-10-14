#!/usr/bin/env python3
"""Contact sheets of a shot directory: strip.py <dir> <out prefix> <first frame> <last frame> [step]"""
import glob, sys
from PIL import Image, ImageDraw
d, out, a, b = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
step = int(sys.argv[5]) if len(sys.argv) > 5 else 1
ps = [p for p in sorted(glob.glob(d + "/frame_*.png")) if a <= int(p[-10:-4]) <= b][::step]
for n in range(0, len(ps), 48):
    chunk = ps[n:n + 48]
    s = Image.new("RGB", (8 * 244, ((len(chunk) + 7) // 8) * 139), (70, 0, 70))
    dr = ImageDraw.Draw(s)
    for i, p in enumerate(chunk):
        s.paste(Image.open(p).resize((240, 135)), ((i % 8) * 244, (i // 8) * 139))
        dr.text(((i % 8) * 244 + 2, (i // 8) * 139 + 1), p[-10:-4].lstrip("0"), fill=(255, 255, 0))
    s.save("%s_%d.png" % (out, n // 48))
    print("%s_%d.png" % (out, n // 48))
