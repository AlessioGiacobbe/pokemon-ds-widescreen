#!/usr/bin/env python3
"""Summarises a SSR_SAMPLE file: samples.py <binary> <file> [depth]
Prints self time per function and inclusive time per function, per thread group."""
import collections, subprocess, sys

binary, path = sys.argv[1], sys.argv[2]
rows = [l.split() for l in open(path) if l.strip()]
addrs = sorted({a for r in rows for a in r[1:]})
out = subprocess.run(["addr2line", "-f", "-C", "-e", binary] + ["0x" + a for a in addrs],
                     capture_output=True, text=True).stdout.split("\n")
name = {a: out[2 * i][:90] for i, a in enumerate(addrs)}
threads = collections.Counter(r[0] for r in rows)
total = len(rows)
print(f"{total} samples; threads: " + ", ".join(f"{t}:{n}" for t, n in threads.most_common(8)))
main = threads.most_common(1)[0][0]
for label, sel in (("busiest thread", [r for r in rows if r[0] == main]), ("other threads", [r for r in rows if r[0] != main])):
    if not sel:
        continue
    self_t, incl = collections.Counter(), collections.Counter()
    for r in sel:
        if len(r) < 2:
            continue
        self_t[name[r[1]]] += 1
        for f in {name[a] for a in r[1:]}:
            incl[f] += 1
    print(f"\n== {label}: {len(sel)} samples ({100 * len(sel) / total:.0f}%)")
    print("-- self")
    for f, n in self_t.most_common(40):
        print(f"{100 * n / len(sel):5.1f}%  {f}")
    print("-- inclusive")
    for f, n in incl.most_common(40):
        print(f"{100 * n / len(sel):5.1f}%  {f}")
