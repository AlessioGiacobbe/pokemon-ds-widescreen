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

// ARMv5::CodeRead32(addr, false), inlined: the prefetch of every recompiled instruction
// goes through it. Has to stay identical to the function in CP15.cpp.
static inline melonDS::u32 rc_code_read32(melonDS::ARMv5* cpu, melonDS::u32 addr)
{
    if (addr < cpu->ITCMSize)
    {
        cpu->CodeCycles = 1;
        return *(melonDS::u32*)&cpu->ITCM[addr & (melonDS::ITCMPhysicalSize - 1)];
    }
    melonDS::s32 cycles = cpu->RegionCodeCycles;
    if (cycles == 0xFF) cycles = !(addr & 0x1F) ? 3 /* kCodeCacheTiming in CP15.cpp */ : 1;
    cpu->CodeCycles = cycles;
    if (__builtin_expect(cpu->CodeMem.Mem != nullptr, 1)) return *(melonDS::u32*)&cpu->CodeMem.Mem[addr & cpu->CodeMem.Mask];
    return cpu->CodeRead32(addr, false);
}

// Chunks are entered with R15 pointing one instruction past the one to run and the
// pipeline loaded, which is the state the interpreter has at every instruction boundary.
#define RC_A_FETCH \
    cpu->R[15] += 4; \
    cpu->CurInstr = cpu->NextInstr[0]; \
    cpu->NextInstr[0] = cpu->NextInstr[1]; \
    cpu->NextInstr[1] = rc_code_read32(cpu, cpu->R[15]);

#define RC_T_FETCH_READ \
    cpu->R[15] += 2; \
    cpu->CurInstr = cpu->NextInstr[0]; \
    cpu->NextInstr[0] = cpu->NextInstr[1]; \
    cpu->NextInstr[1] = rc_code_read32(cpu, cpu->R[15]);

#define RC_T_FETCH_SHIFT \
    cpu->R[15] += 2; \
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
