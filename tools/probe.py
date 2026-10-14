# One-off probe: summarize code regions per section from the NEF's mapping symbols.
import sys, os, collections
from elftools.elf.elffile import ELFFile

build = sys.argv[1]
nef = [f for f in os.listdir(build) if f.endswith('.nef')][0]
with open(os.path.join(build, nef), 'rb') as f:
    elf = ELFFile(f)
    secs = [(i, s.name, s['sh_addr'], s['sh_flags'], s['sh_type'], s['sh_size']) for i, s in enumerate(elf.iter_sections())]
    symtab = elf.get_section_by_name('.symtab')
    maps = collections.defaultdict(list)
    odd = 0
    nfunc = 0
    for sym in symtab.iter_symbols():
        n = sym.name
        if n in ('$a', '$t', '$d'):
            maps[sym['st_shndx']].append((sym['st_value'], n))
            if sym['st_value'] & 1: odd += 1
        elif sym['st_info']['type'] == 'STT_FUNC':
            nfunc += 1
print('sections', len(secs), 'odd mapping addrs', odd, 'func syms', nfunc)
tot = collections.Counter()
rows = []
for idx, name, addr, flags, typ, size in secs:
    if idx not in maps: continue
    m = sorted(maps[idx])
    fn = os.path.join(build, name + '.sbin')
    fsize = os.path.getsize(fn) if os.path.exists(fn) else -1
    c = collections.Counter()
    for (a, k), nxt in zip(m, m[1:] + [(None, None)]):
        end = nxt[0] if nxt[0] is not None else (addr + fsize if fsize > 0 else a)
        c[k] += end - a
    tot.update(c)
    rows.append((name, hex(addr), fsize, c['$a'], c['$t'], c['$d'], hex(m[0][0]), hex(m[-1][0])))
for r in rows[:12]: print(r)
print('...', len(rows), 'code sections')
print('total bytes arm', tot['$a'], 'thumb', tot['$t'], 'data', tot['$d'])
print('instr slots', tot['$a'] // 4 + tot['$t'] // 2)
