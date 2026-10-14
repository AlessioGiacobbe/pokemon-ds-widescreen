// Runtime side of the static recompiler: chunk tables and dispatch.
#pragma once
#include "types.h"

namespace melonDS { class ARMv5; }

struct RcRange
{
    melonDS::u32 start, end;
};

struct RcChunk
{
    melonDS::u32 start, end;
    melonDS::u8 thumb;
    melonDS::u16 section;
    void (*fn)(melonDS::ARMv5*, melonDS::u32);
};

struct RcSection
{
    const char* name;
    melonDS::u32 base;
    melonDS::u32 size;
    const unsigned char* image;
    const RcRange* ranges;
    melonDS::u32 numRanges;
    melonDS::u32 firstChunk;
    melonDS::u32 numChunks;
    // runtime state
    bool stamped = false;
    melonDS::u32 missesSinceCheck = 0;
    melonDS::u32 timesStamped = 0;
    melonDS::u32 timesEvicted = 0;
};

extern const RcChunk rc_chunks[];
extern const melonDS::u32 rc_num_chunks;
extern RcSection rc_sections[];
extern const melonDS::u32 rc_num_sections;

// When set, chunks return to the dispatcher after every instruction (used for tracing).
extern bool rc_single;

struct RcStats
{
    melonDS::u64 dispatches = 0;      // chunk entries
    melonDS::u64 interpreted = 0;     // instructions that fell back to the interpreter loop
    melonDS::u64 guardFails = 0;      // opcode in memory differed from the recompiled one
};
extern RcStats rc_stats;

// Frame counter maintained by the frontend, and the last frame in which each section ran code.
// Lets the frontend tell which overlays (bag, battle, ...) are currently active.
extern melonDS::u32 rc_frame;
extern melonDS::u32 rc_section_last_frame[];

void rc_reset();
void rc_print_stats();

// Guard failure paths: run the real instruction through the interpreter and drop the stale section.
void rc_bail_arm(melonDS::ARMv5* cpu, melonDS::u32 section);
void rc_bail_thumb(melonDS::ARMv5* cpu, melonDS::u32 section);
