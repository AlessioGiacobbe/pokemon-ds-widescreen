// ssrecomp: the 3D scene drawn on the graphics chip.
//
// melonDS's OpenGL renderer (GPU3D_OpenGL.cpp) draws the DS-resolution picture, which is read
// back for melonDS's software 2D renderer exactly as the software 3D renderer's would be, and
// a second one draws the widescreen view's polygons (GPU3D_Wide.cpp) at the output's size.
//
// All the OpenGL work is done on one thread, the one that shows the frames (the "service"
// thread: the frontend's main thread, which also draws with SDL), switching to this
// renderer's own context and back each time. On the Switch, drawing from a second thread at
// the same time as SDL hung the whole console (Mesa's nouveau driver is not safe for that).
// The emulation, on its own thread, only copies what a frame is drawn from and hands it over:
// when a frame is drawn (RenderFrame, at scanline 215) the polygon list, the rendering
// registers and the texture memory are copied; the service thread draws it when it next calls
// Service() (between emulated frames, or while it waits for one); the picture is needed from
// the first line of the next frame (GetLine), and everything drawn from the polygon lists has
// to be finished before they change at the next flush (VCount144 waits for that). When the
// emulation runs on the service thread itself (headless tests, no pipeline) the work is done
// at once.
#pragma once
#ifdef OGLRENDERER_ENABLED
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "GPU3D.h"

namespace melonDS
{
class GLRenderer;
struct GLFrameState;

class GLThreadRenderer : public Renderer3D
{
public:
    // Set by the frontend. CreateContext makes the OpenGL context and makes it current on the
    // calling thread (false, with a reason in info, if there is none); Enter makes it current
    // again and Leave gives the thread back to whoever drew there before (SDL).
    using ContextHook = bool (*)(std::string& info);
    static inline ContextHook CreateContext = nullptr;
    static inline void (*Enter)() = nullptr;
    static inline void (*Leave)() = nullptr;
    // Each step of the first frames (and then now and then) to the log, made to reach the file
    // at once, so that a hang shows where it happened.
    static inline void (*Trace)(const char* step) = nullptr;

    // On the service thread: sets up the renderers; nullptr (and the reason in info) if that
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
    // On the service thread: draws what is queued, if anything (dsOnly: only the DS-resolution
    // picture, which the emulation needs from the first line of its next frame; the widescreen
    // one can wait until that frame is under way).
    void Service(bool dsOnly = false);
    bool HasWork();

    // The instance in use, for GPU3D_Wide.cpp and the frontend (nullptr when the software
    // renderer is).
    static inline GLThreadRenderer* Current = nullptr;

private:
    GLThreadRenderer();
    bool OnServiceThread() const { return std::this_thread::get_id() == ServiceThread; }
    void DrawDS();
    void DrawWide();
    void Step(const char* what);

    std::thread::id ServiceThread;
    std::mutex Mutex;
    std::condition_variable Done;
    // Work queued, done in this order.
    bool PendingDS = false, PendingWide = false;
    bool DSReady = false, DSFetched = false;
    u64 FramesDrawn = 0;

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
