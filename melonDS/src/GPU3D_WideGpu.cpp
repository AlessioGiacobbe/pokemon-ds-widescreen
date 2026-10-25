// ssrecomp: the widescreen view's polygons drawn by the graphics chip. See GPU3D_WideGpu.h.
#include "GPU3D_WideGpu.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "GPU.h"
#include "GPU3D.h"
#include "SsrThreads.h"

namespace melonDS
{
std::unique_ptr<WideGpuBackend> WideGpuCurrent;
u64 WideGpuTexVersion = 0;
std::atomic<u64> WideGpuBusyNs { 0 };

namespace
{
std::unique_ptr<WideGpuBackend> MakeBackend(std::string& info, bool small)
{
    (void)small;
#ifdef SSR_DEKO3D
    return WideGpuMakeDeko(info, small);
#elif defined(OGLRENDERER_ENABLED)
    return WideGpuMakeGL(info);
#else
    info = "not in this build";
    return nullptr;
#endif
}
}

WideGpuDSRenderer::WideGpuDSRenderer(std::unique_ptr<WideGpuBackend> backend)
    : Renderer3D(false), Backend(std::move(backend))
{
}

WideGpuDSRenderer::~WideGpuDSRenderer()
{
    if (Pending) Backend->Fetch(0);
    WideGpuTexVersion = 0;
}

std::unique_ptr<WideGpuDSRenderer> WideGpuMakeDS(std::string& info)
{
    auto b = MakeBackend(info, true);
    if (!b) return nullptr;
    return std::make_unique<WideGpuDSRenderer>(std::move(b));
}

void WideGpuDSRenderer::RenderFrame(GPU& gpu)
{
    // The flat copies of texture memory brought up to date, as the software renderer does it.
    auto textureDirty = gpu.VRAMDirty_Texture.DeriveState(gpu.VRAMMap_Texture, gpu);
    auto texPalDirty = gpu.VRAMDirty_TexPal.DeriveState(gpu.VRAMMap_TexPal, gpu);
    const bool textureChanged = gpu.MakeVRAMFlat_TextureCoherent(textureDirty);
    const bool texPalChanged = gpu.MakeVRAMFlat_TexPalCoherent(texPalDirty);
    if (textureChanged || texPalChanged || !Version) Version++;
    WideGpuTexVersion = Version;

    if (Pending) Backend->Fetch(0); // (an earlier picture nobody asked for)
    Pixels = Backend->Target(0, 256, 192);
    WideGpuBuild(gpu, gpu.GPU3D.RenderPolygonRAM.data(), gpu.GPU3D.RenderNumPolygons, 256, 192, Frame);
    Frame.TexVersion = Version;
    Pending = Pixels && Backend->Submit(Frame, 0);
    Fetched = false;
}

u32* WideGpuDSRenderer::GetLine(int line)
{
    if (!Fetched)
    {
        Fetched = true;
        if (Pending)
        {
            SsrWaitTimer t(SsrStats().Wait3DNs);
            Backend->Fetch(0);
            Pending = false;
            // XRGB (8 bits) with the alpha in the top byte, to the software renderer's format:
            // red, green, blue in 6 bits from bits 0, 8, 16, alpha in 5 from bit 24.
            for (int i = 0; i < 256 * 192; i++)
            {
                const u32 v = Pixels[i];
                Lines[i] = ((v >> 18) & 0x3F) | (((v >> 10) & 0x3F) << 8) | (((v >> 2) & 0x3F) << 16) | (((v >> 27) & 0x1F) << 24);
            }
        }
    }
    return &Lines[256 * (line < 0 ? 0 : line > 191 ? 191 : line)];
}

bool WideGpuStart(std::string& info)
{
    WideGpuStop();
    WideGpuCurrent = MakeBackend(info, false);
    return WideGpuCurrent != nullptr;
}

void WideReleaseGpu();

void WideGpuStop()
{
    // (Pictures in the renderer's memory, the one on show among them, are copied out first.)
    if (WideGpuCurrent) WideReleaseGpu();
    WideGpuCurrent.reset();
}

void WideGpuBuild(const GPU& gpu, Polygon* const* list, u32 count, int W, int H, WideGpuFrame& f)
{
    const GPU3D& g = gpu.GPU3D;
    f.W = W;
    f.H = H;
    f.Vertices.clear();
    f.Polygons.clear();
    f.Draws.clear();

    WideGpuFrameConstants& k = f.Constants;
    const u32 dispCnt = g.RenderDispCnt;
    k.Size[0] = (u32)W;
    k.Size[1] = (u32)H;
    k.Size[2] = dispCnt;
    k.Size[3] = g.RenderAlphaRef;
    k.Scale[0] = 2.0f / W;
    k.Scale[1] = 2.0f / H;
    k.Scale[2] = 1.0f;
    k.Scale[3] = 0.0f;
    k.Fog[0] = g.RenderFogColor;
    k.Fog[1] = g.RenderFogOffset;
    k.Fog[2] = g.RenderFogShift;
    k.Fog[3] = ((dispCnt >> 7) & 1) | ((dispCnt & (1 << 6)) ? 0 : 2);
    for (int i = 0; i < 32; i++) k.Toon[i] = g.RenderToonTable[i];
    for (int i = 0; i < 36; i++) k.FogDensity[i] = i < 34 ? g.RenderFogDensityTable[i] : 0;

    // The rear plane, as GPU3D_Wide.cpp clears to it.
    const u32 a1 = g.RenderClearAttr1;
    const u32 clearAlpha = (a1 >> 16) & 0x1F;
    if (clearAlpha)
    {
        const u32 r = (a1 << 3) & 0xF8, gr = (a1 >> 2) & 0xF8, b = (a1 >> 7) & 0xF8;
        f.ClearColor[0] = (float)(r | (r >> 5)) / 255.0f;
        f.ClearColor[1] = (float)(gr | (gr >> 5)) / 255.0f;
        f.ClearColor[2] = (float)(b | (b >> 5)) / 255.0f;
        f.ClearColor[3] = 1.0f;
    }
    else
        f.ClearColor[0] = f.ClearColor[1] = f.ClearColor[2] = f.ClearColor[3] = 0.0f;
    f.ClearDepth = ((g.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;
    // (W-buffering is chosen for the whole frame, when it is flushed.)
    f.WDepth = false;
    for (u32 i = 0; i < count; i++)
        if (list[i])
        {
            f.WDepth = list[i]->WBuffer;
            break;
        }
    f.ClearDepthValue = f.WDepth ? 1.0f / (float)std::max<u32>(f.ClearDepth, 1) : (float)f.ClearDepth / 16777216.0f;
    f.ClearFog = (a1 >> 15) & 1;
    f.ClearStencil = clearAlpha ? 0x80 : 0;
    f.TexMem = gpu.VRAMFlat_Texture;
    f.PalMem = gpu.VRAMFlat_TexPal;
    f.TexVersion = WideGpuTexVersion;

    const bool texturing = dispCnt & 1;
    const bool blendEnabled = dispCnt & (1 << 3);
    const float sx = W / (256.0f * 16), sy = H / (192.0f * 16);

    // A polygon's triangles (a fan), appended to the vertices.
    auto add = [&](const Polygon* poly) {
        const u32 index = (u32)f.Polygons.size();
        WideGpuPolygon p;
        p.Attr = poly->Attr;
        p.TexParam = poly->TexParam;
        p.TexPalette = poly->TexPalette;
        p.Flags = (poly->WBuffer ? 1 : 0) | (texturing && ((poly->TexParam >> 26) & 0x7) ? 2 : 0);
        f.Polygons.push_back(p);
        WideGpuVertex v[10];
        for (u32 i = 0; i < poly->NumVertices; i++)
        {
            const Vertex* vtx = poly->Vertices[i];
            WideGpuVertex& o = v[i];
            o.X = vtx->HiresPosition[0] * sx;
            o.Y = vtx->HiresPosition[1] * sy;
            o.W = poly->FinalW[i] > 0 ? (float)poly->FinalW[i] : 1.0f;
            o.Z = (float)poly->FinalZ[i];
            o.R = (float)(vtx->FinalColor[0] >> 3);
            o.G = (float)(vtx->FinalColor[1] >> 3);
            o.B = (float)(vtx->FinalColor[2] >> 3);
            o.S = (float)vtx->TexCoords[0];
            o.T = (float)vtx->TexCoords[1];
            o.Poly = index;
            o.Pad0 = o.Pad1 = 0;
        }
        for (u32 i = 1; i + 1 < poly->NumVertices; i++)
        {
            f.Vertices.push_back(v[0]);
            f.Vertices.push_back(v[i]);
            f.Vertices.push_back(v[i + 1]);
        }
    };
    auto drawable = [](const Polygon* poly) {
        return poly && !poly->Degenerate && !poly->IsShadowMask && !poly->IsShadow && poly->Type == 0 && poly->NumVertices >= 3;
    };
    auto frontFirst = [](const Polygon* poly) { return !poly->Translucent && !(poly->Attr & (1 << 14)); };

    // Whether the shader can discard any of a polygon's pixels (drawn with all its pixels): not
    // when it is opaque (alpha 31, or 0, wireframe, drawn as 31) and so is every texel: no
    // texture, or one of 4, 16 or 256 colours whose colour 0 is not transparent. (Alpha 31 then
    // passes any alpha test but one with reference 31.)
    auto neverDiscards = [&](const Polygon* poly) {
        const u32 polyAlpha = (poly->Attr >> 16) & 31;
        if ((polyAlpha != 31 && polyAlpha != 0) || g.RenderAlphaRef >= 31) return false;
        const u32 format = texturing ? (poly->TexParam >> 26) & 7 : 0;
        return format == 0 || ((format == 2 || format == 3 || format == 4) && !(poly->TexParam & (1u << 29)));
    };

    // The opaque polygons, in the game's order, in as few draws as runs of them alike: those
    // whose pixels are never discarded tested early (see WideGpuDraw::Early).
    {
        bool runEarly = false;
        u32 runFirst = 0;
        auto closeRun = [&]() {
            const u32 n = (u32)f.Vertices.size() - runFirst;
            if (!n) return;
            WideGpuDraw d {};
            d.First = runFirst;
            d.Count = n;
            d.Pixels = 0;
            d.DepthWrite = true;
            d.StencilTest = WideGpuStencil::Always;
            d.StencilRef = 0x80;
            d.StencilMask = 0xFF;
            d.WriteDepthAttr = d.WriteFogAttr = true;
            d.Early = runEarly;
            f.Draws.push_back(d);
            runFirst = (u32)f.Vertices.size();
        };
        for (u32 i = 0; i < count; i++)
            if (drawable(list[i]) && frontFirst(list[i]))
            {
                const bool early = neverDiscards(list[i]);
                if (early != runEarly)
                {
                    closeRun();
                    runEarly = early;
                }
                add(list[i]);
            }
        closeRun();
    }
    const size_t opaqueDraws = f.Draws.size();

    // The others, in order. Consecutive polygons that are drawn alike share their draws: all
    // opaque ones (decals), or all translucent ones of one polygon ID; a polygon with pixels
    // of both kinds is drawn on its own.
    struct Kind
    {
        bool Opaque, Translucent, Lequal, WriteDepth;
        s32 Bias;
        u8 Id;
        bool operator==(const Kind& o) const
        {
            return Opaque == o.Opaque && Translucent == o.Translucent && Lequal == o.Lequal && WriteDepth == o.WriteDepth &&
                Bias == o.Bias && Id == o.Id;
        }
    };
    Kind group {};
    u32 groupFirst = 0;
    bool open = false;
    auto close = [&]() {
        if (!open) return;
        open = false;
        const u32 n = (u32)f.Vertices.size() - groupFirst;
        if (!n) return;
        WideGpuDraw d {};
        d.First = groupFirst;
        d.Count = n;
        d.DepthLequal = group.Lequal;
        d.DepthBias = group.Bias;
        if (group.Opaque)
        {
            d.Pixels = group.Translucent ? 1 : 0;
            d.DepthWrite = true;
            d.StencilTest = WideGpuStencil::Always;
            d.StencilRef = 0x80;
            d.StencilMask = 0xFF;
            d.WriteDepthAttr = d.WriteFogAttr = true;
            f.Draws.push_back(d);
        }
        if (group.Translucent)
        {
            d.Pixels = 2;
            // (SSR_WIDEGPU_EARLYTRANS=1: translucent polygons that do not write depth tested early
            // too, before shading. In Castelia 1.2 million pixels fewer and 1.5 ms less a 720-line
            // picture, the same picture; but not exact in general: a pixel they discard, a
            // transparent texel, still marks the stencil with their polygon ID, which keeps a
            // later translucent polygon of the same ID from blending there. A battle's grass
            // tufts at the platform's edge came out wrong, so it is off.)
            static const bool earlyTrans = getenv("SSR_WIDEGPU_EARLYTRANS") && !strcmp(getenv("SSR_WIDEGPU_EARLYTRANS"), "1");
            d.Early = earlyTrans && !group.WriteDepth;
            d.DepthWrite = group.WriteDepth;
            d.WriteDepthAttr = group.WriteDepth;
            d.WriteFogAttr = false;
            d.StencilRef = (u8)(0x80 | group.Id);
            if (blendEnabled)
            {
                // Where the picture has no alpha yet: drawn as they are. (Nowhere, when the rear
                // plane has alpha: every pixel's stencil has 0x80 from the start. The pass is then
                // left out, rather than having the graphics chip shade all its pixels, which it
                // does before testing them, for nothing.)
                d.StencilTest = WideGpuStencil::NotEqual;
                d.StencilMask = 0x80;
                d.Blend = false;
                if (!(f.ClearStencil & 0x80)) f.Draws.push_back(d);
                // Elsewhere, blended, except over their own ID.
                d.StencilMask = 0xFF;
                d.Blend = true;
                f.Draws.push_back(d);
            }
            else
            {
                d.StencilTest = WideGpuStencil::NotEqual;
                d.StencilMask = 0xFF;
                d.Blend = false;
                f.Draws.push_back(d);
            }
        }
    };
    for (u32 i = 0; i < count; i++)
    {
        const Polygon* poly = list[i];
        if (!drawable(poly) || frontFirst(poly)) continue;
        const u32 attr = poly->Attr;
        const u32 polyAlpha = (attr >> 16) & 0x1F;
        Kind kind;
        kind.Translucent = poly->Translucent && polyAlpha != 0;
        // (Alpha 31 is reached by translucent polygons only through their texture's alpha.)
        kind.Opaque = !poly->Translucent || polyAlpha == 31 || polyAlpha == 0;
        kind.Lequal = attr & (1 << 14);
        kind.Bias = kind.Lequal ? (poly->WBuffer ? 0xFF : 0x200) : 0;
        kind.WriteDepth = attr & (1 << 11);
        kind.Id = kind.Translucent ? (u8)(((attr >> 24) & 0x3F) + 1) : 0;
        if (!kind.Translucent) kind.WriteDepth = true;
        const bool mixed = kind.Opaque && kind.Translucent;
        if (!open || mixed || !(kind == group))
        {
            close();
            group = kind;
            groupFirst = (u32)f.Vertices.size();
            open = true;
        }
        add(poly);
        if (mixed) close();
    }
    close();
    // Development aid: how many polygons use W-buffering (SSR_WIDE_GPU_STATS=1).
    static const bool stats = getenv("SSR_WIDE_GPU_STATS") != nullptr;
    if (stats && W > 256)
    {
        // And how many pixels each kind of draw covers (as the graphics chip shades them all
        // when its shader may discard): triangles clipped to the picture.
        auto area = [&](const WideGpuVertex* t) {
            float px[16], py[16], qx[16], qy[16];
            int n = 3;
            for (int i = 0; i < 3; i++) { px[i] = t[i].X; py[i] = t[i].Y; }
            for (int edge = 0; edge < 4; edge++)
            {
                int m = 0;
                auto inside = [&](float x, float y) {
                    return edge == 0 ? x >= 0 : edge == 1 ? x <= W : edge == 2 ? y >= 0 : y <= H;
                };
                for (int i = 0; i < n; i++)
                {
                    const int j = (i + 1) % n;
                    const bool a = inside(px[i], py[i]), b = inside(px[j], py[j]);
                    if (a) { qx[m] = px[i]; qy[m] = py[i]; m++; }
                    if (a != b)
                    {
                        const float bound = edge == 0 ? 0 : edge == 1 ? W : edge == 2 ? 0 : H;
                        const float tt = edge < 2 ? (bound - px[i]) / (px[j] - px[i]) : (bound - py[i]) / (py[j] - py[i]);
                        qx[m] = px[i] + (px[j] - px[i]) * tt;
                        qy[m] = py[i] + (py[j] - py[i]) * tt;
                        m++;
                    }
                }
                n = m;
                std::copy(qx, qx + m, px);
                std::copy(qy, qy + m, py);
            }
            double sum = 0;
            for (int i = 0; i < n; i++) sum += (double)px[i] * py[(i + 1) % n] - (double)px[(i + 1) % n] * py[i];
            return std::abs(sum) / 2;
        };
        static double cover[5] = {}; // opaque that cannot discard, opaque that may, other opaque, translucent, frames
        for (size_t di = 0; di < f.Draws.size(); di++)
        {
            const WideGpuDraw& d = f.Draws[di];
            for (u32 v = d.First; v + 2 < d.First + d.Count + 0; v += 3)
            {
                const WideGpuPolygon& p = f.Polygons[f.Vertices[v].Poly];
                const double a = area(&f.Vertices[v]);
                int kind;
                (void)p;
                if (d.Early) kind = 0;
                else if (di < opaqueDraws) kind = 1;
                else kind = d.Pixels == 2 ? 3 : 2;
                cover[kind] += a;
            }
        }
        if (++cover[4] >= 120)
        {
            const double n = cover[4] * 1e6;
            fprintf(stderr, "[widegpu] million pixels a picture: opaque without discards %.2f, opaque that may discard %.2f, "
                "other opaque %.2f, translucent passes %.2f (rear plane alpha %u, blending %d)\n", cover[0] / n, cover[1] / n, cover[2] / n, cover[3] / n,
                clearAlpha, blendEnabled ? 1 : 0);
            std::fill(cover, cover + 5, 0.0);
        }
        static u64 frames = 0, polys = 0, wpolys = 0;
        frames++;
        for (const WideGpuPolygon& p : f.Polygons)
        {
            polys++;
            if (p.Flags & 1) wpolys++;
        }
        if (frames % 120 == 0) fprintf(stderr, "[widegpu] polygons %llu, W-buffered %llu\n", (unsigned long long)polys, (unsigned long long)wpolys);
    }
}
}
