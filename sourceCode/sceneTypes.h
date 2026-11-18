// Modern external renderer for CoppeliaSim.
//
// Shared plain-old-data types that cross the boundary between the plugin glue
// code (simOpenGL3.cpp) and the GL layer (sceneRenderer.cpp).
//
// Everything that is uploaded to the GPU lives here so that the std140 layout
// can be checked against the GLSL declarations in one single place.

#pragma once

#include "math3d.h"

#include <array>
#include <cstddef>
#include <vector>

namespace modern
{

// ---------------------------------------------------------------------------
// CoppeliaSim enums, repeated here on purpose.
//
// A renderer plugin has to keep working across CoppeliaSim versions, and the
// names of these constants changed more than once (sim_light_directional ->
// sim_lighttype_directional -> lightType::directional).  The numeric values
// themselves never did, so they are the stable ABI.
// ---------------------------------------------------------------------------

// Marker constants.  Kept local like SimLightType: the renderer core does not
// include the CoppeliaSim headers, and these values are part of the ext-renderer
// callback contract (sim_markertype_* / sim_markeropts_* in simConst.h).
enum MarkerType
{
    markerTypePoints = 0,
    markerTypeLines = 1,
    markerTypeTriangles = 2,
    markerTypeSpheres = 7,
    markerTypeSquares = 9,
    markerTypeDiscs = 10,
    markerTypeCubes = 11,
    markerTypeCylinders = 12,
    markerTypeCustom = 13
};

enum MarkerOption
{
    // The marker frame (data[13]/data[14]) is always applied, so there is no
    // "local" option any more; the value is kept documented because the engine
    // still sets that bit.
    markerOptionCyclic = 2,
    markerOptionOverlay = 4,
    // Draw the item with its plain colour: no light and no ambient lighting.
    markerOptionNotShaded = 8
};
enum SimLightType
{
    simLightOmnidirectional = 1,
    simLightSpot = 2,
    simLightDirectional = 3
};

enum SimTextureApplyMode
{
    simTextureModulate = 0,
    simTextureDecal = 1,
    simTextureAdd = 2
};

enum SimFogType
{
    simFogLinear = 0,
    simFogExponential = 1,
    simFogExponentialSquared = 2
};

// Hard limit of the lights uniform block.  The legacy renderer stopped at 5
// because it used one sampler and one unrolled struct per light; we use a
// texture array and a uniform block instead, so the limit is only about the
// size of the uniform block.
constexpr int kMaxLights = 32;

// Attributes of the static per-mesh vertex layout.
enum VertexLocation
{
    kLocPosition = 0,
    kLocNormal = 1,
    kLocTexCoord = 2,

    kLocInstanceModel0 = 3, // mat4, 4 consecutive vec4 slots
    kLocInstanceNormal0 = 7, // mat3, 3 consecutive vec4 slots
    kLocInstanceDiffuse = 10,
    kLocInstanceSpecular = 11,
    kLocInstanceColorCode = 12,

