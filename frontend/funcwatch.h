// Which of a game's functions ran recently, for games that are not recompiled (Pokemon Black 2,
// from the pokebw2 decompilation's function map; see tools/bw2/funcmap.py). It plays the part
// ssrecomp's recompiler probes play for SoulSilver, on melonDS's interpreter or its JIT: the
// core reports jumps into (interpreter) and blocks starting at (JIT) addresses marked in a
// bitmap of function starts (melonDS/src/SsrWatch.h).
//
// Many overlays are loaded at the same addresses, so a function in an overlay counts as entered
// only when the first bytes in memory are its own.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ARM.h"
#include "NDS.h"
#include "SsrWatch.h"

class FuncWatch
{
public:
    struct Func
    {
        uint32_t addr;
        bool thumb;
        bool check;      // compare the first bytes (overlay functions)
        uint32_t first;  // those bytes, as a little-endian word
        std::string module;
        std::string name;
    };

    bool Load(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
        {
            std::istringstream ss(line);
            std::string addr, kind, module, first, name;
            if (!(ss >> addr >> kind >> module >> first >> name)) continue;
            Func fn;
            fn.addr = (uint32_t)strtoul(addr.c_str(), nullptr, 16);
            fn.thumb = kind == "t";
            fn.check = first != "-";
            fn.first = 0;
            if (fn.check)
                for (int i = 0; i < 4; i++)
                    fn.first |= (uint32_t)strtoul(first.substr(i * 2, 2).c_str(), nullptr, 16) << (i * 8);
            fn.module = module;
            fn.name = name;
            if (fn.addr < Base || fn.addr >= End) continue; // outside the watched range
            Funcs.push_back(fn);
        }
        Last.assign(Funcs.size(), 0);
        Bitmap.assign(((End - Base) >> 1) / 8 + 1, 0);
        for (size_t i = 0; i < Funcs.size(); i++)
        {
            const uint32_t a = Funcs[i].addr;
            const uint32_t bit = (a - Base) >> 1;
            Bitmap[bit >> 3] |= 1 << (bit & 7);
            ByAddr[a].push_back((int)i);
            ByName.emplace(Funcs[i].name, (int)i);
        }
        fprintf(stderr, "[funcwatch] %zu functions from %s\n", Funcs.size(), path.c_str());
        return !Funcs.empty();
    }

    // Watches the functions named; with no names, every function in the table (for
    // --trace-funcs, which costs more). With the JIT only these become block boundaries, so it
    // keeps its optimisations elsewhere.
    void Attach(melonDS::NDS& nds, const std::vector<std::string>& names)
    {
        Nds = &nds;
        const std::vector<uint8_t>* bitmap = &Bitmap;
        if (!names.empty())
        {
            JitBitmap.assign(Bitmap.size(), 0);
            size_t n = 0;
            for (const std::string& name : names)
            {
                auto range = ByName.equal_range(name);
                for (auto it = range.first; it != range.second; ++it, ++n)
                {
                    const uint32_t bit = (Funcs[it->second].addr - Base) >> 1;
                    JitBitmap[bit >> 3] |= 1 << (bit & 7);
                }
            }
            bitmap = &JitBitmap;
            fprintf(stderr, "[funcwatch] watching %zu functions\n", n);
        }
        else
            fprintf(stderr, "[funcwatch] watching all %zu functions\n", Funcs.size());
        melonDS::SsrWatch.Bitmap = bitmap->data();
        melonDS::SsrWatch.Base = Base;
        melonDS::SsrWatch.End = End;
        melonDS::SsrWatch.Hook = &FuncWatch::JitHook;
        melonDS::SsrWatch.User = this;
    }

    void NextFrame() { Frame++; }
    uint32_t CurrentFrame() const { return Frame; }

    int Find(const std::string& name) const
    {
        auto it = ByName.find(name);
        return it == ByName.end() ? -1 : it->second;
    }
    // True if the function was entered within the last `frames` frames (0: this frame only).
    bool Ran(int f, unsigned frames) const { return f >= 0 && Last[f] && Last[f] + frames >= Frame; }
    bool Ran(const std::string& name, unsigned frames) const { return Ran(Find(name), frames); }
    uint32_t LastFrame(int f) const { return f >= 0 ? Last[f] : 0; }
    const Func& Get(int f) const { return Funcs[f]; }
    size_t Count() const { return Funcs.size(); }

    // Writes "module name" for every function entered in the last `frames` frames.
    void Dump(const std::string& path, unsigned frames) const
    {
        std::ofstream out(path);
        for (size_t i = 0; i < Funcs.size(); i++)
            if (Last[i] && Last[i] + frames >= Frame) out << Funcs[i].module << " " << Funcs[i].name << "\n";
    }

private:
    // Main RAM and the ITCM below it, where the game's code runs.
    static constexpr uint32_t Base = 0x01FF8000, End = 0x02400000;

    static void JitHook(void* user, uint32_t addr, bool thumb) { static_cast<FuncWatch*>(user)->Hit(addr, thumb); }

    void Hit(uint32_t addr, bool thumb)
    {
        auto it = ByAddr.find(addr);
        if (it == ByAddr.end()) return;
        for (int i : it->second)
        {
            const Func& fn = Funcs[i];
            if (fn.thumb != thumb) continue;
            if (fn.check)
            {
                if (addr < 0x02000000) continue;
                uint32_t mem;
                memcpy(&mem, &Nds->MainRAM[addr & Nds->MainRAMMask], 4);
                if (mem != fn.first) continue;
            }
            Last[i] = Frame;
        }
    }

    std::vector<Func> Funcs;
    std::vector<uint32_t> Last;
    std::vector<uint8_t> Bitmap, JitBitmap;
    std::unordered_map<uint32_t, std::vector<int>> ByAddr;
    std::unordered_multimap<std::string, int> ByName;
    melonDS::NDS* Nds = nullptr;
    uint32_t Frame = 1;
};
