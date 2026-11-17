// Frame orchestration for the modern renderer.
//
// One SceneRenderer owns every GL object that survives between frames: the
// shader programs, the geometry and texture caches, the shadow atlas and the
// uniform buffers.  It is used by exactly one GL context (the plugin creates a
// single context for all views), which is a large simplification compared to
// the legacy plugin: that one created a context, a shader program and a full
// set of caches per camera and per vision sensor.

#pragma once
#include <cstdint>

#include "glUtil.h"
#include "offscreenTarget.h"
#include "shadowAtlas.h"
#include "sceneTypes.h"
#include "renderSettings.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace modern
{

struct CameraData
{
    int width = 1;
    int height = 1;
    bool perspective = true;
    float viewAngle = 1.0f;
    float orthoViewSize = 1.0f;
    float nearPlane = 0.01f;
    float farPlane = 10.0f;

    Mat4 view;
    Mat4 projection;
    Mat4 viewProjection;

    Vec3 eye;
    Vec3 ambient;
    Vec3 backgroundColor;
    Vec3 fogColor;

    int fogType = simFogLinear;
    float fogStart = 0.0f;
    float fogEnd = 1.0f;
    float fogDensity = 0.0f;
    bool fogEnabled = false;
    bool colorCoded = false; // write object handles instead of shaded colour
};

// One marker object, copied out of the ext-renderer callback.  The engine hands
// out pointers into the marker object that are only valid during the callback,
// so everything is copied before it is used.
//
// Layout as sent by CoppeliaSim.  The optional arrays are sized *per type*, not
// per point, and over-reading them is what made large marker sets crash, so the
// handler copies exactly what the type defines:
//   points   3 floats per point, already in world space unless the options carry
//            sim_markeropts_local, in which case they are relative to `transform`
//   colors   4 bytes per point, for every type except axes (which carry none)
//   quats    4 floats per item, only for the one-point primitives and custom
//   sizes    3 floats per item for those same types, but a tube carries a single
//            size triple whose first float is the radius
//   custom   data[8]/[9] vertices (3 floats per point), data[10]/[11] indices
//            (3 per triangle, zero based into the vertices), data[12] optional
//            normals, 3 floats per index, i.e. one per triangle corner
struct MarkerItem
{
    int type = 0;
    int options = 0;
    int handle = 0; // marker object handle, encoded in colour coded frames
    Mat4 transform; // marker pose (always applied)

    std::vector<float> points;
    std::vector<float> quats;
    std::vector<unsigned char> colors;
    std::vector<float> sizes;

    // sim_markertype_custom only.
    std::vector<float> vertices;
    std::vector<int> indices;
    std::vector<float> normals;

    bool isOverlay() const { return (options & markerOptionOverlay) != 0; }
    bool isNotShaded() const { return (options & markerOptionNotShaded) != 0; }
};

// A point cloud as the engine reports it: positions in the object's frame and an
// optional rgba colour per point.
struct PointCloudItem
{
    std::vector<float> positions;      // 3 floats per point
    std::vector<unsigned char> colors; // 4 bytes per point, empty when unset
    float pointSize = 1.0f;            // pixels
    Mat4 transform;                    // object pose
    int handle = 0;
};

// An oc-tree as the engine reports it: one cube of `voxelSize` per point, in the
// object's frame.
struct OctreeItem
{
    std::vector<float> positions;      // 3 floats per voxel centre
    std::vector<unsigned char> colors; // 4 bytes per voxel, empty when unset
    float voxelSize = 0.05f;
    Mat4 transform; // object pose
    int handle = 0;
};

struct FrameData
{
    CameraData camera;
    std::vector<LightShadowInfo> lights;
    std::vector<DrawItem> items;
    std::vector<MarkerItem> markers;

    // Point clouds and oc-trees: fetched from the engine by the plugin, drawn
    // after the shapes and before the markers.
    std::vector<PointCloudItem> pointClouds;
    std::vector<OctreeItem> octrees;
};

class SceneRenderer
{
public:
    SceneRenderer() = default;
    ~SceneRenderer() = default;
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    bool initialize(GLFuncs& gl, const RenderSettings& settings);
    void shutdown(GLFuncs& gl);
    bool isReady() const { return m_ready; }
    const RenderSettings& settings() const { return m_settings; }

    // --- resource caches ---------------------------------------------------
    bool hasGeometry(std::uint64_t meshKey) const;
    bool hasTexture(int textureId) const;

    // The arrays follow CoppeliaSim's layout, which is *not* uniform:
    //   positions : one per unique vertex, addressed through `indices`
    //   normals   : one per *triangle corner*, in index order
    //   texCoords : one per *triangle corner* as well
    // CMesh::pushGenesisOrChangeEvent() sizes the normals from
    // _indices.size(), and CTextureProperty::getTextureCoordinates() sizes the
    // texture coordinates from triangles.size().  The counts are passed in so
    // that a per-vertex layout would still be handled correctly.
    void uploadGeometry(GLFuncs& gl, std::uint64_t meshKey, const float* positions, int vertexCount,
                        const float* normals, int normalCount, const float* texCoords,
                        int texCoordCount, const int* indices, int indexCount);

    void uploadTexture(GLFuncs& gl, int textureId, const unsigned char* rgba, int width,
                       int height, bool repeatU, bool repeatV, bool interpolate);

    void releaseCachedResources(GLFuncs& gl);

    // --- frame -------------------------------------------------------------
    // Renders `frame` into `target` and reads the requested buffers back into
    // `rgbTarget` / `depthTarget` (which are owned by CoppeliaSim).
    void renderFrame(GLFuncs& gl, OffscreenTarget& target, FrameData& frame,
                     unsigned char* rgbTarget, float* depthTarget, bool readRgb,
                     bool readDepth);

    struct Statistics
    {
        int drawCalls = 0;
        int shadowDrawCalls = 0;
        int instances = 0;
        int lights = 0;
        int shadowLayers = 0;
    };
    const Statistics& statistics() const { return m_statistics; }

private:
    struct Geometry
    {
        GpuBuffer vertexBuffer;
        GLuint vao = 0;
        int vertexCount = 0; // corners, since the buffer is expanded per corner
        bool hasUv = false;
        Vec3 boundsCenter;
        float boundsRadius = 0.0f;

        // Content signature and last frame this geometry was referenced, used to
        // skip re-uploads and to evict entries whose object disappeared.
        std::uint64_t contentHash = 0;
        std::uint64_t lastUsedFrame = 0;

        void destroy(GLFuncs& gl)
        {
            if (vao != 0)
            {
                gl.glDeleteVertexArrays(1, &vao);
                vao = 0;
            }
            vertexBuffer.destroy(gl);
        }
    };

    struct Run
    {
        int startInstance = 0;
        int instanceCount = 0;
        const Geometry* geometry = nullptr;
        int textureId = 0;
        int applyMode = simTextureModulate;
        bool cullBackFaces = true;

    };

    struct Frustum
    {
        Vec4 planes[6];
        void build(const Mat4& viewProjection);
        bool intersects(const Vec3& center, float radius) const;
    };

    const Geometry* findGeometry(std::uint64_t meshKey) const;

    // Tessellates the frame's markers and draws them: points, lines, triangles,
    // tubes and the instanced primitives, split into a normal and an overlay
    // batch (overlay markers ignore the depth buffer).
    void renderMarkers(GLFuncs& gl, const FrameData& frame);
    // Point clouds and oc-tree voxels: unlit, drawn after the shapes.
    void renderPointsAndVoxels(GLFuncs& gl, const FrameData& frame);

    // Drops geometry and textures that have not been referenced for a while, so
    // deleted objects (and objects whose identity the engine regenerates) cannot
    // make the caches grow without bound.
    void evictUnusedResources(GLFuncs& gl);
    GLuint textureFor(int textureId) const;

    void updateCameraBlock(GLFuncs& gl, const CameraData& camera);
    bool updateLightBlock(GLFuncs& gl, FrameData& frame);

    void buildFrame(GLFuncs& gl, FrameData& frame);
    InstanceData buildInstance(const DrawItem& item);
    void renderShadowMaps(GLFuncs& gl);
    void renderOpaque(GLFuncs& gl);
    void renderTranslucent(GLFuncs& gl);
    void renderDepthPrepass(GLFuncs& gl);
    void renderTonemap(GLFuncs& gl, OffscreenTarget& target);
    // Colour coded frames skip exposure, tone mapping and the sRGB encode.
    void renderTonemapPassThrough(GLFuncs& gl, OffscreenTarget& target);

    void drawRuns(GLFuncs& gl, const std::vector<Run>& runs, bool shadowPass);
    void bindRunAttributes(GLFuncs& gl, const Run& run);



private:
    RenderSettings m_settings;
    bool m_ready = false;

    GpuProgram m_sceneProgram;
    GpuProgram m_depthProgram;
    GpuProgram m_depthOmniProgram;
    GpuProgram m_tonemapProgram;
    GpuProgram m_markerProgram;
    GpuProgram m_colorCodeProgram;
    GpuProgram m_pointProgram;
    GpuProgram m_voxelProgram;
    GpuBuffer m_pointBuffer; // interleaved position + colour
    GpuBuffer m_voxelBuffer; // per instance: centre, size, colour
    GLuint m_voxelVertexArray = 0;
    GLuint m_pointVertexArray = 0;
    GpuBuffer m_voxelVertexBuffer; // unit cube
    GLsizei m_voxelVertexCount = 0;
    GLuint m_emptyVao = 0;

    // Cached uniform locations, resolved once after linking.  A location is
    // only valid for the program that owns it, so the depth pass keeps one per
    // program even though both declare the same uniform name.
    int m_uLightSpaceMatrix = -1;
    int m_uDepthClipPlanes = -1;
    int m_uLightSpaceMatrixOmni = -1;
    int m_uLightPosition = -1;
    int m_uNearPlane = -1;
    int m_uFarPlane = -1;
    int m_uApplyMode = -1;
    int m_uPcfRadius = -1;
    int m_uShadowEnabled = -1;
    int m_uAlbedo = -1;
    int m_uShadowMap = -1;
    int m_uHdrColor = -1;
    int m_uExposure = -1;
    int m_uToneMapping = -1;
    int m_uGamma = -1;
    int m_uColorCodeViewProjection = -1;
    int m_uColorCodeClipPlanes = -1;
    int m_uPointModel = -1;
    int m_uPointSize = -1;
    int m_uVoxelModel = -1;
    int m_uMarkerLit = -1;

    GpuBuffer m_cameraBuffer;
    GpuBuffer m_lightBuffer;
    GpuBuffer m_instanceBuffer;
    GpuBuffer m_markerBuffer;
    GLuint m_markerVao = 0;

    ShadowAtlas m_shadowAtlas;
    GpuTexture m_whiteTexture;
    PixelReader m_pixelReader;

    std::unordered_map<std::uint64_t, std::unique_ptr<Geometry>> m_geometry;

    struct TextureEntry
    {
        std::unique_ptr<GpuTexture> texture;
        std::uint64_t lastUsedFrame = 0;
    };
    std::unordered_map<int, TextureEntry> m_textures;

    std::vector<InstanceData> m_instances;
    std::vector<Run> m_opaqueRuns;
    std::vector<Run> m_translucentRuns;
    std::vector<Run> m_shadowRuns;

    // Marker batches, rebuilt every frame: three vertex formats (points, lines,
    // triangles) times normal/overlay.
    struct MarkerVertex
    {
        float position[3];
        float color[4];
        float normal[3];
        float lit; // 0 for sim_markeropts_notshaded, 1 otherwise
    };
    std::vector<MarkerVertex> m_markerPoints;
    std::vector<MarkerVertex> m_markerLines;
    std::vector<MarkerVertex> m_markerTriangles;
    std::vector<MarkerVertex> m_markerOverlayPoints;
    std::vector<MarkerVertex> m_markerOverlayLines;
    std::vector<MarkerVertex> m_markerOverlayTriangles;

    // Per-frame state used by the passes.
    CameraData m_camera;
    long long m_frameIndex = 0;
    std::vector<LightShadowInfo> m_lights;
    std::vector<GpuLight> m_gpuLights;

    Statistics m_statistics;
};

} // namespace modern


