#!/usr/bin/env python3
"""Makes a copy of gen/ without the recompiled code, for a SoulSilver build that runs on melonDS's
JIT (--jit) or interpreter (--interp) instead.

    python3 tools/soulsilver/tables_only.py gen gen-tables

Without the recompiled code SoulSilver still needs to know which of its functions and overlays
run, for its layouts (the X menu, the Bag, battles, the widescreen overworld): the runtime gets
that from the core's watch on their addresses (rc_watch_attach in runtime/recomp_rt.cpp), using
the chunk table in gen/rc_tables.cpp and each section's code image (gen/rc_image_*.bin) to tell
apart overlays loaded at the same addresses. This copies those and the application table, drops
the 1.7 million recompiled instructions (gen/rc_NNN.cpp) and points the chunk table's functions
at nothing. The build then takes a couple of minutes instead of a quarter of an hour, and the
program is about a tenth of the size.

Build with it as with gen/:  cmake ... -DSSR_JIT=ON -DSSR_GENERATED_DIR=<out dir>
The output, like gen/, holds your game's code (the images): keep it to yourself.
"""
import os
import re
import shutil
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    tables = os.path.join(src, 'rc_tables.cpp')
    if not os.path.isfile(tables):
        sys.exit(f'{tables} is missing: run tools/recomp.py first')
    os.makedirs(out, exist_ok=True)

    names = ['rc_images.S', 'apps_table.cpp'] + [n for n in os.listdir(src) if re.fullmatch(r'rc_image_\d+\.bin', n)]
    for name in names:
        a, b = os.path.join(src, name), os.path.join(out, name)
        # Only what changed is copied, so that a rebuild does not recompile what did not.
        if not os.path.exists(b) or open(a, 'rb').read() != open(b, 'rb').read():
            shutil.copyfile(a, b)

    decl = re.compile(r'^void rc_s\d+_[0-9A-F]+\(')
    fn = re.compile(r', rc_s\d+_[0-9A-F]+\}')
    lines = []
    chunks = 0
    with open(tables) as f:
        for line in f:
            if decl.match(line):
                continue
            line, n = fn.subn(', nullptr}', line)
            chunks += n
            lines.append(line)
    write_if_changed(os.path.join(out, 'rc_tables.cpp'), ''.join(lines))
    write_if_changed(os.path.join(out, 'sources.cmake'),
                     'set(RC_GENERATED_SOURCES\n'
                     '    "${CMAKE_CURRENT_LIST_DIR}/rc_tables.cpp"\n'
                     '    "${CMAKE_CURRENT_LIST_DIR}/rc_images.S"\n'
                     ')\n')
    print(f'{out}: {chunks} chunks, {len(names) - 2} section images, no recompiled code')


def write_if_changed(path, text):
    try:
        if open(path).read() == text:
            return
    except OSError:
        pass
    with open(path, 'w', newline='\n') as f:
        f.write(text)


if __name__ == '__main__':
    main()
