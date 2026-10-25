// What the frontend may ask the recompiler runtime, kept apart from recomp_rt.h so that
// changing it does not rebuild the generated game code.
#pragma once

// A game application (bag, party menu, battle, ...) and the function it runs every frame.
struct RcApp
{
    const char* name;
    const char* section;
    unsigned execAddr;
    // The functions that start and end the application, run every frame while it starts
    // and ends (0 when it has none of its own).
    unsigned initAddr;
    const char* initSection;
    unsigned exitAddr;
    const char* exitSection;
};
extern const RcApp rc_apps[];
extern const unsigned rc_num_apps;

// True if recompiled code at this address, in this section, ran within the last `frames` frames.
bool rc_ran_recently(const char* section, unsigned addr, unsigned frames);

// Functions named in tools/focus_symbols.txt (same layout as RcApp: name, section, address).
extern const RcApp rc_focus_syms[];
extern const unsigned rc_num_focus_syms;

// Probes record the last frame in which a function was entered by recompiled code.
// The address must be the function's first instruction.
int rc_add_probe(const char* section, unsigned addr);
unsigned rc_probe_last_frame(int probe);
// Current value of the runtime's frame counter, for comparing with rc_probe_last_frame.
unsigned rc_current_frame();

// Without the recompiled code (melonDS's interpreter or JIT: --interp, --jit), the same probes
// and the overlays' activity (rc_section_last_frame, rc_ran_recently) come from the core's
// watch on chosen addresses (melonDS/src/SsrWatch.h) instead of from the dispatch into
// recompiled chunks: every chunk start of the overlays, and every probe. The tables in gen/
// (chunks, and each section's code image to tell overlays at the same address apart) are all
// it needs. Returns false if the watch is taken already (a game with a function table).
namespace melonDS { class NDS; }
bool rc_watch_attach(melonDS::NDS& nds);
