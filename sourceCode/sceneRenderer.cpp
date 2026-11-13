#include "sceneRenderer.h"
#include "shaders_embedded.h"

#include "log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace modern
{
namespace
{

// Number of frames for which the GL error queue is drained.  glGetError() can
// flush the pipeline on some drivers, so this is not done indefinitely.
constexpr long long kErrorSweepFrames = 3;



inline Vec4 rowOf(const Mat4& m, int row)
{
    return Vec4(m(row, 0), m(row, 1), m(row, 2), m(row, 3));
}

inline Mat4 yRotation180()
{
    Mat4 rotation;
    rotation.rotate(180.0f, 0.0f, 1.0f, 0.0f);
    return rotation;
}

} // namespace

// ---------------------------------------------------------------------------
// Frustum
// ---------------------------------------------------------------------------

void SceneRenderer::Frustum::build(const Mat4& viewProjection)
{
    const Vec4 row0 = rowOf(viewProjection, 0);
    const Vec4 row1 = rowOf(viewProjection, 1);
    const Vec4 row2 = rowOf(viewProjection, 2);
    const Vec4 row3 = rowOf(viewProjection, 3);

    planes[0] = row3 + row0; // left
    planes[1] = row3 - row0; // right
    planes[2] = row3 + row1; // bottom
    planes[3] = row3 - row1; // top
    planes[4] = row3 + row2; // near
    planes[5] = row3 - row2; // far
}

bool SceneRenderer::Frustum::intersects(const Vec3& center, float radius) const
{
    for (const Vec4& plane : planes)
    {
        const float distance = plane.x() * center.x() + plane.y() * center.y() +
                               plane.z() * center.z() + plane.w();
        if (distance < -radius)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

bool SceneRenderer::initialize(GLFuncs& gl, const RenderSettings& settings)
{
    m_settings = settings;
    m_ready = false;

    if (!m_sceneProgram.build(gl, shaders::kSceneVert, shaders::kSceneFrag, "forward"))
        return false;
    if (!m_depthProgram.build(gl, shaders::kDepthVert, shaders::kDepthFrag, "depth"))
        return false;
    if (!m_depthOmniProgram.build(gl, shaders::kDepthOmniVert, shaders::kDepthOmniFrag,
                                  "depth_omni"))
        return false;
    if (!m_markerProgram.build(gl, shaders::kMarkerVert, shaders::kMarkerFrag, "marker"))
        return false;

    if (!m_colorCodeProgram.build(gl, shaders::kColorCodeVert, shaders::kColorCodeFrag, "colorcode"))
        return false;

    // The colour coded shader takes the camera matrix as a plain uniform (it
    // needs nothing else from the camera block).
    m_colorCodeProgram.use(gl);
    m_uColorCodeViewProjection = m_colorCodeProgram.location(gl, "uViewProjection");

    m_markerProgram.use(gl);
    m_markerProgram.bindUniformBlock(gl, "CameraBlock", kBlockCamera);
    m_markerProgram.bindUniformBlock(gl, "LightBlock", kBlockLights);
    m_uMarkerLit = m_markerProgram.location(gl, "uLit");


    if (!m_tonemapProgram.build(gl, shaders::kFullscreenVert, shaders::kTonemapFrag, "tonemap"))
        return false;

    m_sceneProgram.use(gl);
    const bool cameraBlockBound = m_sceneProgram.bindUniformBlock(gl, "CameraBlock", kBlockCamera);
    const bool lightBlockBound = m_sceneProgram.bindUniformBlock(gl, "LightBlock", kBlockLights);
    if (!cameraBlockBound || !lightBlockBound)
    {
        logError("the shader programs do not expose their uniform blocks (camera=%d lights=%d); "
                 "every matrix and light would be zero and the image would be black",
                 int(cameraBlockBound), int(lightBlockBound));
    }

    m_uAlbedo = m_sceneProgram.location(gl, "uAlbedo");
    m_uShadowMap = m_sceneProgram.location(gl, "uShadowMap");
    m_uApplyMode = m_sceneProgram.location(gl, "uApplyMode");
    m_uPcfRadius = m_sceneProgram.location(gl, "uPcfRadius");
    m_uShadowEnabled = m_sceneProgram.location(gl, "uShadowEnabled");
    m_sceneProgram.set(gl, m_uAlbedo, int(kUnitAlbedo));
    m_sceneProgram.set(gl, m_uShadowMap, int(kUnitShadow));
    m_sceneProgram.set(gl, m_uPcfRadius, m_settings.pcfKernelRadius);

    // A uniform that the compiler optimised away is normal; a missing sampler
    // or apply-mode is not, and it is invisible at run time.
    if (m_uAlbedo < 0 || m_uShadowMap < 0 || m_uApplyMode < 0 || m_uShadowEnabled < 0)
    {
        logWarning("forward shader is missing expected uniforms (albedo=%d shadowMap=%d "
                   "applyMode=%d shadowEnabled=%d)",
                   m_uAlbedo, m_uShadowMap, m_uApplyMode, m_uShadowEnabled);
    }
    (void)cameraBlockBound;
    (void)lightBlockBound;

    m_depthProgram.use(gl);
    m_uLightSpaceMatrix = m_depthProgram.location(gl, "uLightSpaceMatrix");

    m_depthOmniProgram.use(gl);
    m_uLightPosition = m_depthOmniProgram.location(gl, "uLightPosition");
    m_uLightSpaceMatrixOmni = m_depthOmniProgram.location(gl, "uLightSpaceMatrix");

    m_tonemapProgram.use(gl);
    m_uHdrColor = m_tonemapProgram.location(gl, "uHdrColor");
    m_uExposure = m_tonemapProgram.location(gl, "uExposure");
    m_uToneMapping = m_tonemapProgram.location(gl, "uToneMapping");
    m_uGamma = m_tonemapProgram.location(gl, "uGamma");
    m_tonemapProgram.set(gl, m_uHdrColor, 0);
    m_tonemapProgram.set(gl, m_uExposure, m_settings.exposure);
    m_tonemapProgram.set(gl, m_uToneMapping, int(m_settings.toneMapping));
    m_tonemapProgram.set(gl, m_uGamma, m_settings.gammaCorrect ? 0 : 1);

    gl.glUseProgram(0);

    // The near/far planes of a point light shadow are per light, so they are
    // set as uniforms rather than in the uniform block.
    m_uNearPlane = m_depthOmniProgram.location(gl, "uNearPlane");
    m_uFarPlane = m_depthOmniProgram.location(gl, "uFarPlane");

    m_cameraBuffer.create(gl);
    m_cameraBuffer.allocate(gl, GL_UNIFORM_BUFFER, sizeof(GpuCamera), GL_DYNAMIC_DRAW);
    m_cameraBuffer.bindBase(gl, GL_UNIFORM_BUFFER, kBlockCamera);

    const GLsizeiptr lightBlockSize =
        GLsizeiptr(sizeof(GpuLightsHeader) + sizeof(GpuLight) * kMaxLights);
    m_lightBuffer.create(gl);
    m_lightBuffer.allocate(gl, GL_UNIFORM_BUFFER, lightBlockSize, GL_DYNAMIC_DRAW);
    m_lightBuffer.bindBase(gl, GL_UNIFORM_BUFFER, kBlockLights);

    m_instanceBuffer.create(gl);

    gl.glGenVertexArrays(1, &m_emptyVao);

    // 1x1 white sRGB texture, used for untextured shapes and for the colour
    // attachments that must never be sampled uninitialised (unlike the legacy
    // plugin, which bound textures that had no storage at all).
    const unsigned char white[4] = {255, 255, 255, 255};
    m_whiteTexture.create2D(gl, 1, 1, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, white, false);

    logGLError(gl, "SceneRenderer::initialize");
    m_ready = true;
    return true;
}

void SceneRenderer::shutdown(GLFuncs& gl)
{
    releaseCachedResources(gl);

    m_shadowAtlas.destroy(gl);
    m_pixelReader.destroy(gl);
    m_whiteTexture.destroy(gl);

    if (m_emptyVao != 0)
    {
        gl.glDeleteVertexArrays(1, &m_emptyVao);
        m_emptyVao = 0;
    }

    m_instanceBuffer.destroy(gl);
    m_lightBuffer.destroy(gl);
    m_cameraBuffer.destroy(gl);

    m_tonemapProgram.destroy(gl);
    m_markerProgram.destroy(gl);
    m_colorCodeProgram.destroy(gl);
    m_markerBuffer.destroy(gl);
    if (m_markerVao != 0)
    {
        gl.glDeleteVertexArrays(1, &m_markerVao);
        m_markerVao = 0;
    }
    m_depthOmniProgram.destroy(gl);
    m_depthProgram.destroy(gl);
    m_sceneProgram.destroy(gl);

    m_ready = false;
}

void SceneRenderer::releaseCachedResources(GLFuncs& gl)
{
    for (auto& entry : m_geometry)
        entry.second->destroy(gl);
    m_geometry.clear();

    for (auto& entry : m_textures)
        entry.second.texture->destroy(gl);
    m_textures.clear();
}

// ---------------------------------------------------------------------------
// Resource caches
// ---------------------------------------------------------------------------

bool SceneRenderer::hasGeometry(std::uint64_t meshKey) const
{
    return m_geometry.find(meshKey) != m_geometry.end();
}

bool SceneRenderer::hasTexture(int textureId) const
{
    return m_textures.find(textureId) != m_textures.end();
}

const SceneRenderer::Geometry* SceneRenderer::findGeometry(std::uint64_t meshKey) const
{
    const auto it = m_geometry.find(meshKey);
    return it == m_geometry.end() ? nullptr : it->second.get();
}

GLuint SceneRenderer::textureFor(int textureId) const
{
    const auto it = m_textures.find(textureId);
    if (it == m_textures.end() || !it->second.texture->isValid())
        return m_whiteTexture.id();
    return it->second.texture->id();
}

namespace
{

// FNV-1a over the raw attribute data.  O(n) like the interleaving it guards,
// and it is what lets an unchanged mesh keep its buffers instead of being
// re-uploaded every frame.
std::uint64_t hashGeometry(const float* positions, int vertexCount, const float* normals,
                           int normalCount, const float* texCoords, int texCoordCount,
                           const int* indices, int indexCount)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto mixBytes = [&hash](const void* data, std::size_t bytes) {
        const unsigned char* bytes8 = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < bytes; ++i)
        {
            hash ^= bytes8[i];
            hash *= 1099511628211ull;
        }
    };

    if (positions != nullptr)
        mixBytes(positions, std::size_t(vertexCount) * 3 * sizeof(float));
    if (normals != nullptr)
        mixBytes(normals, std::size_t(normalCount) * 3 * sizeof(float));
    if (texCoords != nullptr)
        mixBytes(texCoords, std::size_t(texCoordCount) * 2 * sizeof(float));
    if (indices != nullptr)
        mixBytes(indices, std::size_t(indexCount) * sizeof(int));
    return hash;
}

} // namespace

void SceneRenderer::uploadGeometry(GLFuncs& gl, std::uint64_t meshKey, const float* positions,
                                   int vertexCount, const float* normals, int normalCount,
                                   const float* texCoords, int texCoordCount, const int* indices,
                                   int indexCount)
{
    if (vertexCount <= 0 || indexCount <= 0)
        return;

    const std::uint64_t contentHash = hashGeometry(positions, vertexCount, normals, normalCount,
                                                   texCoords, texCoordCount, indices, indexCount);

    // Re-upload only when the geometry actually changed.  A hit still refreshes
    // the frame stamp, which is what keeps the entry alive.
    const auto existing = m_geometry.find(meshKey);
    if (existing != m_geometry.end())
    {
        Geometry* geometry = existing->second.get();
        geometry->lastUsedFrame = std::uint64_t(m_frameIndex);
        if (geometry->contentHash == contentHash && geometry->vertexCount == indexCount)
            return;
        geometry->destroy(gl);
        m_geometry.erase(existing);
    }

    // Interleaved position/normal/uv, expanded to one vertex per triangle
    // corner.
    //
    // CoppeliaSim does not send a uniform vertex layout: positions are per
    // unique vertex and addressed through `indices`, but normals and texture
    // coordinates are per *corner*, in index order (CMesh sizes them from
    // _indices.size()).  Each corner therefore has its own normal, so an
    // indexed buffer could not deduplicate anything anyway; expanding is both
    // correct and the representation the data already has.  The normalCount /
    // texCoordCount comparison keeps a per-vertex layout working too.
    const int cornerCount = indexCount;
    std::vector<float> vertices;
    vertices.resize(std::size_t(cornerCount) * 8);

    Vec3 minimum(1e30f, 1e30f, 1e30f);
    Vec3 maximum(-1e30f, -1e30f, -1e30f);

    const bool normalsPerCorner = (normals != nullptr) && (normalCount == cornerCount);
    const bool uvsPerCorner = (texCoords != nullptr) && (texCoordCount == cornerCount);

    for (int corner = 0; corner < cornerCount; ++corner)
    {
        const int vertexIndex = (indices[corner] >= 0 && indices[corner] < vertexCount)
                                    ? indices[corner]
                                    : 0;
        const int normalIndex = normalsPerCorner ? corner : vertexIndex;
        const int uvIndex = uvsPerCorner ? corner : vertexIndex;

        float* out = &vertices[std::size_t(corner) * 8];
        out[0] = positions[3 * vertexIndex + 0];
        out[1] = positions[3 * vertexIndex + 1];
        out[2] = positions[3 * vertexIndex + 2];

        if (normals != nullptr && normalIndex < normalCount)
        {
            out[3] = normals[3 * normalIndex + 0];
            out[4] = normals[3 * normalIndex + 1];
            out[5] = normals[3 * normalIndex + 2];
        }
        else
        {
            out[3] = 0.0f;
            out[4] = 0.0f;
            out[5] = 1.0f;
        }

        if (texCoords != nullptr && uvIndex < texCoordCount)
        {
            out[6] = texCoords[2 * uvIndex + 0];
            out[7] = texCoords[2 * uvIndex + 1];
        }
        else
        {
            out[6] = 0.0f;
            out[7] = 0.0f;
        }

        minimum.setX(std::min(minimum.x(), out[0]));
        minimum.setY(std::min(minimum.y(), out[1]));
        minimum.setZ(std::min(minimum.z(), out[2]));
        maximum.setX(std::max(maximum.x(), out[0]));
        maximum.setY(std::max(maximum.y(), out[1]));
        maximum.setZ(std::max(maximum.z(), out[2]));
    }

    auto geometry = std::make_unique<Geometry>();

    gl.glGenVertexArrays(1, &geometry->vao);
    gl.glBindVertexArray(geometry->vao);

    geometry->vertexBuffer.create(gl);
    geometry->vertexBuffer.uploadStreaming(gl, GL_ARRAY_BUFFER,
                                           GLsizeiptr(vertices.size() * sizeof(float)),
                                           vertices.data());

    gl.glBindBuffer(GL_ARRAY_BUFFER, geometry->vertexBuffer.id());
    const GLsizei stride = GLsizei(8 * sizeof(float));
    gl.glVertexAttribPointer(kLocPosition, 3, GL_FLOAT, GL_FALSE, stride,
                             reinterpret_cast<const void*>(0));
    gl.glVertexAttribPointer(kLocNormal, 3, GL_FLOAT, GL_FALSE, stride,
                             reinterpret_cast<const void*>(3 * sizeof(float)));
    gl.glVertexAttribPointer(kLocTexCoord, 2, GL_FLOAT, GL_FALSE, stride,
                             reinterpret_cast<const void*>(6 * sizeof(float)));
    gl.glEnableVertexAttribArray(kLocPosition);
    gl.glEnableVertexAttribArray(kLocNormal);
    gl.glEnableVertexAttribArray(kLocTexCoord);

    gl.glBindVertexArray(0);

    geometry->vertexCount = cornerCount;
    geometry->hasUv = (texCoords != nullptr);
    geometry->boundsCenter = (minimum + maximum) * 0.5f;
    geometry->boundsRadius = 0.0f;
    for (int i = 0; i < vertexCount; ++i)
    {
        const Vec3 position(positions[3 * i + 0], positions[3 * i + 1], positions[3 * i + 2]);
        geometry->boundsRadius = std::max(geometry->boundsRadius,
                                      (position - geometry->boundsCenter).length());
    }

    geometry->contentHash = contentHash;
    geometry->lastUsedFrame = std::uint64_t(m_frameIndex);

    m_geometry.emplace(meshKey, std::move(geometry));
    logGLError(gl, "SceneRenderer::uploadGeometry");


}

void SceneRenderer::uploadTexture(GLFuncs& gl, int textureId, const unsigned char* rgba, int width,
                                  int height, bool repeatU, bool repeatV, bool interpolate)
{
    if (hasTexture(textureId) || rgba == nullptr || width <= 0 || height <= 0)
        return;

    auto texture = std::make_unique<GpuTexture>();
    // SRGB8_ALPHA8 makes the sampler decode sRGB to linear for free, which is
    // what lets the whole pipeline stay in linear space without a single
    // pow() in the fragment shader.
    texture->create2D(gl, width, height, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, rgba, true);

    gl.glBindTexture(GL_TEXTURE_2D, texture->id());
    const GLint filter = interpolate ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                       interpolate ? GL_LINEAR : GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeatU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeatV ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    gl.glBindTexture(GL_TEXTURE_2D, 0);

    TextureEntry entry;
    entry.texture = std::move(texture);
    entry.lastUsedFrame = std::uint64_t(m_frameIndex);
    m_textures.emplace(textureId, std::move(entry));
    logGLError(gl, "SceneRenderer::uploadTexture");


}

// ---------------------------------------------------------------------------
// Uniform blocks
// ---------------------------------------------------------------------------

void SceneRenderer::updateCameraBlock(GLFuncs& gl, const CameraData& camera)
{
    GpuCamera block;
    std::memcpy(block.view, camera.view.constData(), sizeof(block.view));
    std::memcpy(block.projection, camera.projection.constData(), sizeof(block.projection));
    std::memcpy(block.viewProjection, camera.viewProjection.constData(),
                sizeof(block.viewProjection));
    block.viewPosition = Vec4(camera.eye, 0.0f);
    block.sceneAmbient = Vec4(camera.ambient, 0.0f);
    block.fogColor = Vec4(camera.fogColor, 0.0f);
    block.fogParams = Vec4(float(camera.fogType), camera.fogStart, camera.fogEnd,
                                (camera.fogEnabled && m_settings.fogEnabled) ? 1.0f : 0.0f);
    block.clipPlanes = Vec4(camera.nearPlane, camera.farPlane, 0.0f, 0.0f);

    m_cameraBuffer.upload(gl, GL_UNIFORM_BUFFER, 0, sizeof(GpuCamera), &block);
}

bool SceneRenderer::updateLightBlock(GLFuncs& gl, FrameData& frame)
{
    m_gpuLights.clear();
    m_gpuLights.reserve(frame.lights.size());

    const bool shadowsRequested = m_settings.shadowsEnabled && !frame.lights.empty();

    // Assign one contiguous layer range per light.  The assignment is stable
    // while the number of lights does not change, which is what allows the
    // atlas to be allocated once instead of every frame.
    int nextLayer = 0;
    int atlasSize = m_settings.shadowMapSize;
    int requiredLayers = 0;

    for (LightShadowInfo& light : frame.lights)
    {
        light.firstLayer = -1;
        light.layerCount = 0;

        if (!shadowsRequested)
            continue;

        const int layerCount = (light.type == simLightOmnidirectional) ? 6 : 1;
        if (nextLayer + layerCount > m_settings.maxShadowLayers)
            continue;

        light.firstLayer = nextLayer;
        light.layerCount = layerCount;
        nextLayer += layerCount;
        requiredLayers = nextLayer;
        atlasSize = std::max(atlasSize, light.textureSize);
    }

    bool shadowsAvailable = false;
    if (requiredLayers > 0)
    {
        if (m_shadowAtlas.ensure(gl, requiredLayers, atlasSize, m_settings))
            shadowsAvailable = true;
        else
            logWarning("shadow atlas unavailable, rendering without shadows");
    }

    const Mat4 flip = yRotation180();

    for (LightShadowInfo& light : frame.lights)
    {
        if (!shadowsAvailable)
            light.firstLayer = -1;

        const Mat4 lightView = flip * light.pose.inverted();

        Mat4 projection;
        if (light.type == simLightDirectional)
        {
            const float half = light.orthoSize * 0.5f;
            projection.ortho(-half, half, -half, half, light.nearPlane, light.farPlane);
            light.lightSpace = projection * lightView;
        }
        else if (light.type == simLightSpot)
        {
            // The cutoff angle is the half angle of the cone, so the vertical
            // field of view has to be twice that to enclose it.  (The legacy
            // renderer passed the half angle as the full fov, which clipped
            // the shadow at the edge of the cone.)
            const float fovDegrees = std::min(2.0f * light.cutoffAngle * 57.295779513082f, 179.0f);
            projection.perspective(fovDegrees, 1.0f, light.nearPlane, light.farPlane);
            light.lightSpace = projection * lightView;
        }
        else
        {
            projection.perspective(90.0f, 1.0f, light.nearPlane, light.farPlane);

            const Vec3 position = light.position;
            Mat4 face;
            face.lookAt(position, position + Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
            light.faces[0] = projection * face;
            face.setToIdentity();
            face.lookAt(position, position + Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
            light.faces[1] = projection * face;
            face.setToIdentity();
            face.lookAt(position, position + Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f));
            light.faces[2] = projection * face;
            face.setToIdentity();
            face.lookAt(position, position + Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f));
            light.faces[3] = projection * face;
            face.setToIdentity();
            face.lookAt(position, position + Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f));
            light.faces[4] = projection * face;
            face.setToIdentity();
            face.lookAt(position, position + Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f));
            light.faces[5] = projection * face;

            light.lightSpace = light.faces[0];
        }

        GpuLight gpu;
        gpu.positionKind = Vec4(light.position, float(light.type));
        gpu.directionRange = Vec4(light.direction, light.farPlane);
        gpu.attenuation = Vec4(light.constantAttenuation, light.linearAttenuation,
                                    light.quadraticAttenuation, -1.0f);
        gpu.colorIntensity = Vec4(light.diffuse, 0.0f);
        gpu.specularColor = Vec4(light.specular, 0.0f);
        gpu.shadow = Vec4(float(light.firstLayer), light.bias, light.normalBias,
                               light.nearPlane);

        if (light.type == simLightSpot)
        {
            const float outer = std::cos(light.cutoffAngle);
            gpu.attenuation.setW(outer);
            gpu.colorIntensity.setW(std::cos(light.cutoffAngle * 0.9f));
        }

        std::memcpy(gpu.lightSpace, light.lightSpace.constData(), sizeof(gpu.lightSpace));
        m_gpuLights.push_back(gpu);
    }

    // Upload header + the used part of the array; the remaining instanceSlots keep
    // whatever was there before and are never indexed by the shader.
    std::vector<unsigned char> staging(std::size_t(sizeof(GpuLightsHeader)) +
                                       m_gpuLights.size() * sizeof(GpuLight));
    GpuLightsHeader header;
    header.count = float(m_gpuLights.size());
    header.hasShadows = shadowsAvailable ? 1.0f : 0.0f;
    std::memcpy(staging.data(), &header, sizeof(header));
    if (!m_gpuLights.empty())
    {
        std::memcpy(staging.data() + sizeof(GpuLightsHeader), m_gpuLights.data(),
                    m_gpuLights.size() * sizeof(GpuLight));
    }
    m_lightBuffer.upload(gl, GL_UNIFORM_BUFFER, 0, GLsizeiptr(staging.size()), staging.data());

    return shadowsAvailable;
}

