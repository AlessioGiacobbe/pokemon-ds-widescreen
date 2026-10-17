"""Inline C++ for common ARM9 instructions, for tools/recomp.py.

Each function returns the statement that does what the melonDS handler of that
instruction does (ARMInterpreter_ALU.cpp), with the instruction's fields folded in, or
None when the handler is to be called instead (writes to the PC, the debug hooks, and
everything not listed here). The handler is chosen by its name in melonDS's
instruction table, so the translation always stands for the function the interpreter
would have run. Registers, flags and cycles must come out identical: --lockstep
compares them after every instruction.

AddCycles_C on the ARM9 adds CodeCycles unless R15 is halfword-aligned, and R15 is
known here: the address of the instruction plus 8 (ARM) or 4 (Thumb).
"""
import re

ALU_OPS = ('AND', 'EOR', 'SUB', 'RSB', 'ADD', 'ADC', 'SBC', 'RSC',
           'TST', 'TEQ', 'CMP', 'CMN', 'ORR', 'MOV', 'BIC', 'MVN')
LOGICAL = {'AND', 'EOR', 'TST', 'TEQ', 'ORR', 'MOV', 'BIC', 'MVN'}
TESTS = {'TST', 'TEQ', 'CMP', 'CMN'}
ARM_ALU = re.compile(r'A_(' + '|'.join(ALU_OPS) + r')_(IMM|REG_(LSL|LSR|ASR|ROR)_IMM)(_S)?$')


def ror(x, s):
    s &= 31
    return ((x >> s) | (x << (32 - s))) & 0xFFFFFFFF if s else x


def cycles(r15, extra=0):
    """AddCycles_C (extra 0) or AddCycles_CI(extra) at this R15."""
    code = '' if r15 & 2 else 'cpu->CodeCycles'
    if extra and code:
        return f'cpu->Cycles += {code} + {extra};'
    if extra:
        return f'cpu->Cycles += {extra};'
    return f'cpu->Cycles += {code};' if code else ''


def shift_imm(kind, s, setc):
    """A_CALC_OP2_REG_SHIFT_IMM with LSL_IMM, LSR_IMM, ... (or their _S forms when the
    shifter's carry goes to C), applied to the local b."""
    if kind == 'LSL':
        if s == 0:
            return ''
        return (f'rc_setc(cpu, b & 0x{1 << (32 - s):X}u); ' if setc else '') + f'b <<= {s};'
    if kind == 'LSR':
        if s == 0:
            return ('rc_setc(cpu, b & 0x80000000u); ' if setc else '') + 'b = 0;'
        return (f'rc_setc(cpu, b & 0x{1 << (s - 1):X}u); ' if setc else '') + f'b >>= {s};'
    if kind == 'ASR':
        n = 31 if s == 0 else s
        bit = 0x80000000 if s == 0 else 1 << (s - 1)
        return (f'rc_setc(cpu, b & 0x{bit:X}u); ' if setc else '') + f'b = (uint32_t)((int32_t)b >> {n});'
    # ROR; by 0 it is RRX, through the carry
    if s == 0:
        if setc:
            return 'uint32_t nc = b & 1; b = (b >> 1) | ((cpu->CPSR & 0x20000000u) << 2); rc_setc(cpu, nc);'
        return 'b = (b >> 1) | ((cpu->CPSR & 0x20000000u) << 2);'
    return (f'rc_setc(cpu, b & 0x{1 << (s - 1):X}u); ' if setc else '') + f'b = (b >> {s}) | (b << {32 - s});'


ARM_MEM = re.compile(r'A_(STR|LDR|STRB|LDRB|STRH|LDRH)(_POST)?_IMM$')
ARM_COND = ['EQ', 'NE', 'CS', 'CC', 'MI', 'PL', 'VS', 'VC', 'HI', 'LS', 'GE', 'LT', 'GT', 'LE']
# The ARMv5 functions the handlers reach through ARM's virtual ones, called directly.
READ = {'32': 'cpu->ARMv5::DataRead32', '16': 'cpu->ARMv5::DataRead16', '8': 'cpu->ARMv5::DataRead8'}
WRITE = {'32': 'cpu->ARMv5::DataWrite32', '16': 'cpu->ARMv5::DataWrite16', '8': 'cpu->ARMv5::DataWrite8'}
CD, CDI = 'cpu->ARMv5::AddCycles_CD();', 'cpu->ARMv5::AddCycles_CDI();'


