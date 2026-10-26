#!/usr/bin/env python3
"""HeartGold / SoulSilver saves (512 KB): the party, for test saves.

    sav4.py SAVE                         print the party
    sav4.py SAVE OUT setexp SLOT LEVEL   put the party Pokemon SLOT one EXP point below LEVEL
                                         (and at LEVEL-1), so that the next battle it wins
                                         takes it to LEVEL

Layout (as PKHeX's SAV4HGSS): two copies of the save, at 0 and 0x40000; the general block is
0xF628 bytes with a 0x10-byte footer (save count, size, magic 0x20060623, CRC16-CCITT at the
end, over the block without its footer); the party count is at 0x94, the party (6 x 236
bytes) at 0x98. A Pokemon (PK4): PID, checksum, four 32-byte blocks shuffled by the PID and
encrypted from the checksum; the party data after them encrypted from the PID.
"""
import struct
import sys

GEN, FOOT = 0xF628, 0x10
SHUFFLE = ['ABCD', 'ABDC', 'ACBD', 'ACDB', 'ADBC', 'ADCB', 'BACD', 'BADC', 'BCAD', 'BCDA', 'BDAC', 'BDCA',
           'CABD', 'CADB', 'CBAD', 'CBDA', 'CDAB', 'CDBA', 'DABC', 'DACB', 'DBAC', 'DBCA', 'DCAB', 'DCBA']


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def active(d):
    """Offset of the copy the game loads: the valid one saved last."""
    best = None
    for base in (0, 0x40000):
        f = base + GEN - FOOT
        count, size, magic = struct.unpack_from('<III', d, f)
        ok = magic == 0x20060623 and size == GEN and crc16(d[base:base + GEN - FOOT]) == struct.unpack_from('<H', d, base + GEN - 2)[0]
        if ok and (best is None or count > best[0]):
            best = (count, base)
    return best[1]


def crypt(data, seed):
    out = bytearray(data)
    x = seed
    for i in range(0, len(out), 2):
        x = (x * 0x41C64E6D + 0x6073) & 0xFFFFFFFF
        k = x >> 16
        v = struct.unpack_from('<H', out, i)[0] ^ k
        struct.pack_into('<H', out, i, v)
    return bytes(out)


def decode(pk):
    pid, chk = struct.unpack_from('<IxxH', pk, 0)
    blocks = crypt(pk[8:0x88], chk)
    order = SHUFFLE[((pid & 0x3E000) >> 13) % 24]
    plain = bytearray(128)
    for i, letter in enumerate(order):
        j = 'ABCD'.index(letter)
        plain[j * 32:(j + 1) * 32] = blocks[i * 32:(i + 1) * 32]
    party = crypt(pk[0x88:0xEC], pid)
    return pid, bytes(plain), party


def encode(pid, plain, party):
    order = SHUFFLE[((pid & 0x3E000) >> 13) % 24]
    shuffled = bytearray(128)
    for i, letter in enumerate(order):
        j = 'ABCD'.index(letter)
        shuffled[i * 32:(i + 1) * 32] = plain[j * 32:(j + 1) * 32]
    chk = sum(struct.unpack('<64H', plain)) & 0xFFFF
    return struct.pack('<IHH', pid, 0, chk) + crypt(bytes(shuffled), chk) + crypt(party, pid)


def medium_slow(n):
    return 6 * n ** 3 // 5 - 15 * n * n + 100 * n - 140


def party(d, base):
    n = d[base + 0x94]
    out = []
    for i in range(n):
        o = base + 0x98 + i * 236
        pid, plain, pt = decode(d[o:o + 236])
        species, item = struct.unpack_from('<HH', plain, 0)
        exp = struct.unpack_from('<I', plain, 8)[0]
        level = pt[4]
        out.append((i, o, species, level, exp))
    return out


if __name__ == '__main__':
    d = bytearray(open(sys.argv[1], 'rb').read())
    base = active(d)
    print('active copy at 0x%X' % base)
    for i, o, species, level, exp in party(d, base):
        print('slot %d: species %d level %d exp %d' % (i, species, level, exp))
    if len(sys.argv) > 3 and sys.argv[3] == 'setexp':
        slot, lvl = int(sys.argv[4]), int(sys.argv[5])
        o = base + 0x98 + slot * 236
        pid, plain, pt = decode(d[o:o + 236])
        plain = bytearray(plain)
        pt = bytearray(pt)
        struct.pack_into('<I', plain, 8, medium_slow(lvl) - 1)
        pt[4] = lvl - 1
        d[o:o + 236] = encode(pid, bytes(plain), bytes(pt))
        struct.pack_into('<H', d, base + GEN - 2, crc16(d[base:base + GEN - FOOT]))
        open(sys.argv[2], 'wb').write(d)
        print('slot %d: level %d, exp %d (one short of level %d)' % (slot, lvl - 1, medium_slow(lvl) - 1, lvl))