// ---------------------------------------------------------------------------
// Frame construction
// ---------------------------------------------------------------------------

void SceneRenderer::buildFrame(GLFuncs& gl, FrameData& frame)
{
    m_instances.clear();
    m_opaqueRuns.clear();
    m_translucentRuns.clear();
    m_shadowRuns.clear();

    if (frame.items.empty())
        return;

    Frustum frustum;
    frustum.build(m_camera.viewProjection);

    std::vector<int> opaqueItems;
    std::vector<int> translucentItems;
    opaqueItems.reserve(frame.items.size());
    translucentItems.reserve(frame.items.size());

    for (int i = 0; i < int(frame.items.size()); ++i)
    {
        DrawItem& item = frame.items[i];
        const Geometry* geometry = findGeometry(item.meshKey);
        if (geometry == nullptr)
        {
            item.visible = false;
            continue;
        }

        const Vec3 center = item.model.map(geometry->boundsCenter);
        const float scaleX = Vec3(item.model(0, 0), item.model(1, 0), item.model(2, 0)).length();
        const float scaleY = Vec3(item.model(0, 1), item.model(1, 1), item.model(2, 1)).length();
        const float scaleZ = Vec3(item.model(0, 2), item.model(1, 2), item.model(2, 2)).length();
        const float scale = std::max(scaleX, std::max(scaleY, scaleZ));

        item.visible = !m_settings.frustumCulling ||
                       frustum.intersects(center, geometry->boundsRadius * scale);

        if (item.translucid)
            translucentItems.push_back(i);
        else
            opaqueItems.push_back(i);
    }

    const auto keyEquals = [&frame](int a, int b) {
        const DrawItem& lhs = frame.items[a];
        const DrawItem& rhs = frame.items[b];
        return lhs.meshKey == rhs.meshKey && lhs.textureId == rhs.textureId &&
               lhs.applyMode == rhs.applyMode && lhs.cullBackFaces == rhs.cullBackFaces;
    };

    // Opaque geometry is sorted by material state so that identical state comes
    // out as one instanced draw call; translucent geometry is sorted back to
    // front instead, because correct alpha blending matters more there.
    std::sort(opaqueItems.begin(), opaqueItems.end(), [&](int a, int b) {
        const DrawItem& lhs = frame.items[a];
        const DrawItem& rhs = frame.items[b];
        if (lhs.meshKey != rhs.meshKey)
            return lhs.meshKey < rhs.meshKey;
        if (lhs.textureId != rhs.textureId)
            return lhs.textureId < rhs.textureId;
        if (lhs.applyMode != rhs.applyMode)
            return lhs.applyMode < rhs.applyMode;

        return int(lhs.cullBackFaces) < int(rhs.cullBackFaces);
    });

    std::sort(translucentItems.begin(), translucentItems.end(), [&](int a, int b) {
        return frame.items[a].viewDepth > frame.items[b].viewDepth;
    });

    // Instance instanceSlots follow the sorted order; the runs below are slices of it.
    std::vector<int> instanceSlots(frame.items.size(), -1);

    for (int index : opaqueItems)
    {
        instanceSlots[index] = int(m_instances.size());
        m_instances.push_back(buildInstance(frame.items[index]));
    }
    for (int index : translucentItems)
    {
        instanceSlots[index] = int(m_instances.size());
        m_instances.push_back(buildInstance(frame.items[index]));
    }

    // --- opaque forward runs -----------------------------------------------
    for (std::size_t i = 0; i < opaqueItems.size();)
    {
        const bool visible = frame.items[opaqueItems[i]].visible;
        std::size_t j = i + 1;
        while (j < opaqueItems.size() && keyEquals(opaqueItems[i], opaqueItems[j]) &&
               frame.items[opaqueItems[j]].visible == visible)
            ++j;

        if (visible)
        {
            Run run;
            run.startInstance = instanceSlots[opaqueItems[i]];
            run.instanceCount = int(j - i);
            run.geometry = findGeometry(frame.items[opaqueItems[i]].meshKey);
            run.textureId = frame.items[opaqueItems[i]].textureId;
            run.applyMode = frame.items[opaqueItems[i]].applyMode;
            run.cullBackFaces = frame.items[opaqueItems[i]].cullBackFaces;
            m_opaqueRuns.push_back(run);
        }
        i = j;
    }

    // --- translucent runs (one instance each, order preserved) -------------
    for (int index : translucentItems)
    {
        if (!frame.items[index].visible)
            continue;

        Run run;
        run.startInstance = instanceSlots[index];
        run.instanceCount = 1;
        run.geometry = findGeometry(frame.items[index].meshKey);
        run.textureId = frame.items[index].textureId;
        run.applyMode = frame.items[index].applyMode;
        run.cullBackFaces = frame.items[index].cullBackFaces;
        m_translucentRuns.push_back(run);
    }

    // --- shadow runs: batched by geometry only -----------------------------
    for (std::size_t i = 0; i < opaqueItems.size();)
    {
        const std::uint64_t meshId = frame.items[opaqueItems[i]].meshKey;
        std::size_t j = i + 1;
        while (j < opaqueItems.size() && frame.items[opaqueItems[j]].meshKey == meshId)
            ++j;

        Run run;
        run.startInstance = instanceSlots[opaqueItems[i]];
        run.instanceCount = int(j - i);
        run.geometry = findGeometry(meshId);
        m_shadowRuns.push_back(run);
        i = j;
    }

    for (std::size_t i = 0; i < translucentItems.size();)
    {
        const std::uint64_t meshId = frame.items[translucentItems[i]].meshKey;
        std::size_t j = i + 1;
        while (j < translucentItems.size() && frame.items[translucentItems[j]].meshKey == meshId)
            ++j;

        Run run;
        run.startInstance = instanceSlots[translucentItems[i]];
        run.instanceCount = int(j - i);
        run.geometry = findGeometry(meshId);
        m_shadowRuns.push_back(run);
        i = j;
    }

    if (!m_instances.empty())
    {
        m_instanceBuffer.uploadStreaming(gl, GL_ARRAY_BUFFER,
                                         GLsizeiptr(m_instances.size() * sizeof(InstanceData)),
                                         m_instances.data());
    }
}

