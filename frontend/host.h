// Shared state between the frontend and the Platform implementation.
#pragma once
#include <cstdint>

namespace Host
{
extern bool StopRequested;
extern int LogLevel;

// Called by the core whenever cartridge save memory changes.
void SaveDirty(const uint8_t* data, uint32_t len);
}
