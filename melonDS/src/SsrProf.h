// ssrecomp: where the emulation's time goes, for development (--emu-prof): the time spent in
// a few parts of the DS's hardware, each counted without the parts it calls (a DMA that feeds
// the geometry engine counts the geometry as geometry), the rest being the two processors (the
// JIT or the interpreter) and the scheduler. Off, a part costs one test of a flag.
// Only the emulation thread is timed (every part below runs there). "2D top" is engine A
// without the wait for its 3D lines ("3D lines wait"), "2D bottom" engine B; "3D start" is the
// DS-resolution 3D's start, "wide 3D start" the widescreen picture's (lists sent to the GPU).
#pragma once
#include <chrono>
#include <cstdint>

namespace SsrProf
{
enum Part { Geometry, Draw2D, Sound, DMA, WideFlush, Interp, Render3D, Draw2DB, Wait3D, HelperWait, WideRender, SendWait, Count };
inline bool On = false;
inline uint64_t Ns[Count] = {};
inline int Stack[16];
inline int Depth = 0;
inline std::chrono::steady_clock::time_point Mark;

inline void Enter(Part p)
{
    const auto now = std::chrono::steady_clock::now();
    if (Depth > 0) Ns[Stack[Depth - 1]] += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now - Mark).count();
    if (Depth < 16) Stack[Depth] = p;
    Depth++;
    Mark = now;
}
inline void Leave()
{
    const auto now = std::chrono::steady_clock::now();
    Depth--;
    if (Depth < 16) Ns[Stack[Depth]] += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now - Mark).count();
    Mark = now;
}
struct Scope
{
    bool Active;
    explicit Scope(Part p) : Active(On) { if (Active) Enter(p); }
    ~Scope() { if (Active) Leave(); }
};
}
