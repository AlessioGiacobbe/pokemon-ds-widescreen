// See glcontext.h.
#include "glcontext.h"
#include "frontend/glad/glad.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <cstring>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static std::string EglError(const char* what)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s failed (EGL error 0x%x)", what, (unsigned)eglGetError());
    return buf;
}

bool SsrCreateGLContext(std::string& info)
{
    // A display without a window: Mesa's surfaceless platform where there is one (a PC
    // without a desktop, as in the headless tests), otherwise the default display.
    EGLDisplay display = EGL_NO_DISPLAY;
#ifndef __SWITCH__
    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    const char* clientExt = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (getPlatformDisplay && clientExt && strstr(clientExt, "EGL_MESA_platform_surfaceless"))
        display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
#endif
    if (display == EGL_NO_DISPLAY) display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY)
    {
        info = "no EGL display";
        return false;
    }
    // (On the Switch SDL has already initialised this display for its own drawing; doing it
    // again only counts it.)
    if (!eglInitialize(display, nullptr, nullptr))
    {
        info = EglError("eglInitialize");
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_API))
    {
        info = EglError("eglBindAPI(OpenGL)");
        return false;
    }
    const EGLint configAttrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE };
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(display, configAttrs, &config, 1, &count) || count < 1)
        config = nullptr;
    const EGLint contextAttrs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
    if (context == EGL_NO_CONTEXT)
    {
        info = EglError("eglCreateContext(OpenGL 3.3 core)");
        return false;
    }
    // Drawing goes into the renderer's own framebuffers, so no surface is needed where EGL
    // allows that; otherwise a small off-screen one.
    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
    {
        EGLSurface surface = EGL_NO_SURFACE;
        if (config)
        {
            const EGLint pbufferAttrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
            surface = eglCreatePbufferSurface(display, config, pbufferAttrs);
        }
        if (surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context))
        {
            info = EglError("eglMakeCurrent");
            eglDestroyContext(display, context);
            return false;
        }
    }
    if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress))
    {
        info = "the OpenGL functions could not be loaded";
        return false;
    }
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    info = std::string(renderer ? renderer : "?") + ", OpenGL " + (version ? version : "?");
    return true;
}
