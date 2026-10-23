// Nintendo Switch (homebrew, libnx) parts of the frontend. Kept apart from main.cpp so that
// libnx's <switch.h>, with its short type names and macros, is only seen by one file.
#pragma once
#ifdef __SWITCH__
#include <cstdint>
#include <string>

namespace SwitchPlatform
{
// Called first in main(). Started from the Homebrew Menu (no arguments), it sets up the
// arguments for the ROM, save and settings in the app's own folder on the SD card, sends the
// log there, and checks that the game was started with the whole console's memory. Returns
// false, after saying why on the screen, when the game cannot start.
bool Start(int& argc, char**& argv);

// The processor's boost mode: about 1.8 GHz instead of 1 GHz, as games use while loading.
bool SetCpuBoost(bool on);

// Called every frame: puts the boost speed back when the system has lowered it.
void KeepBoost(uint32_t frame);

// Called once the game has run a few seconds: a start with the JIT worked, so the next one
// uses it again (otherwise the next start falls back to the interpreter by itself).
void JitStarted();

// The console's own date and time, in the time zone set on it. False if it cannot be read.
bool LocalTime(int& year, int& month, int& day, int& hour, int& minute, int& second);

// For the timing lines in the log (SSR_PERF, always on here): the processor, graphics and
// memory clocks and the chip's and the board's temperature right now, as text.
std::string Status();

// Called last: puts the processor back to its normal speed.
void Finish();
}
#endif
