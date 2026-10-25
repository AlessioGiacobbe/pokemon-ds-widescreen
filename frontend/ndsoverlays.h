// Reads a DS ROM's ARM9 overlays from the file, as the game loads them: for function tables
// whose overlay functions' first bytes are left to be read from the player's own ROM
// (tools/bw2/funcmap.py with - for the ROM; frontend/funcwatch.h). Only the overlay table and
// the overlays themselves are read, not the whole ROM (Black 2's is about 300 MB).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class NdsOverlays
{
public:
    struct Overlay
    {
        uint32_t ram = 0;
        std::vector<uint8_t> data; // decompressed
    };

    bool Open(const std::string& path)
    {
        F = fopen(path.c_str(), "rb");
        if (!F) return false;
        uint8_t h[0x58];
        if (!ReadAt(0, h, sizeof h)) return false;
        memcpy(&FatOff, h + 0x48, 4);
        uint32_t fatSize, ovtOff, ovtSize;
        memcpy(&fatSize, h + 0x4C, 4);
        memcpy(&ovtOff, h + 0x50, 4);
        memcpy(&ovtSize, h + 0x54, 4);
        if (ovtSize > 0x10000) return false;
        Table.resize(ovtSize);
        return ovtSize == 0 || ReadAt(ovtOff, Table.data(), ovtSize);
    }
    ~NdsOverlays() { if (F) fclose(F); }

    // Overlay `id` (its number in the overlay table), or null when it is not in the ROM.
    const Overlay* Get(uint32_t id)
    {
        auto it = Cache.find(id);
        if (it != Cache.end()) return it->second.data.empty() ? nullptr : &it->second;
        Overlay& ov = Cache[id];
        for (size_t e = 0; e + 32 <= Table.size(); e += 32)
        {
            uint32_t w[8];
            memcpy(w, &Table[e], 32);
            if (w[0] != id) continue;
            uint32_t range[2];
            if (!ReadAt(FatOff + w[6] * 8, range, 8) || range[1] < range[0] || range[1] - range[0] > 0x400000) break;
            std::vector<uint8_t> file(range[1] - range[0]);
            if (!file.empty() && !ReadAt(range[0], file.data(), file.size())) break;
            ov.ram = w[1];
            ov.data = (w[7] & (1u << 24)) ? Decompress(file) : file;
            break;
        }
        return ov.data.empty() ? nullptr : &ov;
    }

private:
    FILE* F = nullptr;
    uint32_t FatOff = 0;
    std::vector<uint8_t> Table;
    std::map<uint32_t, Overlay> Cache;

    bool ReadAt(uint32_t off, void* out, size_t n)
    {
        return fseek(F, (long)off, SEEK_SET) == 0 && fread(out, 1, n, F) == n;
    }

    // Nintendo's backward LZ (the ARM9 binary's and overlays' compression), as
    // tools/bw2/funcmap.py's blz_decompress.
    static std::vector<uint8_t> Decompress(const std::vector<uint8_t>& pak)
    {
        const size_t len = pak.size();
        if (len < 8) return pak;
        uint32_t incLen, encLen;
        memcpy(&incLen, &pak[len - 4], 4);
        if (!incLen) return pak;
        const uint32_t hdrLen = pak[len - 5];
        memcpy(&encLen, &pak[len - 8], 4);
        encLen &= 0xFFFFFF;
        if (encLen > len || hdrLen > encLen) return {};
        const size_t decLen = len - encLen, pakLen = encLen - hdrLen;
        const size_t want = encLen + incLen;
        // The compressed part is read from its end backwards and the output written backwards;
        // here both are reversed, as the Python does.
        std::vector<uint8_t> src(pak.begin() + decLen, pak.begin() + decLen + pakLen);
        std::reverse(src.begin(), src.end());
        std::vector<uint8_t> out;
        out.reserve(want);
        size_t i = 0;
        unsigned mask = 0, flags = 0;
        while (out.size() < want)
        {
            mask >>= 1;
            if (!mask)
            {
                if (i >= src.size()) break;
                flags = src[i++];
                mask = 0x80;
            }
            if (!(flags & mask))
            {
                if (i >= src.size()) break;
                out.push_back(src[i++]);
            }
            else
            {
                if (i + 1 >= src.size()) break;
                const unsigned pos = (src[i] << 8) | src[i + 1];
                i += 2;
                size_t n = std::min<size_t>((pos >> 12) + 3, want - out.size());
                const size_t back = (pos & 0xFFF) + 3;
                if (back > out.size()) return {};
                for (; n; n--) out.push_back(out[out.size() - back]);
            }
        }
        std::reverse(out.begin(), out.end());
        std::vector<uint8_t> result(pak.begin(), pak.begin() + decLen);
        result.insert(result.end(), out.begin(), out.end());
        return result;
    }
};
