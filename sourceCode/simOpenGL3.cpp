#include "simOpenGL3.h"

#include <simLib-2/simLib.h>

#include "offscreenContext.h"
#include "offscreenTarget.h"
#include "renderSettings.h"
#include "sceneRenderer.h"
#include "sceneTypes.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

LIBRARY simLib;

namespace
{

using namespace modern;

const char* kLogPrefix = "simOpenGL3";

// Per-light projection tuning, read once per light handle from
// simGetExtensionString() exactly like the legacy renderer did, so scenes that
// were tuned for it keep their settings.
struct LightTuning
{
    float nearPlane = 0.01f;
    float farPlane = 10.0f;
    float orthoSize = 8.0f;
    float bias = 0.001f;
    float normalBias = 0.012f;
    int textureSize = 1024;
    bool skipShadows = false;
};

struct RendererState
{
    RenderSettings settings;

    OffscreenContext context;
    std::unique_ptr<SceneRenderer> renderer;
    std::unordered_map<int, std::unique_ptr<OffscreenTarget>> targets;
    std::unordered_map<int, LightTuning> lightTunings;

    FrameData frame;

    int activeHandle = -1;
    OffscreenTarget* activeTarget = nullptr;
    bool glUnavailable = false;
    bool versionReported = false;
    bool instanceSwitchPending = false;


    long long frameIndex = 0;

    std::vector<int> unknownMessages;
    bool markerReported = false;

