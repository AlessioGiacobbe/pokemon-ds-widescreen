// ssrecomp: the 3D scene drawn on the graphics chip, on a thread of its own. See GPU3D_GLThread.h.
#ifdef OGLRENDERER_ENABLED
#include "GPU3D_GLThread.h"
#include <cstring>
#include <pthread.h>
#include "GPU.h"
#include "GPU3D_OpenGL.h"
#include "SsrThreads.h"

namespace melonDS
{

GLThreadRenderer::GLThreadRenderer()
    : Renderer3D(false), State(std::make_unique<GLFrameState>())
{
    TexCopy.resize(4 * 128 * 1024);
    PalCopy.resize(6 * 16 * 1024);
}

std::unique_ptr<GLThreadRenderer> GLThreadRenderer::Start(std::string& info)
{
    std::unique_ptr<GLThreadRenderer> r(new GLThreadRenderer());
    // A large stack: the graphics driver compiles the shaders on this thread, and threads
    // get small stacks by default on the Switch.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4 << 20);
    const int err = pthread_create(&r->Thread, &attr, [](void* p) -> void* {
        static_cast<GLThreadRenderer*>(p)->Loop();
        return nullptr;
    }, r.get());
    pthread_attr_destroy(&attr);
    if (err)
    {
        info = "the drawing thread could not be started";
        return nullptr;
    }
    r->Started = true;
    {
        std::unique_lock<std::mutex> lock(r->Mutex);
        r->Done.wait(lock, [&] { return r->InitDone; });
    }
    info = r->InitInfo;
    if (!r->InitOk)
    {
        pthread_join(r->Thread, nullptr);
        r->Started = false;
        return nullptr;
    }
    Current = r.get();
    return r;
}

GLThreadRenderer::~GLThreadRenderer()
{
    if (Current == this) Current = nullptr;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Quit = true;
    }
    Wake.notify_all();
    if (Started) pthread_join(Thread, nullptr);
}

void GLThreadRenderer::Loop()
{
    // On the Switch: core 1, where the software renderer's thread would be (SsrThreads.h).
    SsrHelperThreadStart(0);
    std::string info;
    bool ok = CreateContext && CreateContext(info);
    if (!CreateContext) info = "no OpenGL context in this build";
    if (ok)
    {
        DS = GLRenderer::NewReadback(2048);
        if (DS) DS->SetRenderSettings(false, 1);
        else
        {
            ok = false;
            info += "; melonDS's OpenGL renderer could not be set up (shaders)";
        }
    }
    {
        std::lock_guard<std::mutex> lock(Mutex);
        InitDone = true;
        InitOk = ok;
        InitInfo = info;
    }
    Done.notify_all();
    if (!ok) return;

    std::unique_lock<std::mutex> lock(Mutex);
    for (;;)
    {
        Wake.wait(lock, [this] { return Quit || PendingDS || PendingWide; });
        if (Quit) break;
        if (PendingDS)
        {
            lock.unlock();
            DrawDS();
            lock.lock();
            PendingDS = false;
            DSReady = true;
        }
        else
        {
            lock.unlock();
            DrawWide();
            lock.lock();
            PendingWide = false;
        }
        Done.notify_all();
    }
    lock.unlock();
    // The renderers' OpenGL objects belong to this thread's context.
    Wide.reset();
    DS.reset();
}

void GLThreadRenderer::DrawDS()
{
    DS->RenderFrameState(*State);
    DS->ReadbackStart();
    const u32* pixels = DS->ReadbackMap();
    if (!pixels)
        std::memset(DSLines, 0, sizeof(DSLines));
    else
    {
        // As melonDS's GLRenderer::GetLine has it: colour in 6 bits, alpha in 5 at bit 24.
        const u64* src = reinterpret_cast<const u64*>(pixels);
        u64* dst = reinterpret_cast<u64*>(DSLines);
        for (int i = 0; i < 256 * 192 / 2; i++)
        {
            const u64 v = src[i];
            dst[i] = ((v & 0x00FCFCFC00FCFCFCull) >> 2) | ((v & 0xF8000000F8000000ull) >> 3);
        }
    }
    DS->ReadbackUnmap();
}

