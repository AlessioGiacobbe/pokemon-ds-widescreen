"""Makes the function table for Pokemon Black (US/Europe, IRBO) from the aletheia decompilation's
function list (layout/function_addrs.tsv) and your own ROM, in the same form as Black 2's
(tools/bw2/funcmap.py), so the same frontend and layouts can use it.

    python3 tools/bw1/funcmap.py <aletheia>/layout/function_addrs.tsv <rom.nds> out/funcs.txt
    python3 tools/bw1/funcmap.py <aletheia>/layout/function_addrs.tsv - out/IRBO.txt

With - for the ROM the overlay functions' first bytes are left as ?, for the program to read
from the player's ROM (see tools/bw2/funcmap.py).

Each line: <address> <a|t> <module> <first bytes|-> <name>, as for Black 2. Names are
aletheia's, except for the functions listed in tools/bw1/aliases.txt: those get the Black 2
(pokebw2) name the shared Black 2 layouts look for (FieldmapProc_Update, BtlMain_ProcMain, ...).
The table holds addresses, names and a few bytes per function: not the game's code.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bw2"))
from funcmap import overlays  # noqa: E402  (Black 2's: the same ROM format)

HERE = os.path.dirname(os.path.abspath(__file__))


def load_aliases(path):
    """(module, address) -> Black 2 name."""
    out = {}
    for line in open(path):
        line = line.split("#", 1)[0].split()
        if len(line) < 4:
            continue
        b2name, _b1name, module, addr = line[:4]
        out[(module, int(addr, 16))] = b2name
    return out


def main():
    tsv, rom_path, out_path = sys.argv[1:4]
    if rom_path == "-":
        rom, ovs = None, {}
    else:
        rom = open(rom_path, "rb").read()
        if rom[0x0C:0x10] != b"IRBO":
            print("warning: %s is not Pokemon Black (US/Europe): game code %r" % (rom_path, rom[0x0C:0x10]))
        ovs = overlays(rom)
    aliases = load_aliases(os.path.join(HERE, "aliases.txt"))
    found = set()
    rows = []
    for i, line in enumerate(open(tsv)):
        if i == 0:
            continue
        region, addr, name, isa, size, _path, _static = line.rstrip("\n").split("\t")
        addr = int(addr, 16)
        if region == "arm9":
            module, check = "main", None
        elif region == "arm9_autoload0_ITCM":
            module, check = "itcm", None
        else:
            m = re.match(r"ov(\d+)$", region)
            if not m:
                continue  # the DSi-only code (twl) and overlay 230's second half (ov230d)
            module, check = region, ovs.get(int(m.group(1)))
        first = "?" if rom is None and module.startswith("ov") else "-"
        if check:
            base, data = check
            off = addr - base
            if 0 <= off and off + 4 <= len(data):
                first = data[off:off + 4].hex()
        alias = aliases.get((module, addr))
        if alias:
            found.add((module, addr))
            name = alias
        rows.append((addr, "t" if isa == "thumb" else "a", module, first, name))
    missing = [aliases[k] for k in aliases if k not in found]
    if missing:
        print("warning: not in the function list: " + ", ".join(missing))
    with open(out_path, "w") as f:
        for r in sorted(rows):
            f.write("%08x %s %s %s %s\n" % r)
    print(f"{len(rows)} functions ({len(found)} with Black 2 names) from {len(ovs)} overlays and the main program")


if __name__ == "__main__":
    main()
