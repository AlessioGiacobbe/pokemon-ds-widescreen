// ssrecomp: the 3D scene drawn on the graphics chip, on a thread of its own.
//
// melonDS's OpenGL renderer (GPU3D_OpenGL.cpp) draws the DS-resolution picture, which is read
// back for melonDS's software 2D renderer exactly as the software 3D renderer's would be, and
// a second one draws the widescreen view's polygons (GPU3D_Wide.cpp) at the output's size.
// Both run on one thread that owns the OpenGL context, so the emulation only copies what a
// frame is drawn from and goes on: the graphics driver's work never lands on its core.
//
// When a frame is drawn (RenderFrame, at scanline 215) the polygon list, the rendering
// registers and the texture memory are copied; the picture is needed from the first line of
// the next frame (GetLine), and everything drawn from the polygon lists has to be finished
// before they change at the next flush (VCount144 waits for that).
#pragma once
#ifdef OGLRENDERER_ENABLED
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <pthread.h>
#include <vector>
#include "GPU3D.h"

namespace melonDS
{
class GLRenderer;
struct GLFrameState;

class GLThreadRenderer : public Renderer3D
{
public:
    // Creates the OpenGL context on the calling thread and makes it current; returns false
    // (with a reason in info) if there is none. Set by the frontend.
    using ContextHook = bool (*)(std::string& info);
    static inline ContextHook CreateContext = nullptr;

    // Starts the thread and the renderers on it; nullptr (and the reason in info) if that
    // failed, in which case the software renderer stays.
    static std::unique_ptr<GLThreadRenderer> Start(std::string& info);
    ~GLThreadRenderer() override;

    void Reset(GPU& gpu) override {}
    void VCount144(GPU& gpu) override;
    void RenderFrame(GPU& gpu) override;
    u32* GetLine(int line) override;
    void Stop(const GPU& gpu) override;

    // The widescreen view's polygons, drawn after the DS-resolution picture of the same frame
    // into out (w x h, XRGB with the top byte 0 where no polygon was drawn, as GPU3D_Wide's
    // own renderer writes it). out has to stay valid until WaitIdle().
    void QueueWide(Polygon* const* list, u32 count, int w, int h, u32* out);
    // Until everything queued is drawn.
    void WaitIdle();

    // The instance in use, for GPU3D_Wide.cpp (nullptr when the software renderer is).
    static inline GLThreadRenderer* Current = nullptr;

private:
    GLThreadRenderer();
    void Loop();
    void DrawDS();
    void DrawWide();

    pthread_t Thread {};
    bool Started = false;
    std::mutex Mutex;
    std::condition_variable Wake, Done;
    bool Quit = false, InitDone = false, InitOk = false;
    std::string InitInfo;
    // Work queued for the thread, done in this order.
    bool PendingDS = false, PendingWide = false;
    bool DSReady = false, DSFetched = false;

    std::unique_ptr<GLRenderer> DS, Wide;

    // What the queued frame is drawn from (copied at RenderFrame).
    std::unique_ptr<GLFrameState> State;
    std::vector<Polygon*> List;
    std::vector<u8> TexCopy, PalCopy;   // 4 x 128 KB, 6 x 16 KB
    // The widescreen view's frame.
    std::vector<Polygon*> WideList;
    int WideW = 0, WideH = 0;
    u32* WideOut = nullptr;

    u32 DSLines[256 * 192] {};           // in the format GetLine returns
};
}
#endif
