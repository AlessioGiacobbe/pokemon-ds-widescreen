#pragma once
// An OpenGL context for drawing the 3D scene on the graphics chip (melonDS/src/GPU3D_GLThread.h),
// made with EGL without a window: on Linux through Mesa or the graphics driver, on the Switch
// through devkitPro's Mesa. Desktop OpenGL 3.3 (core), which melonDS's OpenGL renderer needs.
#include <string>

// Creates the context, makes it current on the calling thread and loads the OpenGL functions.
// Returns false, with the reason in info, if there is no such context; on success info names
// the renderer.
bool SsrCreateGLContext(std::string& info);

// Makes the context current on the calling thread again (after SsrCreateGLContext), and gives
// the thread back to whatever drew there before: SDL's context when one was named with
// SsrSetSdlContext, otherwise the EGL context current before SsrEnterGLContext.
void SsrEnterGLContext();
void SsrLeaveGLContext();
// Lets go of the context on the calling thread, with nothing made current in its place (the end
// of a thread that drew with it).
void SsrReleaseGLContext();
// SDL's window and OpenGL context (its renderer's), which SsrLeaveGLContext makes current again.
void SsrSetSdlContext(void* window, void* context);
// An OpenGL function the bundled loader does not have (eglGetProcAddress).
void* SsrGLGetProc(const char* name);
