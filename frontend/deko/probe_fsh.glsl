#version 460
// ssrecomp, frontend/dekoprobe.cpp: a colour from a uniform buffer and, in alpha, a value
// from a storage buffer chosen by the column (the 3D renderer will read the DS's texture
// memory that way), so that the read-back picture shows both reached the shader.
layout (location = 0) out vec4 outColor;
layout (std140, binding = 0) uniform Params
{
    vec4 uColor;
};
layout (std430, binding = 0) readonly buffer Data
{
    uint data[];
};
void main()
{
    uint x = uint(gl_FragCoord.x) & 15u;
    outColor = vec4(uColor.rgb, float(data[x]) / 255.0);
}
