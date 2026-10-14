#!/usr/bin/env python3
"""ssrecomp setup: builds the game from your own copy of Pokemon SoulSilver (US).

Pick your ROM, press Build, and play. The ROM's code is recompiled to C++ on your
machine (nothing of the game is downloaded or shipped) and compiled together with
the frontend. Needs Python 3 with Tk, CMake, Ninja, a C++ compiler and the SDL2,
libpng and zlib development files; the window says what is missing and how to get it.

Without a display: python3 ssrecomp_setup.py --rom <file> [--jobs N]
"""
import hashlib
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading

WINDOWS = os.name == 'nt'
# Packaged as a single executable (tools/package_setup.py), the project's source travels
# inside it and is unpacked into a folder of its own, where the game is then built.
FROZEN = getattr(sys, 'frozen', False)
if FROZEN:
    if WINDOWS:
        HERE = os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~')), 'ssrecomp')
    else:
        HERE = os.path.join(os.environ.get('XDG_DATA_HOME', os.path.expanduser('~/.local/share')), 'ssrecomp')
else:
    HERE = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(HERE, 'layouts', 'soulsilver-us.json')
GEN = os.path.join(HERE, 'gen')
BUILD = os.path.join(HERE, 'build')
EXE = os.path.join(BUILD, 'ssrecomp.exe' if WINDOWS else 'ssrecomp')
ROM_NOTE = os.path.join(HERE, 'ssrecomp.rom')


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
    stamp = open(os.path.join(sys._MEIPASS, 'bundle.id')).read()
    if os.path.exists(stamp_path) and open(stamp_path).read() == stamp:
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
    """On Windows, the compiler from MSYS2 is used even when this was not started from
    an MSYS2 window."""
    if not WINDOWS or shutil.which('g++'):
        return
    for root in (os.environ.get('MSYS2_ROOT', ''), r'C:\msys64', r'C:\tools\msys64'):
        bindir = os.path.join(root, 'ucrt64', 'bin')
        if root and os.path.exists(os.path.join(bindir, 'g++.exe')):
            os.environ['PATH'] = bindir + os.pathsep + os.environ['PATH']
            return


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
    if not WINDOWS and shutil.which('pkg-config'):
        for lib, name in (('sdl2', 'SDL2'), ('libpng', 'libpng'), ('zlib', 'zlib')):
            if subprocess.run(['pkg-config', '--exists', lib], env=child_env()).returncode != 0:
                missing.append(name + ' (development files)')
    elif not WINDOWS:
        missing.append('pkg-config')
    return missing


