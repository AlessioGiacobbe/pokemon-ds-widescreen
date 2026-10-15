#!/usr/bin/env python3
"""Private ROM validation; outputs stay outside the repository by default.

Run after setup with your own ROM. No game content or reference hashes are shipped.
Checks instruction lockstep, interpreter/recompiled state hashes, and snapshot resume.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import time

HASH = re.compile(r'^frame (\d+) fb [0-9a-f]+ ram [0-9a-f]+ pc9 [0-9a-f]+ pc7 [0-9a-f]+$', re.M)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('executable', type=Path)
    ap.add_argument('rom', type=Path)
    ap.add_argument('--out-dir', type=Path)
    ap.add_argument('--input', type=Path, help='Optional deterministic input script')
    ap.add_argument('--frames', type=int, default=1800)
    args = ap.parse_args()
    if args.frames < 120 or args.frames % 120:
        ap.error('--frames must be a positive multiple of 120 (at least 120)')
    exe, rom = args.executable.resolve(), args.rom.resolve()
    if not exe.is_file() or not rom.is_file():
        ap.error('executable and ROM must exist')
    out = args.out_dir.resolve() if args.out_dir else Path(tempfile.mkdtemp(prefix='ssrecomp-private-validation-'))
    out.mkdir(parents=True, exist_ok=True)
    if (out / 'unused.sav').exists():
        ap.error('output directory contains unused.sav; select a fresh output directory')
    checkpoint = args.frames // 2
    state = out / 'checkpoint.state'
    common = [str(exe), str(rom), '--headless', '--no-save', '--no-wide', '--no-interp60',
              '--save', str(out / 'unused.sav'), '--config', str(out / 'unused.cfg'),
              '--frames', str(args.frames), '--hash-every', '60']
    if args.input:
        common += ['--input', str(args.input.resolve())]
    env = dict(os.environ)
    # Headless uses a fixed noon clock itself. Do not reset the resumed snapshot's RTC.
    env.pop('SSR_CLOCK', None)
    timings = {}

    def run(name, extra):
        cmd = [*common, *extra]
        print(name + ': ' + shlex.join(cmd), flush=True)
        started = time.monotonic()
        # Keep C stdout/stderr separate: buffered hash lines can otherwise be split by diagnostics.
        with (out / (name + '.log')).open('w') as log, (out / (name + '.hashes')).open('w') as hashes:
            subprocess.run(cmd, env=env, cwd=out, stdout=hashes, stderr=log, check=True)
        timings[name] = round(time.monotonic() - started, 3)
        text = (out / (name + '.log')).read_text()
        hash_text = (out / (name + '.hashes')).read_text()
        if name != 'interpreter':
            entries = re.search(r'\[recomp\] chunk entries (\d+)', text)
            if not entries or int(entries.group(1)) == 0:
                raise RuntimeError(name + ': no recompiled chunks executed')
        hashes = {int(m.group(1)): m.group(0) for m in HASH.finditer(hash_text)}
        expected = args.frames // 60 if name != 'resume' else (args.frames - checkpoint) // 60
        if len(hashes) != expected or args.frames not in hashes:
            raise RuntimeError(name + ': missing frame hashes; inspect ' + str(out))
        print(f'{name}: {len(hashes)} frame hashes recorded', flush=True)
        return text, hashes

    lockstep, _ = run('lockstep', ['--lockstep'])
    if f'[lockstep] PASS: {args.frames} frames' not in lockstep:
        raise RuntimeError('Lockstep did not report a pass')
    _, recomp = run('recompiled', ['--state-save', str(state), '--state-at', str(checkpoint)])
    _, interp = run('interpreter', ['--interp'])
    if recomp != interp:
        mismatch = next(f for f in recomp if recomp[f] != interp[f])
        raise RuntimeError(f'Interpreter/recompiled hashes differ at frame {mismatch}; inspect {out}')
    _, resume = run('resume', ['--state-load', str(state)])
    if any(recomp[f] != value for f, value in resume.items()):
        raise RuntimeError('Snapshot continuation differs; inspect ' + str(out))
    summary = {'frames': args.frames, 'hash_interval': 60, 'checkpoint_frame': checkpoint,
               'lockstep': 'pass', 'interpreter_comparison': 'pass', 'snapshot_resume': 'pass',
               'input_script': str(args.input.resolve()) if args.input else None, 'seconds': timings}
    (out / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('All private validation checks passed. Logs: ' + str(out), flush=True)


if __name__ == '__main__':
    main()
