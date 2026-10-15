#!/usr/bin/env python3
"""ssrecomp setup: builds the game from your own copy of Pokemon SoulSilver (US).

Pick your ROM, press Build, and play. The ROM's code is recompiled to C++ on your
machine (nothing of the game is downloaded or shipped) and compiled together with
the frontend. Needs Python 3 with Tk, CMake, Ninja, a C++ compiler and the SDL2,
libpng and zlib development files; the window says what is missing and how to get it.

Without a display: python3 ssrecomp_setup.py --rom <file> [--jobs N]
"""
import glob
import hashlib
import json
import os
import platform
import site
import sysconfig
import tempfile
from functools import lru_cache
import queue
import re
import shlex
import shutil
import subprocess
import sys
import threading

WINDOWS = os.name == 'nt'
DARWIN = sys.platform == 'darwin'
MAC_ARCH = platform.machine() if DARWIN else None
ANDROID_MAC_NOTE = 'Android builds from macOS are not supported yet; use a Linux or Windows host.'
# Packaged with tools/package_setup.py, the project's source travels
# inside it and is unpacked into a folder of its own, where the game is then built.
FROZEN = getattr(sys, 'frozen', False)
if FROZEN:
    if WINDOWS:
        HERE = os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~')), 'ssrecomp')
    elif DARWIN:
        HERE = os.path.expanduser('~/Library/Application Support/ssrecomp')
    else:
        HERE = os.path.join(os.environ.get('XDG_DATA_HOME', os.path.expanduser('~/.local/share')), 'ssrecomp')
else:
    HERE = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(HERE, 'layouts', 'soulsilver-us.json')
GEN = os.path.join(HERE, 'gen')
BUILD = os.path.join(HERE, 'build-macos-' + MAC_ARCH if DARWIN else 'build')
EXE = os.path.join(BUILD, 'ssrecomp.exe' if WINDOWS else 'ssrecomp')
ROM_NOTE = os.path.join(HERE, 'ssrecomp.rom')

# The Android build: Google's NDK (compilers for Android) and SDL2's source are downloaded
# into ANDROID_DIR the first time; the result is the APK at ANDROID_APK.
ANDROID_DIR = os.path.join(HERE, 'android')
ANDROID_APK = os.path.join(HERE, 'SoulSilver-ssrecomp.apk')
ANDROID_ABI = 'arm64-v8a'          # phones and handhelds from the last ten years
ANDROID_PLATFORM = 'android-24'    # Android 7.0 and later
NDK_NAME = 'android-ndk-r27c'
NDK_DOWNLOADS = {   # host -> (url, sha1, size), from Google's repository listing
    'linux': ('https://dl.google.com/android/repository/android-ndk-r27c-linux.zip',
              '090e8083a715fdb1a3e402d0763c388abb03fb4e', 663987688),
    'windows': ('https://dl.google.com/android/repository/android-ndk-r27c-windows.zip',
                'ac5f7762764b1f15341094e148ad4f847d050c38', 781511249),
}
SDL2_NAME = 'SDL2-2.32.10'
SDL2_DOWNLOAD = ('https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz',
                 '5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165')


# ---------------------------------------------------------------------------
# The steps, usable without the window.

def child_env():
    """The environment for programs this starts. A packaged executable points the
    library path at its own bundled libraries; the compiler, pkg-config and the game
    must not see those."""
    env = dict(os.environ)
    if FROZEN and not WINDOWS:
        orig = env.pop('LD_LIBRARY_PATH_ORIG', None)
        if orig is not None:
            env['LD_LIBRARY_PATH'] = orig
        else:
            env.pop('LD_LIBRARY_PATH', None)
    return env


def unpack():
    """Packaged: puts the project's source into HERE (again when this executable is a
    different version), leaving what was built there alone."""
    if not FROZEN:
        return
    stamp_path = os.path.join(HERE, '.bundle')
    with open(os.path.join(sys._MEIPASS, 'bundle.id')) as f:
        stamp = f.read()
    if os.path.exists(stamp_path):
        with open(stamp_path) as f:
            if f.read() == stamp:
                return
    for item in os.listdir(os.path.join(sys._MEIPASS, 'project')):
        src, dst = os.path.join(sys._MEIPASS, 'project', item), os.path.join(HERE, item)
        if os.path.isdir(src):
            shutil.copytree(src, dst, dirs_exist_ok=True)
        else:
            os.makedirs(HERE, exist_ok=True)
            shutil.copy2(src, dst)
    with open(stamp_path, 'w') as f:
        f.write(stamp)


