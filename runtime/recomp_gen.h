// Included by every generated source file. Each recompiled instruction reproduces
// the interpreter's per-instruction sequence exactly (prefetch, execute, cycle
// accounting), so a recompiled run is bit-identical to an interpreted one.
#pragma once
#include "NDS.h"
#include "ARM.h"
#include "ARMInterpreter.h"
#include "ARMInterpreter_ALU.h"
#include "ARMInterpreter_Branch.h"
#include "ARMInterpreter_LoadStore.h"
#include "recomp_rt.h"

namespace melonDS::ARMInterpreter
{
void A_UNK(ARM* cpu);
void T_UNK(ARM* cpu);
}

using namespace melonDS::ARMInterpreter;

#define RC_UNLIKELY(x) __builtin_expect(!!(x), 0)

// ARMv5::CodeRead32(addr, branch), inlined: the prefetch of every recompiled instruction
// goes through it. Has to stay identical to the function in CP15.cpp.
static inline melonDS::u32 rc_code_read32(melonDS::ARMv5* cpu, melonDS::u32 addr, bool branch = false)
{
    if (addr < cpu->ITCMSize)
    {
        cpu->CodeCycles = 1;
        return *(melonDS::u32*)&cpu->ITCM[addr & (melonDS::ITCMPhysicalSize - 1)];
    }
    melonDS::s32 cycles = cpu->RegionCodeCycles;
    if (cycles == 0xFF) cycles = (branch || !(addr & 0x1F)) ? 3 /* kCodeCacheTiming in CP15.cpp */ : 1;
    cpu->CodeCycles = cycles;
    if (__builtin_expect(cpu->CodeMem.Mem != nullptr, 1)) return *(melonDS::u32*)&cpu->CodeMem.Mem[addr & cpu->CodeMem.Mask];
    return cpu->CodeRead32(addr, branch);
}

// ARMv5::JumpTo(addr) without restorecpsr, and ARMv5::DataRead32 and the others, with
// the TCMs and main RAM handled directly instead of through the core's bus functions
// (runtime/recomp_mem.cpp). Separate functions, not inlined: copied into every
// instruction they made the code bigger and slower overall.
void rc_jump(melonDS::ARMv5* cpu, melonDS::u32 addr);
void rc_dr32(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32* val);
void rc_dr16(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32* val);
void rc_dr8(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32* val);
void rc_dw32(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32 val);
void rc_dw16(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u16 val);
void rc_dw8(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u8 val);
void rc_dr32s(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32* val);
void rc_dw32s(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32 val);

// Chunks are entered with R15 pointing one instruction past the one to run and the
// pipeline loaded, which is the state the interpreter has at every instruction boundary.
// R15 is known at each statement: the dispatcher enters with R15 one instruction past
// it, and each way in from another statement has checked R15 or cannot have changed it.
// (Keeping the pipeline in the chunk's locals instead, written to the CPU only when
// leaving, was tried: the code was no smaller and ran about 14% slower.)
#define RC_A_FETCH_AT(r15) \
    cpu->R[15] = (r15); \
    cpu->CurInstr = cpu->NextInstr[0]; \
    cpu->NextInstr[0] = cpu->NextInstr[1]; \
    cpu->NextInstr[1] = rc_code_read32(cpu, (r15));

#define RC_T_FETCH_READ_AT(r15) \
    cpu->R[15] = (r15); \
    cpu->CurInstr = cpu->NextInstr[0]; \
    cpu->NextInstr[0] = cpu->NextInstr[1]; \
    cpu->NextInstr[1] = rc_code_read32(cpu, (r15));

#define RC_T_FETCH_SHIFT_AT(r15) \
    cpu->R[15] = (r15); \
    cpu->CurInstr = cpu->NextInstr[0]; \
    cpu->NextInstr[0] = cpu->NextInstr[1]; \
    cpu->NextInstr[1] >>= 16; \
    cpu->CodeCycles = 0;

// The opcode actually fetched must be the one this statement was generated from.
// It will not be when another overlay occupies the address, or before code is loaded.
#define RC_A_GUARD(opc) \
    if (RC_UNLIKELY(cpu->CurInstr != (opc))) { rc_bail_arm(cpu, RC_SECTION); return; }

#define RC_T_GUARD(opc) \
    if (RC_UNLIKELY((melonDS::u16)cpu->CurInstr != (opc))) { rc_bail_thumb(cpu, RC_SECTION); return; }

// After an instruction: leave the chunk if control flow left the straight line, if the
// CPU must stop (halt or IRQ), or if the time slice is used up. The dispatcher then
// performs the interpreter's post-instruction steps, so returning early is always safe.
#define RC_POST(expected_r15) \
    if (RC_UNLIKELY((cpu->R[15] != (expected_r15)) | cpu->StopExecution | (melonDS::u32)rc_single)) return; \
    { \
        melonDS::u64 ts = nds.ARM9Timestamp + cpu->Cycles; \
        if (RC_UNLIKELY(ts >= nds.ARM9Target)) return; \
        nds.ARM9Timestamp = ts; \
        cpu->Cycles = 0; \
    }

