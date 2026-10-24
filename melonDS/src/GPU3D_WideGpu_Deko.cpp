// ssrecomp: the widescreen view's polygons drawn by deko3D, the Switch's own graphics API
// (GPU3D_WideGpu.h). Tested first with frontend/dekoprobe.cpp: beside SDL's OpenGL, a
// 1280x720 picture took 0.05 ms to send, 1.8 ms on the graphics chip and 1.5 ms to read.
//
// Memory: the frame's data (constants, vertices, polygons, the DS's texture memory) is written
// by the processor into memory the graphics chip reads; the targets are images; the finished
// picture is copied to memory the processor reads through its cache. One frame is drawn at a
// time (GPU3D_Wide.cpp fetches each picture before it hands over the next frame), so nothing
// is double-buffered.
#ifdef SSR_DEKO3D
#include "GPU3D_WideGpu.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deko3d.h>
#include <switch.h>

#include "deko_shaders/wide_vsh_dksh.h"
#include "deko_shaders/wide_fsh_dksh.h"
#include "deko_shaders/wide_fsh_texbuf_dksh.h"
#include "deko_shaders/final_vsh_dksh.h"
#include "deko_shaders/final_fsh_dksh.h"

namespace melonDS
{
namespace
{
constexpr u32 CmdSize = 4 * 1024 * 1024;
constexpr u32 MaxVertices = 4096 * 8 * 3; // (the wide lists' polygons, up to 8 triangles each)
constexpr u32 MaxPolygons = 4096;

// Where things are in the data memory.
constexpr u32 OffFrame = 0;                            // uniform buffer 0
constexpr u32 FrameSize = 0x200;
constexpr u32 OffDraw = 0x200;                         // uniform buffer 1, set per draw
constexpr u32 DrawSize = 0x100;
constexpr u32 OffImageDesc = 0x300;                    // image descriptors (see ImageIds)
constexpr u32 OffSamplerDesc = 0x400;                  // one sampler descriptor
// The images in the descriptor set: the final pass's two textures (what was drawn), and the
// texture and palette memory as buffer textures (the WIDE_TEXBUF shader, see Shared).
enum ImageIds : u32 { ImgColor, ImgAttr, ImgTex8, ImgTex16, ImgPal16, ImageCount };
static_assert(OffImageDesc + ImageCount * 32 <= OffSamplerDesc, "the image descriptors fit");
constexpr u32 OffPolygons = 0x1000;
constexpr u32 OffTex = OffPolygons + MaxPolygons * sizeof(WideGpuPolygon);
constexpr u32 OffPal = OffTex + 512 * 1024;
constexpr u32 OffVertices = OffPal + 128 * 1024;
constexpr u32 DataSize = OffVertices + MaxVertices * sizeof(WideGpuVertex);
static_assert(sizeof(WideGpuFrameConstants) <= FrameSize, "the frame constants fit");

u32 Align(u32 v, u32 a)
{
    return (v + a - 1) & ~(a - 1);
}

void Debug(void*, const char* context, DkResult result, const char* message)
{
    static int reported = 0;
    if (reported++ < 20)
    {
        fprintf(stderr, "[widegpu] deko3D error %d in %s: %s\n", (int)result, context ? context : "?", message ? message : "");
        fflush(stderr);
    }
}

struct DkshHeader
{
    u32 Magic, HeaderSize, ControlSize, CodeSize, ProgramsOffset, NumPrograms;
};

// What every renderer shares: the device, the queue its commands go to, the shaders. (Two
// renderers are in use at once: the widescreen view's and the DS-resolution picture's.)
struct Shared
{
    DkDevice Device = nullptr;
    DkQueue Queue = nullptr;
    DkMemBlock CodeMem = nullptr;
    DkShader MainVsh, MainFsh, FinalVsh, FinalFsh;
    int Users = 0;
    // For measuring on the Switch (SSR_WIDEGPU_SHADER=texbuf, SSR_WIDEGPU_ZCULL=1 in the
    // environment, e.g. through a test run's --env): the fragment shader that reads the texture
    // and palette memory as buffer textures, through the texture cache; the graphics chip's
    // coarse depth test (Zcull), which rejects hidden pixels in blocks before they are shaded.
    bool TexBuf = false, Zcull = false;
};
Shared* Sh = nullptr;

DkMemBlock MakeBlock(u32 size, u32 flags)
{
    DkMemBlockMaker m;
    dkMemBlockMakerDefaults(&m, Sh->Device, Align(size, DK_MEMBLOCK_ALIGNMENT));
    m.flags = flags;
    return dkMemBlockCreate(&m);
}

bool LoadShader(DkShader& shader, const unsigned char* file, size_t size, u32& used)
{
    DkshHeader h;
    if (size < sizeof(h)) return false;
    memcpy(&h, file, sizeof(h));
    if (h.ControlSize + h.CodeSize > size) return false;
    const u32 at = Align(used, DK_SHADER_CODE_ALIGNMENT);
    if (at + h.CodeSize + DK_SHADER_CODE_UNUSABLE_SIZE > dkMemBlockGetSize(Sh->CodeMem)) return false;
    memcpy((u8*)dkMemBlockGetCpuAddr(Sh->CodeMem) + at, file + h.ControlSize, h.CodeSize);
    used = at + h.CodeSize;
    DkShaderMaker m;
    dkShaderMakerDefaults(&m, Sh->CodeMem, at);
    m.control = file;
    m.programId = 0;
    dkShaderInitialize(&shader, &m);
    return dkShaderIsValid(&shader);
}

void ReleaseShared()
{
    if (!Sh || --Sh->Users > 0) return;
    if (Sh->Queue)
    {
        dkQueueWaitIdle(Sh->Queue);
        dkQueueDestroy(Sh->Queue);
    }
    if (Sh->CodeMem) dkMemBlockDestroy(Sh->CodeMem);
    if (Sh->Device) dkDeviceDestroy(Sh->Device);
    delete Sh;
    Sh = nullptr;
}

bool AcquireShared(std::string& info)
{
    if (Sh)
    {
        Sh->Users++;
        return true;
    }
    Sh = new Shared();
    Sh->Users = 1;
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = Debug;
    Sh->Device = dkDeviceCreate(&dm);
    if (!Sh->Device)
    {
        info = "no deko3D device";
        ReleaseShared();
        return false;
    }
    const char* shader = getenv("SSR_WIDEGPU_SHADER");
    const char* zcull = getenv("SSR_WIDEGPU_ZCULL");
    Sh->TexBuf = shader && !strcmp(shader, "texbuf");
    Sh->Zcull = zcull && !strcmp(zcull, "1");
    fprintf(stderr, "[widegpu] deko3D: %s fragment shader, Zcull %s\n", Sh->TexBuf ? "buffer-texture" : "storage-buffer",
        Sh->Zcull ? "on" : "off");
    Sh->CodeMem = MakeBlock(256 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    u32 used = 0;
    if (!Sh->CodeMem || !LoadShader(Sh->MainVsh, wide_vsh_dksh, sizeof(wide_vsh_dksh), used) ||
        !(Sh->TexBuf ? LoadShader(Sh->MainFsh, wide_fsh_texbuf_dksh, sizeof(wide_fsh_texbuf_dksh), used)
                     : LoadShader(Sh->MainFsh, wide_fsh_dksh, sizeof(wide_fsh_dksh), used)) ||
        !LoadShader(Sh->FinalVsh, final_vsh_dksh, sizeof(final_vsh_dksh), used) ||
        !LoadShader(Sh->FinalFsh, final_fsh_dksh, sizeof(final_fsh_dksh), used))
    {
        info = "the deko3D shaders could not be loaded";
        ReleaseShared();
        return false;
    }
    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, Sh->Device);
    qm.flags = DkQueueFlags_Graphics | (Sh->Zcull ? DkQueueFlags_EnableZcull : DkQueueFlags_DisableZcull);
    Sh->Queue = dkQueueCreate(&qm);
    if (!Sh->Queue)
    {
        info = "no deko3D queue";
        ReleaseShared();
        return false;
    }
    return true;
}

class WideGpuDeko : public WideGpuBackend
{
public:
    const char* Name() const override { return "deko3D"; }

