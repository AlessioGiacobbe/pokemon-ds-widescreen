#pragma once
// The game picker of the all-games download (one program for every game): finds the players'
// ROMs, reads each one's name and icon from the ROM itself (every DS game carries a 32x32 icon
// and its title in its banner), and draws the list. Kept free of any platform so that it can be
// drawn into a picture on a PC as well; the Switch shows it with switch_platform.cpp before the
// game's window opens.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

#include "ui.h"

namespace Launcher
{

struct Game
{
    std::string rom;      // the ROM's full path
    std::string folder;   // where its save (and a funcs.txt of the player's own) live
    std::string code;     // the game code, e.g. IREO
    std::string name;     // as shown, e.g. POKEMON BLACK 2
    std::string saveName; // e.g. black2.sav
    bool known = false;   // one of the games this program plays
    bool hasSave = false;
    bool hasIcon = false;
    uint32_t icon[32 * 32] = {}; // ARGB, transparent where the icon is
};

// The games this program plays, in the order the list shows them.
struct KnownGame
{
    const char* code;
    const char* name;
    const char* save;
};
inline const KnownGame* Known(const std::string& code)
{
    static const KnownGame games[] = {
        { "IRBO", "Pokemon Black", "black.sav" },
        { "IRAO", "Pokemon White", "white.sav" },
        { "IREO", "Pokemon Black 2", "black2.sav" },
        { "IRDO", "Pokemon White 2", "white2.sav" },
        { "IPKE", "Pokemon HeartGold", "heartgold.sav" },
        { "IPGE", "Pokemon SoulSilver", "soulsilver.sav" },
    };
    for (const KnownGame& g : games)
        if (code == g.code) return &g;
    return nullptr;
}
inline int Order(const std::string& code)
{
    static const char* order[] = { "IRBO", "IRAO", "IREO", "IRDO", "IPKE", "IPGE" };
    for (int i = 0; i < 6; i++)
        if (code == order[i]) return i;
    return 6;
}

inline bool EndsWithNds(const std::string& name)
{
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".nds";
}

// Reads a ROM's game code, title and icon. False if it is not a DS ROM.
inline bool ReadGame(const std::string& rom, const std::string& folder, Game& g)
{
    FILE* f = fopen(rom.c_str(), "rb");
    if (!f) return false;
    uint8_t header[0x70];
    bool ok = fread(header, 1, sizeof header, f) == sizeof header;
    g = Game();
    g.rom = rom;
    g.folder = folder;
    if (ok)
    {
        g.code.assign((const char*)header + 0x0C, 4);
        for (char c : g.code)
            if (c < 0x20 || c > 0x7E) ok = false;
    }
    // The banner: version, CRCs, then the icon's 4-bit pixels (4x4 tiles of 8x8), its 16
    // colours (BGR555, the first transparent) and the title in six languages (UTF-16).
    const uint32_t banner = ok ? header[0x68] | header[0x69] << 8 | header[0x6A] << 16 | (uint32_t)header[0x6B] << 24 : 0;
    uint8_t ban[0x440];
    if (ok && banner && fseek(f, (long)banner, SEEK_SET) == 0 && fread(ban, 1, sizeof ban, f) == sizeof ban)
    {
        uint32_t pal[16];
        for (int i = 0; i < 16; i++)
        {
            const uint16_t c = (uint16_t)(ban[0x220 + 2 * i] | ban[0x221 + 2 * i] << 8);
            const uint32_t r = (c & 31) * 255 / 31, gr = (c >> 5 & 31) * 255 / 31, b = (c >> 10 & 31) * 255 / 31;
            pal[i] = (i ? 0xFF000000u : 0u) | r << 16 | gr << 8 | b;
        }
        for (int ty = 0; ty < 4; ty++)
            for (int tx = 0; tx < 4; tx++)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                    {
                        const uint8_t byte = ban[0x20 + (ty * 4 + tx) * 32 + y * 4 + x / 2];
                        g.icon[(ty * 8 + y) * 32 + tx * 8 + x] = pal[x & 1 ? byte >> 4 : byte & 15];
                    }
        g.hasIcon = true;
        // The English title's first line, for a game this program does not know.
        for (int i = 0; i < 64; i++)
        {
            const uint16_t ch = (uint16_t)(ban[0x340 + 2 * i] | ban[0x341 + 2 * i] << 8);
            if (ch == 0 || ch == '\n') break;
            g.name += ch == 0xE9 ? 'E' : ch < 0x80 ? (char)ch : ' ';
        }
    }
    fclose(f);
    if (!ok) return false;
    if (const KnownGame* k = Known(g.code))
    {
        g.known = true;
        g.name = k->name;
        g.saveName = k->save;
    }
    else
    {
        if (g.name.empty()) g.name = rom.substr(rom.find_last_of('/') + 1);
        g.saveName = "game.sav";
    }
    for (char& c : g.name) c = (char)toupper((unsigned char)c);
    struct stat st;
    g.hasSave = stat((folder + "/" + g.saveName).c_str(), &st) == 0;
    return true;
}

// The ROMs in a folder and in its roms/ folder; their saves go in the folder itself.
inline void ScanFolder(const std::string& folder, std::vector<Game>& out)
{
    for (const std::string& dir : { folder, folder + "/roms" })
    {
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> names;
        while (dirent* e = readdir(d))
            if (EndsWithNds(e->d_name)) names.push_back(e->d_name);
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const std::string& n : names)
        {
            Game g;
            if (ReadGame(dir + "/" + n, folder, g)) out.push_back(g);
        }
    }
}

