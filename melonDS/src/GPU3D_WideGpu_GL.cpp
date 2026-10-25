// ssrecomp: the widescreen view's polygons drawn by OpenGL (GPU3D_WideGpu.h), to test the
// graphics chip's version of the picture on a PC against GPU3D_Wide.cpp's own rasteriser.
// Its context is made by the frontend's hook (GLThreadRenderer::CreateContext) and stays
// current on the thread that made it, which has to be the one that draws: the headless tests
// run everything on one thread.
#ifdef OGLRENDERER_ENABLED
#include "GPU3D_WideGpu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "OpenGLSupport.h"
#include "GPU3D_GLThread.h"

#include "wide_gpu/wide_vsh_text.h"
#include "wide_gpu/wide_fsh_text.h"
#include "wide_gpu/final_vsh_text.h"
#include "wide_gpu/final_fsh_text.h"

namespace melonDS
{
void* (*WideGpuGLGetProc)(const char* name) = nullptr;

namespace
{
typedef void (*ClipControlFn)(GLenum origin, GLenum depth);
constexpr GLenum LowerLeft = 0x8CA1, ZeroToOne = 0x935F; // GL_LOWER_LEFT, GL_ZERO_TO_ONE

// The shaders are written for deko3D's compiler (GLSL 4.60); OpenGL 4.3 takes them as 4.30.
std::string Source(const unsigned char* text, size_t size)
{
    std::string s(reinterpret_cast<const char*>(text), size);
    const size_t at = s.find("#version 460");
    if (at != std::string::npos) s.replace(at, 12, "#version 430 core");
    return s;
}

// The same with a #define after the #version line (a variant: see wide_fsh.glsl).
std::string Variant(std::string s, const char* define)
{
    const size_t eol = s.find('\n');
    s.insert(eol + 1, std::string("#define ") + define + " 1\n");
    return s;
}

class WideGpuGL : public WideGpuBackend
{
public:
    const char* Name() const override { return "OpenGL"; }

    bool Init(std::string& info)
    {
        // (SSR_WIDEGPU_SHADER=texbuf: the Switch's variant, the texture memory read as buffer
        // textures, to check its pictures here.)
        const char* variant = getenv("SSR_WIDEGPU_SHADER");
        TexBuf = variant && !strcmp(variant, "texbuf");
        std::string fsh = Source(wide_fsh_text, sizeof(wide_fsh_text));
        if (TexBuf)
        {
            fsh = Variant(fsh, "WIDE_TEXBUF");
            fprintf(stderr, "[widegpu] OpenGL: the buffer-texture fragment shader\n");
        }
        if (!OpenGL::CompileVertexFragmentProgram(MainProgram, Source(wide_vsh_text, sizeof(wide_vsh_text)),
                fsh, "WideMain", {}, {}) ||
            !OpenGL::CompileVertexFragmentProgram(EarlyProgram, Source(wide_vsh_text, sizeof(wide_vsh_text)),
                Variant(fsh, "WIDE_EARLY"), "WideEarly", {}, {}) ||
            !OpenGL::CompileVertexFragmentProgram(FinalProgram, Source(final_vsh_text, sizeof(final_vsh_text)),
                Source(final_fsh_text, sizeof(final_fsh_text)), "WideFinal", {}, {}))
        {
            info = "the shaders did not compile (see the log)";
            return false;
        }
        glGenBuffers(1, &FrameUBO);
        glGenBuffers(1, &DrawUBO);
        glGenBuffers(4, SSBO);
        glGenBuffers(1, &ReadPBO);
        glGenVertexArrays(1, &EmptyVAO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO[2]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 512 * 1024, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO[3]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 128 * 1024, nullptr, GL_DYNAMIC_DRAW);
        glGenTextures(4, BufTex);
        glGenBuffers(1, &AtlasBuf);
        glBindBuffer(GL_TEXTURE_BUFFER, AtlasBuf);
        glBufferData(GL_TEXTURE_BUFFER, WideGpuAtlas::Capacity * 4, nullptr, GL_DYNAMIC_DRAW);
        const GLenum formats[4] = { GL_R8UI, GL_R16UI, GL_R16UI, GL_R32UI };
        for (int i = 0; i < 4; i++)
        {
            glBindTexture(GL_TEXTURE_BUFFER, BufTex[i]);
            glTexBuffer(GL_TEXTURE_BUFFER, formats[i], i == 3 ? AtlasBuf : SSBO[i < 2 ? 2 : 3]);
        }
        glBindTexture(GL_TEXTURE_BUFFER, 0);
        glBindBuffer(GL_UNIFORM_BUFFER, FrameUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(WideGpuFrameConstants), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, DrawUBO);
        glBufferData(GL_UNIFORM_BUFFER, 16, nullptr, GL_DYNAMIC_DRAW);
        if (WideGpuGLGetProc) ClipControl = (ClipControlFn)WideGpuGLGetProc("glClipControl");
        return glGetError() == GL_NO_ERROR;
    }

