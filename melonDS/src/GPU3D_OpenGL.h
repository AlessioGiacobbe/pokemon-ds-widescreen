/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#pragma once

#ifdef OGLRENDERER_ENABLED
#include <vector>
#include "GPU3D.h"
#include "GPU_OpenGL.h"
#include "OpenGLSupport.h"

namespace melonDS
{
class GPU;

// ssrecomp: everything a frame is drawn from, so that it can be drawn on another thread
// after the emulation has moved on (GPU3D_GLThread.cpp), and from a list of polygons other
// than the console's own (the widescreen view's, GPU3D_Wide.cpp).
struct GLFrameState
{
    Polygon* const* List = nullptr;
    u32 NumPolygons = 0;
    u32 DispCnt = 0, ClearAttr1 = 0, ClearAttr2 = 0;
    u32 FogColor = 0, FogOffset = 0, FogShift = 0;
    u16 ToonTable[32] {}, EdgeTable[8] {};
    u8 FogDensity[34] {};
    // Texture and palette memory as mapped (4 x 128 KB, 6 x 16 KB); nullptr where nothing
    // is mapped. Not uploaded at all with UploadTextures off (a renderer sharing another's).
    const u8* TexVRAM[4] {};
    const u8* PalVRAM[6] {};
    bool UploadTextures = true;
};

class GLRenderer : public Renderer3D
{
public:
    ~GLRenderer() override;
    void Reset(GPU& gpu) override;

    void SetRenderSettings(bool betterpolygons, int scale) noexcept;
    void SetBetterPolygons(bool betterpolygons) noexcept;
    void SetScaleFactor(int scale) noexcept;
    [[nodiscard]] bool GetBetterPolygons() const noexcept { return BetterPolygons; }
    [[nodiscard]] int GetScaleFactor() const noexcept { return ScaleFactor; }

    void VCount144(GPU& gpu) override {};
    void RenderFrame(GPU& gpu) override;
    void Stop(const GPU& gpu) override;
    u32* GetLine(int line) override;

    void SetupAccelFrame() override;
    void PrepareCaptureFrame() override;
    void Blit(const GPU& gpu) override;

    void BindOutputTexture(int buffer) override;

    static std::unique_ptr<GLRenderer> New() noexcept;

    // ssrecomp: a renderer whose picture is read back for melonDS's software 2D renderer
    // (Accelerated off), for up to maxPolygons polygons a frame (more than the DS's 2048 for
    // the widescreen view).
    static std::unique_ptr<GLRenderer> NewReadback(int maxPolygons) noexcept;
    // Draws at any size: vertices are scaled from the DS's 256x192 to it (the widescreen
    // view's polygons are squeezed into 256 pixels across).
    void SetOutputSize(int w, int h) noexcept;
    // Uses another renderer's texture memory instead of its own (the same GL context).
    void ShareTexturesWith(const GLRenderer& other) noexcept;
    void RenderFrameState(const GLFrameState& state);
    // Reads the picture back as 32-bit BGRA pixels, ScreenW x ScreenH, row 0 at the top:
    // colour in 6 bits (shifted left by 2), alpha in 5 (shifted left by 3).
    void ReadbackStart();
    // The pixels (waiting for the drawing and the copy), valid until ReadbackUnmap; nullptr if
    // they could not be had (ReadbackUnmap is still called).
    const u32* ReadbackMap();
    void ReadbackUnmap();
    int OutputWidth() const { return ScreenW; }
    int OutputHeight() const { return ScreenH; }
private:
    // Used by New()
    GLRenderer(GLCompositor&& compositor, bool accelerated = true, int maxPolygons = 2048) noexcept;
    static std::unique_ptr<GLRenderer> Create(bool accelerated, int maxPolygons) noexcept;

    // GL version requirements
    // * texelFetch: 3.0 (GLSL 1.30)     (3.2/1.50 for MS)
    // * UBO: 3.1

    struct RendererPolygon
    {
        Polygon* PolyData;

        u32 NumIndices;
        u32 IndicesOffset;
        GLuint PrimType;