def arm_mem(m, opc):
    """A_STR_IMM, A_LDR_POST_IMM, ... (A_WB_CALC_OFFSET_IMM / A_HD_CALC_OFFSET_IMM with
    A_STR, A_LDR_POST, ... of ARMInterpreter_LoadStore.cpp)."""
    op, post = m.group(1), bool(m.group(2))
    rn, rd = (opc >> 16) & 0xF, (opc >> 12) & 0xF
    if rd == 15 and op.startswith('LDR'):
        return None   # a jump (LDR), or a message (LDRB, LDRH)
    half = op.endswith('H')
    off = ((opc & 0xF) | ((opc >> 4) & 0xF0)) if half else (opc & 0xFFF)
    if not opc & (1 << 23):
        off = -off & 0xFFFFFFFF
    wb = bool(opc & (1 << 21))
    size = '16' if half else '8' if op.endswith('B') else '32'
    if op.startswith('STR'):
        val = f'cpu->R[{rd}]' + (' + 4' if rd == 15 and size == '32' else '')
        if post:
            return (f'{{ uint32_t ad = cpu->R[{rn}]; {WRITE[size]}(ad, {val}); '
                    f'cpu->R[{rn}] += 0x{off:X}u; {CD} }}')
        return (f'{{ uint32_t ad = 0x{off:X}u + cpu->R[{rn}]; {WRITE[size]}(ad, {val}); '
                + (f'cpu->R[{rn}] = ad; ' if wb else '') + f'{CD} }}')
    if half:
        # LDRH reads straight into the register, after the write-back.
        if post:
            return (f'{{ uint32_t ad = cpu->R[{rn}]; cpu->R[{rn}] += 0x{off:X}u; '
                    f'{READ[size]}(ad, &cpu->R[{rd}]); {CDI} }}')
        return (f'{{ uint32_t ad = 0x{off:X}u + cpu->R[{rn}]; ' + (f'cpu->R[{rn}] = ad; ' if wb else '')
                + f'{READ[size]}(ad, &cpu->R[{rd}]); {CDI} }}')
    rot = ' v = rc_ror(v, (ad & 3) << 3);' if size == '32' else ''
    if post:
        return (f'{{ uint32_t ad = cpu->R[{rn}]; uint32_t v; {READ[size]}(ad, &v);{rot} '
                f'cpu->R[{rn}] += 0x{off:X}u; {CDI} cpu->R[{rd}] = v; }}')
    return (f'{{ uint32_t ad = 0x{off:X}u + cpu->R[{rn}]; uint32_t v; {READ[size]}(ad, &v);{rot} '
            + (f'cpu->R[{rn}] = ad; ' if wb else '') + f'{CDI} cpu->R[{rd}] = v; }}')