def find_msys2():
    """On Windows, the compiler, CMake and libraries from MSYS2 are used even when this
    was not started from an MSYS2 window, and ahead of other copies of CMake or a
    compiler on the PATH (those cannot find MSYS2's libraries)."""
    if not WINDOWS:
        return
    for root in (os.environ.get('MSYS2_ROOT', ''), r'C:\msys64', r'C:\tools\msys64'):
        bindir = os.path.join(root, 'ucrt64', 'bin')
        if root and os.path.exists(os.path.join(bindir, 'g++.exe')):
            os.environ['PATH'] = bindir + os.pathsep + os.environ['PATH']
            return


def find_macos_tools():
    """Finder does not inherit a terminal's Homebrew PATH; prefer this process's arch."""
    if not DARWIN:
        return
    prefixes = ['/opt/homebrew', '/usr/local'] if MAC_ARCH == 'arm64' else ['/usr/local', '/opt/homebrew']
    # Honor an explicit PATH first, then add both standard Homebrew installations.
    paths = os.environ.get('PATH', os.defpath).split(os.pathsep)
    paths += [sysconfig.get_path('scripts'), os.path.join(site.getuserbase(), 'bin')]
    paths += ['/Library/Frameworks/Python.framework/Versions/Current/bin']
    paths += sorted(glob.glob(os.path.expanduser('~/Library/Python/*/bin')), reverse=True)
    paths += [p + '/bin' for p in prefixes] + ['/usr/bin', '/bin', '/usr/sbin', '/sbin']
    os.environ['PATH'] = os.pathsep.join(dict.fromkeys(paths))
    os.environ.setdefault('CC', 'clang')
    os.environ.setdefault('CXX', 'clang++')


@lru_cache(maxsize=4)
def macos_toolchain_problem(path, cxx):
    """Check the compiler and SDK before generating the game.

    Cache by tool environment so the UI can refresh without repeatedly invoking clang.
    """
    try:
        compiler = shlex.split(cxx) if cxx else ['xcrun', 'clang++']
        with tempfile.TemporaryDirectory(prefix='ssrecomp-toolchain-') as tmp:
            source = os.path.join(tmp, 'probe.cpp')
            with open(source, 'w') as f:
                f.write('#include <thread>\n#include <zlib.h>\n'
                        'int main() { std::thread t([] {}); t.join(); return zlibVersion() == nullptr; }\n')
            cmd = [*compiler, '-std=c++17', '-arch', MAC_ARCH, '-mmacosx-version-min=11.0', source,
                   '-lz', '-o', os.path.join(tmp, 'probe')]
            result = subprocess.run(cmd, env=child_env(), text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=30)
            if result.returncode:
                return ('macOS compiler/SDK check failed for ' + MAC_ARCH +
                        ':\n' + result.stdout[-2500:])
    except (OSError, subprocess.SubprocessError) as e:
        return 'macOS compiler/SDK check failed: ' + str(e)
    return None


def windows_prefix():
    """The MSYS2 environment (e.g. C:\\msys64\\ucrt64) the compiler belongs to, or None."""
    gxx = shutil.which('g++')
    if not WINDOWS or not gxx:
        return None
    return os.path.dirname(os.path.dirname(os.path.abspath(gxx)))


def check_rom(path):
    """Returns None if this is the right game, otherwise what is wrong with it."""
    if not os.path.isfile(path):
        return 'That file does not exist.'
    layout = json.load(open(LAYOUT))
    digest = hashlib.sha1(open(path, 'rb').read()).hexdigest()
    if digest == layout['rom_sha1']:
        return None
    # A dump with a different header or padding can still hold the same code; the
    # recompiler checks every code section, so let it decide.
    return ('This is not the exact ROM the layout was made for (SHA-1 %s, expected %s). '
            'It will still be used if its code is identical.' % (digest, layout['rom_sha1']))