        u32 NumEdgeIndices;
        u32 EdgeIndicesOffset;

        u32 RenderKey;
    };

    GLCompositor CurGLCompositor;
    // ssrecomp: sized for MaxPolygons (2048 for the DS's own picture).
    const int MaxPolygons;
    std::vector<RendererPolygon> PolygonList;

    bool BuildRenderShader(u32 flags, const std::string& vs, const std::string& fs);
    void UseRenderShader(u32 flags);
    void SetupPolygon(RendererPolygon* rp, Polygon* polygon) const;
    u32* SetupVertex(const Polygon* poly, int vid, const Vertex* vtx, u32 vtxattr, u32* vptr) const;
    void BuildPolygons(RendererPolygon* polygons, int npolys);
    int RenderSinglePolygon(int i) const;
    int RenderPolygonBatch(int i) const;
    int RenderPolygonEdgeBatch(int i) const;
    void RenderSceneChunk(int y, int h);
    void AllocateBuffers() noexcept;
public:
    // ssrecomp: where texture and palette memory are mapped (nullptr where nothing is).
    static void GetTextureMemory(const GPU& gpu, const u8* tex[4], const u8* pal[6]);
private:

    enum
    {
        RenderFlag_WBuffer     = 0x01,
        RenderFlag_Trans       = 0x02,
        RenderFlag_ShadowMask  = 0x04,
        RenderFlag_Edge        = 0x08,
    };


    GLuint ClearShaderPlain {};

    GLuint RenderShader[16] {};
    GLuint CurShaderID = -1;

    GLuint FinalPassEdgeShader {};
    GLuint FinalPassFogShader {};

    // std140 compliant structure
    struct
    {
        float uScreenSize[2];       // vec2       0 / 2
        u32 uDispCnt;               // int        2 / 1
        u32 __pad0;
        float uToonColors[32][4];   // vec4[32]   4 / 128
        float uEdgeColors[8][4];    // vec4[8]    132 / 32
        float uFogColor[4];         // vec4       164 / 4
        float uFogDensity[34][4];   // float[34]  168 / 136
        u32 uFogOffset;             // int        304 / 1
        u32 uFogShift;              // int        305 / 1
        u32 _pad1[2];               // int        306 / 2
    } ShaderConfig {};

    GLuint ShaderConfigUBO {};
    int NumFinalPolys {}, NumOpaqueFinalPolys {};

    GLuint ClearVertexBufferID = 0, ClearVertexArrayID {};
    GLint ClearUniformLoc[4] {};

    // vertex buffer
    // * XYZW: 4x16bit
    // * RGBA: 4x8bit
    // * ST: 2x16bit
    // * polygon data: 3x32bit (polygon/texture attributes)
    //
    // polygon attributes:
    // * bit4-7, 11, 14-15, 24-29: POLYGON_ATTR
    // * bit16-20: Z shift
    // * bit8: front-facing (?)
    // * bit9: W-buffering (?)

    GLuint VertexBufferID {};
    std::vector<u32> VertexBuffer; // MaxPolygons * 10 vertices of 7 words
    u32 NumVertices {};

    GLuint VertexArrayID {};
    GLuint IndexBufferID {};
    std::vector<u16> IndexBuffer;  // MaxPolygons * 30 for triangles, then * 20 for edges
    u32 NumIndices {}, NumEdgeIndices {};

    const u32 EdgeIndicesOffset;

    GLuint TexMemID {};
    GLuint TexPalMemID {};

    int ScaleFactor {};
    bool BetterPolygons {};
    int ScreenW {}, ScreenH {};

    GLuint ColorBufferTex {}, DepthBufferTex {}, AttrBufferTex {};
    GLuint DownScaleBufferTex {};
    GLuint PixelbufferID {};

    GLuint MainFramebuffer {}, DownscaleFramebuffer {};
    u32 Framebuffer[256*192] {};
    // ssrecomp: see GLFrameState. St is the frame being drawn.
    GLFrameState St;
    bool CustomSize = false, OwnsTextures = true;
    GLuint ReadbackBufferID {};
};
}
#endif