    bool markerRejectedReported = false;
};

RendererState& state()
{
    static RendererState instance;
    return instance;
}


// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

void logMessage(int verbosity, const std::string& message)
{
    simAddLog(kLogPrefix, verbosity, message.c_str());
}

// Everything the renderer core logs (shader errors, incomplete framebuffers,
// missing GL entry points, ...) goes through this sink, so it lands in
// CoppeliaSim's console instead of the process' stderr, which is invisible for
// a GUI application.  Without it a failure on a remote machine is silent.
void logSink(LogLevel level, const char* message)
{
    const int verbosity = (level == LogLevel::Error) ? sim_verbosity_errors : sim_verbosity_warnings;
    simAddLog(kLogPrefix, verbosity, message);
}


// sim_message_eventcallback_extrenderer_marker.  Spelled out instead of taken
// from simConst.h because it was added to the engine before the public headers
// shipped with it, so a 4.10 header set does not declare it.
constexpr int kMessageExtRendererMarker = 346;

float readFloat(const void* pointer, float fallback)
{
    return pointer != nullptr ? *static_cast<const float*>(pointer) : fallback;
}

int readInt(const void* pointer, int fallback)
{
    return pointer != nullptr ? *static_cast<const int*>(pointer) : fallback;
}

bool readBool(const void* pointer, bool fallback)
{
    return pointer != nullptr ? *static_cast<const bool*>(pointer) : fallback;
}

Vec3 readVector3(const void* pointer, const Vec3& fallback)
{
    if (pointer == nullptr)
        return fallback;
    const float* values = static_cast<const float*>(pointer);
    return Vec3(values[0], values[1], values[2]);
}

// Surface colours (shape diffuse/specular, textures) are authored in sRGB, so
// they are decoded to linear before lighting.
//
// Light colours and the scene ambient are deliberately *not* decoded: they are
// intensity multipliers rather than display colours, and CoppeliaSim's own
// renderer used them verbatim.  Decoding them as well darkens the image twice
// (a 0.5 light becomes 0.22), which is exactly what "much darker than the old
// renderer" looks like.
Vec3 toLinear(const Vec3& color)
{
    return state().settings.gammaCorrect ? linearFromSrgb(color) : color;
}

// CoppeliaSim's view convention: the camera pose has to be inverted and then
// flipped 180 degrees around Y before it can be used as an OpenGL view matrix.
Mat4 viewFromPose(const float quaternion[4], const float position[3])
{
    const Mat4 pose = poseToMatrix(quaternion, position);
    Mat4 flip;
    flip.rotate(180.0f, 0.0f, 1.0f, 0.0f);
    return flip * pose.inverted();
}

// ---------------------------------------------------------------------------
// Renderer / target lifetime
// ---------------------------------------------------------------------------

bool ensureRenderer()
{
    RendererState& s = state();
    if (s.glUnavailable)
        return false;
    if (s.renderer != nullptr && s.renderer->isReady())
        return true;

    if (!s.context.ensureCreated())
    {
        s.glUnavailable = true;
        logMessage(sim_verbosity_errors,
                   "cannot create the off-screen OpenGL context; the renderer is disabled");
        return false;
    }

    if (!s.context.supportsRequiredVersion())
    {
        // Deliberately not fatal: the context string is the only authority, and
        // shader compilation reports a genuinely old context far more precisely
        // than a version comparison can.
        logMessage(sim_verbosity_warnings,
                   "the context reports \"" + s.context.versionString() +
                       "\" but 3.3 core is required; continuing, shader compilation will fail "
                       "if the context is really too old");
    }

    s.renderer = std::make_unique<SceneRenderer>();
    if (!s.renderer->initialize(*s.context.functions(), s.settings))
    {
        s.renderer.reset();
        s.glUnavailable = true;
        logMessage(sim_verbosity_errors,
                   "shader compilation failed; see the console for details");
        return false;
    }

    if (!s.versionReported)
    {
        s.versionReported = true;

    }
    return true;
}

OffscreenTarget* acquireTarget(int handle, int width, int height)
{
    RendererState& s = state();
    GLFuncs* gl = s.context.functions();
    if (gl == nullptr)
        return nullptr;

    const auto it = s.targets.find(handle);
    if (it != s.targets.end() && it->second->isValid() && it->second->matches(width, height))
        return it->second.get();

    auto target = std::make_unique<OffscreenTarget>();
    if (!target->create(*gl, width, height, s.settings))
    {
        char message[128];
        std::snprintf(message, sizeof(message), "cannot create a %dx%d render target", width,
                      height);
        logMessage(sim_verbosity_errors, message);
        return nullptr;
    }

    if (it != s.targets.end())
        it->second->destroy(*gl);

    OffscreenTarget* raw = target.get();
    s.targets[handle] = std::move(target);
    return raw;
}

void releaseGlResources()
{
    RendererState& s = state();
    if (!s.context.isCreated())
        return;

    if (!s.context.makeCurrent())
        return;

    GLFuncs* gl = s.context.functions();
    if (gl != nullptr)
    {
        for (auto& entry : s.targets)
            entry.second->destroy(*gl);
        if (s.renderer)
            s.renderer->releaseCachedResources(*gl);
    }
    s.targets.clear();
    s.lightTunings.clear();
    s.context.doneCurrent();
}

void shutdownRenderer()
{
    RendererState& s = state();
    if (s.context.isCreated() && s.context.makeCurrent())
    {
        GLFuncs* gl = s.context.functions();
        if (gl != nullptr)
        {
            for (auto& entry : s.targets)
                entry.second->destroy(*gl);
            if (s.renderer)
            {
                s.renderer->releaseCachedResources(*gl);
                s.renderer->shutdown(*gl);
            }
        }
        s.targets.clear();
        s.context.doneCurrent();
    }
    s.renderer.reset();
    s.context.destroy();
}

// ---------------------------------------------------------------------------
// Light tuning
// ---------------------------------------------------------------------------

std::string readExtensionString(int handle, const char* key, bool* found)
{
    *found = false;
    char* text = simGetExtensionString(handle, -1, key);
    if (text == nullptr)
        return std::string();

    *found = true;
    const std::string result(text);
    simReleaseBuffer(text);
    return result;
}

const LightTuning& tuningFor(int handle, int lightType)
{
    RendererState& s = state();
    const auto it = s.lightTunings.find(handle);
    if (it != s.lightTunings.end())
        return it->second;

    LightTuning tuning;

    // Defaults derived the same way the legacy renderer derived them, so a
    // scene that was authored against it keeps its shadow quality.
    if (lightType == simLightDirectional)
        tuning.normalBias = 0.005f;
    else if (lightType == simLightSpot)
    {
        tuning.bias = 0.0f;
        tuning.normalBias = 0.00008f;
    }
    tuning.textureSize = s.settings.shadowMapSize;

    bool found = false;
    std::string value = readExtensionString(handle, "nearPlane@lightProjection@openGL3", &found);
    if (found)
        tuning.nearPlane = std::strtof(value.c_str(), nullptr);
    value = readExtensionString(handle, "farPlane@lightProjection@openGL3", &found);
    if (found)
        tuning.farPlane = std::strtof(value.c_str(), nullptr);
    value = readExtensionString(handle, "orthoSize@lightProjection@openGL3", &found);
    if (found)
        tuning.orthoSize = std::strtof(value.c_str(), nullptr);
    value = readExtensionString(handle, "shadowTextureSize@lightProjection@openGL3", &found);
    if (found)
        tuning.textureSize = std::atoi(value.c_str());
    value = readExtensionString(handle, "bias@lightProjection@openGL3", &found);
    if (found)
        tuning.bias = std::strtof(value.c_str(), nullptr);
    value = readExtensionString(handle, "normalBias@lightProjection@openGL3", &found);
    if (found)
        tuning.normalBias = std::strtof(value.c_str(), nullptr);
    value = readExtensionString(handle, "noShadow@lightProjection@openGL3", &found);
    if (found)
        tuning.skipShadows = std::atoi(value.c_str()) != 0;

    tuning.nearPlane = std::max(tuning.nearPlane, 1e-4f);
    tuning.farPlane = std::max(tuning.farPlane, tuning.nearPlane + 1e-3f);
    tuning.orthoSize = std::max(tuning.orthoSize, 1e-3f);
    tuning.textureSize = clampValue(tuning.textureSize, 128, 4096);

    return s.lightTunings.emplace(handle, tuning).first->second;
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

void handleStart(void** data)
{
    RendererState& s = state();
    if (!ensureRenderer())
        return;

    const int width = readInt(data[0], 1);
    const int height = readInt(data[1], 1);
    if (width <= 0 || height <= 0)
        return;

    if (!s.context.makeCurrent())
        return;

    OffscreenTarget* target = acquireTarget(readInt(data[19], -1), width, height);
    if (target == nullptr)
    {
        s.context.doneCurrent();
        return;
    }

    s.frame = FrameData{};
    s.activeHandle = readInt(data[19], -1);
    s.activeTarget = target;


    CameraData& camera = s.frame.camera;
    camera.width = width;
    camera.height = height;
    camera.perspective = (readInt(data[5], 0) & 1) == 0;
    // data[29]: the engine wants a colour coded frame (handles as colours).
    camera.colorCoded = readInt(data[29], 0) != 0;
    camera.viewAngle = readFloat(data[8], 1.0f);
    camera.nearPlane = readFloat(data[9], 0.01f);
    camera.farPlane = readFloat(data[10], 10.0f);
    camera.orthoViewSize = readFloat(data[18], 1.0f);

    // The projection is built from exactly what the callback reports: the
    // per-axis angles (data[6]/data[7], full angles in perspective mode and
    // aperture sizes in orthographic mode) and the clipping planes
    // (data[9]/data[10]).  Recomputing the angles from the single view angle
    // would quietly disagree with the engine whenever the ratio convention
    // differs.
    const float xAngle = readFloat(data[6], camera.viewAngle);
    const float yAngle = readFloat(data[7], camera.viewAngle);
    const float* position = static_cast<const float*>(data[3]);
    const float* quaternion = static_cast<const float*>(data[4]);
    if (position == nullptr || quaternion == nullptr)
    {
        s.context.doneCurrent();
        s.activeTarget = nullptr;
        return;
    }

    camera.eye = Vec3(position[0], position[1], position[2]);
    camera.view = viewFromPose(quaternion, position);


    camera.projection.setToIdentity();
    if (camera.perspective)
    {
        // Same construction as the engine's glFrustum() call.
        const float tangentX = std::tan(std::max(xAngle, 1e-4f) * 0.5f);
        const float tangentY = std::tan(std::max(yAngle, 1e-4f) * 0.5f);
        camera.projection.frustum(-camera.nearPlane * tangentX, camera.nearPlane * tangentX,
                                  -camera.nearPlane * tangentY, camera.nearPlane * tangentY,
                                  camera.nearPlane, camera.farPlane);
    }
    else
    {
        const float halfX = std::max(xAngle, 1e-4f) * 0.5f;
        const float halfY = std::max(yAngle, 1e-4f) * 0.5f;
        camera.projection.ortho(-halfX, halfX, -halfY, halfY, camera.nearPlane, camera.farPlane);
    }
    camera.viewProjection = camera.projection * camera.view;


    // The background is a display colour and is decoded; the ambient is an
    // intensity multiplier and is not (see toLinear()).
    camera.backgroundColor = toLinear(readVector3(data[2], Vec3(0.3f, 0.3f, 0.35f)));
    camera.fogColor = camera.backgroundColor;
    camera.ambient = readVector3(data[11], Vec3(0.2f, 0.2f, 0.2f));

    // Fog: data[13] is the type and data[17] the enabled flag for both camera
    // and vision sensor.  data[14..16] are floats for a camera but doubles for
    // a vision sensor (an inconsistency on the engine side), so the values are
    // validated before they are used: a mismatch simply disables fog instead
    // of fogging the whole image.
    camera.fogEnabled = false;
    if (readBool(data[17], false) && data[14] != nullptr && data[15] != nullptr &&
        data[16] != nullptr)
    {
        const float fogStart = readFloat(data[14], 0.0f);
        const float fogEnd = readFloat(data[15], 0.0f);
        const float fogDensity = readFloat(data[16], 0.0f);
        const int fogType = readInt(data[13], 0);

        const bool finite = std::isfinite(fogStart) && std::isfinite(fogEnd) &&
                            std::isfinite(fogDensity);
        const bool usable = finite && ((fogType == simFogLinear) ? (fogEnd > fogStart)
                                                                 : (fogDensity > 0.0f));
        if (usable)
        {
            camera.fogEnabled = true;
            camera.fogType = fogType;
            camera.fogStart = fogStart;
            camera.fogEnd = fogEnd;
            camera.fogDensity = fogDensity;
            // The fog colour is blended into the finished (linear) image, so it
            // is decoded like any other display colour.
            camera.fogColor = toLinear(readVector3(data[12], camera.backgroundColor));
        }
    }
}

void handleLight(void** data)
{
    RendererState& s = state();
    if (s.activeTarget == nullptr)
        return;
    if (int(s.frame.lights.size()) >= kMaxLights)
        return;

    const int type = readInt(data[0], simLightOmnidirectional);
    const int handle = readInt(data[13], 0);
    const float* position = static_cast<const float*>(data[7]);
    const float* quaternion = static_cast<const float*>(data[8]);
    const float* colors = static_cast<const float*>(data[3]);
    if (position == nullptr || quaternion == nullptr || colors == nullptr)
        return;

    const LightTuning& tuning = tuningFor(handle, type);
    if (tuning.skipShadows)
        return;

    LightShadowInfo light;
    light.handle = handle;
    light.type = type;
    light.pose = poseToMatrix(quaternion, position);
    light.position = Vec3(position[0], position[1], position[2]);
    light.direction = Vec3(light.pose(0, 2), light.pose(1, 2), light.pose(2, 2));

    // A light is handed two colour triples: diffuse followed by specular.
    // Where they start depends on the build: the 15 float colour array keeps
    // the object layout, so the light diffuse sits at 3..5 and specular at
    // 6..8, while builds that pass the six light floats directly start at 0.
    // The first triple is zero in the object layout, so the two are told apart
    // by which one is actually populated.
    Vec3 diffuseColors(colors[0], colors[1], colors[2]);
    Vec3 specularColors(colors[3], colors[4], colors[5]);
    if (diffuseColors.x() <= 0.0f && diffuseColors.y() <= 0.0f && diffuseColors.z() <= 0.0f)
    {
        diffuseColors = Vec3(colors[3], colors[4], colors[5]);
        specularColors = Vec3(colors[6], colors[7], colors[8]);
    }

    // Intensity multipliers, used as authored (see toLinear()).
    light.diffuse = diffuseColors;
    light.specular = specularColors;

    light.constantAttenuation = readFloat(data[4], 1.0f);
    light.linearAttenuation = readFloat(data[5], 0.0f);
    light.quadraticAttenuation = readFloat(data[6], 0.0f);
    light.cutoffAngle = clampValue(readFloat(data[1], 0.5f), 0.01f, 1.55f);

    light.nearPlane = tuning.nearPlane;
    light.farPlane = tuning.farPlane;
    light.orthoSize = tuning.orthoSize;
    light.bias = tuning.bias;
    light.normalBias = tuning.normalBias;
    light.textureSize = tuning.textureSize;

    s.frame.lights.push_back(light);
}

// Slot contract of sim_message_eventcallback_extrenderer_mesh.
//
// CMesh::display_extRenderer() fills the array in two different shapes:
//
//   always          : 0..8 (geometry), 18 (textured), 19..27, 29..33
//   textured only   : 9..17 (uvs, texture, wrap, interpolate, applyMode)
//
// The array is a plain `void* data[40]` on the engine's stack, so reading a
// slot that the current branch did not fill reads uninitialised memory.  That
// is not theoretical: reading data[17] on an untextured mesh crashed on macOS
// (the stale slot held 0x3) while happening to work on Windows.
void handleMesh(void** data)
{
    RendererState& s = state();
    if (s.activeTarget == nullptr || s.renderer == nullptr)
        return;

    GLFuncs* gl = s.context.functions();
    if (gl == nullptr)
        return;

    const float* vertices = static_cast<const float*>(data[0]);
    const int vertexCount = readInt(data[1], 0);
    const int* indices = static_cast<const int*>(data[2]);
    const int triangleCount = readInt(data[3], 0);
    const float* normals = static_cast<const float*>(data[4]);
    const int normalCount = readInt(data[5], 0);
    const float* position = static_cast<const float*>(data[6]);
    const float* quaternion = static_cast<const float*>(data[7]);
    const float* colors = static_cast<const float*>(data[8]);

    if (vertices == nullptr || indices == nullptr || position == nullptr ||
        quaternion == nullptr || colors == nullptr || vertexCount <= 0 || triangleCount <= 0)
        return;

    const bool textured = readBool(data[18], false);
    const bool translucid = readBool(data[21], false);
    const bool cullBackFaces = readBool(data[23], true);
    const float opacity = readFloat(data[22], 1.0f);

    const int meshId = readInt(data[24], 0); // regenerated on every call, see below
    const int textureId = readInt(data[25], 0);
    const int shapeHandle = readInt(data[32], -1);
    const int componentIndex = readInt(data[33], 0);

    // CMesh::display_extRenderer() bumps the mesh id on *every* callback in
    // 4.11 (`if (true) // mesh scaling was not reflected`), so using it as a
    // cache key made every frame upload every mesh again and the cache grew
    // without bound.  The shape handle and component index are stable for the
    // lifetime of the component; the vertex data is hashed separately, so an
    // edited or animated shape is still re-uploaded.
    const std::uint64_t meshKey =
        shapeHandle < 0 ? std::uint64_t(std::uint32_t(meshId))
                        : (std::uint64_t(std::uint32_t(shapeHandle)) << 32) |
                              std::uint64_t(std::uint32_t(componentIndex));
    // data[9..17] are only valid on the textured branch of the engine's
    // callback; everything below that reads them is guarded by `textured`.
    const int applyMode = textured ? readInt(data[17], simTextureModulate) : simTextureModulate;
    const float* texCoords = textured ? static_cast<const float*>(data[9]) : nullptr;
    const int texCoordCount = textured ? readInt(data[10], 0) : 0;

    if (textured && !s.renderer->hasTexture(textureId))
    {
        const unsigned char* pixels = static_cast<const unsigned char*>(data[11]);
        const int textureWidth = readInt(data[12], 0);
        const int textureHeight = readInt(data[13], 0);
        if (pixels != nullptr && textureWidth > 0 && textureHeight > 0)
        {
            s.renderer->uploadTexture(*gl, textureId, pixels, textureWidth, textureHeight,
                                      readBool(data[14], false), readBool(data[15], false),
                                      readBool(data[16], true));
        }
    }

    s.renderer->uploadGeometry(*gl, meshKey, vertices, vertexCount, normals, normalCount,
                               texCoords, texCoordCount, indices, triangleCount * 3);

    DrawItem item;
    item.meshKey = meshKey;
    item.textureId = textured ? textureId : 0;
    item.textured = textured;
    item.applyMode = textured ? applyMode : simTextureModulate;
    item.translucid = translucid;
    item.opacity = clampValue(opacity, 0.0f, 1.0f);
    item.cullBackFaces = cullBackFaces;
    item.handle = shapeHandle;
    item.model = poseToMatrix(quaternion, position);
    item.normalMatrix = item.model.normalMatrix();

    // The diffuse colour is a surface colour and is decoded from sRGB like a
    // texture.  The specular colour is not: it is a highlight coefficient that
    // CoppeliaSim authors in the small range a fixed-function pipeline expects
    // (0.1..0.3), and the decode would shrink it by five times or more, which
    // is exactly what makes a highlight disappear.  The reference renderer used
    // it verbatim, and the shininess is the same constant it used.
    item.material.diffuse = toLinear(Vec3(colors[0], colors[1], colors[2]));
    item.material.specular = Vec3(colors[6], colors[7], colors[8]);
    item.material.shininess = 48.0f;

    const Vec4 viewPosition = s.frame.camera.view * Vec4(item.model.column(3).toVector3D(),
                                                                  1.0f);
    item.viewDepth = -viewPosition.z();


    s.frame.items.push_back(item);
}

// sim_message_eventcallback_extrenderer_marker
//
// The engine sends pointers into the marker object that are only valid for the
// duration of the callback, so everything is copied into the frame here.
void handleMarker(void** data)
{
    RendererState& s = state();
    if (s.activeTarget == nullptr)
    {
        if (!s.markerRejectedReported)
        {
            s.markerRejectedReported = true;
            logMessage(sim_verbosity_warnings,
                       "marker callback received outside of a render frame, ignored");
        }
        return;
    }


    const int type = readInt(data[0], markerTypePoints);
    const int options = readInt(data[1], 0);

    MarkerItem marker;
    marker.type = type;
    marker.options = options;
    marker.handle = readInt(data[15], 0);

    // data[3..6] and [8..12] are pointers to pointers: the engine stores the
    // array pointers in local variables and passes their address.
    const int pointDataCount = readInt(data[2], 0);
    const float* points = data[3] != nullptr ? *static_cast<const float* const*>(data[3]) : nullptr;
    if (points == nullptr || pointDataCount <= 0)
    {
        // A custom mesh is described entirely by its vertices and indices, so it
        // is still renderable without a point list: place it at the origin.
        if (type != markerTypeCustom)
        {
            if (!s.markerRejectedReported)
            {
                s.markerRejectedReported = true;
                logMessage(sim_verbosity_warnings,
                           "marker callback without usable data (points=" +
                               std::to_string(points != nullptr ? 1 : 0) + ", count=" +
                               std::to_string(pointDataCount) + ")");
            }
            return;
        }
        marker.points.assign({0.0f, 0.0f, 0.0f});
    }
    else
    {
        const int pointCount = pointDataCount / 3;
        marker.points.assign(points, points + std::size_t(pointCount) * 3);
    }

    const int pointCount = int(marker.points.size() / 3);

    // How the optional arrays are sized depends on the type, and guessing wrong
    // means reading past the end of the engine's vector:
    //   colours  always 4 per point (2*4 per line, 3*4 per triangle)
    //   quats    4 per item, only for the one-point primitives and custom
    //   sizes    3 per item for those same types
    const int pointsPerItem = (type == markerTypeLines) ? 2 : (type == markerTypeTriangles ? 3 : 1);
    const int itemCount = pointsPerItem > 0 ? pointCount / pointsPerItem : 0;
    const bool usesItemPose =
        (type == markerTypeSpheres || type == markerTypeSquares || type == markerTypeDiscs ||
         type == markerTypeCubes || type == markerTypeCylinders || type == markerTypeCustom);

    if (data[5] != nullptr)
    {
        const unsigned char* colors = *static_cast<const unsigned char* const*>(data[5]);
        if (colors != nullptr)
            marker.colors.assign(colors, colors + std::size_t(pointCount) * 4);
    }
    if (usesItemPose && data[4] != nullptr)
    {
        const float* quats = *static_cast<const float* const*>(data[4]);
        if (quats != nullptr && itemCount > 0)
            marker.quats.assign(quats, quats + std::size_t(itemCount) * 4);
    }
    if (data[6] != nullptr)
    {
        const float* sizes = *static_cast<const float* const*>(data[6]);
        if (sizes != nullptr)
        {
            if (usesItemPose && itemCount > 0)
                marker.sizes.assign(sizes, sizes + std::size_t(itemCount) * 3);
        }
    }

    if (type == markerTypeCustom)
    {
        // Custom mesh payload:
        //   data[8]/data[9]   vertices, 3 floats per point
        //   data[10]/data[11] indices, 3 per triangle, zero based into the
        //                     vertices
        //   data[12]          optional normals, 3 floats per index, i.e. one
        //                     vector per triangle corner
        //
        // The counts are used as the engine hands them over (elements of the
        // matching array), and the vertex copy is cut at the highest index that
        // is actually referenced, which is always inside the array and all the
        // renderer needs.
        if (marker.quats.empty())
            marker.quats.assign({0.0f, 0.0f, 0.0f, 1.0f}); // identity, (x, y, z, w)
        if (marker.sizes.empty())
            marker.sizes.assign({1.0f, 1.0f, 1.0f});

        const int indexCount = readInt(data[11], 0);
        if (indexCount <= 0 || (indexCount % 3) != 0)
            return;

        if (data[10] != nullptr)
        {
            const int* indices = *static_cast<const int* const*>(data[10]);
            if (indices != nullptr)
                marker.indices.assign(indices, indices + std::size_t(indexCount));
        }
        if (marker.indices.empty())
            return;

        int highestIndex = 0;
        for (int index : marker.indices)
        {
            if (index > highestIndex)
                highestIndex = index;
        }

        if (data[8] != nullptr)
        {
            const float* vertices = *static_cast<const float* const*>(data[8]);
            if (vertices != nullptr && highestIndex >= 0 && highestIndex < 10000000)
                marker.vertices.assign(vertices,
                                       vertices + (std::size_t(highestIndex) + 1) * 3);
        }
        if (data[12] != nullptr)
        {
            const float* normals = *static_cast<const float* const*>(data[12]);
            if (normals != nullptr)
                marker.normals.assign(normals, normals + std::size_t(indexCount) * 3);
        }


    }

    // The marker frame is always applied: the points are relative to it, and so
    // are the per-item quaternions.  The position and quaternion arrive in
    // double precision with the quaternion in (qx, qy, qz, w) order, while
    // poseToMatrix() takes the engine's internal (w, x, y, z) order.
    marker.transform.setToIdentity();
    if (data[13] != nullptr && data[14] != nullptr)
    {
        const double* position = static_cast<const double*>(data[13]);
        const double* quaternion = static_cast<const double*>(data[14]);
        const float positionF[3] = {float(position[0]), float(position[1]), float(position[2])};
        const float quaternionF[4] = {float(quaternion[3]), float(quaternion[0]),
                                      float(quaternion[1]), float(quaternion[2])};
        marker.transform = poseToMatrix(quaternionF, positionF);
    }

    s.frame.markers.push_back(std::move(marker));
}
// ---------------------------------------------------------------------------
// Point clouds and oc-trees
//
// These are not part of the external renderer callback stream: the plugin walks
// the scene through simLib-2 and reads the object properties directly.  Both
// objects carry their geometry as packed buffers, and the pose they are drawn at
// comes from simGetObjectPose().
// ---------------------------------------------------------------------------

std::vector<float> readFloatBuffer(SIMHANDLE objectHandle, const char* name)
{
    char* buffer = nullptr;
    int length = 0;
    if (simGetBufferProperty == nullptr ||
        simGetBufferProperty(objectHandle, name, &buffer, &length) != sim_propertyret_ok ||
        buffer == nullptr || length <= 0)
    {
        return {};
    }

    std::vector<float> values(static_cast<std::size_t>(length) / sizeof(float));
    std::memcpy(values.data(), buffer, values.size() * sizeof(float));
    simReleaseBuffer(buffer);
    return values;
}

std::vector<unsigned char> readByteBuffer(SIMHANDLE objectHandle, const char* name)
{
    char* buffer = nullptr;
    int length = 0;
    if (simGetBufferProperty == nullptr ||
        simGetBufferProperty(objectHandle, name, &buffer, &length) != sim_propertyret_ok ||
        buffer == nullptr || length <= 0)
    {
        return {};
    }

    std::vector<unsigned char> values(static_cast<std::size_t>(length), 0);
    std::memcpy(values.data(), buffer, values.size());
    simReleaseBuffer(buffer);
    return values;
}

// Objects are only drawn when their 'visible' property says so; when the
// property cannot be read at all the object is kept, which is the safer
// behaviour for a missing query.
bool isObjectVisible(SIMHANDLE objectHandle)
{
    int visible = 1;
    if (simGetBoolProperty == nullptr ||
        simGetBoolProperty(objectHandle, "visible", &visible) != sim_propertyret_ok)
    {
        return true;
    }
    return visible != 0;
}

// Deterministic pseudo random colour, so the points do not flicker between
// frames (no RNG state, just a hash of the index).
Vec4 randomColor(std::size_t index)
{
    unsigned int hash = unsigned(index) * 2654435761u;
    hash ^= hash >> 13;
    hash *= 2246822519u;
    hash ^= hash >> 16;
    const float r = float((hash >> 0) & 0xFFu) / 255.0f;
    const float g = float((hash >> 8) & 0xFFu) / 255.0f;
    const float b = float((hash >> 16) & 0xFFu) / 255.0f;
    return Vec4(0.25f + 0.75f * r, 0.25f + 0.75f * g, 0.25f + 0.75f * b, 1.0f);
}

// Fills a rgba byte array, either from the object's own colours or from the hash.
void fillColors(std::vector<unsigned char>& colors, std::size_t count,
                const std::vector<unsigned char>& source, bool random)
{
    if (!random && source.size() >= count * 4)
    {
        colors = source;
        return;
    }

    colors.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i)
    {
        const Vec4 color = random ? randomColor(i) : Vec4(1.0f, 1.0f, 1.0f, 1.0f);
        colors[i * 4 + 0] = static_cast<unsigned char>(color.x() * 255.0f);
        colors[i * 4 + 1] = static_cast<unsigned char>(color.y() * 255.0f);
        colors[i * 4 + 2] = static_cast<unsigned char>(color.z() * 255.0f);
        colors[i * 4 + 3] = static_cast<unsigned char>(color.w() * 255.0f);
    }
}

// Fallback for oc-trees whose buffer property comes back empty: the same voxel
// positions are exposed as a 3 x N matrix.
std::vector<float> readPointsMatrix(SIMHANDLE objectHandle, const char* name)
{
    if (simGetMatrixProperty == nullptr)
        return {};

    double* values = nullptr;
    int rows = 0;
    int columns = 0;
    if (simGetMatrixProperty(objectHandle, name, &values, &rows, &columns) != sim_propertyret_ok ||
        values == nullptr || rows <= 0 || columns <= 0)
    {
        return {};
    }

    std::vector<float> positions;
    positions.reserve(std::size_t(rows) * std::size_t(columns));
    // Row major: three rows of `columns` coordinates.
    for (int column = 0; column < columns; ++column)
    {
        for (int row = 0; row < rows; ++row)
            positions.push_back(float(values[std::size_t(row) * std::size_t(columns) +
                                             std::size_t(column)]));
    }

    simReleaseBuffer(values);
    return positions;
}
Mat4 objectPoseMatrix(SIMHANDLE objectHandle)
{
    double pose[7] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    if (simGetObjectPose != nullptr && simGetObjectPose(objectHandle, -1, pose) ==
                                           sim_propertyret_ok)
    {
        const float position[3] = {float(pose[0]), float(pose[1]), float(pose[2])};
        // pose holds (x, y, z, qx, qy, qz, qw); poseToMatrix takes (w, x, y, z).
        const float quaternion[4] = {float(pose[6]), float(pose[3]), float(pose[4]),
                                     float(pose[5])};
        return poseToMatrix(quaternion, position);
    }
    return Mat4();
}

// Short description of where a transformed point set ended up, so a misplaced
// object can be told apart from an undrawn one.
std::string describeBounds(const std::vector<float>& localPositions, const Mat4& transform)
{
    if (localPositions.empty())
        return "(no points)";

    Vec3 minimum(localPositions[0], localPositions[1], localPositions[2]);
    Vec3 maximum = minimum;
    for (std::size_t i = 1; i * 3 + 2 < localPositions.size(); ++i)
    {
        minimum.setX(std::min(minimum.x(), localPositions[i * 3 + 0]));
        minimum.setY(std::min(minimum.y(), localPositions[i * 3 + 1]));
        minimum.setZ(std::min(minimum.z(), localPositions[i * 3 + 2]));
        maximum.setX(std::max(maximum.x(), localPositions[i * 3 + 0]));
        maximum.setY(std::max(maximum.y(), localPositions[i * 3 + 1]));
        maximum.setZ(std::max(maximum.z(), localPositions[i * 3 + 2]));
    }

    // The centre of the local box, transformed: enough to see whether the object
    // is where it should be.
    const Vec3 centre = (minimum + maximum) * 0.5f;
    const Vec4 world = transform * Vec4(centre, 1.0f);
    char text[192];
    std::snprintf(text, sizeof(text), "local %.3f..%.3f/%.3f..%.3f/%.3f..%.3f world (%.3f, %.3f, %.3f)",
                  double(minimum.x()), double(maximum.x()), double(minimum.y()),
                  double(maximum.y()), double(minimum.z()), double(maximum.z()), double(world.x()),
                  double(world.y()), double(world.z()));
    return std::string(text);
}
// Walks every point cloud and oc-tree of the scene.  Index based enumeration ends
// at the first negative handle.
void collectPointCloudsAndOctrees(FrameData& frame)
{
    frame.pointClouds.clear();
    frame.octrees.clear();

    // What the walk found, so an empty scene can be told apart from a rendering
    // problem with a single line.
    static bool reported = false;
    std::size_t cloudCount = 0;
    std::size_t cloudPoints = 0;
    std::size_t octreeCount = 0;
    std::size_t octreeVoxels = 0;
    double firstCellSize = 0.0;
    std::string firstCloudBounds;
    std::string firstOctreeBounds;
    float firstCloudPointSize = 0.0f;
    int firstOctreeShowPoints = -1;

    if (simGetObjects == nullptr)
        return;

    // Point clouds.
    for (int index = 0; index < 100000; ++index)
    {
        const SIMHANDLE handle = simGetObjects(index, sim_sceneobject_pointcloud);
        if (handle < 0)
            break;
        if (!isObjectVisible(handle))
            continue;

        PointCloudItem cloud;
        cloud.handle = int(handle);
        cloud.positions = readFloatBuffer(handle, "packedPoints");
        if (cloud.positions.empty())
            continue;

        int pointSize = 1;
        if (simGetIntProperty == nullptr ||
            simGetIntProperty(handle, "pointSize", &pointSize) != sim_propertyret_ok)
        {
            pointSize = 1;
        }
        cloud.pointSize = float(std::max(pointSize, 1));

        int random = 0;
        if (simGetBoolProperty == nullptr ||
            simGetBoolProperty(handle, "randomColors", &random) != sim_propertyret_ok)
        {
            random = 0;
        }

        fillColors(cloud.colors, cloud.positions.size() / 3, readByteBuffer(handle, "colors"),
                   random != 0);
        cloud.transform = objectPoseMatrix(handle);
        ++cloudCount;
        cloudPoints += cloud.positions.size() / 3;
        if (firstCloudBounds.empty())
        {
            firstCloudBounds = describeBounds(cloud.positions, cloud.transform);
            firstCloudPointSize = cloud.pointSize;
        }
        frame.pointClouds.push_back(std::move(cloud));
    }

    // Oc-trees: same payload, one cube of voxelSize per point.
    for (int index = 0; index < 100000; ++index)
    {
        const SIMHANDLE handle = simGetObjects(index, sim_sceneobject_octree);
        if (handle < 0)
            break;
        if (!isObjectVisible(handle))
            continue;

        OctreeItem octree;
        octree.handle = int(handle);
        octree.positions = readFloatBuffer(handle, "packedPoints");
        if (octree.positions.empty())
            octree.positions = readPointsMatrix(handle, "points");
        if (octree.positions.empty())
            continue;

        double voxelSize = 0.05;
        if (simGetFloatProperty == nullptr ||
            simGetFloatProperty(handle, "voxelSize", &voxelSize) != sim_propertyret_ok)
        {
            voxelSize = 0.05;
        }
        octree.voxelSize = float(std::max(voxelSize, 1e-6));

        int random = 0;
        if (simGetBoolProperty == nullptr ||
            simGetBoolProperty(handle, "randomColors", &random) != sim_propertyret_ok)
        {
            random = 0;
        }

        fillColors(octree.colors, octree.positions.size() / 3, readByteBuffer(handle, "colors"),
                   random != 0);
        octree.transform = objectPoseMatrix(handle);
        ++octreeCount;
        octreeVoxels += octree.positions.size() / 3;
        if (firstCellSize == 0.0)
        {
            firstCellSize = voxelSize;
            firstOctreeBounds = describeBounds(octree.positions, octree.transform);
            int showPoints = 0;
            if (simGetBoolProperty != nullptr &&
                simGetBoolProperty(handle, "showPoints", &showPoints) == sim_propertyret_ok)
            {
                firstOctreeShowPoints = showPoints;
            }
        }
        frame.octrees.push_back(std::move(octree));
    }

    if (!reported)
    {
        reported = true;
        logMessage(sim_verbosity_warnings,
                   "primitives: " + std::to_string(cloudCount) + " point cloud(s), " +
                       std::to_string(cloudPoints) + " points, " + std::to_string(octreeCount) +
                       " oc-tree(s), " + std::to_string(octreeVoxels) + " voxels, cell " +
                       std::to_string(firstCellSize) + " m, showPoints " +
                       std::to_string(firstOctreeShowPoints) + "; cloud pointSize " +
                       std::to_string(firstCloudPointSize) + " " + firstCloudBounds +
                       "; oc-tree " + firstOctreeBounds);
    }
}
void handleStop(void** data)
{
    RendererState& s = state();
    if (s.activeTarget == nullptr)
        return;

    unsigned char* rgbBuffer = static_cast<unsigned char*>(data[0]);
    float* depthBuffer = static_cast<float*>(data[1]);
    const bool readRgb = readBool(data[2], true);
    const bool readDepth = readBool(data[3], false);

    if (s.renderer != nullptr && s.renderer->isReady())
    {
        // Point clouds and oc-trees are fetched here, right after the shapes have
        // been submitted through the callbacks.
        collectPointCloudsAndOctrees(s.frame);

        s.renderer->renderFrame(*s.context.functions(), *s.activeTarget, s.frame, rgbBuffer,
                                depthBuffer, readRgb, readDepth);


    }

    s.context.doneCurrent();


    s.activeTarget = nullptr;
    s.activeHandle = -1;
    s.frame.items.clear();
    s.frame.lights.clear();
    s.frame.pointClouds.clear();
    s.frame.octrees.clear();
    ++s.frameIndex;
}

} // namespace

