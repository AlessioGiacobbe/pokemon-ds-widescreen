// ssrecomp: the widescreen view's polygons drawn by the graphics chip instead of
// GPU3D_Wide.cpp's own rasteriser, into the same picture (XRGB, the top byte the alpha, 0
// where nothing was drawn), so that everything that uses it stays as it is.
//
// What is drawn, and how, is worked out here the same way for every graphics API (the
// shaders in wide_gpu/ are the same for both too); a backend only carries it out:
// GPU3D_WideGpu_Deko.cpp on the Switch (deko3D, its own API: about 3 ms a 1280x720 picture,
// where Mesa's OpenGL took 47), GPU3D_WideGpu_GL.cpp on a PC, for testing it against the
// software rasteriser (SSR_WIDE_GPU=1 with --headless).
//
// The picture follows GPU3D_Wide.cpp's rules, with these differences:
// - Pixels are covered where the graphics chip covers them (no widening of the edges by a
//   third of a pixel), and colours are blended with rounding instead of truncation (at most
//   one step of 255 apart).
// - Polygons that pass the depth test on equal depth (decals) pass on any depth up to the
//   tolerance in front of what is there, not only within it (as melonDS's own OpenGL
//   renderer does); the depth they store is the lower end of their tolerance.
//
// How it is drawn: the rear plane cleared; the opaque polygons (not translucent, not tested
// on equal depth) in one draw in the game's order with a "less" depth test, which gives what
// the software's near-to-far order with its tie rule gives; then the others in the game's
// order. Their opaque pixels (alpha 31) are drawn as opaque ones; their translucent pixels
// first where the picture has no alpha yet (stencil 0: drawn as they are, as the DS does)
// and then elsewhere, blended, except over pixels of their own translucent polygon ID (the
// stencil holds 0x80 where the picture has alpha, plus the ID + 1 of the translucent
// polygon drawn there last). A final pass applies the fog and writes the picture.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "types.h"

namespace melonDS
{
class GPU;
struct Polygon;

struct WideGpuVertex
{
    float X, Y, W, Z;   // output pixels; W for perspective; depth as the DS stores it
    float R, G, B, S;   // colour (6 bits a channel); texture coordinates in 1/16 texel
    float T;
    u32 Poly;           // index into the polygon table
    u32 Pad0, Pad1;
};
static_assert(sizeof(WideGpuVertex) == 48, "matches wide_vsh.glsl");

struct WideGpuPolygon
{
    u32 Attr, TexParam, TexPalette, Flags; // Flags: bit 0 W-buffer, bit 1 textured
};

// The shaders' uniform buffer 0 (std140).
struct WideGpuFrameConstants
{
    u32 Size[4];          // width, height, DISP3DCNT, alpha test reference
    float Scale[4];       // 2 / width, 2 / height, direction of y (backend's), unused
    u32 Fog[4];           // fog colour, offset, shift, flags (bit 0 fog on, bit 1 fog colours)
    u32 Toon[32];
    u32 FogDensity[36];
};

enum class WideGpuStencil : u8 { Always, Equal, NotEqual };

struct WideGpuDraw
{
    u32 First, Count;     // vertices (triangles of three)
    u8 Pixels;            // 0 all, 1 opaque (alpha 31) only, 2 translucent only
    bool DepthLequal;     // "less or equal" (equal-depth polygons, with DepthBias), else "less"
    bool DepthWrite;
    WideGpuStencil StencilTest;
    u8 StencilRef;        // compared under StencilMask; written whole where drawn
    u8 StencilMask;
    bool Blend;
    bool WriteDepthAttr, WriteFogAttr; // the second target's two channels
    s32 DepthBias;        // subtracted from the depth tested and stored
};

struct WideGpuFrame
{
    int W = 0, H = 0;
    WideGpuFrameConstants Constants {};
    std::vector<WideGpuVertex> Vertices;
    std::vector<WideGpuPolygon> Polygons;
    std::vector<WideGpuDraw> Draws;
    float ClearColor[4] {}; // red, green, blue (0-1), alpha (0 or 1)
    u32 ClearDepth = 0;     // as the DS stores it
    u32 ClearFog = 0;
    u8 ClearStencil = 0;
    const u8* TexMem = nullptr; // 512 KB, read during Submit
    const u8* PalMem = nullptr; // 128 KB
};

class WideGpuBackend
{
public:
    virtual ~WideGpuBackend() = default;
    virtual const char* Name() const = 0;
    // Starts drawing the frame (the backend's direction of y is put in Constants.Scale[2]);
    // everything in it is copied before this returns.
    virtual bool Submit(WideGpuFrame& frame) = 0;
    // Waits for the picture of the last Submit and copies it to out (W x H).
    virtual void Fetch(u32* out) = 0;
};

// Works out what to draw for the polygons of a wide frame (as GPU3D_Wide.cpp's Draw sees
// them), at w x h.
void WideGpuBuild(const GPU& gpu, Polygon* const* list, u32 count, int w, int h, WideGpuFrame& out);

// The backend in use, if any (the frontend sets it up; see WideGpuStart).
extern std::unique_ptr<WideGpuBackend> WideGpuCurrent;
// Sets up the best backend this build has; false (and why, in info) if none could be.
bool WideGpuStart(std::string& info);
void WideGpuStop();

#ifdef SSR_DEKO3D
std::unique_ptr<WideGpuBackend> WideGpuMakeDeko(std::string& info);
#endif
#ifdef OGLRENDERER_ENABLED
std::unique_ptr<WideGpuBackend> WideGpuMakeGL(std::string& info);
#endif
}