InstanceData SceneRenderer::buildInstance(const DrawItem& item)
{
    InstanceData data{};
    std::memcpy(data.model, item.model.constData(), sizeof(data.model));

    const float* normal = item.normalMatrix.constData();
    for (int column = 0; column < 3; ++column)
    {
        data.normal[column * 4 + 0] = normal[column * 3 + 0];
        data.normal[column * 4 + 1] = normal[column * 3 + 1];
        data.normal[column * 4 + 2] = normal[column * 3 + 2];
        data.normal[column * 4 + 3] = 0.0f;
    }

    data.diffuse[0] = item.material.diffuse.x();
    data.diffuse[1] = item.material.diffuse.y();
    data.diffuse[2] = item.material.diffuse.z();
    data.diffuse[3] = item.translucid ? item.opacity : 1.0f;

    data.specular[0] = item.material.specular.x();
    data.specular[1] = item.material.specular.y();
    data.specular[2] = item.material.specular.z();
    data.specular[3] = item.material.shininess;

    // Colour coded pass: (h & 255, (h >> 8) & 255, (h >> 16) & 255) / 255.
    const unsigned int handle = static_cast<unsigned int>(item.handle);
    data.colorCode[0] = float(handle & 0xFFu) / 255.0f;
    data.colorCode[1] = float((handle >> 8) & 0xFFu) / 255.0f;
    data.colorCode[2] = float((handle >> 16) & 0xFFu) / 255.0f;
    data.colorCode[3] = 1.0f;
    return data;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void SceneRenderer::bindRunAttributes(GLFuncs& gl, const Run& run)
{
    gl.glBindVertexArray(run.geometry->vao);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_instanceBuffer.id());

    const GLsizei stride = GLsizei(sizeof(InstanceData));
    const auto* base = reinterpret_cast<const char*>(std::size_t(run.startInstance) * sizeof(InstanceData));

    const auto setAttribute = [&](int location, int components, std::size_t offset) {
        gl.glVertexAttribPointer(location, components, GL_FLOAT, GL_FALSE, stride,
                                 base + offset);
        gl.glEnableVertexAttribArray(location);
        gl.glVertexAttribDivisor(location, 1);
    };

    setAttribute(kLocInstanceModel0 + 0, 4, 0);
    setAttribute(kLocInstanceModel0 + 1, 4, 16);
    setAttribute(kLocInstanceModel0 + 2, 4, 32);
    setAttribute(kLocInstanceModel0 + 3, 4, 48);

    setAttribute(kLocInstanceNormal0 + 0, 4, 64);
    setAttribute(kLocInstanceNormal0 + 1, 4, 80);
    setAttribute(kLocInstanceNormal0 + 2, 4, 96);

    setAttribute(kLocInstanceDiffuse, 4, 112);
    setAttribute(kLocInstanceSpecular, 4, 128);
    setAttribute(kLocInstanceColorCode, 4, 144);
}