// ---------------------------------------------------------------------------
// Plugin entry points
// ---------------------------------------------------------------------------

// Where CoppeliaSim keeps its resources.  Plugins have no search path of their
// own, so the configuration file lives next to the resources instead of next to
// the library: <paths.resources>/simOpenGL3.ini.
//
// The plugin ABI declares this as
//     int simGetStringProperty(int64_t target, const char* pName, char** pState)
// (see simLib-2/simLib.h), and simLib-2 binds it to the library's
// simGetStringProperty2 export.  The buffer it allocates is owned by the
// caller and has to be released with simReleaseBuffer.
std::string resourcesDirectory()
{
    if (simGetStringProperty == nullptr)
    {
        logMessage(sim_verbosity_warnings,
                   "simGetStringProperty is not available in this build, using the built-in "
                   "defaults");
        return std::string();
    }

    char* location = nullptr;
    const int result = simGetStringProperty(sim_handle_app, "paths.resources", &location);
    if (result != sim_propertyret_ok || location == nullptr)
    {
        logMessage(sim_verbosity_warnings,
                   "cannot query the resources directory, using the built-in defaults");
        return std::string();
    }

    const std::string directory(location);
    simReleaseBuffer(location);
    return directory;
}
SIM_DLLEXPORT int simInit(SSimInit* info)
{
    // Nothing may be logged through simAddLog yet: the plugin name is only
    // registered by CoppeliaSim after simInit() returns, and the lookup takes
    // the whole application down with it.  Messages are buffered and flushed
    // from simMsg()/renderer callbacks instead; logError() falls back to stderr
    // until then, which is always safe.
    deferMessages(true);

    simLib = loadSimLibrary(info->coppeliaSimLibPath);
    if (simLib == nullptr)
    {
        logError("could not find or correctly load the CoppeliaSim library; cannot start the "
                 "plugin");
        deferMessages(false);
        return 0;
    }

    if (getSimProcAddresses(simLib) == 0)
    {
        logError("could not find all required functions in the CoppeliaSim library; cannot "
                 "start the plugin");
        unloadSimLibrary(simLib);
        simLib = nullptr;
        deferMessages(false);
        return 0;
    }

    setLogSink(&logSink);
    const std::string resources = resourcesDirectory();
    state().settings = loadRenderSettings(resources.c_str());
    deferMessages(false);

    return 3; // plugin interface version, 3 since CoppeliaSim 4.6
}

