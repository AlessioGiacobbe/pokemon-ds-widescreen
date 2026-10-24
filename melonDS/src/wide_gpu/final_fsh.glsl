#version 460
// ssrecomp, GPU3D_WideGpu.h: the picture in the form GPU3D_Wide.cpp's own rasteriser leaves
// it (XRGB, the top byte the alpha: 0 where nothing was drawn), with the fog.

layout (std140, binding = 0) uniform Frame
{
    uvec4 uSize;
    vec4 uScale;
    uvec4 uFog;           // fog colour, offset, shift, flags (bit 0 fog on, bit 1 fog colours)
    uvec4 uToon[8];
    uvec4 uFogDensity[9];
};

layout (binding = 0) uniform sampler2D uColor;
layout (binding = 1) uniform usampler2D uAttr;

layout (location = 0) out vec4 oColor;

uint Density(uint i)
{
    uvec4 e = uFogDensity[i >> 2];
    uint j = i & 3u;
    return j == 0u ? e.x : j == 1u ? e.y : j == 2u ? e.z : e.w;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = texelFetch(uColor, p, 0);
    uint stored = uint(c.a * 255.0 + 0.5);
    if (stored == 0u)
    {
        oColor = vec4(0.0);
        return;
    }
    uvec3 rgb = uvec3(c.rgb * 255.0 + 0.5);
    // (alpha + 1) / 32 back to alpha, and to 8 bits.
    uint a = uint(float(stored) * 32.0 / 255.0 + 0.5) - 1u;
    uint a8 = (a << 3) | (a >> 2);

    uvec2 attr = texelFetch(uAttr, p, 0).xy;
    if ((uFog.w & 3u) == 3u && attr.y != 0u)
    {
        uint z = attr.x, id, frac;
        if (z < uFog.y)
        {
            id = 0u;
            frac = 0u;
        }
        else
        {
            z -= uFog.y;
            z = (z >> 2) << uFog.z;
            id = z >> 17;
            if (id >= 32u)
            {
                id = 32u;
                frac = 0u;
            }
            else
                frac = z & 0x1FFFFu;
        }
        uint density = (Density(id) * (0x20000u - frac) + Density(id + 1u) * frac) >> 17;
        if (density >= 127u) density = 128u;
        if (density != 0u)
        {
            uint fc = uFog.x;
            uvec3 fog = uvec3((fc << 3) & 0xF8u, (fc >> 2) & 0xF8u, (fc >> 7) & 0xF8u);
            rgb = (fog * density + rgb * (128u - density)) >> 7;
        }
    }
    oColor = vec4(vec3(rgb) / 255.0, float(a8) / 255.0);
}