// Known games first, in the list's order, then the rest by name.
inline void Sort(std::vector<Game>& games)
{
    std::stable_sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
        const int oa = Order(a.code), ob = Order(b.code);
        if (oa != ob) return oa < ob;
        return a.name < b.name;
    });
}

// The list, drawn over the whole canvas (1280x720 on the Switch). first is the top row shown.
inline void Draw(Canvas& c, const std::vector<Game>& games, int selected, int& first, const std::string& note)
{
    const int s = std::max(1, c.h / 240); // the font's scale: 3 at 720 lines
    c.Fill(0, 0, c.w, c.h, 0xFF101A2E);
    // A band along the top with the title.
    c.Fill(0, 0, c.w, 26 * s, 0xFF1C2A44);
    c.Fill(0, 26 * s, c.w, s, 0xFF3D6FD1);
    const std::string title = "POKEMON DS WIDESCREEN";
    c.Text((c.w - Canvas::TextWidth(title, s + 1)) / 2, 8 * s, title, s + 1, 0xFFFFD75E);

    const int rowH = 26 * s, top = 32 * s, bottom = c.h - 20 * s;
    const int rows = std::max(1, (bottom - top) / rowH);
    if (selected < first) first = selected;
    if (selected >= first + rows) first = selected - rows + 1;
    const int listW = std::min(c.w - 16 * s, 240 * s), lx = (c.w - listW) / 2;
    const int is = std::max(1, s * 2 / 3); // the icons' scale: 2 at 720 lines (64 pixels)
    const int ts = std::max(1, s * 2 / 3); // the second line's
    for (int i = first; i < (int)games.size() && i < first + rows; i++)
    {
        const Game& g = games[i];
        const int y = top + (i - first) * rowH, h = rowH - 3 * s;
        const bool sel = i == selected;
        c.Fill(lx, y, listW, h, sel ? 0xFF3D6FD1 : 0xFF1C2A44);
        // The game's own icon.
        if (g.hasIcon)
        {
            const int ix = lx + 4 * s, iy = y + (h - 32 * is) / 2;
            for (int py = 0; py < 32; py++)
                for (int px = 0; px < 32; px++)
                    if (g.icon[py * 32 + px] >> 24) c.Fill(ix + px * is, iy + py * is, is, is, g.icon[py * 32 + px]);
        }
        const int tx = lx + 8 * s + 32 * is;
        c.Text(tx, y + 4 * s, g.name, s, sel ? 0xFFFFFFFF : 0xFFC8D0E0);
        const std::string sub = !g.known ? "NOT ONE OF THE GAMES THIS PLAYS" : g.hasSave ? "SAVE FOUND" : "NEW GAME";
        c.Text(tx, y + 14 * s, sub, ts, !g.known ? 0xFFE07A6A : sel ? 0xFFDDE6FF : 0xFF8FA0C0);
    }
    // More games above or below the ones shown.
    if (first > 0) c.Fill(lx + listW / 2 - 6 * s, top - 4 * s, 12 * s, s, 0xFF8FA0C0);
    if (first + rows < (int)games.size()) c.Fill(lx + listW / 2 - 6 * s, top + rows * rowH - 2 * s, 12 * s, s, 0xFF8FA0C0);
    const std::string hint = note.empty() ? "A: PLAY    +: QUIT" : note;
    c.Text((c.w - Canvas::TextWidth(hint, s)) / 2, c.h - 13 * s, hint, s, 0xFF8FA0C0);
}

}
