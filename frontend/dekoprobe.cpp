// ssrecomp: a first test of deko3D on the Switch (see dekoprobe.h).
#if defined(__SWITCH__) && defined(SSR_DEKO3D)
#include "dekoprobe.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include <deko3d.h>
#include <switch.h>

#include "deko_shaders/probe_vsh.h"
#include "deko_shaders/probe_fsh.h"

namespace DekoProbe
{
namespace
{
double NowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Step(const char* what)
{
    fprintf(stderr, "[deko] %s\n", what);
    fflush(stderr);
}

void Debug(void*, const char* context, DkResult result, const char* message)
{
    fprintf(stderr, "[deko] error %d in %s: %s\n", (int)result, context ? context : "?", message ? message : "");
    fflush(stderr);
}

uint32_t Align(uint32_t v, uint32_t a)
{
    return (v + a - 1) & ~(a - 1);
}

// A .dksh file (uam's output): the control section (which starts with this header) and
// then the code, which goes to code memory.
struct DkshHeader
{
    uint32_t magic, headerSize, controlSize, codeSize, programsOffset, numPrograms;
};

bool LoadShader(DkShader& shader, const unsigned char* file, size_t size, DkMemBlock code, uint32_t& codeUsed)
{
    DkshHeader h;
    if (size < sizeof(h)) return false;
    memcpy(&h, file, sizeof(h));
    if (h.controlSize + h.codeSize > size) return false;
    const uint32_t at = Align(codeUsed, DK_SHADER_CODE_ALIGNMENT);
    if (at + h.codeSize > dkMemBlockGetSize(code)) return false;
    memcpy((uint8_t*)dkMemBlockGetCpuAddr(code) + at, file + h.controlSize, h.codeSize);
    codeUsed = at + h.codeSize;
    DkShaderMaker m;
    dkShaderMakerDefaults(&m, code, at);
    m.control = file; // (read when the shader is made, not kept)
    m.programId = 0;
    dkShaderInitialize(&shader, &m);
    return dkShaderIsValid(&shader);
}

DkMemBlock MakeBlock(DkDevice dev, uint32_t size, uint32_t flags)
{
    DkMemBlockMaker m;
    dkMemBlockMakerDefaults(&m, dev, Align(size, DK_MEMBLOCK_ALIGNMENT));
    m.flags = flags;
    return dkMemBlockCreate(&m);
}
}

void Run(int w, int h)
{
    Step("starting (a test of the Switch's own graphics API, deko3D, beside SDL's OpenGL)");
    const double t0 = NowMs();
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = Debug;
    DkDevice dev = dkDeviceCreate(&dm);
    if (!dev)
    {
        Step("no device");
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "device made in %.2f ms", NowMs() - t0);
    Step(buf);

    // Command memory, shader code, uniforms and the storage buffer (CPU writes, GPU reads).
    DkMemBlock cmdMem = MakeBlock(dev, 64 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    DkMemBlock codeMem = MakeBlock(dev, 64 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    DkMemBlock dataMem = MakeBlock(dev, 64 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    // The picture: drawn into a GPU image (block linear, as the GPU likes it) and copied to
    // memory the processor reads through its cache; and, to compare, drawn straight into
    // that kind of memory (pitch linear).
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, dev);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = (uint32_t)w;
    lm.dimensions[1] = (uint32_t)h;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    DkMemBlock imageMem = MakeBlock(dev, (uint32_t)dkImageLayoutGetSize(&layout) + dkImageLayoutGetAlignment(&layout),
        DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    const uint32_t stride = Align((uint32_t)w * 4, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    DkMemBlock readMem = MakeBlock(dev, stride * h, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached);
    DkImageLayoutMaker lm2 = lm;
    lm2.flags = DkImageFlags_UsageRender | DkImageFlags_PitchLinear;
    lm2.pitchStride = stride;
    DkImageLayout layout2;
    dkImageLayoutInitialize(&layout2, &lm2);
    DkMemBlock linearMem = MakeBlock(dev, stride * h, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!cmdMem || !codeMem || !dataMem || !imageMem || !readMem || !linearMem)
    {
        Step("memory could not be had");
        dkDeviceDestroy(dev);
        return;
    }
    DkImage image, linear;
    dkImageInitialize(&image, &layout, imageMem, 0);
    dkImageInitialize(&linear, &layout2, linearMem, 0);
    snprintf(buf, sizeof(buf), "memory: image %llu bytes (alignment %u), linear stride %u", (unsigned long long)dkImageLayoutGetSize(&layout),
        dkImageLayoutGetAlignment(&layout), stride);
    Step(buf);

    uint32_t codeUsed = 0;
    DkShader vsh, fsh;
    if (!LoadShader(vsh, probe_vsh, sizeof(probe_vsh), codeMem, codeUsed) || !LoadShader(fsh, probe_fsh, sizeof(probe_fsh), codeMem, codeUsed))
    {
        Step("the shaders could not be loaded");
        dkMemBlockDestroy(linearMem);
        dkMemBlockDestroy(readMem);
        dkMemBlockDestroy(imageMem);
        dkMemBlockDestroy(dataMem);
        dkMemBlockDestroy(codeMem);
        dkMemBlockDestroy(cmdMem);
        dkDeviceDestroy(dev);
        return;
    }
    Step("shaders loaded");

    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, dev);
    qm.flags = DkQueueFlags_Graphics | DkQueueFlags_DisableZcull;
    DkQueue queue = dkQueueCreate(&qm);
    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, dev);
    DkCmdBuf cmd = dkCmdBufCreate(&cm);
    if (!queue || !cmd)
    {
        Step("no queue");
        return;
    }
    Step("queue made");

    uint32_t* data = (uint32_t*)dkMemBlockGetCpuAddr(dataMem);
    for (int i = 0; i < 16; i++) data[64 + i] = (uint32_t)(i * 16 + 7); // (at 256: the uniforms are at 0)
    const DkGpuAddr dataAddr = dkMemBlockGetGpuAddr(dataMem);

    // Each way 60 times: draw, copy if needed, wait, read every pixel through the cache.
    for (int way = 0; way < 2; way++)
    {
        double submit = 0, wait = 0, read = 0;
        int bad = 0;
        for (int i = 0; i < 60; i++)
        {
            float* color = (float*)data;
            color[0] = (float)(i & 7) / 7.0f;
            color[1] = 0.5f;
            color[2] = 1.0f;
            color[3] = 1.0f;
            const double ta = NowMs();
            dkCmdBufClear(cmd);
            dkCmdBufAddMemory(cmd, cmdMem, 0, 64 * 1024);
            DkImageView view;
            dkImageViewDefaults(&view, way == 0 ? &image : &linear);
            const DkImageView* targets[] = { &view };
            dkCmdBufBindRenderTargets(cmd, targets, 1, nullptr);
            const DkViewport vp = { 0, 0, (float)w, (float)h, 0, 1 };
            dkCmdBufSetViewports(cmd, 0, &vp, 1);
            const DkScissor sc = { 0, 0, (uint32_t)w, (uint32_t)h };
            dkCmdBufSetScissors(cmd, 0, &sc, 1);
            const float clear[4] = { 0, 0, 0, 0 };
            dkCmdBufClearColor(cmd, 0, DkColorMask_RGBA, clear);
            const DkShader* shaders[] = { &vsh, &fsh };
            dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, 2);
            const DkBufExtents ubo = { dataAddr, 256 };
            dkCmdBufBindUniformBuffers(cmd, DkStage_Fragment, 0, &ubo, 1);
            const DkBufExtents ssbo = { dataAddr + 256, 64 };
            dkCmdBufBindStorageBuffers(cmd, DkStage_Fragment, 0, &ssbo, 1);
            DkRasterizerState rs;
            dkRasterizerStateDefaults(&rs);
            rs.cullMode = DkFace_None;
            dkCmdBufBindRasterizerState(cmd, &rs);
            DkColorState cs;
            dkColorStateDefaults(&cs);
            dkCmdBufBindColorState(cmd, &cs);
            DkColorWriteState cws;
            dkColorWriteStateDefaults(&cws);
            dkCmdBufBindColorWriteState(cmd, &cws);
            DkDepthStencilState ds;
            dkDepthStencilStateDefaults(&ds);
            ds.depthTestEnable = false;
            ds.depthWriteEnable = false;
            dkCmdBufBindDepthStencilState(cmd, &ds);
            dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
            if (way == 0)
            {
                dkCmdBufBarrier(cmd, DkBarrier_Fragments, 0);
                DkImageRect rect = { 0, 0, 0, (uint32_t)w, (uint32_t)h, 1 };
                DkCopyBuf dst = { dkMemBlockGetGpuAddr(readMem), stride, 0 }; // (row length in bytes)
                dkCmdBufCopyImageToBuffer(cmd, &view, &rect, &dst, 0);
            }
            dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
            DkFence fence;
            dkCmdBufSignalFence(cmd, &fence, true);
            dkQueueSubmitCommands(queue, dkCmdBufFinishList(cmd));
            dkQueueFlush(queue);
            const double tb = NowMs();
            if (dkFenceWait(&fence, 2000000000ll) != DkResult_Success)
            {
                Step("the GPU did not finish within 2 seconds");
                bad = -1;
                break;
            }
            const double tc = NowMs();
            DkMemBlock src = way == 0 ? readMem : linearMem;
            dkMemBlockFlushCpuCache(src, 0, stride * h);
            const uint32_t* px = (const uint32_t*)dkMemBlockGetCpuAddr(src);
            // A pass over every pixel, as the widescreen picture's would be used.
            uint32_t sum = 0;
            for (int y = 0; y < h; y++)
            {
                const uint32_t* row = px + (size_t)y * (stride / 4);
                for (int x = 0; x < w; x++) sum += row[x];
            }
            const double td = NowMs();
            // A few pixels checked: red from the uniform, alpha from the storage buffer.
            const uint32_t expectR = (uint32_t)((float)(i & 7) / 7.0f * 255.0f + 0.5f);
            for (int x : { 0, 5, 15, 100, w - 1 })
            {
                const uint32_t p = px[(size_t)(h / 2) * (stride / 4) + x];
                const uint32_t r = p & 0xFF, a = p >> 24;
                if ((r > expectR ? r - expectR : expectR - r) > 1 || a != (uint32_t)((x & 15) * 16 + 7)) bad++;
                if (i == 59 && x == 5)
                {
                    snprintf(buf, sizeof(buf), "pixel (5, %d) = %08x (want red %u, alpha %u); sum %08x", h / 2, p, expectR, (5 & 15) * 16 + 7, sum);
                    Step(buf);
                }
            }
            submit += tb - ta;
            wait += tc - tb;
            read += td - tc;
        }
        if (bad < 0) break;
        snprintf(buf, sizeof(buf), "%s %dx%d: building and sending %.3f ms, waiting for the GPU %.3f ms, reading every pixel %.3f ms "
            "(per picture, over 60); %d wrong pixels", way == 0 ? "drawn as an image and copied" : "drawn straight into memory",
            w, h, submit / 60, wait / 60, read / 60, bad);
        Step(buf);
    }

    dkQueueWaitIdle(queue);
    dkCmdBufDestroy(cmd);
    dkQueueDestroy(queue);
    dkMemBlockDestroy(linearMem);
    dkMemBlockDestroy(readMem);
    dkMemBlockDestroy(imageMem);
    dkMemBlockDestroy(dataMem);
    dkMemBlockDestroy(codeMem);
    dkMemBlockDestroy(cmdMem);
    dkDeviceDestroy(dev);
    Step("done; everything released");
}
}
#endif
