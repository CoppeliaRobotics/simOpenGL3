// ===========================================================================
// Common declarations shared by the forward shading pass.
//
// This file is pulled in with #include and must therefore NOT contain a
// #version directive: the including shader has to keep that on its first line.
//
// The uniform block indices are assigned from C++ with glUniformBlockBinding()
// because "layout(binding = ...)" on a uniform block needs GLSL 4.20, while
// this renderer targets 3.30.
// ===========================================================================

layout(std140) uniform CameraBlock
{
    mat4 uView;
    mat4 uProjection;
    mat4 uViewProjection;

    vec4 uViewPosition;  // xyz eye position
    vec4 uSceneAmbient;  // rgb linear scene ambient
    vec4 uFogColor;      // rgb linear fog colour
    vec4 uFogParams;     // x type, y start, z end/density, w enabled
    vec4 uClipPlanes;    // x near, y far
};

struct GpuLight
{
    vec4 positionKind;   // xyz world position, w light type (1 omni, 2 spot, 3 dir)
    vec4 directionRange; // xyz spot/dir axis,  w shadow far plane
    vec4 colorIntensity; // rgb linear colour,  w cos(inner cone)
    vec4 attenuation;    // x const, y linear, z quadratic, w cos(outer cone)
    vec4 shadow;         // x first layer, y depth bias, z normal bias, w shadow near
    vec4 specularColor;  // rgb light specular
    mat4 lightSpace;     // world -> light clip space (directional and spot only)
};

layout(std140) uniform LightBlock
{
    vec4 uLightHeader; // x = light count, y = shadows available
    GpuLight uLights[32];
};

const int LIGHT_OMNI = 1;
const int LIGHT_SPOT = 2;
const int LIGHT_DIR = 3;
