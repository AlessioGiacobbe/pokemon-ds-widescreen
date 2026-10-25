"""Finds Black's (aletheia) counterparts of Black 2's (pokebw2) functions, by their code: for
when the shared Black 2 layouts start watching a function that tools/bw1/aliases.txt does not
list yet. A developer tool: funcs.txt is made without it (tools/bw1/funcmap.py).

    python3 tools/bw1/match_bw2.py <aletheia>/layout/function_addrs.tsv <black.nds> \
        <pokebw2>/config/b2_us/arm9 <black2.nds> NAME [NAME...]

For each Black 2 function NAME it prints a line for aliases.txt (or "-" if none was found).
Takes under a minute. How it matches:
  1. exact: the same instructions once addresses, branch targets and literal-pool offsets are
     masked, and only one such function in each game;
  2. align: between two exact matches in one module of Black 2 and the same two in one module
     of Black, the functions in between are paired in order (sequence alignment) when their
     masked instructions are at least 55% alike.
Check what it finds: the aletheia name and source file should fit (BtlMain_ProcMain ->
BTL_MAIN_ProcMain in btl_main.c); a low similarity or a function Black 2 rewrote needs a look
by hand (ConfirmDialog_Update in aliases.txt).
"""
import collections
import difflib
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bw2"))
from funcmap import blz_decompress, overlays  # noqa: E402

SYM = re.compile(r"^(\S+) kind:function\((arm|thumb)(?:,size=0x([0-9a-fA-F]+))?[^)]*\) addr:0x([0-9a-fA-F]+)")


class Rom:
    def __init__(self, path):
        rom = open(path, "rb").read()
        off, _entry, ram, size = struct.unpack_from("<IIII", rom, 0x20)
        data = bytearray(rom[off:off + size])
        p = data.find(b"\x21\x06\xc0\xde\xde\xc0\x06\x21") - 0x1C  # the ARM9's module parameters
        comp_end = struct.unpack_from("<I", data, p + 0x14)[0]
        if comp_end:
            n = comp_end - ram
            data = bytearray(blz_decompress(bytes(data[:n]))) + data[n:]
        self.ram, self.main, self.ovs = ram, bytes(data), overlays(rom)

    def read(self, module, addr, size):
        if module == "main":
            base, data = self.ram, self.main
        else:
            m = re.match(r"ov(\d+)$", module)
            if not m or int(m.group(1)) not in self.ovs:
                return None
            base, data = self.ovs[int(m.group(1))]
        o = addr - base
        return data[o:o + size] if 0 <= o and o + size <= len(data) else None


def is_addr(w):
    return 0x01FF8000 <= w < 0x02400000 or 0x027E0000 <= w < 0x02800000


def tokens(code, isa):
    t = []
    if isa == "arm":
        for i in range(0, len(code) - 3, 4):
            w = struct.unpack_from("<I", code, i)[0]
            if is_addr(w):
                t.append(0xADD0ADD0)
                continue
            if (w >> 25) & 7 == 5:
                w &= 0xFF000000                                  # b / bl / blx
            elif (w & 0x0F7F0000) == 0x051F0000:
                w &= 0xFFFFF000                                  # ldr rX, [pc, #imm]
            t.append(w)
        return tuple(t)
    i, n = 0, len(code)
    while i + 1 < n:
        h = struct.unpack_from("<H", code, i)[0]
        if i % 4 == 0 and i + 3 < n and is_addr(struct.unpack_from("<I", code, i)[0]):
            t.append(0xADD0ADD0)
            i += 4
            continue
        if (h >> 11) == 0x1E and i + 3 < n:                        # bl / blx pair
            t.append(0xF000 | ((struct.unpack_from("<H", code, i + 2)[0] >> 11) << 16))
            i += 4
            continue
        if (h >> 11) == 0x09:
            h &= 0xFF00                                          # ldr rX, [pc, #imm]
        elif (h >> 12) == 0xD and ((h >> 8) & 0xF) < 0xE:
            h &= 0xFF00                                          # b<cond>
        elif (h >> 11) == 0x1C:
            h &= 0xF800                                          # b
        t.append(h)
        i += 2
    return tuple(t)


def load_b1(tsv, rom):
    out = []
    for i, line in enumerate(open(tsv)):
        if i == 0:
            continue
        region, addr, name, isa, size, path, _static = line.rstrip("\n").split("\t")
        module = "main" if region == "arm9" else region
        f = dict(name=name, isa=isa, size=int(size), addr=int(addr, 16), module=module, file=path)
        code = rom.read(module, f["addr"], f["size"])
        f["tok"] = tokens(code, isa) if code else None
        out.append(f)
    return out