def install_hint():
    if WINDOWS:
        return ('Install MSYS2 (https://www.msys2.org), open "MSYS2 UCRT64" and run:\n'
                '  pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake '
                'mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-libpng '
                'mingw-w64-ucrt-x86_64-zlib' + ('' if FROZEN else ' mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-tk') +
                '\nthen start this program again.')
    if os.path.exists('/run/ostree-booted'):
        return ('This is an immutable system (Bazzite, Silverblue, SteamOS...). Run this inside a container:\n'
                '  distrobox create -n ssrecomp -i ubuntu:24.04\n'
                '  distrobox enter ssrecomp -- sudo apt install -y ' + ('' if FROZEN else 'python3-tk ') + 'cmake ninja-build g++ '
                'pkg-config libsdl2-dev libpng-dev zlib1g-dev\n'
                '  distrobox enter ssrecomp -- ' + (sys.executable if FROZEN else 'python3 ssrecomp_setup.py') + '\n'
                'The finished game also runs outside the container.')
    tk_apt, tk_dnf, tk_pac = ('', '', '') if FROZEN else ('python3-tk ', 'python3-tkinter ', 'tk ')
    if shutil.which('apt-get'):
        return 'sudo apt install ' + tk_apt + 'cmake ninja-build g++ pkg-config libsdl2-dev libpng-dev zlib1g-dev'
    if shutil.which('dnf'):
        return 'sudo dnf install ' + tk_dnf + 'cmake ninja-build gcc-c++ pkgconf-pkg-config SDL2-devel libpng-devel zlib-devel'
    if shutil.which('pacman'):
        return 'sudo pacman -S --needed ' + tk_pac + 'cmake ninja gcc pkgconf sdl2 libpng zlib'
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
        else:
            for line in open('/proc/meminfo'):
                if line.startswith('MemAvailable:'):
                    jobs = min(jobs, max(1, int(line.split()[1]) // 1500000))
    except (OSError, AttributeError, ValueError):
        pass
    return max(1, int(jobs))


def run_steps(rom, jobs, log, progress):
    """Generates and compiles. log(text) reports a line, progress(fraction, label) the
    progress. Raises RuntimeError with a message for the user on failure."""
    def run(cmd, parse_ninja=False):
        log('$ ' + ' '.join(cmd))
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
    progress(0.08, 'Configuring the build')
    if not os.path.exists(os.path.join(BUILD, 'build.ninja')):
        run(['cmake', '-S', '.', '-B', 'build', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo'])
    progress(0.1, 'Compiling (the first time takes 20-40 minutes)')
    run(['ninja', '-C', 'build', '-j', str(jobs)], parse_ninja=True)
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


def play(extra=()):
    rom = open(ROM_NOTE).read().strip()
    return subprocess.Popen([EXE, rom, '--save', os.path.join(HERE, 'soulsilver.sav'), *extra], cwd=HERE,
                            env=child_env())


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
            lines.append('Compiler and libraries: found.')
        checks.config(text='\n'.join(lines))
        ready = bool(rom) and os.path.isfile(rom) and not missing
        build_btn.state(['!disabled'] if ready else ['disabled'])
        built = os.path.exists(EXE) and os.path.exists(ROM_NOTE)
        for b in (play_btn, windowed_btn):
            b.state(['!disabled'] if built else ['disabled'])

    def start():
        build_btn.state(['disabled'])
        rom, jobs = rom_var.get().strip(), jobs_var.get()

        def worker():
            try:
                run_steps(rom, jobs, lambda t: events.put(('log', t)), lambda f, l: events.put(('progress', f, l)))
                events.put(('done', None))
            except Exception as e:  # shown to the user
                events.put(('done', str(e)))
        threading.Thread(target=worker, daemon=True).start()
    build_btn.config(command=start)

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
                    bar['value'] = ev[1]
                    status.config(text=ev[2])
                elif ev[0] == 'done':
                    update_checks()
                    if ev[1]:
                        status.config(text='Failed')
                        messagebox.showerror('Build failed', ev[1])
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
    args = sys.argv[1:]
    if '--rom' in args:
        rom = args[args.index('--rom') + 1]
        jobs = int(args[args.index('--jobs') + 1]) if '--jobs' in args else default_jobs()
        problem = check_rom(rom)
        if problem:
            print(problem)
        missing = missing_tools()
        if missing:
            sys.exit('Missing: ' + ', '.join(missing) + '\n' + install_hint())
        try:
            # Straight to the terminal: while the recompiler runs, print() itself is
            # redirected into this log.
            out = sys.__stdout__
            run_steps(rom, jobs, lambda t: print(t, file=out, flush=True),
                      lambda f, l: print(f'[{int(f * 100):3d}%] {l}', file=out, flush=True))
        except RuntimeError as e:
            sys.exit(str(e))
        print('Built. Start the game by running this program again (without --rom) and pressing Play' +
              ('.' if FROZEN else ', or with ./run.sh --fullscreen.'))
        return
    try:
        import tkinter  # noqa: F401
    except ImportError:
        sys.exit('This needs Python\'s Tk support for its window. ' + (
            'Install it (python3-tk on Ubuntu/Debian, python3-tkinter on Fedora, tk on Arch) '
            'or run: python3 ssrecomp_setup.py --rom <your ROM>'))
    gui()


if __name__ == '__main__':
    main()