    kLocInstanceCount = 12
};

enum UniformBlockBinding
{
    kBlockCamera = 0,
    kBlockLights = 1
};

enum TextureUnit
{
    kUnitAlbedo = 0,
    kUnitShadow = 1
};

// ---------------------------------------------------------------------------
// GLSL std140 mirror of "struct GpuLight".
//
// The matrix is a plain float[16] rather than a Mat4 so that the struct is
// guaranteed to be 144 bytes with no padding; Mat4::constData() is already
// column major, which is exactly what a GLSL mat4 expects, so the copy is a
// memcpy.
// ---------------------------------------------------------------------------
struct GpuLight
{
    Vec4 positionKind;   // xyz = world position,   w = SimLightType
    Vec4 directionRange; // xyz = spot/light axis,  w = shadow far plane
    Vec4 colorIntensity; // rgb = linear colour,    w = cos(inner cone)
    Vec4 attenuation;    // xyz = const/lin/quad,   w = cos(outer cone)
    Vec4 shadow;         // x = first shadow layer, y = depth bias,
                         // z = normal bias,        w = shadow near plane
    Vec4 specularColor;  // rgb = light specular
    float lightSpace[16]; // world -> light clip space (dir/spot only)
};

constexpr std::size_t kGpuLightSize = 6 * sizeof(Vec4) + 16 * sizeof(float);

static_assert(sizeof(Vec4) == 16, "Vec4 must be tightly packed");
static_assert(sizeof(GpuLight) == kGpuLightSize, "GpuLight must not be padded");

// ---------------------------------------------------------------------------
// GLSL std140 mirror of the camera uniform block.
// ---------------------------------------------------------------------------
struct GpuCamera
{
    float view[16];
    float projection[16];
    float viewProjection[16];
    Vec4 viewPosition;   // xyz = eye,          w = unused
    Vec4 sceneAmbient;   // rgb = linear colour  w = unused
    Vec4 fogColor;       // rgb = linear colour  w = unused
    Vec4 fogParams;      // x = type, y = start, z = end/density, w = enabled
    Vec4 clipPlanes;     // x = near, y = far,   zw = unused
};

static_assert(sizeof(GpuCamera) == 3 * 64 + 5 * 16, "GpuCamera must be tightly packed");

struct GpuLightsHeader
{
    // ints are uploaded as floats to avoid any int/float packing surprise.
    float count = 0.0f;
    float hasShadows = 0.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};

// ---------------------------------------------------------------------------
// What a single sim_message_eventcallback_extrenderer_mesh call produces.
// ---------------------------------------------------------------------------
struct Material
{
    Vec3 diffuse{0.6f, 0.6f, 0.6f};
    Vec3 specular{0.0f, 0.0f, 0.0f};
    float shininess = 48.0f;
};

// One mesh instance ready to be drawn (or batched).
struct DrawItem
{
    // Geometry cache key.  Not the engine's mesh id: CMesh::display_extRenderer()
    // bumps that on every call (`if (true)` in 4.11), so it never identifies the
    // same geometry twice.  Built from shape handle + component index instead,
    // which are stable for the lifetime of the component.
    std::uint64_t meshKey = 0;
    int textureId = 0; // texture  cache key      (data[25])
    int applyMode = simTextureModulate;
    bool textured = false;
    bool translucid = false;
    bool cullBackFaces = true; // data[23]: cull this shape's back faces
    int handle = 0; // object handle, encoded into the colour by the colour coded pass

    Mat4 model;
    Mat3 normalMatrix; // (model^-1)^T, needed because CoppeliaSim
                             // bakes non-uniform scaling into the pose
    Material material;
    float opacity = 1.0f;
    float viewDepth = 0.0f; // view-space distance, for back-to-front sorting

    // Filled in by the renderer.
    bool visible = true;
};

// Per-instance bytes uploaded for one draw call.  Must match the attribute
// layout declared in resources/shaders/scene.vert.
struct InstanceData
{
    float model[16];
    float normal[12]; // three vec4 slots
    float diffuse[4]; // rgb + opacity
    float specular[4]; // rgb + shininess
    float colorCode[4]; // rgb = object handle bits, used by the colour coded pass
};

// One submitted light, together with everything the renderer needs to build
// its shadow map.  The plugin fills this in from the extrenderer_light
// callback; the renderer assigns the shadow layers and the matrices.
struct LightShadowInfo
{
    int handle = 0;
    int type = simLightOmnidirectional;

    Mat4 pose;     // light world matrix (columns = axes, translation)
    Vec3 position;
    Vec3 direction; // light Z axis, points away from the beam
    Vec3 diffuse{1.0f, 1.0f, 1.0f};
    Vec3 specular{1.0f, 1.0f, 1.0f};
    float constantAttenuation = 1.0f;
    float linearAttenuation = 0.0f;
    float quadraticAttenuation = 0.0f;
    float cutoffAngle = 0.5f; // spot half angle, radians

    float nearPlane = 0.01f;
    float farPlane = 10.0f;
    float orthoSize = 8.0f;
    float bias = 0.001f;
    float normalBias = 0.012f;
    int textureSize = 1024;

    // Filled in by the renderer.
    int firstLayer = -1;
    int layerCount = 0;
    Mat4 lightSpace;
    std::array<Mat4, 6> faces;
};

} // namespace modern


