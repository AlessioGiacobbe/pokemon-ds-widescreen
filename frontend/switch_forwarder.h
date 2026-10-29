// HOME Menu entries for the games of the all-games program on the Switch: one per game, installed
// by the program itself on the player's own console (no PC, no keys file: the one key needed is
// made by the console). Each starts the all-games program straight into its game
// ("--game IREO"). frontend/switch_forwarder.cpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SwitchForwarder
{
struct Entry
{
    std::string name;            // as the HOME Menu shows it, e.g. "Pokemon Black 2"
    std::string args;            // after the program's path, e.g. "--game IREO"
    std::vector<uint8_t> icon;   // a 256x256 JPEG
};

// Installs the entries (again: an entry made before is replaced), each starting nroPath
// ("sdmc:/switch/pokemon-ds-wide/pokemon-ds-wide.nro") with its arguments. Returns how many were
// installed; error says what failed, if anything did.
int Install(const std::string& nroPath, const std::vector<Entry>& entries, std::string& error);
}
