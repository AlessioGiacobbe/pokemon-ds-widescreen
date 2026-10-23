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
extern "C" uint32_t svcGetCurrentProcessorNumber(void);
#endif
#include <atomic>
#include <chrono>
#include <cstdint>

// ssrecomp: for the timing lines in the log: how long the emulation waited for the helper
// threads, and on which core each started its frames (Switch only).
struct SsrThreadStats
{
    std::atomic<uint64_t> Wait3DNs{0}, WaitWideNs{0};
    std::atomic<uint32_t> RenderCore[4]{}, WideCore[4]{};
};
inline SsrThreadStats& SsrStats()
{
    static SsrThreadStats s;
    return s;
}
// Adds the time from its creation to its end to a counter.
struct SsrWaitTimer
{
    std::atomic<uint64_t>& To;
    std::chrono::steady_clock::time_point T0 = std::chrono::steady_clock::now();
    explicit SsrWaitTimer(std::atomic<uint64_t>& to) : To(to) {}
    ~SsrWaitTimer()
    {
        To += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - T0).count();
    }
};
inline void SsrNoteCore(bool render)
{
#ifdef __SWITCH__
    const uint32_t core = svcGetCurrentProcessorNumber() & 3;
    (render ? SsrStats().RenderCore : SsrStats().WideCore)[core]++;
#else
    (void)render;
#endif
}

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