void GLThreadRenderer::DrawWide()
{
    if (!Wide)
    {
        Wide = GLRenderer::NewReadback(4096);
        if (!Wide) return;
        Wide->ShareTexturesWith(*DS);
    }
    Wide->SetOutputSize(WideW, WideH);
    GLFrameState st = *State;
    st.List = WideList.data();
    st.NumPolygons = (u32)WideList.size();
    st.UploadTextures = false; // the DS-resolution picture of the same frame did
    Wide->RenderFrameState(st);
    Wide->ReadbackStart();
    const size_t n = (size_t)WideW * WideH;
    const u32* pixels = Wide->ReadbackMap();
    if (!pixels)
    {
        Wide->ReadbackUnmap();
        std::memset(WideOut, 0, n * 4);
        return;
    }
    // XRGB with 8-bit colour; the top byte is the alpha (0 where nothing was drawn, the rear
    // plane where it has some), as the software widescreen renderer writes it. (melonDS's
    // shaders keep the DS's red in the low byte, as its 2D renderer wants it.)
    for (size_t i = 0; i < n; i++)
    {
        const u32 v = pixels[i];
        const u32 a = v >> 27;
        if (!a)
        {
            WideOut[i] = 0;
            continue;
        }
        u32 r = v & 0xFC, g = (v >> 8) & 0xFC, b = (v >> 16) & 0xFC;
        r |= r >> 6;
        g |= g >> 6;
        b |= b >> 6;
        WideOut[i] = (((a << 3) | (a >> 2)) << 24) | (r << 16) | (g << 8) | b;
    }
    Wide->ReadbackUnmap();
}

void GLThreadRenderer::WaitIdle()
{
    std::unique_lock<std::mutex> lock(Mutex);
    if (!PendingDS && !PendingWide) return;
    SsrWaitTimer t(SsrStats().WaitWideNs);
    Done.wait(lock, [this] { return !PendingDS && !PendingWide; });
}

void GLThreadRenderer::VCount144(GPU& gpu)
{
    // The polygon lists drawn from change at the next flush.
    WaitIdle();
}

void GLThreadRenderer::Stop(const GPU& gpu)
{
    WaitIdle();
}

void GLThreadRenderer::RenderFrame(GPU& gpu)
{
    WaitIdle();
    const GPU3D& g = gpu.GPU3D;
    GLFrameState& st = *State;
    List.assign(g.RenderPolygonRAM.begin(), g.RenderPolygonRAM.begin() + g.RenderNumPolygons);
    st.List = List.data();
    st.NumPolygons = (u32)List.size();
    st.DispCnt = g.RenderDispCnt;
    st.ClearAttr1 = g.RenderClearAttr1;
    st.ClearAttr2 = g.RenderClearAttr2;
    st.FogColor = g.RenderFogColor;
    st.FogOffset = g.RenderFogOffset;
    st.FogShift = g.RenderFogShift;
    std::memcpy(st.ToonTable, g.RenderToonTable, sizeof(st.ToonTable));
    std::memcpy(st.EdgeTable, g.RenderEdgeTable, sizeof(st.EdgeTable));
    std::memcpy(st.FogDensity, g.RenderFogDensityTable, sizeof(st.FogDensity));
    st.UploadTextures = true;
    // Texture memory is copied: the emulation may change it while the thread draws.
    const u8* tex[4];
    const u8* pal[6];
    GLRenderer::GetTextureMemory(gpu, tex, pal);
    for (int i = 0; i < 4; i++)
    {
        st.TexVRAM[i] = nullptr;
        if (!tex[i]) continue;
        u8* copy = &TexCopy[(size_t)i * 128 * 1024];
        std::memcpy(copy, tex[i], 128 * 1024);
        st.TexVRAM[i] = copy;
    }
    for (int i = 0; i < 6; i++)
    {
        st.PalVRAM[i] = nullptr;
        if (!pal[i]) continue;
        u8* copy = &PalCopy[(size_t)i * 16 * 1024];
        std::memcpy(copy, pal[i], 16 * 1024);
        st.PalVRAM[i] = copy;
    }
    {
        std::lock_guard<std::mutex> lock(Mutex);
        PendingDS = true;
        DSReady = false;
        DSFetched = false;
    }
    Wake.notify_all();
}

void GLThreadRenderer::QueueWide(Polygon* const* list, u32 count, int w, int h, u32* out)
{
    {
        std::lock_guard<std::mutex> lock(Mutex);
        WideList.assign(list, list + count);
        WideW = w;
        WideH = h;
        WideOut = out;
        PendingWide = true;
    }
    Wake.notify_all();
}

u32* GLThreadRenderer::GetLine(int line)
{
    if (!DSFetched)
    {
        std::unique_lock<std::mutex> lock(Mutex);
        if (PendingDS && !DSReady)
        {
            SsrWaitTimer t(SsrStats().Wait3DNs);
            Done.wait(lock, [this] { return DSReady || !PendingDS; });
        }
        DSFetched = true;
    }
    return &DSLines[256 * line];
}

}
#endif
