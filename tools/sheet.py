#!/usr/bin/env python3
"""Tile screenshots into one labelled contact sheet: sheet.py out.png shot1.png shot2.png ..."""
import os
import sys
from PIL import Image, ImageDraw

out, paths = sys.argv[1], sys.argv[2:]
cols = min(len(paths), 6)
rows = (len(paths) + cols - 1) // cols
imgs = [Image.open(p) for p in paths]
w, h = imgs[0].size
sheet = Image.new('RGB', (cols * (w + 4), rows * (h + 16)), (40, 40, 40))
draw = ImageDraw.Draw(sheet)
for i, (p, im) in enumerate(zip(paths, imgs)):
    x, y = (i % cols) * (w + 4), (i // cols) * (h + 16)
    sheet.paste(im, (x, y + 14))
    draw.text((x + 2, y + 1), os.path.basename(p).replace('frame_', '').replace('.png', ''), fill=(255, 255, 0))
sheet.save(out)
