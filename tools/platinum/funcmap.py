"""Makes the function table for Pokemon Platinum (US, CPUE; the English release of every region
plays the same code) from a build of the pokeplatinum decompilation (pret/pokeplatinum, rev 1:
build/main.nef and platinum.us/main.lsf.in) and your own ROM.

    python3 tools/platinum/funcmap.py <pokeplatinum> <rom.nds> out/CPUE.txt
    python3 tools/platinum/funcmap.py <pokeplatinum> - out/CPUE.txt

With - for the ROM, the overlay functions' first bytes are left as ?: the program reads them
from the player's ROM when it starts (frontend/funcwatch.h), so the table can go in a program
that is shared (the Switch's, tools/switch/build_game.sh).

Each line: <address> <a|t> <module> <first bytes|-> <name>, as tools/bw2/funcmap.py writes it.
The table holds addresses, names and a few bytes per function: not the game's code.
"""
import os
import re
import struct
import sys

from elftools.elf.elffile import ELFFile


def overlay_ids(lsf):
    """Overlay names in the linker spec, in order: their position is the overlay's number."""
    ids = {}
    for line in open(lsf):
        m = re.match(r"\s*Overlay\s+(\S+)", line)
        if m:
            ids[m.group(1)] = len(ids)
    return ids


def rom_overlays(rom):
    """Each overlay's RAM address and bytes, from the ROM (Platinum's overlays are not compressed
    in the ROM except where flagged; flagged ones are left out)."""
    ovt, ovs, fat = struct.unpack_from("<I", rom, 0x50)[0], struct.unpack_from("<I", rom, 0x54)[0], struct.unpack_from("<I", rom, 0x48)[0]
    out = {}
    for i in range(ovs // 32):
        oid, ram, size, bss, s0, s1, fid, flag = struct.unpack_from("<8I", rom, ovt + i * 32)
        start, end = struct.unpack_from("<II", rom, fat + fid * 8)
        if flag & 0x01000000:
            continue
        out[oid] = (ram, rom[start:end])
    return out


def main():
    decomp, rom_path, out_path = sys.argv[1:4]
    ids = overlay_ids(os.path.join(decomp, "platinum.us", "main.lsf.in"))
    rom, ovs = None, {}
    if rom_path != "-":
        rom = open(rom_path, "rb").read()
        if rom[0x0C:0x10] != b"CPUE":
            print("warning: %s is not Pokemon Platinum (English): game code %r" % (rom_path, rom[0x0C:0x10]))
        ovs = rom_overlays(rom)
    elf = ELFFile(open(os.path.join(decomp, "build", "main.nef"), "rb"))
    symtab = elf.get_section_by_name(".symtab")
    # ARM or Thumb: the mapping symbols ($a, $t) the linker leaves at the start of each stretch.
    marks = {}
    for s in symtab.iter_symbols():
        if s.name in ("$a", "$t") and isinstance(s["st_shndx"], int):
            marks.setdefault(s["st_shndx"], {})[s["st_value"] & ~1] = s.name == "$t"
    rows = set()
    for s in symtab.iter_symbols():
        if s["st_info"]["type"] != "STT_FUNC" or not isinstance(s["st_shndx"], int) or not s.name or s.name.startswith("$"):
            continue
        sec = elf.get_section(s["st_shndx"]).name
        addr = s["st_value"]
        thumb = addr & 1
        addr &= ~1
        thumb = thumb or marks.get(s["st_shndx"], {}).get(addr, False)
        if sec == "main":
            module = "main"
        elif sec == "ITCM":
            module = "itcm"
        elif sec == "DTCM":
            module = "dtcm"
        elif sec in ids:
            module = "ov%d" % ids[sec]
        else:
            continue
        first = "-"
        if module.startswith("ov"):
            first = "?"
            if rom is not None:
                first = "-"
                ov = ovs.get(ids[sec])
                if ov and 0 <= addr - ov[0] and addr - ov[0] + 4 <= len(ov[1]):
                    first = ov[1][addr - ov[0]:addr - ov[0] + 4].hex()
        rows.add((addr, "t" if thumb else "a", module, first, s.name))
    with open(out_path, "w") as f:
        for r in sorted(rows):
            f.write("%08x %s %s %s %s\n" % r)
    print("%d functions" % len(rows))


if __name__ == "__main__":
    main()