def missing_tools():
    """What is needed to compile and is not installed, as (name, how to get it) pairs."""
    missing = []
    for tool in ('cmake', 'ninja'):
        if not shutil.which(tool):
            missing.append(tool)
    if not (shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')):
        missing.append('a C++ compiler')
    if not WINDOWS and not DARWIN and shutil.which('pkg-config'):
        for lib, name in (('sdl2', 'SDL2'), ('libpng', 'libpng'), ('zlib', 'zlib')):
            if subprocess.run(['pkg-config', '--exists', lib], env=child_env()).returncode != 0:
                missing.append(name + ' (development files)')
    elif not WINDOWS and not DARWIN:
        missing.append('pkg-config')
    prefix = windows_prefix()
    if prefix:
        for path, name in ((os.path.join('lib', 'cmake', 'SDL2'), 'SDL2'), (os.path.join('include', 'png.h'), 'libpng'),
                           (os.path.join('include', 'zlib.h'), 'zlib')):
            if not os.path.exists(os.path.join(prefix, path)):
                missing.append(name)
    if DARWIN and not missing:
        problem = macos_toolchain_problem(os.environ.get('PATH', ''), os.environ.get('CXX', ''))
        if problem:
            missing.append(problem)
    return missing


def os_id():
    """The Linux distribution's ID from /etc/os-release (e.g. 'steamos', 'arch'), or ''."""
    try:
        for line in open('/etc/os-release'):
            if line.startswith('ID='):
                return line[3:].strip().strip('"')
    except OSError:
        pass
    return ''


def install_hint():
    if DARWIN:
        return ('Install Xcode Command Line Tools and Python 3 with Tk from https://www.python.org/downloads/macos/, '
                'then run:\n'
                '  xcode-select --install\n'
                '  python3 -m pip install "cmake>=3.21,<4" ninja\n'
                'CMake and Ninja can also be installed with Homebrew. '
                'SDL2 and libpng are downloaded and built automatically for macOS 11 or later; '
                'no Homebrew libraries or pkg-config are needed. Use native ' + MAC_ARCH + ' Python and tools.')
    if WINDOWS:
        return ('Install MSYS2 (https://www.msys2.org), open "MSYS2 UCRT64" and run:\n'
                '  pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake '
                'mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-libpng '
                'mingw-w64-ucrt-x86_64-zlib' + ('' if FROZEN else ' mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-tk') +
                '\nthen start this program again.')
    if os.path.exists('/run/ostree-booted') or os_id() == 'steamos':
        return ('This is an immutable system (Bazzite, Silverblue, SteamOS...), where programs are not installed '
                'directly. Open a terminal (Konsole on a Steam Deck or KDE) and set up a container with:\n'
                '  distrobox create -n ssrecomp -i ubuntu:24.04\n'
                '  distrobox enter ssrecomp -- sudo apt install -y ' + ('' if FROZEN else 'python3-tk ') + 'cmake ninja-build g++ '
                'pkg-config libsdl2-dev libpng-dev zlib1g-dev\n'
                '  distrobox enter ssrecomp -- ' + (shlex.quote(os.environ.get('APPIMAGE', sys.executable)) if FROZEN
                                                    else 'python3 ssrecomp_setup.py') + '\n'
                'The finished game also runs outside the container.')
    tk_apt, tk_dnf, tk_pac = ('', '', '') if FROZEN else ('python3-tk ', 'python3-tkinter ', 'tk ')
    how = ('Open a terminal, run this command (it asks for your password), then close and start this '
           'program again:\n  ')
    if shutil.which('apt-get'):
        return how + 'sudo apt install ' + tk_apt + 'cmake ninja-build g++ pkg-config libsdl2-dev libpng-dev zlib1g-dev'
    if shutil.which('dnf'):
        return how + 'sudo dnf install ' + tk_dnf + 'cmake ninja-build gcc-c++ pkgconf-pkg-config SDL2-devel libpng-devel zlib-devel'
    if shutil.which('pacman'):
        return how + 'sudo pacman -S --needed ' + tk_pac + 'cmake ninja gcc pkgconf sdl2 libpng zlib'
    return 'Install CMake, Ninja, a C++ compiler, pkg-config and the SDL2, libpng and zlib development files.'


def default_jobs():
    """Parallel compile jobs: one per CPU thread, but no more than the memory allows
    (each of the large generated files needs about 1.5 GB to compile)."""
    jobs = os.cpu_count() or 2
    try:
        if WINDOWS:
            import ctypes

            class MemoryStatus(ctypes.Structure):
                _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong),
                            ('ullTotalPhys', ctypes.c_ulonglong), ('ullAvailPhys', ctypes.c_ulonglong),
                            ('ullTotalPageFile', ctypes.c_ulonglong), ('ullAvailPageFile', ctypes.c_ulonglong),
                            ('ullTotalVirtual', ctypes.c_ulonglong), ('ullAvailVirtual', ctypes.c_ulonglong),
                            ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
            status = MemoryStatus()
            status.dwLength = ctypes.sizeof(MemoryStatus)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
            jobs = min(jobs, max(1, status.ullAvailPhys // (1500 * 1024 * 1024)))
        elif DARWIN:
            # Unified memory is shared with the GPU: reserve half and budget 2 GiB/job.
            memory = int(subprocess.check_output(['/usr/sbin/sysctl', '-n', 'hw.memsize'], text=True, stderr=subprocess.DEVNULL))
            jobs = min(jobs, max(1, memory // (4 * 1024**3)))
        else:
            for line in open('/proc/meminfo'):
                if line.startswith('MemAvailable:'):
                    jobs = min(jobs, max(1, int(line.split()[1]) // 1500000))
    except (OSError, AttributeError, ValueError, subprocess.SubprocessError):
        if DARWIN:
            jobs = min(jobs, 2)
    return max(1, int(jobs))


def download(url, digest, path, log, progress, label, algo='sha1'):
    """Downloads url to path (resuming a partial file) and checks its digest."""
    import urllib.request
    os.makedirs(os.path.dirname(path), exist_ok=True)
    part = path + '.part'
    if not os.path.exists(path):
        have = os.path.getsize(part) if os.path.exists(part) else 0
        req = urllib.request.Request(url, headers={'Range': 'bytes=%d-' % have} if have else {})
        log('Downloading ' + url)
        context = None
        try:
            # Packaged, Python may not find this system's certificates; certifi brings its own.
            import certifi
            import ssl
            context = ssl.create_default_context(cafile=certifi.where())
        except ImportError:
            pass
        with urllib.request.urlopen(req, context=context) as r, open(part, 'ab' if have and r.status == 206 else 'wb') as f:
            if r.status != 206:
                have = 0
            total = have + int(r.headers.get('Content-Length') or 0)
            done, last = have, -1
            while True:
                block = r.read(1 << 20)
                if not block:
                    break
                f.write(block)
                done += len(block)
                if total and done * 100 // total != last:
                    last = done * 100 // total
                    progress(None, '%s: %d of %d MB' % (label, done >> 20, total >> 20))
        os.replace(part, path)
    h = hashlib.new(algo)
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    if h.hexdigest() != digest:
        os.remove(path)
        raise RuntimeError('The download of %s was damaged (checksum mismatch). Try again.' % url)


def extract_zip(path, dest):
    """Unzips, keeping executable bits and symbolic links (Python's zipfile writes a
    link as a small file holding its target, and drops the bits)."""
    import stat
    import zipfile
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode) and not WINDOWS:
                out = os.path.join(dest, info.filename)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                if os.path.lexists(out):
                    os.remove(out)
                os.symlink(z.read(info).decode(), out)
                continue
            out = z.extract(info, dest)
            if mode and not WINDOWS:
                os.chmod(out, mode & 0o7777)


def android_ndk(log, progress):
    """The NDK: from ANDROID_NDK_HOME or ANDROID_NDK_ROOT when set, else downloaded."""
    if DARWIN:
        raise RuntimeError(ANDROID_MAC_NOTE)
    for var in ('ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT'):
        ndk = os.environ.get(var)
        if ndk and os.path.exists(os.path.join(ndk, 'build', 'cmake', 'android.toolchain.cmake')):
            return ndk
    ndk = os.path.join(ANDROID_DIR, NDK_NAME)
    if not os.path.exists(os.path.join(ndk, 'build', 'cmake', 'android.toolchain.cmake')):
        url, sha1, size = NDK_DOWNLOADS['windows' if WINDOWS else 'linux']
        zip_path = os.path.join(ANDROID_DIR, 'downloads', os.path.basename(url))
        download(url, sha1, zip_path, log, progress, "Downloading Google's Android NDK (%d MB, once)" % (size >> 20))
        progress(None, 'Unpacking the NDK')
        extract_zip(zip_path, ANDROID_DIR)
        os.remove(zip_path)
    return ndk


def run_steps(rom, jobs, log, progress, android=False):
    """Generates and compiles (for Android: and makes the APK). log(text) reports a line,
    progress(fraction, label) the progress (fraction None: only the label changes).
    Raises RuntimeError with a message for the user on failure."""
    if DARWIN and android:
        raise RuntimeError(ANDROID_MAC_NOTE)
    if jobs < 1:
        raise RuntimeError('Parallel compile jobs must be at least 1.')
    os.makedirs(HERE, exist_ok=True)

    def run(cmd, parse_ninja=False):
        log('$ ' + shlex.join(cmd))
        proc = subprocess.Popen(cmd, cwd=HERE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                errors='replace', env=child_env())
        tail = []
        for line in proc.stdout:
            line = line.rstrip()
            tail = (tail + [line])[-30:]
            m = re.match(r'\[(\d+)/(\d+)\]', line) if parse_ninja else None
            if m:
                done, total = int(m.group(1)), int(m.group(2))
                progress(0.1 + 0.9 * done / total, f'Compiling ({done} of {total})')
                if done % 5 == 0 or done == total:
                    log(line)
            else:
                log(line)
        if proc.wait() != 0:
            raise RuntimeError('This step failed:\n' + ' '.join(cmd) + '\n\n' + '\n'.join(tail[-12:]))

    def tool(script, *args):
        # Run in this process: packaged as one executable there is no separate Python.
        import contextlib
        import runpy

        class Lines:
            # print() writes a line in pieces; they are put together before logging.
            def __init__(self):
                self.pending = ''

            def write(self, text):
                self.pending += text
                while '\n' in self.pending:
                    line, self.pending = self.pending.split('\n', 1)
                    if line.strip():
                        log(line)

            def flush(self):
                pass
        log('$ ' + script + ' ' + ' '.join(args))
        saved = sys.argv
        sys.argv = [script, *args]
        try:
            with contextlib.redirect_stdout(Lines()), contextlib.redirect_stderr(Lines()):
                runpy.run_path(os.path.join(HERE, 'tools', script), run_name='__main__')
        except SystemExit as e:
            if e.code not in (0, None):
                raise RuntimeError(str(e.code))
        finally:
            sys.argv = saved

    progress(0.0, 'Checking your ROM and recompiling its code')
    tool('recomp.py', '--rom', rom, '--layout', LAYOUT, os.path.join(HERE, 'melonDS', 'src'), GEN)
    tool('apps.py', '--layout', LAYOUT, GEN)
    if android:
        return android_steps(rom, jobs, log, progress, run)
    progress(0.08, 'Configuring the build')
    prefix = windows_prefix()
    # Reconfigure every time: source updates and dependency changes must reach Ninja.
    run(['cmake', '-S', HERE, '-B', BUILD, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
         *(['-DCMAKE_PREFIX_PATH=' + prefix.replace('\\', '/')] if prefix else []),
         *(['-DCMAKE_OSX_ARCHITECTURES=' + MAC_ARCH, '-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0', '-DSSR_MACOS_SOURCE_DEPS=ON'] if DARWIN else [])])
    progress(0.1, 'Compiling (the first time takes 20-40 minutes)')
    run(['ninja', '-C', BUILD, '-j', str(jobs)], parse_ninja=True)
    if WINDOWS:
        # So that the game also starts outside the MSYS2 window: the libraries it uses go
        # next to it.
        bindir = os.path.dirname(shutil.which('ninja') or shutil.which('g++') or '')
        for dll in ('SDL2.dll', 'libpng16-16.dll', 'zlib1.dll', 'libstdc++-6.dll', 'libgcc_s_seh-1.dll',
                    'libwinpthread-1.dll'):
            if bindir and os.path.exists(os.path.join(bindir, dll)):
                shutil.copy2(os.path.join(bindir, dll), BUILD)
    with open(ROM_NOTE, 'w') as f:
        f.write(os.path.abspath(rom) + '\n')
    progress(1.0, 'Done')


def android_steps(rom, jobs, log, progress, run):
    import tarfile
    ndk = android_ndk(log, progress)
    toolchain = os.path.join(ndk, 'build', 'cmake', 'android.toolchain.cmake').replace('\\', '/')
    android_args = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TOOLCHAIN_FILE=' + toolchain,
                    '-DANDROID_ABI=' + ANDROID_ABI, '-DANDROID_PLATFORM=' + ANDROID_PLATFORM]

    # SDL2, built for Android and installed into a folder of its own.
    sdl_prefix = os.path.join(ANDROID_DIR, 'sdl2-' + ANDROID_ABI)
    sdl_lib = os.path.join(sdl_prefix, 'lib', 'libSDL2.so')
    if not os.path.exists(sdl_lib):
        tar_path = os.path.join(ANDROID_DIR, 'downloads', SDL2_NAME + '.tar.gz')
        download(SDL2_DOWNLOAD[0], SDL2_DOWNLOAD[1], tar_path, log, progress, 'Downloading SDL2', 'sha256')
        with tarfile.open(tar_path) as t:
            t.extractall(ANDROID_DIR)
        progress(0.1, 'Compiling SDL2 for Android')
        sdl_build = os.path.join(ANDROID_DIR, 'build-sdl2-' + ANDROID_ABI)
        run(['cmake', '-S', os.path.join(ANDROID_DIR, SDL2_NAME), '-B', sdl_build, *android_args,
             '-DCMAKE_INSTALL_PREFIX=' + sdl_prefix.replace('\\', '/'), '-DSDL_STATIC=OFF', '-DSDL_TEST=OFF'])
        run(['ninja', '-C', sdl_build, '-j', str(jobs), 'install'])

    # The game, as libmain.so.
    build = os.path.join(HERE, 'build-android-' + ANDROID_ABI)
    progress(0.12, 'Configuring the Android build')
    if not os.path.exists(os.path.join(build, 'build.ninja')):
        prefix = sdl_prefix.replace('\\', '/')
        # Named directly: the NDK's toolchain only searches its own folders.
        run(['cmake', '-S', '.', '-B', build, *android_args,
             '-DSDL2_DIR=' + prefix + '/lib/cmake/SDL2'])
    progress(0.14, 'Compiling for Android (the first time takes 20-40 minutes)')
    run(['ninja', '-C', build, '-j', str(jobs)], parse_ninja=True)

    progress(0.98, 'Packing the APK')
    sys.path.insert(0, os.path.join(HERE, 'tools', 'android'))
    import apk
    apk.build(os.path.join(HERE, 'tools', 'android', 'skeleton.apk'), ANDROID_ABI, os.path.join(build, 'libmain.so'),
              sdl_lib, rom, os.path.join(ANDROID_DIR, 'signing-key.json'), ANDROID_APK)
    with open(ROM_NOTE, 'w') as f:
        f.write(os.path.abspath(rom) + '\n')
    log('Made ' + ANDROID_APK)
    progress(1.0, 'Done: ' + ANDROID_APK)


ANDROID_HELP = (
    'Made ' + ANDROID_APK + '\n\n'
    'To install it: copy it to your phone or handheld (USB cable, cloud drive...), open it there '
    'and allow your file manager to install apps when asked. It holds your copy of the game: '
    'do not share it.\n\n'
    'Updates install over it as long as they are built on this computer (with the key in ' +
    os.path.join(ANDROID_DIR, 'signing-key.json') + '). Uninstalling the app deletes its save.')


def play(extra=()):
    with open(ROM_NOTE) as f:
        rom = f.read().strip()
    with open(os.path.join(HERE, 'game.log'), 'a') as log:
        return subprocess.Popen([EXE, rom, '--save', os.path.join(HERE, 'soulsilver.sav'),
                                 '--config', os.path.join(HERE, 'ssrecomp.cfg'), *extra], cwd=HERE,
                                stdout=log, stderr=subprocess.STDOUT, env=child_env())


# ---------------------------------------------------------------------------
# The window.

def gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    root = tk.Tk()
    root.title('ssrecomp setup')
    root.minsize(640, 520)
    events = queue.Queue()

    frame = ttk.Frame(root, padding=16)
    frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='SoulSilver for one screen', font=('TkDefaultFont', 16, 'bold')).pack(anchor='w')
    ttk.Label(frame, wraplength=600, justify='left', text=(
        'Builds the game from your own copy of Pokemon SoulSilver (US). Its code is recompiled on this '
        'computer; nothing of the game is downloaded.' +
        (f' The game is built in {HERE}.' if FROZEN else ''))).pack(anchor='w', pady=(4, 12))

    row = ttk.Frame(frame)
    row.pack(fill='x')
    ttk.Label(row, text='Your ROM:').pack(side='left')
    rom_var = tk.StringVar(value=open(ROM_NOTE).read().strip() if os.path.exists(ROM_NOTE) else '')
    ttk.Entry(row, textvariable=rom_var).pack(side='left', fill='x', expand=True, padx=6)

    def browse():
        path = filedialog.askopenfilename(title='Your Pokemon SoulSilver ROM',
                                          filetypes=[('Nintendo DS ROM', '*.nds'), ('All files', '*')])
        if path:
            rom_var.set(path)
            update_checks()
    ttk.Button(row, text='Browse...', command=browse).pack(side='left')

    checks = ttk.Label(frame, wraplength=600, justify='left')
    checks.pack(anchor='w', pady=8)
    commands = []

    def copy_commands():
        root.clipboard_clear()
        root.clipboard_append('\n'.join(commands))
        copy_btn.config(text='Copied - paste into a terminal')
    copy_btn = ttk.Button(frame, text='Copy the commands', command=copy_commands)

    jobs_row = ttk.Frame(frame)
    jobs_row.pack(fill='x')
    ttk.Label(jobs_row, text='Parallel compile jobs:').pack(side='left')
    jobs_var = tk.IntVar(value=default_jobs())
    ttk.Spinbox(jobs_row, from_=1, to=64, width=4, textvariable=jobs_var).pack(side='left', padx=6)
    ttk.Label(jobs_row, text='(fewer if the computer runs out of memory)').pack(side='left')

    bar = ttk.Progressbar(frame, maximum=1.0)
    bar.pack(fill='x', pady=(12, 2))
    status = ttk.Label(frame, text='')
    status.pack(anchor='w')

    log_box = tk.Text(frame, height=10, wrap='none', state='disabled')
    log_box.pack(fill='both', expand=True, pady=6)

    buttons = ttk.Frame(frame)
    buttons.pack(fill='x')
    build_btn = ttk.Button(buttons, text='Build')
    build_btn.pack(side='left')
    play_btn = ttk.Button(buttons, text='Play', command=lambda: play(['--fullscreen']))
    play_btn.pack(side='left', padx=6)
    windowed_btn = ttk.Button(buttons, text='Play in a window', command=lambda: play())
    windowed_btn.pack(side='left')
    android_btn = ttk.Button(buttons, text='Build for Android')
    android_btn.pack(side='right')

    def update_checks():
        lines = []
        rom = rom_var.get().strip()
        if rom:
            problem = check_rom(rom)
            lines.append('ROM: ' + ('Pokemon SoulSilver (US), correct.' if problem is None else problem))
        else:
            lines.append('ROM: choose your Pokemon SoulSilver (US) ROM.')
        missing = missing_tools()
        if missing:
            lines.append('Missing to compile: ' + ', '.join(missing) + '.\nTo install them:\n' + install_hint())
        else:
            lines.append('Build tools: found. SDL2 and libpng will be built automatically.' if DARWIN
                         else 'Compiler and libraries: found.')
        if DARWIN:
            lines.append(ANDROID_MAC_NOTE)
        checks.config(text='\n'.join(lines))
        # The install commands are the indented lines of the hint.
        commands[:] = [l.strip() for l in '\n'.join(lines).splitlines() if l.startswith('  ')]
        if missing and commands:
            copy_btn.config(text='Copy the commands')
            copy_btn.pack(anchor='w', after=checks, pady=(0, 8))
        else:
            copy_btn.pack_forget()
        ready = bool(rom) and os.path.isfile(rom) and not missing
        build_btn.state(['!disabled'] if ready else ['disabled'])
        # Android needs only CMake and Ninja here: its compiler and libraries are downloaded.
        android_ready = not DARWIN and bool(rom) and os.path.isfile(rom) and shutil.which('cmake') and shutil.which('ninja')
        android_btn.state(['!disabled'] if android_ready else ['disabled'])
        built = os.path.exists(EXE) and os.path.exists(ROM_NOTE)
        for b in (play_btn, windowed_btn):
            b.state(['!disabled'] if built else ['disabled'])

    def start(android=False):
        build_btn.state(['disabled'])
        android_btn.state(['disabled'])
        rom, jobs = rom_var.get().strip(), jobs_var.get()

        def worker():
            try:
                with open(os.path.join(HERE, 'build.log'), 'w') as build_log:
                    def log(text):
                        build_log.write(text + '\n')
                        build_log.flush()
                        events.put(('log', text))
                    run_steps(rom, jobs, log, lambda f, l: events.put(('progress', f, l)), android=android)
                events.put(('done', None, android))
            except Exception as e:  # shown to the user
                events.put(('done', str(e), android))
        threading.Thread(target=worker, daemon=True).start()
    build_btn.config(command=start)
    android_btn.config(command=lambda: start(android=True))

    def pump():
        try:
            while True:
                ev = events.get_nowait()
                if ev[0] == 'log':
                    log_box.config(state='normal')
                    log_box.insert('end', ev[1] + '\n')
                    log_box.see('end')
                    log_box.config(state='disabled')
                elif ev[0] == 'progress':
                    if ev[1] is not None:
                        bar['value'] = ev[1]
                    status.config(text=ev[2])
                elif ev[0] == 'done':
                    update_checks()
                    if ev[1]:
                        status.config(text='Failed')
                        messagebox.showerror('Build failed', ev[1])
                    elif ev[2]:
                        messagebox.showinfo('Android build ready', ANDROID_HELP)
                    else:
                        messagebox.showinfo('Ready', 'The game is built. Press Play to start it.')
        except queue.Empty:
            pass
        root.after(100, pump)

    update_checks()
    pump()
    root.mainloop()


def main():
    unpack()
    find_msys2()
    find_macos_tools()
    args = sys.argv[1:]
    if DARWIN and '--android' in args:
        sys.exit(ANDROID_MAC_NOTE)
    if '--rom' in args:
        rom = args[args.index('--rom') + 1]
        jobs = int(args[args.index('--jobs') + 1]) if '--jobs' in args else default_jobs()
        problem = check_rom(rom)
        if problem:
            print(problem)
        android = '--android' in args
        missing = [t for t in ('cmake', 'ninja') if not shutil.which(t)] if android else missing_tools()
        if missing:
            sys.exit('Missing: ' + ', '.join(missing) + '\n' + install_hint())
        try:
            # Straight to the terminal: while the recompiler runs, print() itself is
            # redirected into this log.
            out = sys.__stdout__
            last = [0.0]

            def progress(f, label):
                if f is not None:
                    last[0] = f
                print(f'[{int(last[0] * 100):3d}%] {label}', file=out, flush=True)
            run_steps(rom, jobs, lambda t: print(t, file=out, flush=True), progress, android=android)
        except RuntimeError as e:
            sys.exit(str(e))
        if android:
            print(ANDROID_HELP)
            return
        print('Built. Start the game by running this program again (without --rom) and pressing Play' +
              ('.' if FROZEN else ', or with ./run.sh --fullscreen.'))
        return
    try:
        import tkinter  # noqa: F401
    except ImportError:
        if DARWIN:
            sys.exit('This needs Python with Tk support.\n' + install_hint())
        sys.exit('This needs Python\'s Tk support for its window. ' + (
            'Install it (python3-tk on Ubuntu/Debian, python3-tkinter on Fedora, tk on Arch) '
            'or run: python3 ssrecomp_setup.py --rom <your ROM>'))
    gui()


if __name__ == '__main__':
    main()