def ldm_stm(load, opc):
    """A_LDM and A_STM without the S bit (no user-bank transfer or mode change), unrolled:
    the same accesses in the same order, the first nonsequential, the write-back rules of
    the ARM9 (Num == 0) included."""
    if opc & (1 << 22):
        return None
    rn, rlist = (opc >> 16) & 0xF, opc & 0xFFFF
    up, wb, preinc = bool(opc & (1 << 23)), bool(opc & (1 << 21)), bool(opc & (1 << 24))
    n = bin(rlist).count('1')
    out = [f'uint32_t base = cpu->R[{rn}];']
    if not load:
        out.append('uint32_t oldbase = base;')
    if not up:
        out.append(f'base -= {4 * n}u;')
        if wb:
            out.append(f'cpu->R[{rn}] = base;' if not load else 'uint32_t wbbase = base;')
        preinc = not preinc
    first = True
    regs = [i for i in range(16) if rlist & (1 << i)]
    for i in regs:
        if load and i == 15:
            break
        if preinc:
            out.append('base += 4;')
        fn = ('DataRead32' if load else 'DataWrite32') + ('' if first else 'S')
        if load:
            out.append(f'cpu->ARMv5::{fn}(base, &cpu->R[{i}]);')
        else:
            out.append(f'cpu->ARMv5::{fn}(base, {"oldbase" if i == rn else f"cpu->R[{i}]"});')
        first = False
        if not preinc:
            out.append('base += 4;')
    if load:
        if rlist & 0x8000:
            out.append('uint32_t pc = 0;')
            if preinc:
                out.append('base += 4;')
            out.append(f'cpu->ARMv5::DataRead32{"" if first else "S"}(base, &pc);')
            if not preinc:
                out.append('base += 4;')
        if wb:
            if up:
                out.append('uint32_t wbbase = base;')
            others = rlist & ~(1 << rn)
            above = rlist & ~((2 << rn) - 1)
            if not (rlist & (1 << rn)) or not others or above:
                out.append(f'cpu->R[{rn}] = wbbase;')
        if rlist & 0x8000:
            out.append('cpu->ARMv5::JumpTo(pc);')
        out.append(CDI)
    else:
        if up and wb:
            out.append(f'cpu->R[{rn}] = base;')
        out.append(CD)
    return '{ ' + ' '.join(out) + ' }'


