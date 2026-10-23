// ssrecomp: telling the frontend when the ARM9 enters one of a set of watched addresses, with the
// JIT running (for games that are not recompiled, e.g. Pokemon Black 2: see frontend/funcwatch.h).
//
// The JIT compiles runs of instructions into blocks and returns to its dispatcher after each one,
// so a watched address is noticed when a block starts there: the JIT ends a block before a
// watched address and does not follow a branch into one (ARMJIT::CompileBlock), and the
// dispatcher (ARMv5::Execute) calls Hook when the block it is about to run starts at one. That
// costs one bit test per block, the same on every CPU the JIT supports.
#pragma once
#include "types.h"

namespace melonDS
{
struct SsrWatchState
{
    // One bit per halfword from Base (inclusive) to End (exclusive); null: nothing is watched.
    const u8* Bitmap = nullptr;
    u32 Base = 0, End = 0;
    void (*Hook)(void* user, u32 addr, bool thumb) = nullptr;
    void* User = nullptr;

    bool Test(u32 addr) const
    {
        if (!Bitmap || addr - Base >= End - Base) return false;
        const u32 bit = (addr - Base) >> 1;
        return Bitmap[bit >> 3] & (1 << (bit & 7));
    }
};
extern SsrWatchState SsrWatch;
}