    ~WideGpuGL() override
    {
        glDeleteProgram(MainProgram);
        glDeleteProgram(EarlyProgram);
        glDeleteProgram(FinalProgram);
        glDeleteBuffers(1, &FrameUBO);
        glDeleteBuffers(1, &DrawUBO);
        glDeleteTextures(4, BufTex);
        glDeleteBuffers(1, &AtlasBuf);
        glDeleteBuffers(4, SSBO);
        glDeleteBuffers(1, &ReadPBO);
        glDeleteVertexArrays(1, &EmptyVAO);
        FreeTargets();
    }

    // (Drawn at once, so always finished; SSR_WIDE_GL_LATE=1 says it is not, to test what
    // GPU3D_Wide.cpp does with a picture the graphics chip is late with.)
    bool TakesAtlas() const override
    {
        static const bool off = getenv("SSR_WIDEGPU_ATLAS") && !strcmp(getenv("SSR_WIDEGPU_ATLAS"), "0");
        return TexBuf && !off;
    }

    bool Done(int /*slot*/) override
    {
        static const bool late = getenv("SSR_WIDE_GL_LATE") != nullptr;
        return !late;
    }

    u32* Target(int slot, int w, int h) override
    {
        Slots[slot].resize((size_t)w * h);
        return Slots[slot].data();
    }

