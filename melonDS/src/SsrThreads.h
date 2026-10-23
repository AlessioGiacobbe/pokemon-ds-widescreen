// ssrecomp: where helper threads (the 3D render thread, the widescreen drawing threads) run.
//
// On the Nintendo Switch a new thread stays on the process's default core, core 0, which is
// where the emulation itself runs, unless it moves itself: Horizon does not spread threads
// over the cores as a desktop system does. So each helper thread calls this first and moves
// to cores 1 and 2 (core 3 belongs to the system).
//
// Horizon also does not share a core between threads of the same priority by time: one runs
// until it waits. The emulation waits for the DS-resolution 3D thread line by line, so with
// the JIT (which leaves the emulation waiting for it 3-9 ms a frame when it queued behind a
// widescreen thread) that thread has core 1 to itself at a higher priority than the
// widescreen threads, which take cores 2 and 1 and give way to it.
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
extern "C" uint32_t svcSetThreadPriority(uint32_t handle, uint32_t priority);
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

// role: 0 for the DS-resolution 3D thread, 1, 2, ... for the widescreen drawing threads.
inline void SsrHelperThreadStart(int role)
{
#ifdef __SWITCH__
    const uint32_t currentThread = 0xFFFF8000; // libnx's CUR_THREAD_HANDLE
    // The 3D thread: core 1 only, at the main thread's priority (0x2C; new threads get 0x3B).
    // Widescreen threads: the first prefers core 2, the next core 1, both may use either.
    const int core = role == 0 ? 1 : (role & 1) ? 2 : 1;
    const uint32_t mask = role == 0 ? (1u << 1) : (1u << 1) | (1u << 2);
    const uint32_t rc = svcSetThreadCoreMask(currentThread, core, mask);
    if (rc != 0)
        fprintf(stderr, "[switch] a helper thread could not move to core %d (0x%x); it shares core 0\n", core, (unsigned)rc);
    else
        fprintf(stderr, "[switch] %s on core %d\n", role == 0 ? "3D thread" : "widescreen thread", core);
    if (role == 0)
    {
        const uint32_t prc = svcSetThreadPriority(currentThread, 0x2C);
        if (prc != 0) fprintf(stderr, "[switch] the 3D thread's priority could not be raised (0x%x)\n", (unsigned)prc);
    }
#else
    (void)role;
#endif
}
