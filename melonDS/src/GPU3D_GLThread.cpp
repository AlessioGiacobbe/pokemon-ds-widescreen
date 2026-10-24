// ssrecomp: the 3D scene drawn on the graphics chip. See GPU3D_GLThread.h.
#ifdef OGLRENDERER_ENABLED
#include "GPU3D_GLThread.h"
#include <chrono>
#include <cstdio>
#include <cstring>
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
    if (!CreateContext || !Enter || !Leave)
    {
        info = "no OpenGL context in this build";
        return nullptr;
    }
    std::unique_ptr<GLThreadRenderer> r(new GLThreadRenderer());
    r->ServiceThread = std::this_thread::get_id();
    r->Step("creating the context");
    if (!CreateContext(info))
    {
        Leave();
        return nullptr;
    }
    r->Step("setting up the renderer (shaders)");
    r->DS = GLRenderer::NewReadback(2048);
    if (r->DS) r->DS->SetRenderSettings(false, 1);
    else info += "; melonDS's OpenGL renderer could not be set up (shaders)";
    Leave();
    if (!r->DS) return nullptr;
    r->Step("ready");
    Current = r.get();
    return r;
}

GLThreadRenderer::~GLThreadRenderer()
{
    if (Current == this) Current = nullptr;
    // The renderers' OpenGL objects belong to the context (this is the service thread).
    if (DS || Wide)
    {
        Enter();
        Wide.reset();
        DS.reset();
        Leave();
    }
}

void GLThreadRenderer::Step(const char* what)
{
    if (Trace && (FramesDrawn < 3 || FramesDrawn % 600 == 0))
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "frame %llu: %s", (unsigned long long)FramesDrawn, what);
        Trace(buf);
    }
}

bool GLThreadRenderer::HasWork()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return PendingDS || PendingWide;
}

// Where a turn's time goes (milliseconds, summed), written to the log every 300 turns.
namespace
{
struct TurnTimes
{
    double Enter = 0, DSDraw = 0, DSRead = 0, WideDraw = 0, WideRead = 0, Leave = 0;
    unsigned Turns = 0, DSFrames = 0, WideFrames = 0;
};
TurnTimes Times;
double Now()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

void GLThreadRenderer::Service(bool dsOnly)
{
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (!PendingDS && (dsOnly || !PendingWide)) return;
    }
    Step("switching to the 3D context");
    double t = Now();
    Enter();
    Times.Enter += Now() - t;
    for (;;)
    {
        bool ds, wide;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            ds = PendingDS;
            wide = PendingWide;
        }
        if (ds)
        {
            DrawDS();
            std::lock_guard<std::mutex> lock(Mutex);
            PendingDS = false;
            DSReady = true;
        }
        else if (wide && !dsOnly)
        {
            DrawWide();
            std::lock_guard<std::mutex> lock(Mutex);
            PendingWide = false;
        }
        else break;
        Done.notify_all();
    }
    Step("back to SDL's context");
    t = Now();
    Leave();
    Times.Leave += Now() - t;
    FramesDrawn++;
    if (++Times.Turns == 300 && Trace)
    {
        char buf[256];
        const double n = Times.Turns;
        snprintf(buf, sizeof(buf), "per turn (300 turns, %u DS and %u wide pictures): enter %.2f, DS draw %.2f, "
            "DS read back %.2f, wide draw %.2f, wide read back %.2f, leave %.2f ms",
            Times.DSFrames, Times.WideFrames, Times.Enter / n, Times.DSDraw / n, Times.DSRead / n,
            Times.WideDraw / n, Times.WideRead / n, Times.Leave / n);
        Trace(buf);
        Times = TurnTimes();
    }
}

void GLThreadRenderer::DrawDS()
{
    Step("drawing the DS picture");
    double t = Now();
    DS->RenderFrameState(*State);
    DS->ReadbackStart();
    Times.DSDraw += Now() - t;
    Times.DSFrames++;
    Step("reading the DS picture back");
    t = Now();
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
    Times.DSRead += Now() - t;
}

void GLThreadRenderer::DrawWide()
{
    Step("drawing the wide picture");
    double t = Now();
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
    Times.WideDraw += Now() - t;
    Times.WideFrames++;
    const size_t n = (size_t)WideW * WideH;
    Step("reading the wide picture back");
    t = Now();
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
    Times.WideRead += Now() - t;
}

void GLThreadRenderer::WaitIdle()
{
    if (OnServiceThread())
    {
        Service();
        return;
    }
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
    if (OnServiceThread()) Service();
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
    if (OnServiceThread()) Service();
}

u32* GLThreadRenderer::GetLine(int line)
{
    if (!DSFetched)
    {
        if (OnServiceThread()) Service();
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