void SceneRenderer::drawRuns(GLFuncs& gl, const std::vector<Run>& runs, bool shadowPass)
{
    for (const Run& run : runs)
    {
        if (run.geometry == nullptr || run.instanceCount <= 0 || run.geometry->vertexCount <= 0)
            continue;

        if (!shadowPass)
        {
            if (!m_camera.colorCoded)
            {
                // The colour coded program has no texture or apply mode; binding
                // them would only poke the wrong program's uniforms.
                const GLuint texture = textureFor(run.textureId);
                gl.glActiveTexture(GL_TEXTURE0 + kUnitAlbedo);
                gl.glBindTexture(GL_TEXTURE_2D, texture);
                m_sceneProgram.set(gl, m_uApplyMode, run.applyMode);
            }

        }

        // Shapes are culled when CoppeliaSim's per-shape flag says so.
        if (run.cullBackFaces && m_settings.cullBackFaces)
        {
            gl.glEnable(GL_CULL_FACE);
            gl.glCullFace(GL_BACK);
        }
        else
        {
            gl.glDisable(GL_CULL_FACE);
        }

        bindRunAttributes(gl, run);
        gl.glDrawArraysInstanced(GL_TRIANGLES, 0, run.geometry->vertexCount, run.instanceCount);

        if (shadowPass)
            ++m_statistics.shadowDrawCalls;
        else
            ++m_statistics.drawCalls;
        m_statistics.instances += run.instanceCount;
    }
    gl.glBindVertexArray(0);
}

