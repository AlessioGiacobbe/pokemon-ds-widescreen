"""Finds where Pokemon White (US/Europe, IRAO) has each of Black's (aletheia) functions, and
writes tools/bw1/white_shifts.txt, from which tools/bw1/funcmap.py --white makes White's table.
A developer tool: the shifts are in the repository, so White's table is made without it.

    python3 tools/bw1/match_white.py <aletheia>/layout/function_addrs.tsv <black.nds> <white.nds>

Black and White are the same program with a few small differences: White's main program has
32 more bytes (its static data and the overlays after it start 0x20 later) and its start menu
overlay (88) 32 more. Every function of Black's is looked for in White's same module at the
offset the function before it had, then a little either side: found when its instructions are
the same once addresses, branch targets and literal-pool offsets are masked
(tools/bw1/match_bw2.py's tokens). The shifts are written as ranges: from a Black address on,
White's function is that many bytes later, until the next line of the same module.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_bw2 import Rom, tokens  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
WINDOW = 0x200


def main():
    tsv, black_path, white_path = sys.argv[1:4]
    out_path = sys.argv[4] if len(sys.argv) > 4 else os.path.join(HERE, "white_shifts.txt")
    black, white = Rom(black_path), Rom(white_path)
    funcs = {}
    for i, line in enumerate(open(tsv)):
        if i == 0:
            continue
        region, addr, name, isa, size, _path, _static = line.rstrip("\n").split("\t")
        module = "main" if region == "arm9" else region
        funcs.setdefault(module, []).append((int(addr, 16), name, isa, int(size)))
    lines, found, missed, total = [], 0, [], 0
    for module in sorted(funcs, key=lambda m: (m != "main", int(m[2:]) if m[2:].isdigit() else -1, m)):
        if module == "main":
            delta = 0
        elif module.startswith("ov") and module[2:].isdigit() and int(module[2:]) in black.ovs:
            oid = int(module[2:])
            delta = white.ovs[oid][0] - black.ovs[oid][0]
        else:
            continue  # the ITCM's code and the DSi-only parts: not watched
        last = None
        for addr, name, isa, size in sorted(funcs[module]):
            total += 1
            code = black.read(module, addr, size)
            if not code or size < 4:
                missed.append((module, addr, name, "no code"))
                continue
            tok = tokens(code, isa)
            step = 4 if isa == "arm" else 2
            hit = None
            for k in range(0, WINDOW // step + 1):
                for d in ((delta,) if k == 0 else (delta + k * step, delta - k * step)):
                    w = white.read(module, addr + d, size)
                    if w is not None and tokens(w, isa) == tok:
                        hit = d
                        break
                if hit is not None:
                    break
            if hit is None:
                missed.append((module, addr, name, "kept %+#x" % delta))
                continue
            found += 1
            # (A short function's masked instructions can be found in more than one place: it
            # moves the shift only where it is where the shift says.)
            if size < 24 and hit != delta:
                continue
            if hit != last:
                lines.append("%s %08x %+#x %s" % (module, addr, hit, name))
                last = hit
            delta = hit
    with open(out_path, "w") as f:
        f.write("# Pokemon White (IRAO): where Black's (aletheia) functions are, as shifts from Black's\n"
                "# addresses: <module> <Black address from which> <White - Black> <the first function so moved>.\n"
                "# Made by tools/bw1/match_white.py; read by tools/bw1/funcmap.py --white.\n")
        for l in lines:
            f.write(l + "\n")
    print(f"{found} of {total} functions found in White, {len(lines)} shift lines; not found: {len(missed)}")
    for m in missed[:40]:
        print("  %s %08x %s (%s)" % m)


if __name__ == "__main__":
    main()
