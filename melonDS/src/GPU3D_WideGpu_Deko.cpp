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
#include <cstring>
#include <deko3d.h>
#include <switch.h>

#include "deko_shaders/wide_vsh_dksh.h"
#include "deko_shaders/wide_fsh_dksh.h"
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
constexpr u32 OffImageDesc = 0x300;                    // two image descriptors
constexpr u32 OffSamplerDesc = 0x380;                  // one sampler descriptor
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

class WideGpuDeko : public WideGpuBackend
{
public:
    const char* Name() const override { return "deko3D"; }

    bool Init(std::string& info)
    {
        DkDeviceMaker dm;
        dkDeviceMakerDefaults(&dm);
        dm.cbDebug = Debug;
        Device = dkDeviceCreate(&dm);
        if (!Device)
        {
            info = "no deko3D device";
            return false;
        }
        CmdMem = Block(CmdSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        CodeMem = Block(256 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
        DataMem = Block(DataSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!CmdMem || !CodeMem || !DataMem)
        {
            info = "deko3D memory could not be had";
            return false;
        }
        u32 used = 0;
        if (!Shader(MainVsh, wide_vsh_dksh, sizeof(wide_vsh_dksh), used) || !Shader(MainFsh, wide_fsh_dksh, sizeof(wide_fsh_dksh), used) ||
            !Shader(FinalVsh, final_vsh_dksh, sizeof(final_vsh_dksh), used) || !Shader(FinalFsh, final_fsh_dksh, sizeof(final_fsh_dksh), used))
        {
            info = "the deko3D shaders could not be loaded";
            return false;
        }
        DkQueueMaker qm;
        dkQueueMakerDefaults(&qm, Device);
        qm.flags = DkQueueFlags_Graphics | DkQueueFlags_DisableZcull;
        Queue = dkQueueCreate(&qm);
        DkCmdBufMaker cm;
        dkCmdBufMakerDefaults(&cm, Device);
        Cmd = dkCmdBufCreate(&cm);
        if (!Queue || !Cmd)
        {
            info = "no deko3D queue";
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
        return true;
    }

    ~WideGpuDeko() override
    {
        if (Queue) dkQueueWaitIdle(Queue);
        FreeTargets();
        if (Cmd) dkCmdBufDestroy(Cmd);
        if (Queue) dkQueueDestroy(Queue);
        if (DataMem) dkMemBlockDestroy(DataMem);
        if (CodeMem) dkMemBlockDestroy(CodeMem);
        if (CmdMem) dkMemBlockDestroy(CmdMem);
        if (Device) dkDeviceDestroy(Device);
    }

    bool Submit(WideGpuFrame& f) override
    {
        if (f.Vertices.size() > MaxVertices || f.Polygons.size() > MaxPolygons) return false;
        if (f.W != W || f.H != H)
        {
            if (!MakeTargets(f.W, f.H)) return false;
        }
        // deko3D's rows run from the top, as in memory (see wide_vsh.glsl).
        f.Constants.Scale[2] = 1.0f;

        memcpy(Data + OffFrame, &f.Constants, sizeof(f.Constants));
        memcpy(Data + OffPolygons, f.Polygons.data(), f.Polygons.size() * sizeof(WideGpuPolygon));
        memcpy(Data + OffTex, f.TexMem, 512 * 1024);
        memcpy(Data + OffPal, f.PalMem, 128 * 1024);
        memcpy(Data + OffVertices, f.Vertices.data(), f.Vertices.size() * sizeof(WideGpuVertex));

        dkCmdBufClear(Cmd);
        dkCmdBufAddMemory(Cmd, CmdMem, 0, CmdSize);
        // What the processor wrote since the last frame is read afresh.
        dkCmdBufBarrier(Cmd, DkBarrier_None, DkInvalidateFlags_Shader | DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);

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
        dkCmdBufClearDepthStencil(Cmd, true, (float)f.ClearDepth / 16777216.0f, 0xFF, f.ClearStencil);

        const DkShader* main[2] = { &MainVsh, &MainFsh };
        dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, main, 2);
        const DkBufExtents frame = { DataAddr + OffFrame, FrameSize };
        const DkBufExtents draw = { DataAddr + OffDraw, DrawSize };
        const DkBufExtents ubos[2] = { frame, draw };
        dkCmdBufBindUniformBuffers(Cmd, DkStage_Vertex, 0, ubos, 2);
        dkCmdBufBindUniformBuffers(Cmd, DkStage_Fragment, 0, ubos, 2);
        const DkBufExtents vertices = { DataAddr + OffVertices, Align((u32)std::max<size_t>(16, f.Vertices.size() * sizeof(WideGpuVertex)), 16) };
        dkCmdBufBindStorageBuffers(Cmd, DkStage_Vertex, 0, &vertices, 1);
        const DkBufExtents frag[3] = {
            { DataAddr + OffPolygons, MaxPolygons * (u32)sizeof(WideGpuPolygon) },
            { DataAddr + OffTex, 512 * 1024 },
            { DataAddr + OffPal, 128 * 1024 },
        };
        dkCmdBufBindStorageBuffers(Cmd, DkStage_Fragment, 1, frag, 3);

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
            const u32 params[4] = { d.First, d.Pixels, (u32)d.DepthBias, 0 };
            dkCmdBufPushConstants(Cmd, draw.addr, draw.size, 0, sizeof(params), params);
            DkDepthStencilState ds;
            dkDepthStencilStateDefaults(&ds);
            ds.depthTestEnable = true;
            ds.depthWriteEnable = d.DepthWrite;
            ds.depthCompareOp = d.DepthLequal ? DkCompareOp_Lequal : DkCompareOp_Less;
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
        const DkShader* fin[2] = { &FinalVsh, &FinalFsh };
        dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, fin, 2);
        dkCmdBufBindImageDescriptorSet(Cmd, DataAddr + OffImageDesc, 2);
        dkCmdBufBindSamplerDescriptorSet(Cmd, DataAddr + OffSamplerDesc, 1);
        const DkResHandle tex[2] = { dkMakeTextureHandle(0, 0), dkMakeTextureHandle(1, 0) };
        dkCmdBufBindTextures(Cmd, DkStage_Fragment, 0, tex, 2);
        dkCmdBufDraw(Cmd, DkPrimitive_Triangles, 3, 1, 0, 0);

        // The picture to memory the processor reads.
        dkCmdBufBarrier(Cmd, DkBarrier_Fragments, 0);
        const DkImageRect rect = { 0, 0, 0, (u32)W, (u32)H, 1 };
        const DkCopyBuf dst = { dkMemBlockGetGpuAddr(ReadMem), (u32)W * 4, 0 };
        dkCmdBufCopyImageToBuffer(Cmd, &finalView, &rect, &dst, 0);
        dkCmdBufBarrier(Cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
        dkCmdBufSignalFence(Cmd, &Fence, true);
        dkQueueSubmitCommands(Queue, dkCmdBufFinishList(Cmd));
        dkQueueFlush(Queue);
        Pending = true;
        return true;
    }

    void Fetch(u32* out) override
    {
        if (!Pending) return;
        Pending = false;
        if (dkFenceWait(&Fence, 1000000000ll) != DkResult_Success)
        {
            static int reported = 0;
            if (reported++ < 5) fprintf(stderr, "[widegpu] the graphics chip did not finish a picture within a second\n");
            memset(out, 0, (size_t)W * H * 4);
            return;
        }
        dkMemBlockFlushCpuCache(ReadMem, 0, (u32)W * H * 4);
        memcpy(out, dkMemBlockGetCpuAddr(ReadMem), (size_t)W * H * 4);
    }

private:
    DkMemBlock Block(u32 size, u32 flags)
    {
        DkMemBlockMaker m;
        dkMemBlockMakerDefaults(&m, Device, Align(size, DK_MEMBLOCK_ALIGNMENT));
        m.flags = flags;
        return dkMemBlockCreate(&m);
    }

    bool Shader(DkShader& shader, const unsigned char* file, size_t size, u32& used)
    {
        DkshHeader h;
        if (size < sizeof(h)) return false;
        memcpy(&h, file, sizeof(h));
        if (h.ControlSize + h.CodeSize > size) return false;
        const u32 at = Align(used, DK_SHADER_CODE_ALIGNMENT);
        if (at + h.CodeSize + DK_SHADER_CODE_UNUSABLE_SIZE > dkMemBlockGetSize(CodeMem)) return false;
        memcpy((u8*)dkMemBlockGetCpuAddr(CodeMem) + at, file + h.ControlSize, h.CodeSize);
        used = at + h.CodeSize;
        DkShaderMaker m;
        dkShaderMakerDefaults(&m, CodeMem, at);
        m.control = file;
        m.programId = 0;
        dkShaderInitialize(&shader, &m);
        return dkShaderIsValid(&shader);
    }

    bool Image(DkImage& image, DkMemBlock& mem, DkImageFormat format, int w, int h, u32 flags)
    {
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, Device);
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

    void FreeTargets()
    {
        for (DkMemBlock* m : { &ColorMem, &AttrMem, &DepthMem, &FinalMem, &ReadMem })
        {
            if (*m) dkMemBlockDestroy(*m);
            *m = nullptr;
        }
        W = H = 0;
    }

    bool MakeTargets(int w, int h)
    {
        if (Queue) dkQueueWaitIdle(Queue);
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
        ReadMem = Block((u32)w * h * 4, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached);
        if (!ReadMem)
        {
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
        memcpy(Data + OffImageDesc, desc, sizeof(desc));
        W = w;
        H = h;
        fprintf(stderr, "[widegpu] deko3D targets %dx%d\n", w, h);
        return true;
    }

    DkDevice Device = nullptr;
    DkMemBlock CmdMem = nullptr, CodeMem = nullptr, DataMem = nullptr;
    DkMemBlock ColorMem = nullptr, AttrMem = nullptr, DepthMem = nullptr, FinalMem = nullptr, ReadMem = nullptr;
    DkQueue Queue = nullptr;
    DkCmdBuf Cmd = nullptr;
    DkShader MainVsh, MainFsh, FinalVsh, FinalFsh;
    DkImage ColorImage, AttrImage, DepthImage, FinalImage;
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