// After an instruction that cannot change R15 or stop the CPU (translated arithmetic,
// which calls nothing): only the time slice is checked.
#define RC_POST_PURE \
    if (RC_UNLIKELY((melonDS::u32)rc_single)) return; \
    { \
        melonDS::u64 ts = nds.ARM9Timestamp + cpu->Cycles; \
        if (RC_UNLIKELY(ts >= nds.ARM9Target)) return; \
        nds.ARM9Timestamp = ts; \
        cpu->Cycles = 0; \
    }

// The same for a branch whose target is inside this chunk: when it is taken, execution
// continues at the target's statement instead of going back through the dispatcher.
#define RC_POST_BR(expected_r15, target_r15, label) \
    if (RC_UNLIKELY(cpu->StopExecution | (melonDS::u32)rc_single)) return; \
    if (cpu->R[15] == (target_r15)) \
    { \
        melonDS::u64 ts = nds.ARM9Timestamp + cpu->Cycles; \
        if (RC_UNLIKELY(ts >= nds.ARM9Target)) return; \
        nds.ARM9Timestamp = ts; \
        cpu->Cycles = 0; \
        goto label; \
    } \
    RC_POST(expected_r15)

// For the instructions translated to inline C++ (tools/recomp_inline.py): the flag
// updates of ARM::SetC, SetNZ and SetNZCV and the carry and overflow tests of
// ARMInterpreter_ALU.cpp, with the same results.
static inline void rc_setc(melonDS::ARMv5* cpu, bool c)
{
    cpu->CPSR = (cpu->CPSR & ~0x20000000u) | (c ? 0x20000000u : 0);
}
static inline void rc_nz(melonDS::ARMv5* cpu, uint32_t r)
{
    cpu->CPSR = (cpu->CPSR & ~0xC0000000u) | (r & 0x80000000u) | (r ? 0 : 0x40000000u);
}
static inline void rc_nzcv(melonDS::ARMv5* cpu, uint32_t r, bool c, bool v)
{
    cpu->CPSR = (cpu->CPSR & ~0xF0000000u) | (r & 0x80000000u) | (r ? 0 : 0x40000000u) |
        (c ? 0x20000000u : 0) | (v ? 0x10000000u : 0);
}
static inline uint32_t rc_ror(uint32_t x, uint32_t n)   // ROR in ARM.h
{
    return (x >> (n & 0x1F)) | (x << ((32 - n) & 0x1F));
}
static inline bool rc_carry_add(uint32_t a, uint32_t b) { return (0xFFFFFFFFu - a) < b; }
static inline bool rc_carry_sub(uint32_t a, uint32_t b) { return a >= b; }
static inline bool rc_overflow_add(uint32_t a, uint32_t b)
{
    uint32_t r = a + b;
    return !((a ^ b) & 0x80000000u) && ((a ^ r) & 0x80000000u);
}
static inline bool rc_overflow_sub(uint32_t a, uint32_t b)
{
    uint32_t r = a - b;
    return ((a ^ b) & 0x80000000u) && ((a ^ r) & 0x80000000u);
}
static inline bool rc_overflow_adc(uint32_t a, uint32_t b, uint32_t c)
{
    int64_t full = (int64_t)(int32_t)a + (int32_t)b + c;
    return (int32_t)(a + b + c) != full;
}
static inline bool rc_overflow_sbc(uint32_t a, uint32_t b, uint32_t c)
{
    int64_t full = (int64_t)(int32_t)a - (int32_t)b - c;
    return (int32_t)(a - b - c) != full;
}

#define RC_N (cpu->CPSR & 0x80000000u)
#define RC_Z (cpu->CPSR & 0x40000000u)
#define RC_C (cpu->CPSR & 0x20000000u)
#define RC_V (cpu->CPSR & 0x10000000u)

#define RC_CC_EQ (RC_Z)
#define RC_CC_NE (!RC_Z)
#define RC_CC_CS (RC_C)
#define RC_CC_CC (!RC_C)
#define RC_CC_MI (RC_N)
#define RC_CC_PL (!RC_N)
#define RC_CC_VS (RC_V)
#define RC_CC_VC (!RC_V)
#define RC_CC_HI (RC_C && !RC_Z)
#define RC_CC_LS (!RC_C || RC_Z)
#define RC_CC_GE (!RC_N == !RC_V)
#define RC_CC_LT (!RC_N != !RC_V)
#define RC_CC_GT (!RC_Z && (!RC_N == !RC_V))
#define RC_CC_LE (RC_Z || (!RC_N != !RC_V))