def arm(handler, opc, addr):
    r15 = addr + 8
    sext24 = (opc & 0xFFFFFF) - (0x1000000 if opc & 0x800000 else 0)
    if handler == 'A_B':
        return f'cpu->ARMv5::JumpTo(0x{(r15 + (sext24 << 2)) & 0xFFFFFFFF:08X}u);'
    if handler == 'A_BL':
        return (f'{{ cpu->R[14] = 0x{r15 - 4:08X}u; '
                f'cpu->ARMv5::JumpTo(0x{(r15 + (sext24 << 2)) & 0xFFFFFFFF:08X}u); }}')
    if handler in ('A_LDM', 'A_STM'):
        return ldm_stm(handler == 'A_LDM', opc)
    if handler == 'A_MOV_REG_LSL_IMM_DBG' and opc != 0xE1A0C00C:
        handler = 'A_MOV_REG_LSL_IMM'   # its debug-print hook is for mov r12, r12 only
    mm = ARM_MEM.match(handler)
    if mm:
        return arm_mem(mm, opc)
    m = ARM_ALU.match(handler)
    if not m:
        return None
    op, form, kind, s_suffix = m.group(1), m.group(2), m.group(3), bool(m.group(4))
    rd, rn = (opc >> 12) & 0xF, (opc >> 16) & 0xF
    flags = s_suffix or op in TESTS
    if rd == 15 and op not in TESTS:
        return None   # JumpTo, and with S a mode change: left to the handler
    # The shifter's carry reaches C in flag-setting logical operations only (the "s"
    # argument of A_IMPLEMENT_ALU_OP / A_IMPLEMENT_ALU_TEST).
    setc = flags and op in LOGICAL
    if form == 'IMM':
        rot = (opc >> 7) & 0x1E
        b = ror(opc & 0xFF, rot)
        pre = f'uint32_t b = 0x{b:X}u;'
        if setc and rot:
            pre += f' rc_setc(cpu, {1 if b & 0x80000000 else 0});'
    else:
        pre = f'uint32_t b = cpu->R[{opc & 0xF}]; ' + shift_imm(kind, (opc >> 7) & 0x1F, setc)
    a = f'uint32_t a = cpu->R[{rn}];' if op not in ('MOV', 'MVN') else ''
    carry_in = '(cpu->CPSR & 0x20000000u ? 1u : 0u)'
    borrow = '(cpu->CPSR & 0x20000000u ? 0u : 1u)'
    if op in ('AND', 'TST'):
        calc, f = 'uint32_t r = a & b;', 'rc_nz(cpu, r);'
    elif op in ('EOR', 'TEQ'):
        calc, f = 'uint32_t r = a ^ b;', 'rc_nz(cpu, r);'
    elif op == 'ORR':
        calc, f = 'uint32_t r = a | b;', 'rc_nz(cpu, r);'
    elif op == 'BIC':
        calc, f = 'uint32_t r = a & ~b;', 'rc_nz(cpu, r);'
    elif op == 'MOV':
        calc, f = 'uint32_t r = b;', 'rc_nz(cpu, r);'
    elif op == 'MVN':
        calc, f = 'uint32_t r = ~b;', 'rc_nz(cpu, r);'
    elif op in ('SUB', 'CMP'):
        calc, f = 'uint32_t r = a - b;', 'rc_nzcv(cpu, r, rc_carry_sub(a, b), rc_overflow_sub(a, b));'
    elif op == 'RSB':
        calc, f = 'uint32_t r = b - a;', 'rc_nzcv(cpu, r, rc_carry_sub(b, a), rc_overflow_sub(b, a));'
    elif op in ('ADD', 'CMN'):
        calc, f = 'uint32_t r = a + b;', 'rc_nzcv(cpu, r, rc_carry_add(a, b), rc_overflow_add(a, b));'
    elif op == 'ADC':
        if flags:
            calc = f'uint32_t t = a + b; uint32_t c = {carry_in}; uint32_t r = t + c;'
            f = 'rc_nzcv(cpu, r, rc_carry_add(a, b) | rc_carry_add(t, c), rc_overflow_adc(a, b, c));'
        else:
            calc, f = f'uint32_t r = a + b + {carry_in};', ''
    elif op == 'SBC':
        if flags:
            calc = f'uint32_t t = a - b; uint32_t c = {borrow}; uint32_t r = t - c;'
            f = 'rc_nzcv(cpu, r, rc_carry_sub(a, b) & rc_carry_sub(t, c), rc_overflow_sbc(a, b, c));'
        else:
            calc, f = f'uint32_t r = a - b - {borrow};', ''
    else:  # RSC
        if flags:
            calc = f'uint32_t t = b - a; uint32_t c = {borrow}; uint32_t r = t - c;'
            f = 'rc_nzcv(cpu, r, rc_carry_sub(b, a) & rc_carry_sub(t, c), rc_overflow_sbc(b, a, c));'
        else:
            calc, f = f'uint32_t r = b - a - {borrow};', ''
    if not flags:
        f = ''
    store = '' if op in TESTS else f' cpu->R[{rd}] = r;'
    return '{ ' + ' '.join(x for x in (pre, a, calc, f) if x) + store + ' ' + cycles(addr + 8) + ' }'