void SceneRenderer::renderShadowMaps(GLFuncs& gl)
{
    if (!m_shadowAtlas.isValid())
        return;

    gl.glDisable(GL_BLEND);
    gl.glDisable(GL_CULL_FACE);
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LEQUAL);
    gl.glDepthMask(GL_TRUE);

    if (m_settings.polygonOffsetShadows)
    {
        gl.glEnable(GL_POLYGON_OFFSET_FILL);
        gl.glPolygonOffset(2.0f, 4.0f);
    }

    for (const LightShadowInfo& light : m_lights)
    {
        if (light.firstLayer < 0)
            continue;

        if (light.type == simLightOmnidirectional)
        {
            m_depthOmniProgram.use(gl);
            m_depthOmniProgram.set(gl, m_uLightPosition, light.position);
            m_depthOmniProgram.set(gl, m_uNearPlane, light.nearPlane);
            m_depthOmniProgram.set(gl, m_uFarPlane, light.farPlane);

            for (int face = 0; face < 6; ++face)
            {
                m_shadowAtlas.beginLayer(gl, light.firstLayer + face);
                m_depthOmniProgram.set(gl, m_uLightSpaceMatrixOmni, light.faces[face]);
                drawRuns(gl, m_shadowRuns, true);
            }
        }
        else
        {
            m_depthProgram.use(gl);
            m_shadowAtlas.beginLayer(gl, light.firstLayer);
            m_depthProgram.set(gl, m_uLightSpaceMatrix, light.lightSpace);
            drawRuns(gl, m_shadowRuns, true);
        }
    }

    if (m_settings.polygonOffsetShadows)
        gl.glDisable(GL_POLYGON_OFFSET_FILL);

    m_shadowAtlas.end(gl);
    m_statistics.shadowLayers = m_shadowAtlas.layers();
}

void SceneRenderer::renderOpaque(GLFuncs& gl)
{
    m_sceneProgram.use(gl);
    m_sceneProgram.set(gl, m_uShadowEnabled, m_shadowAtlas.isValid() ? 1 : 0);
    m_shadowAtlas.bind(gl, kUnitShadow);

    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LEQUAL);
    gl.glDepthMask(m_settings.depthPrepass ? GL_FALSE : GL_TRUE);
    gl.glDisable(GL_BLEND);

    drawRuns(gl, m_opaqueRuns, false);
    gl.glDepthMask(GL_TRUE);
}

void SceneRenderer::renderDepthPrepass(GLFuncs& gl)
{
    m_depthProgram.use(gl);
    m_depthProgram.set(gl, m_uLightSpaceMatrix, m_camera.viewProjection);

    gl.glDisable(GL_BLEND);
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LEQUAL);
    gl.glDepthMask(GL_TRUE);

    for (const Run& run : m_opaqueRuns)
    {
        if (run.geometry == nullptr || run.instanceCount <= 0)
            continue;

        if (run.cullBackFaces && m_settings.cullBackFaces)
        {
            gl.glEnable(GL_CULL_FACE);
            gl.glCullFace(GL_BACK);
        }
        else
        {
            gl.glDisable(GL_CULL_FACE);
        }

        bindRunAttributes(gl, run);
        gl.glDrawArraysInstanced(GL_TRIANGLES, 0, run.geometry->vertexCount, run.instanceCount);
    }
    gl.glBindVertexArray(0);
    ++m_statistics.drawCalls;
}

void SceneRenderer::renderTranslucent(GLFuncs& gl)
{
    if (m_translucentRuns.empty())
        return;

    m_sceneProgram.use(gl);
    m_sceneProgram.set(gl, m_uShadowEnabled, m_shadowAtlas.isValid() ? 1 : 0);
    m_shadowAtlas.bind(gl, kUnitShadow);

    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LEQUAL);
    gl.glDepthMask(GL_FALSE);
    gl.glEnable(GL_BLEND);
    gl.glBlendEquation(GL_FUNC_ADD);
    gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawRuns(gl, m_translucentRuns, false);

    gl.glDisable(GL_BLEND);
    gl.glDepthMask(GL_TRUE);
}

void SceneRenderer::renderTonemapPassThrough(GLFuncs& gl, OffscreenTarget& target)
{
    gl.glBindFramebuffer(GL_FRAMEBUFFER, target.presentFramebuffer());
    gl.glViewport(0, 0, target.width(), target.height());

    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_BLEND);
    gl.glDisable(GL_CULL_FACE);
    gl.glDepthMask(GL_FALSE);

    m_tonemapProgram.use(gl);
    m_tonemapProgram.set(gl, m_uExposure, 1.0f);
    m_tonemapProgram.set(gl, m_uToneMapping, int(ToneMapping::None));
    m_tonemapProgram.set(gl, m_uGamma, 1); // no transfer function: handles must survive

    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D, target.resolveColorTexture());

    gl.glBindVertexArray(m_emptyVao);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    gl.glBindVertexArray(0);

    gl.glDepthMask(GL_TRUE);

    // Restore the configured tone mapping for the next shaded frame.
    m_tonemapProgram.set(gl, m_uExposure, m_settings.exposure);
    m_tonemapProgram.set(gl, m_uToneMapping, int(m_settings.toneMapping));
    m_tonemapProgram.set(gl, m_uGamma, m_settings.gammaCorrect ? 0 : 1);
}
void SceneRenderer::renderTonemap(GLFuncs& gl, OffscreenTarget& target)
{
    gl.glBindFramebuffer(GL_FRAMEBUFFER, target.presentFramebuffer());
    gl.glViewport(0, 0, target.width(), target.height());

    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_BLEND);
    gl.glDisable(GL_CULL_FACE);
    gl.glDepthMask(GL_FALSE);

    m_tonemapProgram.use(gl);
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D, target.resolveColorTexture());

    gl.glBindVertexArray(m_emptyVao);
    gl.glDrawArrays(GL_TRIANGLES, 0, 3);
    gl.glBindVertexArray(0);

    gl.glDepthMask(GL_TRUE);
}

// ---------------------------------------------------------------------------
// Depth linearisation
//
// CoppeliaSim asks for linear depth, and expects exactly the same conversion
// its built-in renderer applies after glReadPixels, so the formula is kept
// identical here.
// ---------------------------------------------------------------------------