def load_b2(cfg, rom):
    out = []

    def read(path, module):
        if not os.path.exists(path):
            return
        for line in open(path):
            m = SYM.match(line)
            if not m:
                continue
            f = dict(name=m.group(1), isa=m.group(2), size=int(m.group(3) or "0", 16), addr=int(m.group(4), 16), module=module)
            code = rom.read(module, f["addr"], f["size"]) if f["size"] else None
            f["tok"] = tokens(code, f["isa"]) if code else None
            out.append(f)

    read(os.path.join(cfg, "symbols.txt"), "main")
    od = os.path.join(cfg, "overlays")
    for sub in sorted(os.listdir(od)):
        read(os.path.join(od, sub, "symbols.txt"), sub)
    return out


def match(b1, b2, thr=0.55):
    idx1, idx2 = collections.defaultdict(list), collections.defaultdict(list)
    for i, f in enumerate(b1):
        if f["tok"]:
            idx1[(f["isa"], f["tok"])].append(i)
    for i, f in enumerate(b2):
        if f["tok"]:
            idx2[(f["isa"], f["tok"])].append(i)
    m21, how = {}, {}
    for k, v in idx1.items():
        if len(v) == 1 and len(idx2.get(k, [])) == 1:
            m21[idx2[k][0]] = v[0]
            how[idx2[k][0]] = "exact"
    used1 = set(m21.values())

    def sim(i, j):
        a, b = b2[i], b1[j]
        if a["isa"] != b["isa"] or not a["tok"] or not b["tok"]:
            return 0.0
        la, lb = len(a["tok"]), len(b["tok"])
        if min(la, lb) / max(la, lb) < 0.5:
            return 0.0
        return difflib.SequenceMatcher(None, a["tok"], b["tok"], autojunk=False).ratio()

    by2, by1 = collections.defaultdict(list), collections.defaultdict(list)
    for i, f in enumerate(b2):
        by2[f["module"]].append(i)
    for j, f in enumerate(b1):
        by1[f["module"]].append(j)
    for k in by2:
        by2[k].sort(key=lambda i: b2[i]["addr"])
    for k in by1:
        by1[k].sort(key=lambda j: b1[j]["addr"])
    pos1 = {j: p for l in by1.values() for p, j in enumerate(l)}
    for l2 in by2.values():
        anchors = [p for p, i in enumerate(l2) if i in m21]
        bounds = [(-1, None)] + [(p, m21[l2[p]]) for p in anchors] + [(len(l2), None)]
        for (pa, ja), (pb, jb) in zip(bounds, bounds[1:]):
            g2 = [l2[p] for p in range(pa + 1, pb) if l2[p] not in m21]
            if not g2 or (ja is None and jb is None):
                continue
            m1 = b1[ja if ja is not None else jb]["module"]
            if ja is not None and jb is not None and b1[jb]["module"] != m1:
                jb = None
            L1 = by1[m1]
            s = pos1[ja] + 1 if ja is not None else max(0, pos1[jb] - len(g2) - 8)
            e = pos1[jb] if jb is not None else min(len(L1), pos1[ja] + 1 + len(g2) + 8)
            if e <= s or e - s > 400 or len(g2) > 400:
                continue
            g1 = [j for j in L1[s:e] if j not in used1]
            if not g1:
                continue
            n, m = len(g2), len(g1)
            S = [[0.0] * (m + 1) for _ in range(n + 1)]
            B = [[0] * (m + 1) for _ in range(n + 1)]
            V = {}
            for x in range(1, n + 1):
                for y in range(1, m + 1):
                    v = V[(x, y)] = sim(g2[x - 1], g1[y - 1])
                    best, bk = S[x - 1][y], 1
                    if S[x][y - 1] > best:
                        best, bk = S[x][y - 1], 2
                    if v >= thr and S[x - 1][y - 1] + v > best:
                        best, bk = S[x - 1][y - 1] + v, 3
                    S[x][y], B[x][y] = best, bk
            x, y = n, m
            while x and y:
                if B[x][y] == 3:
                    i, j = g2[x - 1], g1[y - 1]
                    m21[i], how[i] = j, "align%.2f" % V[(x, y)]
                    used1.add(j)
                    x, y = x - 1, y - 1
                elif B[x][y] == 1:
                    x -= 1
                else:
                    y -= 1
    return m21, how


def main():
    tsv, rom1, cfg, rom2 = sys.argv[1:5]
    names = sys.argv[5:]
    b1 = load_b1(tsv, Rom(rom1))
    b2 = load_b2(cfg, Rom(rom2))
    m21, how = match(b1, b2)
    print("# %d of Black 2's %d functions matched" % (len(m21), len(b2)), file=sys.stderr)
    for name in names:
        hits = [i for i, f in enumerate(b2) if f["name"] == name]
        j = m21.get(hits[0]) if hits else None
        if j is None:
            print("%-28s -" % name)
            continue
        f = b1[j]
        print("%-28s %-36s %-6s %08x  %s  (%s)" % (name, f["name"], f["module"], f["addr"], how[hits[0]], f["file"].split("/")[-1]))


if __name__ == "__main__":
    main()
