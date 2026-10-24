#version 460
// ssrecomp: the widescreen view's polygons on the graphics chip (GPU3D_WideGpu.h).
// Vertices are read from a buffer (no vertex attributes to set up, the same in OpenGL and
// deko3D): each holds its place in output pixels, the polygon's W (for perspective-correct
// colours and texture coordinates), its depth, colour and texture coordinates.

struct WideVertex
{
    float x, y, w, z;
    float r, g, b, s;
    float t;
    uint poly;
    uint pad0, pad1;
};

layout (std430, binding = 0) readonly buffer Vertices
{
    WideVertex vtx[];
};

layout (std140, binding = 0) uniform Frame
{
    uvec4 uSize;          // width, height, DISP3DCNT, alpha test reference
    vec4 uScale;          // 2 / width, 2 / height, direction of y (+1 deko3D, -1 OpenGL), unused
    uvec4 uFog;           // fog colour, offset, shift, flags (bit 0 fog on, bit 1 fog colours)
    uvec4 uToon[8];       // the toon table, four to an entry
    uvec4 uFogDensity[9]; // the fog density table (34 entries), four to an entry
};

layout (std140, binding = 1) uniform Draw
{
    uvec4 uDraw;          // first vertex, which pixels (0 all, 1 opaque only, 2 translucent only), depth bias, unused
};

flat out uint vPoly;
out vec3 vColor;
out vec2 vTex;
noperspective out float vZLinear;
out float vZPersp;

void main()
{
    WideVertex v = vtx[uDraw.x + uint(gl_VertexID)];
    // Rows run from the top of the picture in memory with both APIs: OpenGL's start at the
    // bottom of the view, deko3D's at the top.
    vec2 ndc = vec2(v.x * uScale.x - 1.0, uScale.z * (1.0 - v.y * uScale.y));
    gl_Position = vec4(ndc * v.w, 0.0, v.w);
    vPoly = v.poly;
    vColor = vec3(v.r, v.g, v.b);
    vTex = vec2(v.s, v.t);
    // Z-buffered depth runs straight across the screen; W-buffered depth is W itself,
    // which perspective-correct interpolation gives (the vertices carry it scaled alike).
    vZLinear = v.z;
    vZPersp = v.z;
}
