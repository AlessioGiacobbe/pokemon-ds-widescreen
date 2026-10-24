// More of a screen than the DS shows: its background layers drawn on below its last line,
// from their maps in video memory, as the 2D engine would draw them if the screen were
// taller. For screens where what the game has ready below the screen is worth showing on a
// taller view: Black 2's start menu, whose list goes on below the screen (the DS shows its
// eighth entry cut in half; the rest is reached by scrolling).
//
// Only what can be drawn without the screen's own picture: text (tiled) backgrounds, with
// the scroll, palettes and brightness of the screen's last line. Not sprites, rotating or
// bitmap backgrounds, windows or blending between layers; nor the 3D layer. A background
// that fills the screen's last line but whose map stops there (a pattern behind everything
// else, made only as tall as the screen) goes on as its last row of tiles repeated, rather
// than leaving the backdrop colour.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include "NDS.h"
#include "GPU2D_Soft.h"

// Rows [192, 192 + rows) of engine unitNum's screen into out (256 wide, XRGB as the screens
// are). False if the screen is not made the way this can draw (out is then left alone).
inline bool RenderBelowScreen(melonDS::NDS& nds, int unitNum, int rows, uint32_t* out)
{
    using namespace melonDS;
    const GPU2D::Unit& unit = unitNum == 0 ? nds.GPU.GPU2D_A : nds.GPU.GPU2D_B;
    const u32 dispcnt = unit.DispCnt;
    if (((dispcnt >> 16) & 3) != 1) return false;           // not the usual display
    if (dispcnt & (1u << 30)) return false;                 // extended palettes: not handled
    auto* renderer = static_cast<GPU2D::SoftRenderer*>(&nds.GPU.GetRenderer2D());
    const int mode = dispcnt & 7;

    struct Layer { int num, prio; bool c256; int mapW, mapH; u32 charBase, mapBase; };
    Layer layers[4];
    int n = 0;
    for (int prio = 0; prio < 4; prio++)
        for (int b = 0; b < 4; b++)
        {
            const u16 cnt = unit.BGCnt[b];
            if (!(dispcnt & (0x100 << b)) || (cnt & 3) != prio) continue;
            if (b == 0 && (dispcnt & 8)) continue;          // the 3D layer
            // Text backgrounds only: BG0 and BG1 always, BG2 in modes 0-1, BG3 in mode 0.
            if ((b == 2 && mode > 1) || (b == 3 && mode > 0)) continue;
            Layer& l = layers[n++];
            l.num = b;
            l.prio = prio;
            l.c256 = cnt & 0x80;
            l.mapW = (cnt & 0x4000) ? 512 : 256;
            l.mapH = (cnt & 0x8000) ? 512 : 256;
            l.charBase = ((cnt >> 2) & 0xF) * 0x4000;
            l.mapBase = ((cnt >> 8) & 0x1F) * 0x800;
            if (unitNum == 0)
            {
                l.charBase += ((dispcnt >> 24) & 7) * 0x10000;
                l.mapBase += ((dispcnt >> 27) & 7) * 0x10000;
            }
        }
    auto vram16 = [&](u32 a) -> u16 { return unitNum == 0 ? nds.GPU.ReadVRAM_ABG<u16>(a) : nds.GPU.ReadVRAM_BBG<u16>(a); };
    auto vram8 = [&](u32 a) -> u8 { return unitNum == 0 ? nds.GPU.ReadVRAM_ABG<u8>(a) : nds.GPU.ReadVRAM_BBG<u8>(a); };
    // The palettes, scroll and effects the screen's last line was drawn with.
    const u8* pal = renderer->PalSnap[unitNum];
    auto palette = [&](u32 off) -> u16 { u16 v; memcpy(&v, pal + (off & 0x3FE), 2); return v; };
    const u16* blend = renderer->LineBlend[unitNum][191];
    auto colour = [&](u16 c, int target) -> uint32_t {
        const int effect = (blend[0] & (1 << target)) ? (blend[0] >> 6) & 3 : 0;
        const int evy = blend[1] < 16 ? blend[1] : 16;
        const int mbMode = (blend[2] >> 14) & 3, mb = (blend[2] & 0x1F) < 16 ? (blend[2] & 0x1F) : 16;
        int ch[3] = { (c & 0x1F) << 1, ((c >> 5) & 0x1F) << 1, ((c >> 10) & 0x1F) << 1 };
        for (int& v : ch)
        {
            if (effect == 2) v += ((63 - v) * evy + 8) >> 4;
            else if (effect == 3) v -= (v * evy + 7) >> 4;
            if (mbMode == 1) v += ((63 - v) * mb) >> 4;
            else if (mbMode == 2) v -= (v * mb + 15) >> 4;
        }
        auto to8 = [](int v) { return (uint32_t)((v << 2) | (v >> 4)); };
        return 0xFF000000 | (to8(ch[0]) << 16) | (to8(ch[1]) << 8) | to8(ch[2]);
    };
    const uint32_t backdrop = colour(palette(0), 5);
    // A layer's palette index at (x, y) of the screen (0: transparent).
    auto sample = [&](const Layer& l, int x, int y) -> int {
        const int sx = (renderer->LineScroll[unitNum][191][l.num][0] + x) & (l.mapW - 1);
        const int sy = (renderer->LineScroll[unitNum][191][l.num][1] + y) & (l.mapH - 1);
        const int tx = sx >> 3, ty = sy >> 3;
        // 32x32-tile blocks: across first, then down (a 256-wide map has one across).
        const u32 block = (tx >> 5) + (ty >> 5) * (l.mapW >> 8);
        const u16 e = vram16(l.mapBase + block * 0x800 + (((ty & 31) << 5) + (tx & 31)) * 2);
        int px = sx & 7, py = sy & 7;
        if (e & 0x400) px = 7 - px;
        if (e & 0x800) py = 7 - py;
        if (l.c256) return vram8(l.charBase + (e & 0x3FF) * 64 + py * 8 + px);
        const u8 two = vram8(l.charBase + (e & 0x3FF) * 32 + py * 4 + (px >> 1));
        const int idx = (px & 1) ? two >> 4 : two & 0xF;
        return idx ? idx + (e >> 12) * 16 : 0;
    };
    // The layers that fill the screen's last line (nearly all of it).
    bool fills[4] = {};
    for (int i = 0; i < n; i++)
    {
        int covered = 0;
        for (int x = 0; x < 256; x++)
            if (sample(layers[i], x, 191)) covered++;
        fills[i] = covered >= 240;
    }
    for (int r = 0; r < rows; r++)
    {
        const int y = 192 + r;
        uint32_t* row = out + (size_t)r * 256;
        for (int x = 0; x < 256; x++)
        {
            uint32_t c = backdrop;
            bool found = false;
            for (int i = 0; i < n && !found; i++)
                if (const int idx = sample(layers[i], x, y))
                {
                    c = colour(palette(idx * 2), layers[i].num);
                    found = true;
                }
            // Nothing there: the layers that filled the last line, their last 8 rows again.
            for (int i = 0; i < n && !found; i++)
                if (fills[i])
                    if (const int idx = sample(layers[i], x, 184 + (r & 7)))
                    {
                        c = colour(palette(idx * 2), layers[i].num);
                        found = true;
                    }
            row[x] = c;
        }
    }
    return true;
}