    bool Submit(WideGpuFrame& f, int slot) override
    {
        if (f.W != W || f.H != H) MakeTargets(f.W, f.H);
        // Rows from the bottom of the view are the first in memory (see wide_vsh.glsl).
        f.Constants.Scale[2] = -1.0f;
        // Depth from 0 to 1 where glClipControl is there (OpenGL 4.5), otherwise -1 to 1 (which
        // loses W-buffering's 1/W: its values are tiny).
        f.Constants.Scale[3] = ClipControl ? 0.0f : 1.0f;
        if (ClipControl) ClipControl(LowerLeft, ZeroToOne);

        glBindBuffer(GL_UNIFORM_BUFFER, FrameUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(f.Constants), &f.Constants);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, FrameUBO);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, DrawUBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO[0]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<size_t>(16, f.Vertices.size() * sizeof(WideGpuVertex)), f.Vertices.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO[1]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<size_t>(16, f.Polygons.size() * sizeof(WideGpuPolygon)), f.Polygons.data(), GL_STREAM_DRAW);
        if (!f.TexVersion || f.TexVersion != TexVersion)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO[2]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 512 * 1024, f.TexMem);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO[3]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 128 * 1024, f.PalMem);
            TexVersion = f.TexVersion;
        }
        for (int i = 0; i < 4; i++) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, SSBO[i]);
        if (f.AtlasOn)
        {
            const WideGpuAtlas& a = f.Atlas;
            glBindBuffer(GL_TEXTURE_BUFFER, AtlasBuf);
            if (!AtlasSynced)
            {
                glBufferSubData(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)a.Used * 4, a.Texels.data());
                AtlasSynced = true;
            }
            else
                for (const auto& [first, n] : a.Uploads)
                    glBufferSubData(GL_TEXTURE_BUFFER, (GLintptr)first * 4, (GLsizeiptr)n * 4, a.Texels.data() + first);
        }
        if (TexBuf)
            for (int i = 0; i < 4; i++)
            {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_BUFFER, BufTex[i]);
            }
        glActiveTexture(GL_TEXTURE0);

        glBindFramebuffer(GL_FRAMEBUFFER, MainFBO);
        const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, bufs);
        glViewport(0, 0, W, H);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisablei(GL_BLEND, 0);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFF);
        glClearBufferfv(GL_COLOR, 0, f.ClearColor);
        const GLuint attr[4] = { f.ClearDepth, f.ClearFog, 0, 0 };
        glClearBufferuiv(GL_COLOR, 1, attr);
        glClearBufferfi(GL_DEPTH_STENCIL, 0, f.ClearDepthValue, f.ClearStencil);

        glUseProgram(MainProgram);
        glBindVertexArray(EmptyVAO);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
        bool earlyBound = false;
        for (const WideGpuDraw& d : f.Draws)
        {
            if (d.Early != earlyBound)
            {
                glUseProgram(d.Early ? EarlyProgram : MainProgram);
                earlyBound = d.Early;
            }
            const u32 params[4] = { d.First, d.Pixels, (u32)d.DepthBias, f.WDepth ? 1u : 0u };
            glBindBuffer(GL_UNIFORM_BUFFER, DrawUBO);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(params), params);
            glDepthFunc(f.WDepth ? (d.DepthLequal ? GL_GEQUAL : GL_GREATER) : (d.DepthLequal ? GL_LEQUAL : GL_LESS));
            glDepthMask(d.DepthWrite ? GL_TRUE : GL_FALSE);
            const GLenum func = d.StencilTest == WideGpuStencil::Always ? GL_ALWAYS :
                d.StencilTest == WideGpuStencil::Equal ? GL_EQUAL : GL_NOTEQUAL;
            glStencilFunc(func, d.StencilRef, d.StencilMask);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glStencilMask(0xFF);
            if (d.Blend) glEnablei(GL_BLEND, 0);
            else glDisablei(GL_BLEND, 0);
            glColorMaski(1, d.WriteDepthAttr, d.WriteFogAttr, GL_FALSE, GL_FALSE);
            glDrawArrays(GL_TRIANGLES, 0, d.Count);
        }

        // The final pass, and the picture read back.
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisablei(GL_BLEND, 0);
        if (ClipControl) ClipControl(LowerLeft, 0x935E); // GL_NEGATIVE_ONE_TO_ONE again
        glBindFramebuffer(GL_FRAMEBUFFER, FinalFBO);
        glDrawBuffers(1, bufs);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(FinalProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ColorTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, AttrTex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, FinalFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, ReadPBO);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glFlush();
        const GLenum err = glGetError();
        if (err != GL_NO_ERROR)
        {
            static int reported = 0;
            if (reported++ < 5) fprintf(stderr, "[widegpu] OpenGL error 0x%x\n", err);
        }
        return true;
    }

    void Fetch(int slot) override
    {
        u32* out = Slots[slot].data();
        glBindBuffer(GL_PIXEL_PACK_BUFFER, ReadPBO);
        const void* p = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (p) memcpy(out, p, (size_t)W * H * 4);
        else memset(out, 0, (size_t)W * H * 4);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

private:
    void FreeTargets()
    {
        if (MainFBO) glDeleteFramebuffers(1, &MainFBO);
        if (FinalFBO) glDeleteFramebuffers(1, &FinalFBO);
        const GLuint tex[3] = { ColorTex, AttrTex, FinalTex };
        glDeleteTextures(3, tex);
        if (DepthRB) glDeleteRenderbuffers(1, &DepthRB);
        MainFBO = FinalFBO = ColorTex = AttrTex = FinalTex = DepthRB = 0;
    }

    static GLuint Texture(GLenum format, int w, int h)
    {
        GLuint t;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return t;
    }

    void MakeTargets(int w, int h)
    {
        FreeTargets();
        W = w;
        H = h;
        ColorTex = Texture(GL_RGBA8, w, h);
        AttrTex = Texture(GL_RG32UI, w, h);
        FinalTex = Texture(GL_RGBA8, w, h);
        glGenRenderbuffers(1, &DepthRB);
        glBindRenderbuffer(GL_RENDERBUFFER, DepthRB);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, w, h);
        glGenFramebuffers(1, &MainFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, MainFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ColorTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, AttrTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, DepthRB);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "[widegpu] main framebuffer incomplete\n");
        glGenFramebuffers(1, &FinalFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, FinalFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, FinalTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "[widegpu] final framebuffer incomplete\n");
        glBindBuffer(GL_PIXEL_PACK_BUFFER, ReadPBO);
        glBufferData(GL_PIXEL_PACK_BUFFER, (size_t)w * h * 4, nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    GLuint MainProgram = 0, EarlyProgram = 0, FinalProgram = 0;
    GLuint FrameUBO = 0, DrawUBO = 0, SSBO[4] {}, ReadPBO = 0, EmptyVAO = 0;
    GLuint BufTex[4] {};   // the texture memory as bytes and 16-bit texels, the palette memory, the atlas (WIDE_TEXBUF)
    GLuint AtlasBuf = 0;
    bool TexBuf = false, AtlasSynced = false;
    GLuint MainFBO = 0, FinalFBO = 0, ColorTex = 0, AttrTex = 0, FinalTex = 0, DepthRB = 0;
    int W = 0, H = 0;
    ClipControlFn ClipControl = nullptr;
    std::vector<u32> Slots[3];
    u64 TexVersion = 0;
};
}

std::unique_ptr<WideGpuBackend> WideGpuMakeGL(std::string& info)
{
    if (!GLThreadRenderer::CreateContext)
    {
        info = "no OpenGL context in this build";
        return nullptr;
    }
    // One context for every renderer (it stays current on this thread).
    static bool haveContext = false;
    if (!haveContext && !GLThreadRenderer::CreateContext(info)) return nullptr;
    haveContext = true;
    std::unique_ptr<WideGpuGL> r(new WideGpuGL());
    if (!r->Init(info)) return nullptr;
    return r;
}
}
#endif