void SceneRenderer::lineariseDepth(float* depthBuffer, int width, int height) const
{
    const float farMinusNear = m_camera.farPlane - m_camera.nearPlane;
    if (std::abs(farMinusNear) < 1e-9f)
        return;

    const float farDivided = m_camera.farPlane / farMinusNear;
    const float nearTimesFar = m_camera.nearPlane * m_camera.farPlane;
    const float inverseNearTimesFar = nearTimesFar / farMinusNear;

    const int count = width * height;
    for (int i = 0; i < count; ++i)
    {
        depthBuffer[i] = ((inverseNearTimesFar / (farDivided - depthBuffer[i])) -
                          m_camera.nearPlane) / farMinusNear;
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

namespace
{

Mat4 markerTransform(const Mat4& frame, const float* quaternion, const float* sizes)
{
    // translate(position) * rotate(quaternion) * scale(sizes)
    Mat4 result = frame;
    if (quaternion != nullptr)
    {
        // glRotatef equivalent: axis = xyz normalised, angle = 2*acos(w).
        const float w = clampValue(quaternion[3], -1.0f, 1.0f);
        const float axisLength = std::sqrt(quaternion[0] * quaternion[0] +
                                           quaternion[1] * quaternion[1] +
                                           quaternion[2] * quaternion[2]);
        const float angle = 2.0f * std::acos(w);
        if (axisLength > 1e-6f && angle != 0.0f)
            result.rotate(angle * 57.295779513082f, quaternion[0] / axisLength,
                          quaternion[1] / axisLength, quaternion[2] / axisLength);
    }
    if (sizes != nullptr)
        result.scale(sizes[0], sizes[1], sizes[2]);
    return result;
}

} // namespace
void SceneRenderer::renderMarkers(GLFuncs& gl, const FrameData& frame)
{
    m_markerPoints.clear();
    m_markerLines.clear();
    m_markerTriangles.clear();
    m_markerOverlayPoints.clear();
    m_markerOverlayLines.clear();
    m_markerOverlayTriangles.clear();

    if (frame.markers.empty())
        return;

    const bool gamma = m_settings.gammaCorrect;

    const Vec3 eye = m_camera.eye;
    // Set per marker: 0 when sim_markeropts_notshaded asks for the plain colour.
    float currentLit = 1.0f;
    const auto pushVertex = [&currentLit](std::vector<MarkerVertex>& target,
                                          const Vec3& position, const Vec4& color,
                                          const Vec3& normal) {
        MarkerVertex vertex;
        vertex.position[0] = position.x();
        vertex.position[1] = position.y();
        vertex.position[2] = position.z();
        vertex.color[0] = color.x();
        vertex.color[1] = color.y();
        vertex.color[2] = color.z();
        vertex.color[3] = color.w();
        vertex.normal[0] = normal.x();
        vertex.normal[1] = normal.y();
        vertex.normal[2] = normal.z();
        vertex.lit = currentLit;
        target.push_back(vertex);
    };
    // Points and lines have no meaningful normal; facing the camera keeps them
    // lit instead of having them go black when the lights point away.
    const auto facingNormal = [&eye](const Vec3& position) {
        const Vec3 toEye = eye - position;
        return toEye.length() > 1e-6f ? toEye.normalized() : Vec3(0.0f, 0.0f, 1.0f);
    };

    // Marker colours are authored as display colours, so they are decoded like
    // any other colour and encoded again by the tone map pass.
    const bool colorCoded = frame.camera.colorCoded;
    const auto handleColor = [](int handle) {
        const unsigned int bits = static_cast<unsigned int>(handle);
        return Vec4(float(bits & 0xFFu) / 255.0f, float((bits >> 8) & 0xFFu) / 255.0f,
                    float((bits >> 16) & 0xFFu) / 255.0f, 1.0f);
    };

    const auto readColor = [gamma](const unsigned char* rgba, const Vec4& fallback) {
        if (rgba == nullptr)
            return fallback;
        Vec3 rgb(float(rgba[0]) / 255.0f, float(rgba[1]) / 255.0f, float(rgba[2]) / 255.0f);
        if (gamma)
            rgb = linearFromSrgb(rgb);
        return Vec4(rgb, float(rgba[3]) / 255.0f);
    };

    for (const MarkerItem& marker : frame.markers)
    {
        std::vector<MarkerVertex>& points =
            marker.isOverlay() ? m_markerOverlayPoints : m_markerPoints;
        std::vector<MarkerVertex>& lines =
            marker.isOverlay() ? m_markerOverlayLines : m_markerLines;
        std::vector<MarkerVertex>& triangles =
            marker.isOverlay() ? m_markerOverlayTriangles : m_markerTriangles;

        const int pointCount = int(marker.points.size() / 3);
        if (pointCount <= 0)
            continue;

        // Unlit when the marker asks for its plain colour.
        currentLit = marker.isNotShaded() ? 0.0f : 1.0f;

        const auto pointAt = [&marker](int index) {
            return Vec3(marker.points[std::size_t(index) * 3 + 0],
                        marker.points[std::size_t(index) * 3 + 1],
                        marker.points[std::size_t(index) * 3 + 2]);
        };
        const auto colorAt = [&marker, &readColor, colorCoded, &handleColor](int pointIndex) {
            if (colorCoded)
                return handleColor(marker.handle);
            const unsigned char* rgba = marker.colors.empty()
                                            ? nullptr
                                            : &marker.colors[std::size_t(pointIndex) * 4];
            return readColor(rgba, Vec4(1.0f, 1.0f, 1.0f, 1.0f));
        };
        const auto quatAt = [&marker](int itemIndex) {
            return marker.quats.empty() ? nullptr
                                        : &marker.quats[std::size_t(itemIndex) * 4];
        };
        const auto sizeAt = [&marker](int itemIndex) {
            return marker.sizes.empty() ? nullptr
                                        : &marker.sizes[std::size_t(itemIndex) * 3];
        };
        const auto world = [&marker](const Vec3& local) {
            const Vec4 value = marker.transform * Vec4(local, 1.0f);
            return Vec3(value.x(), value.y(), value.z());
        };

        // Per-item orientation/scale only: the frame's translation is already in
        // `world(point)`, so including it here would displace every item by the
        // frame position (the engine composes translate(point) * rotate * scale
        // inside the frame's rotation).
        Mat4 frameRotation = marker.transform;
        frameRotation(0, 3) = 0.0f;
        frameRotation(1, 3) = 0.0f;
        frameRotation(2, 3) = 0.0f;

        if (marker.type == markerTypePoints)
        {
            for (int i = 0; i < pointCount; ++i)
                pushVertex(points, world(pointAt(i)), colorAt(i), facingNormal(world(pointAt(i))));
        }
        else if (marker.type == markerTypeLines)
        {
            for (int i = 0; i + 1 < pointCount; i += 2)
            {
                pushVertex(lines, world(pointAt(i)), colorAt(i), facingNormal(world(pointAt(i))));
                pushVertex(lines, world(pointAt(i + 1)), colorAt(i + 1), facingNormal(world(pointAt(i + 1))));
            }
        }
        else if (marker.type == markerTypeTriangles)
        {
            for (int i = 0; i + 2 < pointCount; i += 3)
            {
                for (int k = 0; k < 3; ++k)
                    pushVertex(triangles, world(pointAt(i + k)), colorAt(i + k), facingNormal(world(pointAt(i + k))));
            }
        }
        else if (marker.type == markerTypeTubes)
        {
            // Mirrors the engine: radius from sizes[0], colour from the first
            // point of the segment, one prism per segment.
            const float radius = marker.sizes.empty() ? 0.01f : marker.sizes[0] * 0.5f;
            const int sides = 8;
            for (int i = 0; i + 1 < pointCount; i += 2)
            {
                const Vec3 p0 = world(pointAt(i));
                const Vec3 p1 = world(pointAt(i + 1));
                const Vec3 axis = p1 - p0;
                const float length = axis.length();
                if (length < 1e-6f)
                    continue;

                const Vec3 direction = axis * (1.0f / length);
                // Any vector perpendicular to the segment works as the basis.
                Vec3 side = (std::fabs(direction.z()) < 0.9f) ? Vec3(0.0f, 0.0f, 1.0f)
                                                              : Vec3(1.0f, 0.0f, 0.0f);
                const Vec3 u = Vec3::crossProduct(direction, side).normalized();
                const Vec3 v = Vec3::crossProduct(direction, u);

                const Vec4 color = colorAt(i);
                for (int side0 = 0; side0 < sides; ++side0)
                {
                    const float a0 = 2.0f * 3.14159265358979f * float(side0) / float(sides);
                    const float a1 = 2.0f * 3.14159265358979f * float(side0 + 1) / float(sides);
                    const Vec3 n0 = u * std::cos(a0) + v * std::sin(a0);
                    const Vec3 n1 = u * std::cos(a1) + v * std::sin(a1);
                    const Vec3 r0 = n0 * radius;
                    const Vec3 r1 = n1 * radius;

                    pushVertex(triangles, p0 + r0, color, n0);
                    pushVertex(triangles, p1 + r0, color, n0);
                    pushVertex(triangles, p1 + r1, color, n1);
                    pushVertex(triangles, p0 + r0, color, n0);
                    pushVertex(triangles, p1 + r1, color, n1);
                    pushVertex(triangles, p0 + r1, color, n1);
                }
            }
        }
        else if (marker.type == markerTypeAxes)
        {
            for (int i = 0; i < pointCount; ++i)
            {
                const Mat4 matrix = markerTransform(frameRotation, quatAt(i), sizeAt(i));
                const Vec3 origin = world(pointAt(i));
                const auto emit = [&](const Vec3& extent, const Vec3& rgb) {
                    const Vec4 a = matrix * Vec4(Vec3(0.0f, 0.0f, 0.0f), 1.0f);
                    const Vec4 b = matrix * Vec4(extent, 1.0f);
                    const Vec4 color = colorCoded ? handleColor(marker.handle)
                                                  : Vec4(gamma ? linearFromSrgb(rgb) : rgb, 1.0f);
                    const Vec3 va = Vec3(a.x(), a.y(), a.z()) + origin;
                    const Vec3 vb = Vec3(b.x(), b.y(), b.z()) + origin;
                    pushVertex(lines, va, color, facingNormal(va));
                    pushVertex(lines, vb, color, facingNormal(vb));
                };
                emit(Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f));
                emit(Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
                emit(Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 1.0f));
            }
        }
        else if (marker.type == markerTypeCustom)
        {
            const int indexCount = int(marker.indices.size());
            for (int i = 0; i < pointCount; ++i)
            {
                const Mat4 matrix =
                    markerTransform(frameRotation, quatAt(i), sizeAt(i));
                const Mat3 normalMatrix = matrix.normalMatrix();
                const Vec4 color = colorAt(i);
                const Vec3 origin = world(pointAt(i));
                for (int corner = 0; corner + 2 < indexCount; corner += 3)
                {
                    const bool hasNormals =
                        marker.normals.size() >= std::size_t(corner) * 3 + 9;
                    for (int k = 0; k < 3; ++k)
                    {
                        const int index = marker.indices[std::size_t(corner + k)];
                        if (index < 0 || std::size_t(index) * 3 + 2 >= marker.vertices.size())
                            continue;
                        const Vec3 local(marker.vertices[std::size_t(index) * 3 + 0],
                                         marker.vertices[std::size_t(index) * 3 + 1],
                                         marker.vertices[std::size_t(index) * 3 + 2]);
                        const Vec4 p = matrix * Vec4(local, 1.0f);
                        const Vec3 worldPosition = Vec3(p.x(), p.y(), p.z()) + origin;

                        Vec3 normal;
                        if (hasNormals)
                        {
                            // The callback carries one normal per triangle corner.
                            const std::size_t base = std::size_t(corner + k) * 3;
                            normal = (normalMatrix * Vec3(marker.normals[base + 0],
                                                          marker.normals[base + 1],
                                                          marker.normals[base + 2]))
                                         .normalized();
                        }
                        else
                        {
                            normal = facingNormal(worldPosition);
                        }
                        pushVertex(triangles, worldPosition, color, normal);
                    }
                }
            }
        }
        else
        {
            // One-point primitives: unit shape in [-0.5, 0.5], posed by the
            // point, the per-item quaternion and the per-item size.
            const int sides = 12;
            for (int i = 0; i < pointCount; ++i)
            {
                const Mat4 matrix =
                    markerTransform(frameRotation, quatAt(i), sizeAt(i));
                const Vec4 color = colorAt(i);
                const Vec3 origin = world(pointAt(i));

                const Mat3 normalMatrix = matrix.normalMatrix();
                const auto emit = [&](const Vec3& local, const Vec3& localNormal) {
                    const Vec4 p = matrix * Vec4(local, 1.0f);
                    const Vec3 n = (normalMatrix * localNormal).normalized();
                    pushVertex(triangles, Vec3(p.x(), p.y(), p.z()) + origin, color, n);
                };
                const Vec3 localUp(0.0f, 0.0f, 1.0f);

                if (marker.type == markerTypeSquares)
                {
                    emit(Vec3(-0.5f, -0.5f, 0.0f), localUp);
                    emit(Vec3(0.5f, -0.5f, 0.0f), localUp);
                    emit(Vec3(0.5f, 0.5f, 0.0f), localUp);
                    emit(Vec3(-0.5f, -0.5f, 0.0f), localUp);
                    emit(Vec3(0.5f, 0.5f, 0.0f), localUp);
                    emit(Vec3(-0.5f, 0.5f, 0.0f), localUp);
                }
                else if (marker.type == markerTypeDiscs)
                {
                    for (int k = 0; k < sides; ++k)
                    {
                        const float a0 = 2.0f * 3.14159265358979f * float(k) / float(sides);
                        const float a1 = 2.0f * 3.14159265358979f * float(k + 1) / float(sides);
                        emit(Vec3(0.0f, 0.0f, 0.0f), localUp);
                        emit(Vec3(0.5f * std::cos(a0), 0.5f * std::sin(a0), 0.0f), localUp);
                        emit(Vec3(0.5f * std::cos(a1), 0.5f * std::sin(a1), 0.0f), localUp);
                    }
                }
                else if (marker.type == markerTypeSpheres)
                {
                    const int stacks = 8;
                    for (int k = 0; k < stacks; ++k)
                    {
                        const float lat0 = 3.14159265358979f * (-0.5f + float(k) / stacks);
                        const float lat1 = 3.14159265358979f * (-0.5f + float(k + 1) / stacks);
                        const float z0 = 0.5f * std::sin(lat0);
                        const float r0 = 0.5f * std::cos(lat0);
                        const float z1 = 0.5f * std::sin(lat1);
                        const float r1 = 0.5f * std::cos(lat1);
                        for (int j = 0; j < sides; ++j)
                        {
                            const float lng0 = 2.0f * 3.14159265358979f * float(j) / float(sides);
                            const float lng1 =
                                2.0f * 3.14159265358979f * float(j + 1) / float(sides);
                            const Vec3 a(r0 * std::cos(lng0), r0 * std::sin(lng0), z0);
                            const Vec3 b(r0 * std::cos(lng1), r0 * std::sin(lng1), z0);
                            const Vec3 c(r1 * std::cos(lng0), r1 * std::sin(lng0), z1);
                            const Vec3 d(r1 * std::cos(lng1), r1 * std::sin(lng1), z1);
                            emit(a, a * 2.0f); emit(c, c * 2.0f); emit(d, d * 2.0f);
                            emit(a, a * 2.0f); emit(d, d * 2.0f); emit(b, b * 2.0f);
                        }
                    }
                }
                else if (marker.type == markerTypeCubes)
                {
                    const Vec3 corners[8] = {
                        Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, -0.5f, -0.5f),
                        Vec3(0.5f, 0.5f, -0.5f),   Vec3(-0.5f, 0.5f, -0.5f),
                        Vec3(-0.5f, -0.5f, 0.5f),  Vec3(0.5f, -0.5f, 0.5f),
                        Vec3(0.5f, 0.5f, 0.5f),    Vec3(-0.5f, 0.5f, 0.5f)};
                    const int faces[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
                                             {1, 5, 6, 2}, {4, 5, 1, 0}, {3, 2, 6, 7}};
                    const Vec3 faceNormals[6] = {Vec3(0.0f, 0.0f, -1.0f),
                                                 Vec3(0.0f, 0.0f, 1.0f),
                                                 Vec3(-1.0f, 0.0f, 0.0f),
                                                 Vec3(1.0f, 0.0f, 0.0f),
                                                 Vec3(0.0f, -1.0f, 0.0f),
                                                 Vec3(0.0f, 1.0f, 0.0f)};
                    for (int f = 0; f < 6; ++f)
                    {
                        const Vec3& n = faceNormals[f];
                        emit(corners[faces[f][0]], n);
                        emit(corners[faces[f][1]], n);
                        emit(corners[faces[f][2]], n);
                        emit(corners[faces[f][0]], n);
                        emit(corners[faces[f][2]], n);
                        emit(corners[faces[f][3]], n);
                    }
                }
                else if (marker.type == markerTypeCylinders)
                {
                    for (int k = 0; k < sides; ++k)
                    {
                        const float a0 = 2.0f * 3.14159265358979f * float(k) / float(sides);
                        const float a1 = 2.0f * 3.14159265358979f * float(k + 1) / float(sides);
                        const Vec3 b0(0.5f * std::cos(a0), 0.5f * std::sin(a0), 0.0f);
                        const Vec3 b1(0.5f * std::cos(a1), 0.5f * std::sin(a1), 0.0f);
                        const Vec3 n0 = Vec3(std::cos(a0), std::sin(a0), 0.0f);
                        const Vec3 n1 = Vec3(std::cos(a1), std::sin(a1), 0.0f);
                        emit(Vec3(b0.x(), b0.y(), 0.5f), n0);
                        emit(Vec3(b1.x(), b1.y(), 0.5f), n1);
                        emit(Vec3(b1.x(), b1.y(), -0.5f), n1);
                        emit(Vec3(b0.x(), b0.y(), 0.5f), n0);
                        emit(Vec3(b1.x(), b1.y(), -0.5f), n1);
                        emit(Vec3(b0.x(), b0.y(), -0.5f), n0);
                        // Caps.
                        emit(Vec3(0.0f, 0.0f, 0.5f), localUp);
                        emit(Vec3(b1.x(), b1.y(), 0.5f), localUp);
                        emit(Vec3(b0.x(), b0.y(), 0.5f), localUp);
                        emit(Vec3(0.0f, 0.0f, -0.5f), localUp * -1.0f);
                        emit(Vec3(b0.x(), b0.y(), -0.5f), localUp * -1.0f);
                        emit(Vec3(b1.x(), b1.y(), -0.5f), localUp * -1.0f);
                    }
                }
            }
        }


    }

    const std::size_t total = m_markerPoints.size() + m_markerLines.size() +
                              m_markerTriangles.size() + m_markerOverlayPoints.size() +
                              m_markerOverlayLines.size() + m_markerOverlayTriangles.size();
    if (total == 0)
        return;

    if (m_markerVao == 0)
        gl.glGenVertexArrays(1, &m_markerVao);

    m_markerBuffer.create(gl);
    gl.glBindVertexArray(m_markerVao);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_markerBuffer.id());
    gl.glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(total * sizeof(MarkerVertex)), nullptr,
                    GL_STREAM_DRAW);

    std::size_t offset = 0;
    const auto upload = [&](const std::vector<MarkerVertex>& source, GLsizei& first) {
        first = GLsizei(offset);
        if (!source.empty())
        {
            gl.glBufferSubData(GL_ARRAY_BUFFER,
                               GLintptr(offset * sizeof(MarkerVertex)),
                               GLsizeiptr(source.size() * sizeof(MarkerVertex)), source.data());
            offset += source.size();
        }
    };

    GLsizei firstPoints = 0, firstLines = 0, firstTriangles = 0;
    GLsizei firstOverlayPoints = 0, firstOverlayLines = 0, firstOverlayTriangles = 0;
    upload(m_markerPoints, firstPoints);
    upload(m_markerLines, firstLines);
    upload(m_markerTriangles, firstTriangles);
    upload(m_markerOverlayPoints, firstOverlayPoints);
    upload(m_markerOverlayLines, firstOverlayLines);
    upload(m_markerOverlayTriangles, firstOverlayTriangles);

    const GLsizei stride = GLsizei(sizeof(MarkerVertex));
    gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    gl.glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                             reinterpret_cast<const void*>(3 * sizeof(float)));
    gl.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                             reinterpret_cast<const void*>(7 * sizeof(float)));
    gl.glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                             reinterpret_cast<const void*>(10 * sizeof(float)));
    gl.glEnableVertexAttribArray(0);
    gl.glEnableVertexAttribArray(1);
    gl.glEnableVertexAttribArray(2);
    gl.glEnableVertexAttribArray(3);

    m_markerProgram.use(gl);
    m_markerProgram.set(gl, m_uMarkerLit, colorCoded ? 0 : 1);

    // Markers always cull their back faces, independently of the settings:
    // their primitives are closed and single sided. Points and lines are not
    // affected by face culling.
    gl.glEnable(GL_CULL_FACE);
    gl.glCullFace(GL_BACK);
    if (colorCoded)
    {
        gl.glDisable(GL_BLEND); // the handle bytes must reach the image untouched
    }
    else
    {
        gl.glEnable(GL_BLEND);
        gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }


    const auto drawBatches = [&](GLsizei pointFirst, GLsizei lineFirst, GLsizei triangleFirst,
                                 std::size_t pointCount2, std::size_t lineCount,
                                 std::size_t triangleCount) {
        if (triangleCount > 0)
        {
            gl.glDrawArrays(GL_TRIANGLES, triangleFirst, GLsizei(triangleCount));
            m_statistics.drawCalls++;
        }
        if (lineCount > 0)
        {
            gl.glDrawArrays(GL_LINES, lineFirst, GLsizei(lineCount));
            m_statistics.drawCalls++;
        }
        if (pointCount2 > 0)
        {
            gl.glPointSize(2.0f);
            gl.glDrawArrays(GL_POINTS, pointFirst, GLsizei(pointCount2));
            m_statistics.drawCalls++;
        }
    };

    drawBatches(firstPoints, firstLines, firstTriangles, m_markerPoints.size(),
                m_markerLines.size(), m_markerTriangles.size());

    if (total > m_markerPoints.size() + m_markerLines.size() + m_markerTriangles.size())
    {
        gl.glDisable(GL_DEPTH_TEST);
        drawBatches(firstOverlayPoints, firstOverlayLines, firstOverlayTriangles,
                    m_markerOverlayPoints.size(), m_markerOverlayLines.size(),
                    m_markerOverlayTriangles.size());
        gl.glEnable(GL_DEPTH_TEST);
    }

    gl.glBindVertexArray(0);
    gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl.glUseProgram(0);
    logGLError(gl, "SceneRenderer::renderMarkers");
}
void SceneRenderer::evictUnusedResources(GLFuncs& gl)
{
    // Anything not referenced for this many frames is gone: the object was
    // deleted, or the identity it was cached under no longer exists.
    constexpr std::uint64_t kEvictAfterFrames = 240;
    if (std::uint64_t(m_frameIndex) < kEvictAfterFrames)
        return;

    const std::uint64_t cutoff = std::uint64_t(m_frameIndex) - kEvictAfterFrames;

    for (auto it = m_geometry.begin(); it != m_geometry.end();)
    {
        if (it->second->lastUsedFrame < cutoff)
        {
            it->second->destroy(gl);
            it = m_geometry.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = m_textures.begin(); it != m_textures.end();)
    {
        if (it->second.lastUsedFrame < cutoff)
        {
            it->second.texture->destroy(gl);
            it = m_textures.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
// ---------------------------------------------------------------------------
// Markers
//
// The engine does not provide marker geometry: it provides the primitive data
// (points, lines, triangles, or a unit mesh plus pose/size/colour per item) and
// its own renderer tessellates it with immediate mode.  This does the same on
// the CPU, into three batches (points, lines, triangles), which keeps the
// marker pass to three draw calls per depth mode.  Marker counts are small
// (annotations, not scene geometry), so rebuilding them every frame is cheap.
// ---------------------------------------------------------------------------

void SceneRenderer::renderFrame(GLFuncs& gl, OffscreenTarget& target, FrameData& frame,
                                unsigned char* rgbTarget, float* depthTarget, bool readRgb,
                                bool readDepth)
{
    m_statistics = Statistics{};
    if (!m_ready || !target.isValid())
        return;

    m_camera = frame.camera;
    m_camera.width = target.width();
    m_camera.height = target.height();

    updateCameraBlock(gl, m_camera);
    updateLightBlock(gl, frame);



    // updateLightBlock() fills in the shadow layers and light-space matrices,
    // so the local copy has to be taken afterwards.
    m_lights = frame.lights;

    buildFrame(gl, frame);

    if (frame.camera.colorCoded)
    {
        // Colour coded frame: the handle bits go straight to the image, so no
        // lighting, no shadows and no transfer function - anything applied here
        // would corrupt the decoded handle.  The background is white, which is
        // what CoppeliaSim's own color coded pass uses for "no object".
        target.beginScene(gl, Vec3(1.0f, 1.0f, 1.0f));

        // beginScene() only binds the target and clears it, and this pass calls
        // drawRuns() directly instead of renderOpaque(), so the depth state has
        // to be established here.  Without it the depth test stays off (the tone
        // map pass leaves it disabled) and everything - markers included - is
        // drawn in submission order, as if every item were an overlay.
        gl.glEnable(GL_DEPTH_TEST);
        gl.glDepthFunc(GL_LEQUAL);
        gl.glDepthMask(GL_TRUE);
        gl.glDisable(GL_BLEND);

        m_colorCodeProgram.use(gl);
        m_colorCodeProgram.set(gl, m_uColorCodeViewProjection, m_camera.viewProjection);

        // Every item, opaque, in submission order.
        drawRuns(gl, m_opaqueRuns, false);
        drawRuns(gl, m_translucentRuns, false);

        renderMarkers(gl, frame);

        target.resolve(gl);
        renderTonemapPassThrough(gl, target);

        if (readRgb && rgbTarget != nullptr)
            target.readColor(gl, m_pixelReader, rgbTarget);
        if (readDepth && depthTarget != nullptr)
        {
            target.readDepth(gl, m_pixelReader, depthTarget);
            lineariseDepth(depthTarget, m_camera.width, m_camera.height);
        }
        return;
    }

    renderShadowMaps(gl);

    target.beginScene(gl, m_camera.backgroundColor);

    if (m_settings.depthPrepass)
        renderDepthPrepass(gl);

    renderOpaque(gl);
    renderTranslucent(gl);

    // Markers go last, into the multisampled target like everything else, so
    // overlay markers are drawn on top of the resolved scene.
    renderMarkers(gl, frame);

    target.resolve(gl);
    renderTonemap(gl, target);

    if (readRgb && rgbTarget != nullptr)
        target.readColor(gl, m_pixelReader, rgbTarget);

    if (readDepth && depthTarget != nullptr)
    {
        target.readDepth(gl, m_pixelReader, depthTarget);
        if (m_camera.perspective)
            lineariseDepth(depthTarget, target.width(), target.height());
    }

    m_statistics.lights = int(m_gpuLights.size());

    // Keep the caches honest: geometry stamps itself on upload, textures have to
    // be stamped from the items that use them, since they are only uploaded once.
    for (const DrawItem& item : frame.items)
    {
        if (!item.textured)
            continue;
        const auto it = m_textures.find(item.textureId);
        if (it != m_textures.end())
            it->second.lastUsedFrame = std::uint64_t(m_frameIndex);
    }
    evictUnusedResources(gl);

    // glGetError() can flush the pipeline on some drivers, so the drain is only
    // done for the first frames, where a driver problem would show up.
    if (m_frameIndex++ < kErrorSweepFrames)
        logGLError(gl, "SceneRenderer::renderFrame");
}

} // namespace modern







