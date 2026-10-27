#pragma once
// AUTOSAVE (from Johto Online's resume points): every few minutes on the overworld, soon after
// each save the game writes, and when the game is quit, a snapshot of the whole console is
// written beside the save file (<save>.resume, the one before kept as <save>.resume.bak). At the
// next start the game carries on from it instead of booting.
//
// Each one records a hash of the save memory it holds, and it is only used while that equals
// the save memory the console has now: loading it writes the same save back, so an in-game save
// made after it (or a save file swapped in) can never be rolled back. Otherwise the game boots as
// without one. It also records the settings that change what the game has in its memory (wide
// map loading in Black and White), and is not used if they differ now.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include "NDS.h"
#include "Savestate.h"

namespace Resume
{

inline uint64_t Hash(const uint8_t* d, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 0x100000001b3ull;
    return h;
}

// What a resume file starts with; the core's savestate follows.
struct Header
{
    char magic[8] = { 'S', 'S', 'R', 'R', 'E', 'S', 'M', '1' };
    uint64_t saveHash = 0; // of the save memory inside the snapshot
    int64_t time = 0;      // Unix time it was made
    uint32_t saveLen = 0;
    uint32_t flags = 0;    // the settings it needs (see above)
    uint32_t frame = 0;    // the frontend's frame count then
    char game[4] = {};     // the ROM's game code
};

class Point
{
public:
    std::string path; // <save>.resume

    ~Point() { Finish(); }

    // Snapshots the console now and writes the file on a thread (the snapshot itself takes a
    // few milliseconds; the file, several megabytes, would hold up a frame).
    bool Write(melonDS::NDS& nds, uint32_t flags, uint32_t frame, const char game[4])
    {
        Finish();
        melonDS::Savestate st;
        if (!nds.DoSavestate(&st) || st.Error) return false;
        st.Finish();
        Header h;
        h.saveLen = nds.GetNDSSaveLength();
        h.saveHash = Hash(nds.GetNDSSave(), h.saveLen);
        h.time = (int64_t)::time(nullptr);
        h.flags = flags;
        h.frame = frame;
        memcpy(h.game, game, 4);
        std::vector<uint8_t> blob(sizeof h + st.Length());
        memcpy(blob.data(), &h, sizeof h);
        memcpy(blob.data() + sizeof h, st.Buffer(), st.Length());
        writer = std::thread([this, blob = std::move(blob)]() {
            const std::string tmp = path + ".tmp", bak = path + ".bak";
            FILE* f = fopen(tmp.c_str(), "wb");
            bool ok = f && fwrite(blob.data(), 1, blob.size(), f) == blob.size();
            if (f) ok = fclose(f) == 0 && ok;
            if (!ok)
            {
                fprintf(stderr, "[autosave] cannot write %s\n", path.c_str());
                return;
            }
            // (The Switch's file system will not rename onto an existing file: each one steps
            // aside first.)
            remove(bak.c_str());
            rename(path.c_str(), bak.c_str());
            if (rename(tmp.c_str(), path.c_str()) != 0) fprintf(stderr, "[autosave] cannot write %s\n", path.c_str());
        });
        return true;
    }

    // Waits for a file still being written.
    void Finish()
    {
        if (writer.joinable()) writer.join();
    }

    // The newest resume point that holds the save memory the console has now and fits the
    // settings: its header, and which file (0 the latest, 1 the backup); -1 if there is none.
    int Find(const melonDS::NDS& nds, uint32_t flags, const char game[4], Header& out)
    {
        Finish();
        const uint32_t len = nds.GetNDSSaveLength();
        if (!len || !nds.GetNDSSave()) return -1;
        const uint64_t hash = Hash(nds.GetNDSSave(), len);
        for (int i = 0; i < 2; i++)
        {
            std::ifstream f(std::filesystem::u8path(i ? path + ".bak" : path), std::ios::binary);
            Header h;
            if (f.read((char*)&h, sizeof h) && !memcmp(h.magic, Header().magic, 8) && h.saveLen == len && h.saveHash == hash &&
                h.flags == flags && !memcmp(h.game, game, 4))
            {
                out = h;
                return i;
            }
        }
        return -1;
    }

    // Loads the newest resume point that fits (see Find). False if there is none, or if the
    // core refused it; `broken` then says the console was left half loaded.
    bool Load(melonDS::NDS& nds, uint32_t flags, const char game[4], Header& h, bool& broken)
    {
        broken = false;
        const int which = Find(nds, flags, game, h);
        if (which < 0) return false;
        std::ifstream f(std::filesystem::u8path(which ? path + ".bak" : path), std::ios::binary | std::ios::ate);
        const std::streamsize size = f ? (std::streamsize)f.tellg() : 0;
        if (size <= (std::streamsize)sizeof h) return false;
        std::vector<uint8_t> blob((size_t)size);
        f.seekg(0);
        if (!f.read((char*)blob.data(), size)) return false;
        melonDS::Savestate st(blob.data() + sizeof h, (melonDS::u32)(blob.size() - sizeof h), false);
        if (st.Error) return false;
        if (!nds.DoSavestate(&st))
        {
            broken = true;
            return false;
        }
        return true;
    }

private:
    std::thread writer;
};

}
