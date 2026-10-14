#!/usr/bin/env python3
"""Writes the overworld's movement permissions as one grid for the test navigator.

usage: navmap.py <pokeheartgold dir> <out.bin> [matrix file]
Output: u16 width, u16 height (in tiles), then width*height u16 permissions
(low byte tile behaviour, bit 15 set when the tile blocks movement).
"""
import struct
import sys

root, out = sys.argv[1], sys.argv[2]
matrix = sys.argv[3] if len(sys.argv) > 3 else root + "/files/fielddata/mapmatrix/map_matrix/map_matrix_0000_EVERYWHERE.bin"


def narc_members(path):
    d = open(path, "rb").read()
    assert d[:4] == b"NARC"
    pos = 16
    assert d[pos:pos + 4] == b"BTAF"
    size, count = struct.unpack_from("<II", d, pos + 4)
    ents = [struct.unpack_from("<II", d, pos + 12 + 8 * i) for i in range(count)]
    pos += size
    assert d[pos:pos + 4] == b"BTNF"
    pos += struct.unpack_from("<I", d, pos + 4)[0]
    assert d[pos:pos + 4] == b"GMIF"
    base = pos + 8
    return [d[base + a:base + b] for a, b in ents]


m = open(matrix, "rb").read()
w, h, has_hdr, has_alt, nlen = m[0], m[1], m[2], m[3], m[4]
pos = 5 + nlen
if has_hdr:
    pos += w * h * 2
if has_alt:
    pos += w * h
models = struct.unpack_from("<%dH" % (w * h), m, pos)

land = narc_members(root + "/files/a/0/6/5")
grid = [0x8000] * (w * 32 * h * 32)
for cz in range(h):
    for cx in range(w):
        idx = models[cz * w + cx]
        if idx == 0xFFFF or idx >= len(land):
            continue
        f = land[idx]
        perm_size = struct.unpack_from("<I", f, 0)[0]
        p = 16
        if struct.unpack_from("<H", f, p)[0] == 0x1234:
            p += 4 + struct.unpack_from("<H", f, p + 2)[0]
        perms = struct.unpack_from("<%dH" % (perm_size // 2), f, p)
        for z in range(32):
            for x in range(32):
                grid[(cz * 32 + z) * (w * 32) + cx * 32 + x] = perms[z * 32 + x]
with open(out, "wb") as fo:
    fo.write(struct.pack("<HH", w * 32, h * 32))
    fo.write(struct.pack("<%dH" % len(grid), *grid))
print("matrix %dx%d cells" % (w, h))
