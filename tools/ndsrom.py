#!/usr/bin/env python3
"""Reads the ARM9 code of a Nintendo DS ROM: the main program and its overlays,
decompressed. Used to get the code to recompile from the player's own copy of the
game instead of from the decompilation's build.

As a script: ndsrom.py <rom> <out dir> writes main.bin and overlay_NNN.bin.
"""
import os
import struct
import sys


def blz_decompress(data):
    """Undoes Nintendo's "backwards LZ" compression (the BLZ of ndstool and of the
    DS SDK's compstatic): the compressed bytes are read from the end towards the start,
    and the last 8 bytes say how much of the file is compressed and how much larger
    the result is."""
    if len(data) < 8:
        return bytes(data)
    bounds, extra = struct.unpack_from('<II', data, len(data) - 8)
    if extra == 0:
        return bytes(data)
    header_len = bounds >> 24
    packed_len = bounds & 0xFFFFFF
    out = bytearray(data) + bytearray(extra)
    src = len(data) - header_len          # compressed stream, read backwards
    dst = len(data) + extra               # output, written backwards
    stop = len(data) - packed_len         # everything before this is stored as is
    while src > stop:
        src -= 1
        flags = data[src]
        for _ in range(8):
            if src <= stop:
                break
            if flags & 0x80:
                src -= 2
                info = data[src] | (data[src + 1] << 8)
                length = (info >> 12) + 3
                disp = (info & 0xFFF) + 3
                for _ in range(length):
                    dst -= 1
                    out[dst] = out[dst + disp]
            else:
                src -= 1
                dst -= 1
                out[dst] = data[src]
            flags = (flags << 1) & 0xFF
    return bytes(out)


NITROCODE = 0xDEC00621


def read_rom(path):
    """Returns (main, overlays): the ARM9 main program as loaded at 0x02000000 and a
    dict of overlay id -> (ram address, bytes), all decompressed."""
    rom = open(path, 'rb').read()
    arm9_off, arm9_entry, arm9_ram, arm9_size = struct.unpack_from('<IIII', rom, 0x20)
    fat_off, fat_size = struct.unpack_from('<II', rom, 0x48)
    ovt_off, ovt_size = struct.unpack_from('<II', rom, 0x50)

    main = bytearray(rom[arm9_off:arm9_off + arm9_size])
    # The 12 bytes after the main program (marker, module parameter offset, ...) belong
    # to it in the build's image as well.
    footer = rom[arm9_off + arm9_size:arm9_off + arm9_size + 12]
    # The module parameters (found through the "nitrocode" marker) say where the
    # compressed part of the main program ends; 0 means it is not compressed.
    marker = struct.pack('<I', NITROCODE)
    pos = main.find(marker)
    while pos >= 0:
        params = pos - 0x1C
        if params >= 0:
            compressed_end = struct.unpack_from('<I', main, params + 0x14)[0]
            if compressed_end and arm9_ram < compressed_end <= arm9_ram + len(main):
                end = compressed_end - arm9_ram
                main = bytearray(blz_decompress(bytes(main[:end])) + bytes(main[end:]))
                # The program clears this after decompressing itself; the build's
                # uncompressed image has it as 0 too.
                struct.pack_into('<I', main, params + 0x14, 0)
            break
        pos = main.find(marker, pos + 1)
    if len(footer) == 12 and struct.unpack_from('<I', footer, 0)[0] == NITROCODE:
        main += footer

    overlays = {}
    for i in range(ovt_size // 32):
        ovy_id, ram, ram_size, bss, sinit_start, sinit_end, file_id, comp = struct.unpack_from('<8I', rom, ovt_off + i * 32)
        start, end = struct.unpack_from('<II', rom, fat_off + file_id * 8)
        data = rom[start:end]
        if comp & (1 << 24):
            data = blz_decompress(data[:comp & 0xFFFFFF])
        overlays[ovy_id] = (ram, bytes(data))
    return bytes(main), overlays


if __name__ == '__main__':
    main, overlays = read_rom(sys.argv[1])
    os.makedirs(sys.argv[2], exist_ok=True)
    open(os.path.join(sys.argv[2], 'main.bin'), 'wb').write(main)
    for i, (ram, data) in overlays.items():
        open(os.path.join(sys.argv[2], f'overlay_{i:03d}.bin'), 'wb').write(data)
    print(f'main {len(main)} bytes, {len(overlays)} overlays')