    bool Init(std::string& info)
    {
        if (!AcquireShared(info)) return false;
        HaveShared = true;
        CmdMem = Block(CmdSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        DataMem = Block(DataSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!CmdMem || !DataMem)
        {
            info = "deko3D memory could not be had";
            return false;
        }
        DkCmdBufMaker cm;
        dkCmdBufMakerDefaults(&cm, Sh->Device);
        Cmd = dkCmdBufCreate(&cm);
        if (!Cmd)
        {
            info = "no deko3D command buffer";
            return false;
        }
        Data = (u8*)dkMemBlockGetCpuAddr(DataMem);
        DataAddr = dkMemBlockGetGpuAddr(DataMem);

        // The final pass reads the targets texel by texel.
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.minFilter = DkFilter_Nearest;
        sampler.magFilter = DkFilter_Nearest;
        sampler.wrapMode[0] = sampler.wrapMode[1] = sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
        DkSamplerDescriptor sd;
        dkSamplerDescriptorInitialize(&sd, &sampler);
        memcpy(Data + OffSamplerDesc, &sd, sizeof(sd));

        // The texture memory (as bytes and as 16-bit texels) and the palette memory (16-bit) as
        // buffer textures over the data memory.
        auto buffer = [&](DkImage& image, DkImageFormat format, u32 offset, u32 texels, u32 id) {
            DkImageLayoutMaker lm;
            dkImageLayoutMakerDefaults(&lm, Sh->Device);
            lm.type = DkImageType_Buffer;
            lm.format = format;
            lm.dimensions[0] = texels;
            DkImageLayout layout;
            dkImageLayoutInitialize(&layout, &lm);
            dkImageInitialize(&image, &layout, DataMem, offset);
            DkImageView view;
            dkImageViewDefaults(&view, &image);
            DkImageDescriptor desc;
            dkImageDescriptorInitialize(&desc, &view, false, false);
            memcpy(Data + OffImageDesc + id * sizeof(desc), &desc, sizeof(desc));
        };
        buffer(Tex8Image, DkImageFormat_R8_Uint, OffTex, 512 * 1024, ImgTex8);
        buffer(Tex16Image, DkImageFormat_R16_Uint, OffTex, 256 * 1024, ImgTex16);
        buffer(Pal16Image, DkImageFormat_R16_Uint, OffPal, 64 * 1024, ImgPal16);
        return true;
    }

    ~WideGpuDeko() override
    {
        if (!HaveShared) return;
        if (Pending) dkFenceWait(&Fence, 1000000000ll);
        dkQueueWaitIdle(Sh->Queue);
        FreeTargets();
        for (Slot& s : Slots)
            if (s.Mem) dkMemBlockDestroy(s.Mem);
        if (Cmd) dkCmdBufDestroy(Cmd);
        if (DataMem) dkMemBlockDestroy(DataMem);
        if (CmdMem) dkMemBlockDestroy(CmdMem);
        ReleaseShared();
    }

    u32* Target(int slot, int w, int h) override
    {
        Slot& s = Slots[slot];
        const u32 size = (u32)w * h * 4;
        if (s.Mem && s.Size != size)
        {
            // (This slot is not the one on show, nor the one before: GPU3D_Wide.cpp.)
            dkMemBlockDestroy(s.Mem);
            s.Mem = nullptr;
        }
        if (!s.Mem)
        {
            s.Mem = Block(size, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached);
            s.Size = s.Mem ? size : 0;
        }
        return s.Mem ? (u32*)dkMemBlockGetCpuAddr(s.Mem) : nullptr;
    }

    bool Submit(WideGpuFrame& f, int slot) override
    {
        if (f.Vertices.size() > MaxVertices || f.Polygons.size() > MaxPolygons) return false;
        if (!Slots[slot].Mem || Slots[slot].Size != (u32)f.W * f.H * 4) return false;
        const double t0 = Now();
        if (f.W != W || f.H != H)
        {
            if (!MakeTargets(f.W, f.H)) return false;
        }
        // deko3D's rows run from the top, as in memory (see wide_vsh.glsl).
        f.Constants.Scale[2] = 1.0f;
        f.Constants.Scale[3] = 0.0f; // depth from 0 to 1

        memcpy(Data + OffFrame, &f.Constants, sizeof(f.Constants));
        memcpy(Data + OffPolygons, f.Polygons.data(), f.Polygons.size() * sizeof(WideGpuPolygon));
        // (The texture memory only when it may have changed.)
        if (!f.TexVersion || f.TexVersion != TexVersion)
        {
            memcpy(Data + OffTex, f.TexMem, 512 * 1024);
            memcpy(Data + OffPal, f.PalMem, 128 * 1024);
            TexVersion = f.TexVersion;
        }
        memcpy(Data + OffVertices, f.Vertices.data(), f.Vertices.size() * sizeof(WideGpuVertex));
        const double t1 = Now();

        dkCmdBufClear(Cmd);
        dkCmdBufAddMemory(Cmd, CmdMem, 0, CmdSize);
        // What the processor wrote since the last frame is read afresh.
        dkCmdBufBarrier(Cmd, DkBarrier_None, DkInvalidateFlags_Shader | DkInvalidateFlags_Image |
            DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);

        DkImageView colorView, attrView, depthView, finalView;
        dkImageViewDefaults(&colorView, &ColorImage);
        dkImageViewDefaults(&attrView, &AttrImage);
        dkImageViewDefaults(&depthView, &DepthImage);
        dkImageViewDefaults(&finalView, &FinalImage);
        const DkImageView* targets[2] = { &colorView, &attrView };
        dkCmdBufBindRenderTargets(Cmd, targets, 2, &depthView);
        const DkViewport vp = { 0, 0, (float)W, (float)H, 0, 1 };
        dkCmdBufSetViewports(Cmd, 0, &vp, 1);
        const DkScissor sc = { 0, 0, (u32)W, (u32)H };
        dkCmdBufSetScissors(Cmd, 0, &sc, 1);

        DkColorWriteState all;
        dkColorWriteStateDefaults(&all);
        dkCmdBufBindColorWriteState(Cmd, &all);
        dkCmdBufClearColor(Cmd, 0, DkColorMask_RGBA, f.ClearColor);
        const u32 clearAttr[4] = { f.ClearDepth, f.ClearFog, 0, 0 };
        dkCmdBufClearColor(Cmd, 1, DkColorMask_RGBA, clearAttr);
        dkCmdBufClearDepthStencil(Cmd, true, f.ClearDepthValue, 0xFF, f.ClearStencil);

        const DkShader* main[2] = { &Sh->MainVsh, &Sh->MainFsh };
        dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, main, 2);
        const DkBufExtents frame = { DataAddr + OffFrame, FrameSize };
        const DkBufExtents draw = { DataAddr + OffDraw, DrawSize };
        const DkBufExtents ubos[2] = { frame, draw };
        dkCmdBufBindUniformBuffers(Cmd, DkStage_Vertex, 0, ubos, 2);
        dkCmdBufBindUniformBuffers(Cmd, DkStage_Fragment, 0, ubos, 2);
        const DkBufExtents vertices = { DataAddr + OffVertices, Align((u32)std::max<size_t>(16, f.Vertices.size() * sizeof(WideGpuVertex)), 16) };
        const DkBufExtents polygons = { DataAddr + OffPolygons, MaxPolygons * (u32)sizeof(WideGpuPolygon) };
        const DkBufExtents vert[2] = { vertices, polygons };
        dkCmdBufBindStorageBuffers(Cmd, DkStage_Vertex, 0, vert, 2);
        dkCmdBufBindImageDescriptorSet(Cmd, DataAddr + OffImageDesc, ImageCount);
        dkCmdBufBindSamplerDescriptorSet(Cmd, DataAddr + OffSamplerDesc, 1);
        if (Sh->TexBuf)
        {
            const DkResHandle mem[3] = { dkMakeTextureHandle(ImgTex8, 0), dkMakeTextureHandle(ImgTex16, 0), dkMakeTextureHandle(ImgPal16, 0) };
            dkCmdBufBindTextures(Cmd, DkStage_Fragment, 0, mem, 3);
        }
        else
        {
            const DkBufExtents frag[3] = {
                polygons,
                { DataAddr + OffTex, 512 * 1024 },
                { DataAddr + OffPal, 128 * 1024 },
            };
            dkCmdBufBindStorageBuffers(Cmd, DkStage_Fragment, 1, frag, 3);
        }

        DkRasterizerState rs;
        dkRasterizerStateDefaults(&rs);
        rs.cullMode = DkFace_None;
        dkCmdBufBindRasterizerState(Cmd, &rs);
        DkBlendState blend;
        dkBlendStateDefaults(&blend);
        dkBlendStateSetOps(&blend, DkBlendOp_Add, DkBlendOp_Max);
        dkBlendStateSetFactors(&blend, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_One, DkBlendFactor_One);
        dkCmdBufBindBlendStates(Cmd, 0, &blend, 1);

        for (const WideGpuDraw& d : f.Draws)
        {
            const u32 params[4] = { d.First, d.Pixels, (u32)d.DepthBias, f.WDepth ? 1u : 0u };
            dkCmdBufPushConstants(Cmd, draw.addr, draw.size, 0, sizeof(params), params);
            DkDepthStencilState ds;
            dkDepthStencilStateDefaults(&ds);
            ds.depthTestEnable = true;
            ds.depthWriteEnable = d.DepthWrite;
            ds.depthCompareOp = f.WDepth ? (d.DepthLequal ? DkCompareOp_Gequal : DkCompareOp_Greater)
                                         : (d.DepthLequal ? DkCompareOp_Lequal : DkCompareOp_Less);
            ds.stencilTestEnable = true;
            const DkCompareOp op = d.StencilTest == WideGpuStencil::Always ? DkCompareOp_Always :
                d.StencilTest == WideGpuStencil::Equal ? DkCompareOp_Equal : DkCompareOp_NotEqual;
            ds.stencilFrontFailOp = ds.stencilBackFailOp = DkStencilOp_Keep;
            ds.stencilFrontDepthFailOp = ds.stencilBackDepthFailOp = DkStencilOp_Keep;
            ds.stencilFrontPassOp = ds.stencilBackPassOp = DkStencilOp_Replace;
            ds.stencilFrontCompareOp = ds.stencilBackCompareOp = op;
            dkCmdBufBindDepthStencilState(Cmd, &ds);
            dkCmdBufSetStencil(Cmd, DkFace_FrontAndBack, 0xFF, d.StencilRef, d.StencilMask);
            DkColorState cs;
            dkColorStateDefaults(&cs);
            dkColorStateSetBlendEnable(&cs, 0, d.Blend);
            dkCmdBufBindColorState(Cmd, &cs);
            DkColorWriteState cw;
            dkColorWriteStateDefaults(&cw);
            dkColorWriteStateSetMask(&cw, 0, DkColorMask_RGBA);
            dkColorWriteStateSetMask(&cw, 1, (d.WriteDepthAttr ? DkColorMask_R : 0) | (d.WriteFogAttr ? DkColorMask_G : 0));
            dkCmdBufBindColorWriteState(Cmd, &cw);
            dkCmdBufDraw(Cmd, DkPrimitive_Triangles, d.Count, 1, 0, 0);
        }

        // The final pass reads what was drawn.
        dkCmdBufBarrier(Cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
        const DkImageView* finalTarget[1] = { &finalView };
        dkCmdBufBindRenderTargets(Cmd, finalTarget, 1, nullptr);
        DkDepthStencilState off;
        dkDepthStencilStateDefaults(&off);
        off.depthTestEnable = false;
        off.depthWriteEnable = false;
        off.stencilTestEnable = false;
        dkCmdBufBindDepthStencilState(Cmd, &off);
        DkColorState plain;
        dkColorStateDefaults(&plain);
        dkCmdBufBindColorState(Cmd, &plain);
        dkCmdBufBindColorWriteState(Cmd, &all);
        const DkShader* fin[2] = { &Sh->FinalVsh, &Sh->FinalFsh };
        dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, fin, 2);
        const DkResHandle tex[2] = { dkMakeTextureHandle(ImgColor, 0), dkMakeTextureHandle(ImgAttr, 0) };
        dkCmdBufBindTextures(Cmd, DkStage_Fragment, 0, tex, 2);
        dkCmdBufDraw(Cmd, DkPrimitive_Triangles, 3, 1, 0, 0);

        // The picture to memory the processor reads.
        dkCmdBufBarrier(Cmd, DkBarrier_Fragments, 0);
        const DkImageRect rect = { 0, 0, 0, (u32)W, (u32)H, 1 };
        const DkCopyBuf dst = { dkMemBlockGetGpuAddr(Slots[slot].Mem), (u32)W * 4, 0 };
        dkCmdBufCopyImageToBuffer(Cmd, &finalView, &rect, &dst, 0);
        dkCmdBufBarrier(Cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
        dkCmdBufSignalFence(Cmd, &Fence, true);
        dkQueueSubmitCommands(Sh->Queue, dkCmdBufFinishList(Cmd));
        dkQueueFlush(Sh->Queue);
        Pending = true;
        const double t2 = Now();
        T.Copy += t1 - t0;
        T.Record += t2 - t1;
        T.Draws += (u32)f.Draws.size();
        T.Triangles += (u32)f.Vertices.size() / 3;
        return true;
    }

    void Fetch(int slot) override
    {
        if (!Pending) return;
        Pending = false;
        const double t0 = Now();
        if (dkFenceWait(&Fence, 1000000000ll) != DkResult_Success)
        {
            static int reported = 0;
            if (reported++ < 5) fprintf(stderr, "[widegpu] the graphics chip did not finish a picture within a second\n");
            return;
        }
        const double t1 = Now();
        // (Cached memory: lines the processor read before are dropped, so that it reads the
        // graphics chip's picture.)
        dkMemBlockFlushCpuCache(Slots[slot].Mem, 0, Slots[slot].Size);
        const double t2 = Now();
        T.Wait += t1 - t0;
        T.Flush += t2 - t1;
        // Where the time goes, now and then in the log.
        if (++T.Pictures == 600)
        {
            const double n = T.Pictures;
            fprintf(stderr, "[widegpu] per picture (%dx%d, over %u): copying the data %.2f ms, making the commands %.2f ms, "
                "waiting for the graphics chip %.2f ms, cache %.2f ms; %u triangles, %u draws\n", W, H, T.Pictures,
                T.Copy / n, T.Record / n, T.Wait / n, T.Flush / n, (u32)(T.Triangles / n), (u32)(T.Draws / n));
            T = Times();
        }
    }

private:
    static DkMemBlock Block(u32 size, u32 flags) { return MakeBlock(size, flags); }

    bool Image(DkImage& image, DkMemBlock& mem, DkImageFormat format, int w, int h, u32 flags)
    {
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, Sh->Device);
        lm.flags = flags;
        lm.format = format;
        lm.dimensions[0] = (u32)w;
        lm.dimensions[1] = (u32)h;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        mem = Block((u32)dkImageLayoutGetSize(&layout) + dkImageLayoutGetAlignment(&layout), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
        if (!mem) return false;
        dkImageInitialize(&image, &layout, mem, 0);
        return true;
    }

    static double Now()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void FreeTargets()
    {
        for (DkMemBlock* m : { &ColorMem, &AttrMem, &DepthMem, &FinalMem })
        {
            if (*m) dkMemBlockDestroy(*m);
            *m = nullptr;
        }
        W = H = 0;
    }

    bool MakeTargets(int w, int h)
    {
        dkQueueWaitIdle(Sh->Queue);
        FreeTargets();
        const u32 render = DkImageFlags_UsageRender;
        if (!Image(ColorImage, ColorMem, DkImageFormat_RGBA8_Unorm, w, h, render) ||
            !Image(AttrImage, AttrMem, DkImageFormat_RG32_Uint, w, h, render) ||
            !Image(DepthImage, DepthMem, DkImageFormat_ZF32_X24S8, w, h, render) ||
            !Image(FinalImage, FinalMem, DkImageFormat_BGRA8_Unorm, w, h, DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine))
        {
            fprintf(stderr, "[widegpu] deko3D images of %dx%d could not be had\n", w, h);
            FreeTargets();
            return false;
        }
        // The final pass's two textures.
        DkImageView colorView, attrView;
        dkImageViewDefaults(&colorView, &ColorImage);
        dkImageViewDefaults(&attrView, &AttrImage);
        DkImageDescriptor desc[2];
        dkImageDescriptorInitialize(&desc[0], &colorView, false, false);
        dkImageDescriptorInitialize(&desc[1], &attrView, false, false);
        memcpy(Data + OffImageDesc + ImgColor * sizeof(desc[0]), desc, sizeof(desc));
        W = w;
        H = h;
        fprintf(stderr, "[widegpu] deko3D targets %dx%d\n", w, h);
        return true;
    }

    bool HaveShared = false;
    u64 TexVersion = 0;
    DkMemBlock CmdMem = nullptr, DataMem = nullptr;
    DkMemBlock ColorMem = nullptr, AttrMem = nullptr, DepthMem = nullptr, FinalMem = nullptr;
    // The pictures (see Target).
    struct Slot
    {
        DkMemBlock Mem = nullptr;
        u32 Size = 0;
    };
    Slot Slots[3];
    struct Times
    {
        double Copy = 0, Record = 0, Wait = 0, Flush = 0;
        u64 Triangles = 0, Draws = 0;
        u32 Pictures = 0;
    };
    Times T;
    DkCmdBuf Cmd = nullptr;
    DkImage ColorImage, AttrImage, DepthImage, FinalImage;
    DkImage Tex8Image, Tex16Image, Pal16Image;
    DkFence Fence;
    u8* Data = nullptr;
    DkGpuAddr DataAddr = 0;
    int W = 0, H = 0;
    bool Pending = false;
};
}

std::unique_ptr<WideGpuBackend> WideGpuMakeDeko(std::string& info)
{
    std::unique_ptr<WideGpuDeko> r(new WideGpuDeko());
    if (!r->Init(info)) return nullptr;
    return r;
}
}
#endif
