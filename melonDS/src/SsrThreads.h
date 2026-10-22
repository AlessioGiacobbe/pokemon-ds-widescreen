// ssrecomp: where helper threads (the 3D render thread, the widescreen drawing threads) run.
//
// On the Nintendo Switch a new thread stays on the process's default core, core 0, which is
// where the emulation itself runs, unless it moves itself: Horizon does not spread threads
// over the cores as a desktop system does. So each helper thread calls this first and moves
// to cores 1 and 2 (core 3 belongs to the system), taking turns for which one it prefers.
// Elsewhere this does nothing.
#pragma once

#ifdef __SWITCH__
#include <atomic>
#include <cstdint>
#include <cstdio>
// From libnx (svc.h); declared here so that this header does not pull in all of <switch.h>,
// whose short type names and macros (BIT, ALIGN, ...) the emulator code does not expect.
extern "C" uint32_t svcSetThreadCoreMask(uint32_t handle, int32_t core_id, uint32_t affinity_mask);
#endif

inline void SsrHelperThreadStart()
{
#ifdef __SWITCH__
    static std::atomic<int> next{0};
    const int core = 1 + (next++ & 1);
    const uint32_t currentThread = 0xFFFF8000; // libnx's CUR_THREAD_HANDLE
    const uint32_t rc = svcSetThreadCoreMask(currentThread, core, (1u << 1) | (1u << 2));
    if (rc != 0)
        fprintf(stderr, "[switch] a helper thread could not move to core %d (0x%x); it shares core 0\n", core, (unsigned)rc);
    else
        fprintf(stderr, "[switch] helper thread on core %d\n", core);
#endif
}
