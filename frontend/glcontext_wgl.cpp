// See glcontext.h. Windows: the context is made with WGL on a hidden window of its own (SDL
// draws with Direct3D there, so the two never meet), desktop OpenGL 4.3 core, else 3.3 core.
#ifdef _WIN32
#include "glcontext.h"
#include "frontend/glad/glad.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#ifdef __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

namespace
{
HWND OurWindow = nullptr;
HDC OurDC = nullptr;
HGLRC OurContext = nullptr;
HMODULE OpenGL32 = nullptr;
// What was current before SsrEnterGLContext (another WGL context, e.g. SDL's when it draws with
// OpenGL).
HDC PrevDC = nullptr;
HGLRC PrevContext = nullptr;
SDL_Window* SdlWindow = nullptr;
SDL_GLContext SdlContext = nullptr;

// wglCreateContextAttribsARB's constants (WGL_ARB_create_context, _profile).
constexpr int ContextMajor = 0x2091, ContextMinor = 0x2092, ContextProfile = 0x9126, CoreProfile = 0x00000001;
typedef HGLRC(WINAPI* CreateContextAttribsFn)(HDC, HGLRC, const int*);

std::string LastError(const char* what)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s failed (Windows error %lu)", what, (unsigned long)GetLastError());
    return buf;
}

void Destroy()
{
    if (OurContext)
    {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(OurContext);
    }
    if (OurDC) ReleaseDC(OurWindow, OurDC);
    if (OurWindow) DestroyWindow(OurWindow);
    OurContext = nullptr;
    OurDC = nullptr;
    OurWindow = nullptr;
}
}

void* SsrGLGetProc(const char* name)
{
    // (wglGetProcAddress gives the functions after OpenGL 1.1 only; those come from opengl32.dll.)
    void* p = (void*)wglGetProcAddress(name);
    if (p == nullptr || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1)
    {
        if (!OpenGL32) OpenGL32 = LoadLibraryA("opengl32.dll");
        p = OpenGL32 ? (void*)GetProcAddress(OpenGL32, name) : nullptr;
    }
    return p;
}

void SsrSetSdlContext(void* window, void* context)
{
    SdlWindow = static_cast<SDL_Window*>(window);
    SdlContext = static_cast<SDL_GLContext>(context);
}

void SsrEnterGLContext()
{
    PrevDC = wglGetCurrentDC();
    PrevContext = wglGetCurrentContext();
    if (PrevContext != OurContext) wglMakeCurrent(OurDC, OurContext);
}

void SsrLeaveGLContext()
{
    glFlush();
    if (SdlWindow && SdlContext)
    {
        // (SDL thinks its context never stopped being current; see glcontext.cpp.)
        wglMakeCurrent(nullptr, nullptr);
        SDL_GL_MakeCurrent(SdlWindow, nullptr);
        SDL_GL_MakeCurrent(SdlWindow, SdlContext);
    }
    else if (PrevContext && PrevContext != OurContext)
        wglMakeCurrent(PrevDC, PrevContext);
    else
        wglMakeCurrent(nullptr, nullptr);
}

void SsrReleaseGLContext()
{
    glFlush();
    wglMakeCurrent(nullptr, nullptr);
}

bool SsrCreateGLContext(std::string& info)
{
    Destroy();
    PrevDC = wglGetCurrentDC();
    PrevContext = wglGetCurrentContext();
    // A window that is never shown, only for its device context.
    static const wchar_t* className = L"SsrGLContextWindow";
    static bool registered = false;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance;
        wc.lpszClassName = className;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            info = LastError("RegisterClass");
            return false;
        }
        registered = true;
    }
    OurWindow = CreateWindowExW(0, className, L"", WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 16, 16, nullptr, nullptr, instance, nullptr);
    if (!OurWindow)
    {
        info = LastError("CreateWindow");
        return false;
    }
    OurDC = GetDC(OurWindow);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(OurDC, &pfd);
    if (!format || !SetPixelFormat(OurDC, format, &pfd))
    {
        info = LastError("SetPixelFormat");
        Destroy();
        return false;
    }
    // A plain context first: only through one can the function that makes a core one be found.
    HGLRC legacy = wglCreateContext(OurDC);
    if (!legacy || !wglMakeCurrent(OurDC, legacy))
    {
        info = LastError("wglCreateContext");
        if (legacy) wglDeleteContext(legacy);
        Destroy();
        return false;
    }
    auto createAttribs = (CreateContextAttribsFn)wglGetProcAddress("wglCreateContextAttribsARB");
    if (createAttribs)
    {
        const int versions[][2] = { { 4, 3 }, { 3, 3 } };
        for (const auto& v : versions)
        {
            const int attrs[] = { ContextMajor, v[0], ContextMinor, v[1], ContextProfile, CoreProfile, 0 };
            OurContext = createAttribs(OurDC, nullptr, attrs);
            if (OurContext) break;
        }
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    if (!OurContext)
    {
        info = createAttribs ? "the graphics driver has no OpenGL 3.3 core context" : "the graphics driver has no wglCreateContextAttribsARB";
        Destroy();
        return false;
    }
    if (!wglMakeCurrent(OurDC, OurContext))
    {
        info = LastError("wglMakeCurrent");
        Destroy();
        return false;
    }
    if (!gladLoadGLLoader((GLADloadproc)SsrGLGetProc))
    {
        info = "the OpenGL functions could not be loaded";
        Destroy();
        return false;
    }
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    info = std::string(renderer ? renderer : "?") + ", OpenGL " + (version ? version : "?");
    return true;
}
#endif