def thumb(handler, opc, addr):
    r15 = addr + 4
    c = cycles(r15)
    # Branches (ARMInterpreter_Branch.cpp), with their targets worked out.
    if handler == 'T_B':
        off = ((opc & 0x7FF) ^ 0x400) - 0x400
        return f'cpu->ARMv5::JumpTo(0x{(r15 + (off << 1) + 1) & 0xFFFFFFFF:08X}u);'
    if handler == 'T_BCOND' and ((opc >> 8) & 0xF) < 14:
        off = ((opc & 0xFF) ^ 0x80) - 0x80
        return (f'if (RC_CC_{ARM_COND[(opc >> 8) & 0xF]}) '
                f'cpu->ARMv5::JumpTo(0x{(r15 + (off << 1) + 1) & 0xFFFFFFFF:08X}u); else {{ {c} }}')
    if handler == 'T_BL_LONG_1':
        off = (((opc & 0x7FF) ^ 0x400) - 0x400) << 12
        return f'{{ cpu->R[14] = 0x{(r15 + off) & 0xFFFFFFFF:08X}u; {c} }}'
    if handler == 'T_BL_LONG_2' and (opc & (1 << 12) or not opc & 1):
        fix = 'pc |= 1;' if opc & (1 << 12) else 'pc &= ~1u;'
        return (f'{{ uint32_t pc = cpu->R[14] + {(opc & 0x7FF) << 1}u; {fix} '
                f'cpu->R[14] = 0x{(r15 - 2) | 1:08X}u; cpu->ARMv5::JumpTo(pc); }}')
    # Loads and stores (ARMInterpreter_LoadStore.cpp).
    if handler == 'T_PUSH':
        regs = [f'cpu->R[{i}]' for i in range(8) if opc & (1 << i)] + (['cpu->R[14]'] if opc & 0x100 else [])
        out = [f'uint32_t base = cpu->R[13] - {4 * len(regs)}u; cpu->R[13] = base;']
        for k, r in enumerate(regs):
            out.append(f'cpu->ARMv5::DataWrite32{"S" if k else ""}(base, {r});' + (' base += 4;' if k < len(regs) - 1 else ''))
        return '{ ' + ' '.join(out) + f' {CD} }}'
    if handler == 'T_POP':
        out, k = ['uint32_t base = cpu->R[13];'], 0
        for i in range(8):
            if opc & (1 << i):
                out.append(f'cpu->ARMv5::DataRead32{"S" if k else ""}(base, &cpu->R[{i}]); base += 4;')
                k += 1
        if opc & 0x100:
            out.append(f'uint32_t pc; cpu->ARMv5::DataRead32{"S" if k else ""}(base, &pc); '
                       'cpu->ARMv5::JumpTo(pc); base += 4;')
        return '{ ' + ' '.join(out) + f' cpu->R[13] = base; {CDI} }}'
    rd_, rb_ = opc & 7, (opc >> 3) & 7
    if handler in ('T_STR_IMM', 'T_LDR_IMM'):
        o = (opc >> 4) & 0x7C
        if handler == 'T_STR_IMM':
            return f'{{ uint32_t ad = {o}u + cpu->R[{rb_}]; {WRITE["32"]}(ad, cpu->R[{rd_}]); {CD} }}'
        return (f'{{ uint32_t ad = {o}u + cpu->R[{rb_}]; uint32_t v; {READ["32"]}(ad, &v); '
                f'cpu->R[{rd_}] = rc_ror(v, 8 * (ad & 3)); {CDI} }}')
    if handler in ('T_STRB_IMM', 'T_LDRB_IMM', 'T_STRH_IMM', 'T_LDRH_IMM'):
        size = '8' if 'B_' in handler else '16'
        o = ((opc >> 6) & 0x1F) if size == '8' else ((opc >> 5) & 0x3E)
        if handler.startswith('T_STR'):
            return f'{{ uint32_t ad = {o}u + cpu->R[{rb_}]; {WRITE[size]}(ad, cpu->R[{rd_}]); {CD} }}'
        return f'{{ uint32_t ad = {o}u + cpu->R[{rb_}]; {READ[size]}(ad, &cpu->R[{rd_}]); {CDI} }}'
    if handler in ('T_STR_SPREL', 'T_LDR_SPREL'):
        o, r = (opc << 2) & 0x3FC, (opc >> 8) & 7
        if handler == 'T_STR_SPREL':
            return f'{{ uint32_t ad = {o}u + cpu->R[13]; {WRITE["32"]}(ad, cpu->R[{r}]); {CD} }}'
        return f'{{ uint32_t ad = {o}u + cpu->R[13]; {READ["32"]}(ad, &cpu->R[{r}]); {CDI} }}'
    if handler == 'T_LDR_PCREL':
        r = (opc >> 8) & 7
        return f'{{ uint32_t ad = (cpu->R[15] & ~0x2u) + {(opc & 0xFF) << 2}u; {READ["32"]}(ad, &cpu->R[{r}]); {CDI} }}'
    if handler in ('T_STR_REG', 'T_LDR_REG'):
        ro = (opc >> 6) & 7
        if handler == 'T_STR_REG':
            return f'{{ uint32_t ad = cpu->R[{rb_}] + cpu->R[{ro}]; {WRITE["32"]}(ad, cpu->R[{rd_}]); {CD} }}'
        return (f'{{ uint32_t ad = cpu->R[{rb_}] + cpu->R[{ro}]; uint32_t v; {READ["32"]}(ad, &v); '
                f'cpu->R[{rd_}] = rc_ror(v, 8 * (ad & 3)); {CDI} }}')
    rd, rs, rn = opc & 7, (opc >> 3) & 7, (opc >> 6) & 7
    imm5, rd8, imm8 = (opc >> 6) & 0x1F, (opc >> 8) & 7, opc & 0xFF
    sub = 'rc_nzcv(cpu, r, rc_carry_sub(a, b), rc_overflow_sub(a, b));'
    add = 'rc_nzcv(cpu, r, rc_carry_add(a, b), rc_overflow_add(a, b));'
    if handler in ('T_LSL_IMM', 'T_LSR_IMM', 'T_ASR_IMM'):
        kind = handler[2:5]
        return (f'{{ uint32_t b = cpu->R[{rs}]; {shift_imm(kind, imm5, True)} cpu->R[{rd}] = b; '
                f'rc_nz(cpu, b); {c} }}')
    if handler in ('T_ADD_REG_', 'T_SUB_REG_', 'T_ADD_IMM_', 'T_SUB_IMM_'):
        b = f'cpu->R[{rn}]' if 'REG' in handler else f'{rn}u'
        plus = handler.startswith('T_ADD')
        return (f'{{ uint32_t a = cpu->R[{rs}]; uint32_t b = {b}; uint32_t r = a {"+" if plus else "-"} b; '
                f'cpu->R[{rd}] = r; {add if plus else sub} {c} }}')
    if handler == 'T_MOV_IMM':
        return f'{{ cpu->R[{rd8}] = {imm8}u; rc_nz(cpu, {imm8}u); {c} }}'
    if handler in ('T_CMP_IMM', 'T_ADD_IMM', 'T_SUB_IMM'):
        plus = handler == 'T_ADD_IMM'
        store = '' if handler == 'T_CMP_IMM' else f' cpu->R[{rd8}] = r;'
        return (f'{{ uint32_t a = cpu->R[{rd8}]; uint32_t b = {imm8}u; uint32_t r = a {"+" if plus else "-"} b;'
                f'{store} {add if plus else sub} {c} }}')
    logic = {'T_AND_REG': 'a & b', 'T_EOR_REG': 'a ^ b', 'T_ORR_REG': 'a | b', 'T_BIC_REG': 'a & ~b'}
    if handler in logic:
        return (f'{{ uint32_t a = cpu->R[{rd}]; uint32_t b = cpu->R[{rs}]; uint32_t r = {logic[handler]}; '
                f'cpu->R[{rd}] = r; rc_nz(cpu, r); {c} }}')
    if handler == 'T_TST_REG':
        return f'{{ uint32_t r = cpu->R[{rd}] & cpu->R[{rs}]; rc_nz(cpu, r); {c} }}'
    if handler == 'T_MVN_REG':
        return f'{{ uint32_t r = ~cpu->R[{rs}]; cpu->R[{rd}] = r; rc_nz(cpu, r); {c} }}'
    if handler == 'T_NEG_REG':
        return (f'{{ uint32_t b = cpu->R[{rs}]; uint32_t r = 0u - b; cpu->R[{rd}] = r; '
                f'rc_nzcv(cpu, r, rc_carry_sub(0, b), rc_overflow_sub(0, b)); {c} }}')
    if handler in ('T_CMP_REG', 'T_CMN_REG'):
        plus = handler == 'T_CMN_REG'
        return (f'{{ uint32_t a = cpu->R[{rd}]; uint32_t b = cpu->R[{rs}]; uint32_t r = a {"+" if plus else "-"} b; '
                f'{add if plus else sub} {c} }}')
    if handler == 'T_ADC_REG':
        return (f'{{ uint32_t a = cpu->R[{rd}]; uint32_t b = cpu->R[{rs}]; uint32_t t = a + b; '
                f'uint32_t k = (cpu->CPSR & 0x20000000u ? 1u : 0u); uint32_t r = t + k; cpu->R[{rd}] = r; '
                f'rc_nzcv(cpu, r, rc_carry_add(a, b) | rc_carry_add(t, k), rc_overflow_adc(a, b, k)); {c} }}')
    if handler == 'T_SBC_REG':
        return (f'{{ uint32_t a = cpu->R[{rd}]; uint32_t b = cpu->R[{rs}]; uint32_t t = a - b; '
                f'uint32_t k = (cpu->CPSR & 0x20000000u ? 0u : 1u); uint32_t r = t - k; cpu->R[{rd}] = r; '
                f'rc_nzcv(cpu, r, rc_carry_sub(a, b) & rc_carry_sub(t, k), rc_overflow_sbc(a, b, k)); {c} }}')
    if handler == 'T_MUL_REG':
        # The ARM9's form: three internal cycles, C untouched.
        return (f'{{ uint32_t r = cpu->R[{rd}] * cpu->R[{rs}]; cpu->R[{rd}] = r; rc_nz(cpu, r); '
                f'{cycles(r15, 3)} }}')
    if handler in ('T_ADD_HIREG', 'T_CMP_HIREG', 'T_MOV_HIREG'):
        hd, hs = (opc & 7) | ((opc >> 4) & 8), (opc >> 3) & 0xF
        if handler == 'T_CMP_HIREG':
            return f'{{ uint32_t a = cpu->R[{hd}]; uint32_t b = cpu->R[{hs}]; uint32_t r = a - b; {sub} {c} }}'
        if hd == 15 or (handler == 'T_MOV_HIREG' and opc == 0x46E4):
            return None   # a jump, or the debug-print hook
        val = f'cpu->R[{hd}] + cpu->R[{hs}]' if handler == 'T_ADD_HIREG' else f'cpu->R[{hs}]'
        return f'{{ {c} cpu->R[{hd}] = {val}; }}'
    if handler == 'T_ADD_PCREL':
        return f'{{ cpu->R[{rd8}] = (cpu->R[15] & ~2u) + {imm8 << 2}u; {c} }}'
    if handler == 'T_ADD_SPREL':
        return f'{{ cpu->R[{rd8}] = cpu->R[13] + {imm8 << 2}u; {c} }}'
    if handler == 'T_ADD_SP':
        d = (opc & 0x7F) << 2
        return f'{{ cpu->R[13] {"-" if opc & 0x80 else "+"}= {d}u; {c} }}'
    return None


