"""Turns a DS save from another tool into the raw save file ssrecomp and melonDS use.

    python3 tools/bw2/convert_save.py <input> <output.sav>

Handles:
  .dsv (DeSmuME, DraStic): the raw save followed by a 122-byte footer
  .duc / .dss / .dst (Action Replay Max DS and similar): a header in front of the raw save
  raw saves: copied as they are
The raw save of Pokemon Black/White and Black 2/White 2 is 512 KB.
"""
import sys

DESMUME_FOOTER = b"|-DESMUME SAVE-|"


def largest_pow2_at_most(n):
    p = 1
    while p * 2 <= n:
        p *= 2
    return p


# Gen 5 keeps two copies of its main save data 0x26000 bytes apart; the end of each copy's
# block table holds this marker at a fixed place (Black 2 / White 2).
GEN5_MARKER = bytes.fromhex("27350531")
B2W2_MARKER_AT = 0x25F9C
GEN5_COPY = 0x26000


def gen5_overlapping_header(data, raw):
    """Some converters write the Action Replay header over the start of the save instead of in
    front of it, leaving the save at offset 0 with its first bytes lost. The backup copy of
    those bytes is put back in their place."""
    head = len(data) - raw
    d = bytearray(data[:raw])
    if d[B2W2_MARKER_AT:B2W2_MARKER_AT + 4] != GEN5_MARKER:
        return None
    if data[head + B2W2_MARKER_AT:head + B2W2_MARKER_AT + 4] == GEN5_MARKER:
        return None  # an ordinary header in front: not this case
    d[0:head] = d[GEN5_COPY:GEN5_COPY + head]
    return bytes(d)


def convert(data):
    if data.endswith(DESMUME_FOOTER):
        # The footer is 122 bytes; the save size is also stored in it, but the power of two
        # below the file size is the save either way.
        return data[:largest_pow2_at_most(len(data))], "DeSmuME/DraStic footer removed"
    size = len(data)
    raw = largest_pow2_at_most(size)
    extra = size - raw
    if extra == 0:
        return data, "already a raw save"
    if extra < 4096:
        fixed = gen5_overlapping_header(data, raw)
        if fixed:
            return fixed, f"{extra}-byte header written over the save's start; those bytes restored from the backup copy"
        # Action Replay style: a header of `extra` bytes, then the save.
        return data[extra:], f"{extra}-byte header removed"
    raise SystemExit(f"unrecognised save layout: {size} bytes")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    out, what = convert(data)
    open(dst, "wb").write(out)
    print(f"{src}: {len(data)} bytes -> {dst}: {len(out)} bytes ({what})")


if __name__ == "__main__":
    main()
