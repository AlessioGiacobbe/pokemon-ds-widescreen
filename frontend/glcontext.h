#pragma once
// An OpenGL context for drawing the 3D scene on the graphics chip (melonDS/src/GPU3D_GLThread.h),
// made with EGL without a window: on Linux through Mesa or the graphics driver, on the Switch
// through devkitPro's Mesa. Desktop OpenGL 3.3 (core), which melonDS's OpenGL renderer needs.
#include <string>

// Creates the context, makes it current on the calling thread and loads the OpenGL functions.
// Returns false, with the reason in info, if there is no such context; on success info names
// the renderer.
bool SsrCreateGLContext(std::string& info);
