// The Switch has no dynamic libraries. melonDS's glad loader (melonDS/src/frontend/glad/glad.c)
// includes this for gladLoadGL(), which ssrecomp does not use: it loads the OpenGL functions
// through EGL (gladLoadGLLoader, frontend/glcontext.cpp).
#pragma once
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100
static inline void* dlopen(const char*, int) { return 0; }
static inline void* dlsym(void*, const char*) { return 0; }
static inline int dlclose(void*) { return 0; }
