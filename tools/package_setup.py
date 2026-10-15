#!/usr/bin/env python3
"""Packages ssrecomp_setup.py and the project's source into one executable with
PyInstaller (dist/ssrecomp-setup, or dist/ssrecomp-setup.exe on Windows). It holds no
game code: the game is still recompiled from the player's own ROM and compiled on
their machine. Run it on the system the executable is for.

usage: pip install pyinstaller certifi && python tools/package_setup.py
"""
import hashlib
import os
import shutil
import sys
import tempfile

import PyInstaller.__main__

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# What the build needs; build output, generated code and anything of the game stay out.
INCLUDE = ['CMakeLists.txt', 'LICENSE', 'README.md', 'CHANGELOG.md', 'run.sh', 'frontend', 'runtime', 'tools', 'layouts',
           'melonDS']
SKIP_DIRS = {'.git', '.github', '__pycache__', 'build', 'gen'}

stage = tempfile.mkdtemp()
project = os.path.join(stage, 'project')
digest = hashlib.sha1()
for item in INCLUDE:
    src = os.path.join(ROOT, item)
    dst = os.path.join(project, item)
    if os.path.isdir(src):
        shutil.copytree(src, dst, ignore=lambda d, names: [n for n in names if n in SKIP_DIRS])
    else:
        os.makedirs(project, exist_ok=True)
        shutil.copy2(src, dst)
for base, dirs, files in sorted(os.walk(project)):
    dirs.sort()
    for f in sorted(files):
        digest.update(os.path.relpath(os.path.join(base, f), project).encode())
        digest.update(open(os.path.join(base, f), 'rb').read())
with open(os.path.join(stage, 'bundle.id'), 'w') as f:
    f.write(digest.hexdigest())

sep = os.pathsep
PyInstaller.__main__.run([
    os.path.join(ROOT, 'ssrecomp_setup.py'),
    '--onefile', '--name', 'ssrecomp-setup', '--noconfirm',
    '--distpath', os.path.join(ROOT, 'dist'), '--workpath', os.path.join(stage, 'work'), '--specpath', stage,
    '--add-data', f'{project}{sep}project',
    '--add-data', f'{os.path.join(stage, "bundle.id")}{sep}.',
    # Modules only the recompiler scripts use (they are run from the unpacked source).
    '--hidden-import', 'argparse', '--hidden-import', 'filecmp', '--hidden-import', 'collections',
    '--hidden-import', 'struct', '--hidden-import', 'runpy', '--hidden-import', 'io',
    # The Android build: tools/android/apk.py and the downloads.
    '--hidden-import', 'secrets', '--hidden-import', 'zipfile', '--hidden-import', 'tarfile',
    '--hidden-import', 'urllib.request', '--hidden-import', 'ssl', '--hidden-import', 'certifi',
    *(['--windowed'] if sys.platform == 'win32' else []),
])
shutil.rmtree(stage)
print('->', os.path.join(ROOT, 'dist'))