def is_pure(code):
    """Whether a translation calls nothing (memory accesses, jumps and the cycle functions
    all go through cpu->ARMv5::). It then cannot change R15 (no write to the PC is
    translated), stop the CPU or raise an interrupt, and the statement after it needs to
    check only the time slice."""
    return code is not None and 'cpu->ARMv5::' not in code


# The memory accesses and jumps of the translations, through the inline versions in
# runtime/recomp_gen.h: the same code as the ARMv5 functions for the TCMs and main RAM,
# the functions themselves for everything else. Applied after is_pure, which they do not
# change (a statement with any of them is still not pure).
FAST = [('cpu->ARMv5::DataRead32S(', 'rc_dr32s(cpu, '), ('cpu->ARMv5::DataWrite32S(', 'rc_dw32s(cpu, '),
        ('cpu->ARMv5::DataRead32(', 'rc_dr32(cpu, '), ('cpu->ARMv5::DataWrite32(', 'rc_dw32(cpu, '),
        ('cpu->ARMv5::DataRead16(', 'rc_dr16(cpu, '), ('cpu->ARMv5::DataWrite16(', 'rc_dw16(cpu, '),
        ('cpu->ARMv5::DataRead8(', 'rc_dr8(cpu, '), ('cpu->ARMv5::DataWrite8(', 'rc_dw8(cpu, '),
        ('cpu->ARMv5::JumpTo(', 'rc_jump(cpu, ')]


def fast(code):
    for a, b in FAST:
        code = code.replace(a, b)
    return code
