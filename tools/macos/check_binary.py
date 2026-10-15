#!/usr/bin/env python3
"""Check Mach-O deployment targets and optional absence of third-party dylibs."""
import argparse
from pathlib import Path
import re
import subprocess


def check_binary(path, max_macos, system_libraries_only=False):
    def version(value):
        parts = tuple(map(int, value.split('.')))
        return parts + (0,) * (3 - len(parts))

    commands = subprocess.check_output(['otool', '-l', str(path)], text=True)
    minimums = re.findall(r'\bcmd LC_(?:BUILD_VERSION|VERSION_MIN_MACOSX)\s.*?'
                          r'\b(?:minos|version) ([\d.]+)', commands, re.S)
    if not minimums:
        raise RuntimeError(f'{path}: no macOS deployment target found')
    for minimum in minimums:
        if version(minimum) > version(max_macos):
            raise RuntimeError(f'{path}: requires macOS {minimum}, expected {max_macos} or earlier')
    if system_libraries_only:
        deps = subprocess.check_output(['otool', '-L', str(path)], text=True)
        for line in deps.splitlines()[1:]:
            dep = line.strip().split(' (compatibility version')[0]
            if not dep.startswith(('/usr/lib/', '/System/Library/')):
                raise RuntimeError(f'{path}: unexpected dependency {dep}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('binary', type=Path)
    ap.add_argument('--max-macos', default='11.0')
    ap.add_argument('--system-libraries-only', action='store_true')
    args = ap.parse_args()
    check_binary(args.binary, args.max_macos, args.system_libraries_only)
    print(f'{args.binary}: macOS {args.max_macos} compatibility checks passed')