SIM_DLLEXPORT void simInit_ui()
{
}

SIM_DLLEXPORT void simCleanup_ui()
{
    shutdownRenderer();
}

SIM_DLLEXPORT void simCleanup()
{
    flushDeferredMessages();
    shutdownRenderer();
    if (simLib != nullptr)
    {
        unloadSimLibrary(simLib);
        simLib = nullptr;
    }
}

SIM_DLLEXPORT void simMsg(SSimMsg* info)
{
    // The plugin name is registered by now, so messages buffered during
    // simInit() can be reported.
    flushDeferredMessages();

    if (info != nullptr && info->msgId == sim_message_eventcallback_instanceswitch)
        state().instanceSwitchPending = true;
}

SIM_DLLEXPORT void simMsg_ui(SSimMsg_ui* info)
{
    (void)info;
    RendererState& s = state();
    if (!s.instanceSwitchPending)
        return;

    s.instanceSwitchPending = false;

    // Object and texture identifiers are only unique per scene, so every cache
    // has to go when the scene changes.
    releaseGlResources();
}

SIM_DLLEXPORT void simOpenGL3Renderer(int message, void* data)
{
    flushDeferredMessages();

    if (data == nullptr)
        return;

    void** values = static_cast<void**>(data);

    switch (message)
    {
    case sim_message_eventcallback_extrenderer_start:
        handleStart(values);
        break;
    case sim_message_eventcallback_extrenderer_light:
        handleLight(values);
        break;
    case sim_message_eventcallback_extrenderer_mesh:
        handleMesh(values);
        break;
    case kMessageExtRendererMarker:
        handleMarker(values);
        break;
    case sim_message_eventcallback_extrenderer_stop:
        handleStop(values);
        break;
    default:
    {
        // Report each unknown id once: if the marker callback arrives with a
        // different value than kMessageExtRendererMarker, this is what shows it.
        RendererState& s = state();
        for (int known : s.unknownMessages)
        {
            if (known == message)
                return;
        }
        s.unknownMessages.push_back(message);
        logMessage(sim_verbosity_warnings,
                   "unhandled ext-renderer message id " + std::to_string(message));
        break;
    }
    }
}


