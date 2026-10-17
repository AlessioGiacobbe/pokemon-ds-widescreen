// The memory accesses and jumps of the recompiled code (declared in recomp_gen.h).
// Each is the ARMv5 function of the core (CP15.cpp, ARM.cpp) with the cases that
// almost all accesses are, the TCMs and main RAM, done here; everything else, and a
// protection fault, goes to the function itself, so the result is always the same.
#include "recomp_gen.h"

using namespace melonDS;

// ARMv5::JumpTo(addr), without restorecpsr. Identical to the function in ARM.cpp.
void rc_jump(melonDS::ARMv5* cpu, melonDS::u32 addr)
{
    melonDS::u32 oldregion = cpu->R[15] >> 24;
    melonDS::u32 newregion = addr >> 24;
    cpu->RegionCodeCycles = cpu->MemTimings[addr >> 12][0];
    if (addr & 0x1)
    {
        addr &= ~0x1u;
        cpu->R[15] = addr + 2;
        if (newregion != oldregion) cpu->SetupCodeMem(addr);
        if (addr & 0x2)
        {
            cpu->NextInstr[0] = rc_code_read32(cpu, addr - 2, true) >> 16;
            cpu->Cycles += cpu->CodeCycles;
            cpu->NextInstr[1] = rc_code_read32(cpu, addr + 2, false);
            cpu->Cycles += cpu->CodeCycles;
        }
        else
        {
            cpu->NextInstr[0] = rc_code_read32(cpu, addr, true);
            cpu->NextInstr[1] = cpu->NextInstr[0] >> 16;
            cpu->Cycles += cpu->CodeCycles;
        }
        cpu->CPSR |= 0x20;
    }
    else
    {
        addr &= ~0x3u;
        cpu->R[15] = addr + 4;
        if (newregion != oldregion) cpu->SetupCodeMem(addr);
        cpu->NextInstr[0] = rc_code_read32(cpu, addr, true);
        cpu->Cycles += cpu->CodeCycles;
        cpu->NextInstr[1] = rc_code_read32(cpu, addr + 4, false);
        cpu->Cycles += cpu->CodeCycles;
        cpu->CPSR &= ~0x20u;
    }
    if (RC_UNLIKELY(!(cpu->PU_Map[addr >> 12] & 0x04)))
    {
        cpu->PrefetchAbort();
        return;
    }
    cpu->NDS.MonitorARM9Jump(addr);
}

// Main RAM is what NDS::ARM9Read32 and ARM9Write32 read and write there.
#define RC_DATA_FAST(width, perm, timing, mask, ACCESS, SLOW) \
    if (RC_UNLIKELY(!(cpu->PU_Map[addr >> 12] & (perm)))) { SLOW; return; } \
    cpu->DataRegion = addr; \
    melonDS::u32 a = addr & ~(mask); \
    melonDS::u8* p; \
    if (a < cpu->ITCMSize) { cpu->DataCycles = 1; p = &cpu->ITCM[a & (melonDS::ITCMPhysicalSize - 1)]; } \
    else if ((a & cpu->DTCMMask) == cpu->DTCMBase) { cpu->DataCycles = 1; p = &cpu->DTCM[a & (melonDS::DTCMPhysicalSize - 1)]; } \
    else if ((a >> 24) == 0x02) { p = &cpu->NDS.MainRAM[a & cpu->NDS.MainRAMMask]; cpu->DataCycles = cpu->MemTimings[a >> 12][timing]; } \
    else { SLOW; return; } \
    ACCESS;

void rc_dr32(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32* val)
{ RC_DATA_FAST(32, 0x01, 2, 3, *val = *(melonDS::u32*)p, cpu->ARMv5::DataRead32(addr, val)) }
void rc_dr16(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32* val)
{ RC_DATA_FAST(16, 0x01, 1, 1, *val = *(melonDS::u16*)p, cpu->ARMv5::DataRead16(addr, val)) }
void rc_dr8(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32* val)
{ RC_DATA_FAST(8, 0x01, 1, 0, *val = *p, cpu->ARMv5::DataRead8(addr, val)) }
void rc_dw32(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32 val)
{ RC_DATA_FAST(32, 0x02, 2, 3, *(melonDS::u32*)p = val, cpu->ARMv5::DataWrite32(addr, val)) }
void rc_dw16(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u16 val)
{ RC_DATA_FAST(16, 0x02, 1, 1, *(melonDS::u16*)p = val, cpu->ARMv5::DataWrite16(addr, val)) }
void rc_dw8(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u8 val)
{ RC_DATA_FAST(8, 0x02, 1, 0, *p = val, cpu->ARMv5::DataWrite8(addr, val)) }

// The sequential accesses of LDM and STM after the first: no protection check, cycles added.
#define RC_DATA_FAST_S(ACCESS, SLOW) \
    melonDS::u32 a = addr & ~3u; \
    melonDS::u8* p; \
    if (a < cpu->ITCMSize) { cpu->DataCycles += 1; p = &cpu->ITCM[a & (melonDS::ITCMPhysicalSize - 1)]; } \
    else if ((a & cpu->DTCMMask) == cpu->DTCMBase) { cpu->DataCycles += 1; p = &cpu->DTCM[a & (melonDS::DTCMPhysicalSize - 1)]; } \
    else if ((a >> 24) == 0x02) { p = &cpu->NDS.MainRAM[a & cpu->NDS.MainRAMMask]; cpu->DataCycles += cpu->MemTimings[a >> 12][3]; } \
    else { SLOW; return; } \
    ACCESS;

void rc_dr32s(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32* val)
{ RC_DATA_FAST_S(*val = *(melonDS::u32*)p, cpu->ARMv5::DataRead32S(addr, val)) }
void rc_dw32s(melonDS::ARMv5* cpu, melonDS::u32 addr, melonDS::u32 val)
{ RC_DATA_FAST_S(*(melonDS::u32*)p = val, cpu->ARMv5::DataWrite32S(addr, val)) }

