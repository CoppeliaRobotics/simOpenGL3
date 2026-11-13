// Stand-alone smoke test for the modern renderer.
//
// It compiles the real shaders, creates the real off-screen target, renders a
// couple of instanced cubes with a shadow casting light and writes the result
// to smoke.png.  This is a development tool: it catches GLSL errors, layout
// mismatches and framebuffer problems without having to start CoppeliaSim.
//
// No Qt and no window system: the context comes from the same platform backend
// the plugin uses, which is what makes this runnable on a headless build slave.
//
// Build:  qmake && nmake -f Makefile.Release   (or: make -f Makefile.Release)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "glUtil.h"
#include "log.h"
#include "math3d.h"
#include "offscreenContext.h"
#include "offscreenTarget.h"
#include "pngWriter.h"
#include "renderSettings.h"
#include "sceneRenderer.h"
#include "sceneTypes.h"

using namespace modern;

namespace
{

// Mirrors CoppeliaSim's layout exactly: 8 shared positions addressed through
// the index list, but normals and texture coordinates per *triangle corner*
// (36 of each) in index order.  Using 8 positions is what makes this test
// meaningful: with the positions deduplicated, a renderer that indexed the
// normals by vertex instead of by corner would read the wrong values and shade
// the faces incorrectly.
void buildCube(std::vector<float>& positions, std::vector<float>& normals,
               std::vector<float>& uvs, std::vector<int>& indices)
{
    const Vec3 corners[8] = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
    };

    struct Face
    {
        Vec3 normal;
        int corner[4];
    };

    // Counter-clockwise when seen from outside.
    const Face faces[6] = {
        {{0, 0, 1}, {4, 5, 6, 7}},
        {{0, 0, -1}, {1, 0, 3, 2}},
        {{1, 0, 0}, {5, 1, 2, 6}},
        {{-1, 0, 0}, {0, 4, 7, 3}},
        {{0, 1, 0}, {7, 6, 2, 3}},
        {{0, -1, 0}, {0, 1, 5, 4}},
    };

    const Vec2 texCoords[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const int localIndices[6] = {0, 1, 2, 0, 2, 3};

    for (const Vec3& corner : corners)
    {
        positions.push_back(corner.x());
        positions.push_back(corner.y());
        positions.push_back(corner.z());
    }

    for (const Face& face : faces)
    {
        // normals and uvs: one per corner, in index order
        for (int k = 0; k < 6; ++k)
        {
            const int i = localIndices[k];
            indices.push_back(face.corner[i]);
            normals.push_back(face.normal.x());
            normals.push_back(face.normal.y());
            normals.push_back(face.normal.z());
            uvs.push_back(texCoords[i].x());
            uvs.push_back(texCoords[i].y());
        }
    }
}

CameraData makeCamera(int width, int height, const Vec3& eye, const Vec3& target, const Vec3& up,
                      bool perspective, float verticalFovDegrees)
{
    CameraData camera;
    camera.width = width;
    camera.height = height;
    camera.perspective = perspective;
    camera.nearPlane = 0.1f;
    camera.farPlane = 50.0f;
    camera.eye = eye;
    camera.ambient = Vec3(0.15f, 0.15f, 0.18f);
    camera.backgroundColor = Vec3(0.06f, 0.07f, 0.09f);
    camera.fogEnabled = false;

    camera.view.setToIdentity();
    camera.view.lookAt(eye, target, up);

    camera.projection.setToIdentity();
    const float ratio = float(width) / float(height);
    if (perspective)
        camera.projection.perspective(verticalFovDegrees, ratio, camera.nearPlane,
                                      camera.farPlane);
    else
        camera.projection.ortho(-4.0f, 4.0f, -4.0f / ratio, 4.0f / ratio, camera.nearPlane,
                                camera.farPlane);

    camera.viewProjection = camera.projection * camera.view;
    return camera;
}

LightShadowInfo makeDirectionalLight()
{
    LightShadowInfo light;
    light.type = simLightDirectional;
    light.position = Vec3(2.0f, 6.0f, 3.0f);

    // The light's +Z axis is the propagation direction (CoppeliaSim sets
    // GL_POSITION to -axis[2] for directional lights), so pointing +Z down
    // makes the light shine down onto the ground plane.
    Mat4 pose;
    pose.translate(light.position);
    pose.rotate(115.0f, 1.0f, 0.0f, 0.0f);
    pose.rotate(25.0f, 0.0f, 1.0f, 0.0f);
    light.pose = pose;
    light.direction = Vec3(pose(0, 2), pose(1, 2), pose(2, 2));

    light.diffuse = Vec3(1.0f, 0.97f, 0.9f);
    light.specular = Vec3(1.0f, 1.0f, 1.0f);
    light.constantAttenuation = 1.0f;
    light.linearAttenuation = 0.0f;
    light.quadraticAttenuation = 0.0f;
    light.cutoffAngle = 0.5f;
    light.nearPlane = 0.1f;
    light.farPlane = 30.0f;
    light.orthoSize = 12.0f;
    light.bias = 0.001f;
    light.normalBias = 0.01f;
    light.textureSize = 1024;
    return light;
}

LightShadowInfo makePointLight()
{
    LightShadowInfo light;
    light.type = simLightOmnidirectional;
    light.position = Vec3(-0.6f, 2.2f, -0.4f);
    light.pose.setToIdentity();
    light.pose.translate(light.position);
    light.direction = Vec3(0.0f, 0.0f, 1.0f);
    light.diffuse = Vec3(0.35f, 0.55f, 1.0f);
    light.specular = Vec3(0.8f, 0.9f, 1.0f);
    light.constantAttenuation = 1.0f;
    light.linearAttenuation = 0.14f;
    light.quadraticAttenuation = 0.07f;
    light.nearPlane = 0.05f;
    light.farPlane = 15.0f;
    light.bias = 0.001f;
    light.normalBias = 0.02f;
    light.textureSize = 1024;
    return light;
}

// The rendered image is bottom-up (OpenGL order); PNG is top-down.
void writeFlippedPng(const char* path, const std::vector<unsigned char>& rgb, int width,
                     int height)
{
    const std::size_t stride = std::size_t(width) * 3;
    std::vector<unsigned char> flipped(std::size_t(height) * stride);
    for (int y = 0; y < height; ++y)
    {
        const unsigned char* source = rgb.data() + std::size_t(height - 1 - y) * stride;
        std::copy(source, source + stride, flipped.begin() + std::ptrdiff_t(y) * stride);
    }
    if (!writePng(path, flipped.data(), width, height))
        std::printf("warning: cannot write %s\n", path);
}

int countNonBackground(const std::vector<unsigned char>& rgb, int width, int height)
{
    int count = 0;
    for (int i = 0; i < width * height; ++i)
    {
        const unsigned char* texel = &rgb[std::size_t(i) * 3];
        if (texel[0] > 30 || texel[1] > 30 || texel[2] > 30)
            ++count;
    }
    return count;
}

} // namespace

int main(int, char**)
{
    RenderSettings settings;
    settings.msaaSamples = 4;
    settings.shadowMapSize = 1024;

    OffscreenContext context;
    if (!context.ensureCreated())
    {
        logError("cannot create the off-screen context");
        return 1;
    }
    std::printf("OpenGL %s (%s)\n", context.versionString().c_str(),
                context.rendererString().c_str());

    if (!context.supportsRequiredVersion())
    {
        logError("OpenGL 3.3 core required");
        return 2;
    }
    if (!context.makeCurrent())
        return 3;

    GLFuncs* gl = context.functions();

    SceneRenderer renderer;
    if (!renderer.initialize(*gl, settings))
    {
        logError("renderer initialisation failed");
        return 4;
    }

    OffscreenTarget target;
    const int width = 480;
    const int height = 320;
    if (!target.create(*gl, width, height, settings))
    {
        logError("cannot create the render target");
        return 5;
    }

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<int> indices;
    buildCube(positions, normals, uvs, indices);
    renderer.uploadGeometry(*gl, 1, positions.data(), int(positions.size() / 3), normals.data(),
                            int(normals.size() / 3), uvs.data(), int(uvs.size() / 2),
                            indices.data(), int(indices.size()));

    // A ground plane as a second, flat piece of geometry.
    const float groundPositions[] = {
        -6.0f, 0.0f, -6.0f, 6.0f, 0.0f, -6.0f, 6.0f, 0.0f, 6.0f, -6.0f, 0.0f, 6.0f,
    };
    const float groundNormals[] = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    const float groundUvs[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1};
    const int groundIndices[] = {0, 2, 1, 0, 3, 2};
    renderer.uploadGeometry(*gl, 2, groundPositions, 4, groundNormals, 6, groundUvs, 6,
                            groundIndices, 6);

    FrameData frame;
    frame.camera = makeCamera(width, height, Vec3(5.5f, 4.5f, 6.5f), Vec3(0.0f, 0.9f, 0.0f),
                              Vec3(0.0f, 1.0f, 0.0f), true, 45.0f);
    frame.lights.push_back(makeDirectionalLight());
    frame.lights.push_back(makePointLight());

    // A marker of every family, so the marker pass is exercised: points, lines,
    // a tube, triangles, the unit primitives, axes and a custom mesh.
    {
        const auto add = [&frame](int type, std::initializer_list<float> points,
                                  std::initializer_list<unsigned char> colors,
                                  std::initializer_list<float> sizes, bool overlay) {
            MarkerItem marker;
            marker.type = type;
            marker.options = (overlay ? markerOptionOverlay : 0);
            marker.transform.setToIdentity();
            marker.points.assign(points.begin(), points.end());
            marker.colors.assign(colors.begin(), colors.end());
            marker.sizes.assign(sizes.begin(), sizes.end());
            const std::size_t itemCount = marker.points.size() / 3;
            marker.quats.assign(itemCount * 4, 0.0f);
            for (std::size_t i = 0; i < itemCount; ++i)
                marker.quats[i * 4 + 3] = 1.0f; // identity quaternion
            frame.markers.push_back(std::move(marker));
        };

        add(markerTypePoints, {2.0f, 2.0f, 2.0f}, {255, 255, 0, 255}, {}, false);
        add(markerTypeLines, {2.0f, 0.0f, 0.0f, 2.0f, 0.0f, -2.0f},
            {255, 0, 0, 255, 0, 255, 0, 255}, {}, false);
        add(markerTypeTubes, {-2.0f, 0.0f, 0.0f, -2.0f, 2.0f, 0.0f}, {0, 255, 255, 255},
            {0.1f, 0.0f, 0.0f}, false);
        add(markerTypeTriangles, {0.0f, 2.0f, 0.0f, 0.8f, 2.0f, 0.5f, -0.8f, 2.0f, 0.5f},
            {255, 0, 255, 255, 0, 255, 255, 255, 255, 255, 0, 255}, {}, false);
        add(markerTypeCubes, {2.5f, 0.5f, 0.0f}, {255, 128, 0, 255}, {0.5f, 0.5f, 0.5f}, false);

        // Same cube again, flat shaded: the two should read differently.
        {
            MarkerItem flat;
            flat.type = markerTypeSpheres;
            flat.options = markerOptionNotShaded;
            flat.transform.setToIdentity();
            flat.points = {2.5f, 1.5f, 1.5f};
            flat.quats = {0.0f, 0.0f, 0.0f, 1.0f};
            flat.colors = {255, 64, 64, 255};
            flat.sizes = {0.5f, 0.5f, 0.5f};
            frame.markers.push_back(std::move(flat));
        }
        add(markerTypeSpheres, {-2.5f, 0.5f, 1.5f}, {0, 128, 255, 255}, {0.4f, 0.4f, 0.4f}, false);
        add(markerTypeCylinders, {0.0f, 0.5f, 2.5f}, {128, 0, 255, 255}, {0.4f, 0.4f, 0.8f}, false);
        add(markerTypeDiscs, {2.0f, 0.0f, 2.0f}, {0, 255, 128, 255}, {0.6f, 0.6f, 0.6f}, false);
        add(markerTypeSquares, {-2.0f, 0.0f, -2.0f}, {255, 255, 255, 255},
            {0.6f, 0.6f, 0.6f}, false);
        add(markerTypeAxes, {0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}, false);
        add(markerTypePoints, {1.5f, 3.0f, 1.5f}, {255, 255, 0, 255}, {}, true); // overlay

        // Custom mesh: a unit tetrahedron.
        MarkerItem custom;
        custom.type = markerTypeCustom;
        custom.options = 0;
        // Non-identity frame: a 90 degree turn about Z and an offset.  The
        // tetrahedron hangs at the frame's origin, so a transform applied twice
        // (or not at all) would put it in the wrong place.
        custom.transform.setToIdentity();
        custom.transform.translate(-2.2f, 2.2f, -1.0f);
        custom.transform.rotate(90.0f, 0.0f, 0.0f, 1.0f);
        custom.points = {0.0f, 0.0f, 0.0f};
        custom.quats = {0.0f, 0.0f, 0.0f, 1.0f};
        custom.colors = {255, 0, 128, 255};
        custom.sizes = {0.8f, 0.8f, 0.8f};
        custom.vertices = {0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.0f, -0.5f, 0.5f,
                           0.0f, 0.5f, 0.0f};
        custom.indices = {0, 1, 2, 0, 1, 3, 1, 2, 3, 0, 2, 3};
        frame.markers.push_back(std::move(custom));

        // Edge cases: no colours at all (axes), no sizes (lines/triangles),
        // a custom mesh without normals, a non-local marker, and flat shading
        // on every type that produces triangles.
        {
            MarkerItem axes;
            axes.type = markerTypeAxes;
            axes.options = 0;
            axes.transform.setToIdentity();
            axes.points = {1.0f, 1.0f, 1.0f};
            axes.quats = {0.0f, 0.0f, 0.0f, 1.0f};
            axes.sizes = {0.7f, 0.7f, 0.7f};
            frame.markers.push_back(std::move(axes));
        }
        {
            MarkerItem tubes;
            tubes.type = markerTypeTubes;
            tubes.options = 0; // no sizes at all, pose supplied anyway
            tubes.transform.setToIdentity();
            tubes.points = {0.5f, 2.0f, 0.5f, 1.5f, 2.0f, 0.5f};
            tubes.colors = {10, 200, 220, 255, 10, 200, 220, 255};
            frame.markers.push_back(std::move(tubes));
        }
        {
            MarkerItem plain; // options == 0: pose must be ignored
            plain.type = markerTypeSquares;
            plain.options = 0;
            plain.points = {1.0f, 0.0f, 1.0f};
            plain.quats = {0.0f, 0.0f, 0.0f, 1.0f};
            plain.colors = {200, 200, 0, 255};
            plain.sizes = {0.5f, 0.5f, 0.5f};
            frame.markers.push_back(std::move(plain));
        }
        {
            MarkerItem flat;
            flat.type = markerTypeCylinders;
            flat.options = markerOptionNotShaded;
            flat.transform.setToIdentity();
            flat.points = {-1.5f, 1.0f, 1.5f};
            flat.quats = {0.0f, 0.0f, 0.0f, 1.0f};
            flat.colors = {90, 220, 90, 255};
            flat.sizes = {0.4f, 0.4f, 0.7f};
            frame.markers.push_back(std::move(flat));
        }
        {
            MarkerItem customNoNormals;
            customNoNormals.type = markerTypeCustom;
            customNoNormals.options = markerOptionNotShaded;
            customNoNormals.transform.setToIdentity();
            customNoNormals.points = {-1.0f, 1.5f, -1.0f};
            customNoNormals.quats = {0.0f, 0.0f, 0.0f, 1.0f};
            customNoNormals.colors = {255, 255, 255, 255};
            customNoNormals.sizes = {0.6f, 0.6f, 0.6f};
            customNoNormals.vertices = {0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
                                        0.0f, -0.5f, 0.5f, 0.0f, 0.5f, 0.0f};
            customNoNormals.indices = {0, 1, 2, 0, 1, 3, 1, 2, 3, 0, 2, 3};
            frame.markers.push_back(std::move(customNoNormals));
        }
    }

    const auto addInstance = [&frame](int meshId, const Vec3& position, const Vec3& scale,
                                      float rotationDegrees, const Vec3& diffuse,
                                      const Vec3& specular, bool translucid, float opacity) {
        DrawItem item;
        item.meshKey = std::uint64_t(meshId);
        item.textureId = 0;
        item.textured = false;
        item.applyMode = simTextureModulate;
        item.cullBackFaces = true;
        item.translucid = translucid;
        item.opacity = opacity;

        item.model.setToIdentity();
        item.model.translate(position);
        item.model.rotate(rotationDegrees, 0.0f, 1.0f, 0.0f);
        item.model.scale(scale);
        item.normalMatrix = item.model.normalMatrix();

        item.material.diffuse = diffuse;
        item.material.specular = specular;
        item.material.shininess = 48.0f;

        item.viewDepth = -(frame.camera.view * Vec4(position, 1.0f)).z();
        frame.items.push_back(item);
    };

    // Mesh 2 is the ground plane and is not a cube: draw it unrotated.
    addInstance(2, Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), 0.0f, Vec3(0.55f, 0.55f, 0.6f),
                Vec3(0.05f, 0.05f, 0.05f), false, 1.0f);

    for (int i = 0; i < 5; ++i)
    {
        const float angle = float(i) * 1.1f;
        addInstance(1, Vec3(std::cos(angle) * 1.9f, 0.55f, std::sin(angle) * 1.9f),
                    Vec3(0.55f, 0.55f, 0.55f), float(i) * 17.0f, Vec3(0.85f, 0.35f, 0.25f),
                    Vec3(0.9f, 0.9f, 0.9f), false, 1.0f);
    }

    // One translucent cube on top of the pile.
    addInstance(1, Vec3(0.0f, 1.9f, 0.0f), Vec3(0.7f, 0.7f, 0.7f), 30.0f, Vec3(0.3f, 0.6f, 0.95f),
                Vec3(1.0f, 1.0f, 1.0f), true, 0.45f);

    std::vector<unsigned char> rgb(std::size_t(width) * std::size_t(height) * 3, 0);
    std::vector<float> depth(std::size_t(width) * std::size_t(height), 1.0f);

    renderer.renderFrame(*gl, target, frame, rgb.data(), depth.data(), true, true);

    const SceneRenderer::Statistics& stats = renderer.statistics();
    std::printf("draw calls %d, shadow draw calls %d, instances %d, lights %d, shadow layers %d\n",
                stats.drawCalls, stats.shadowDrawCalls, stats.instances, stats.lights,
                stats.shadowLayers);

    writeFlippedPng("smoke.png", rgb, width, height);

    const int nonBackground = countNonBackground(rgb, width, height);
    float minimumDepth = 1.0f;
    for (float value : depth)
        minimumDepth = std::min(minimumDepth, value);

    std::printf("centre pixel %u %u %u\n", rgb[std::size_t(height / 2) * width * 3 +
                                                  std::size_t(width / 2) * 3],
                rgb[std::size_t(height / 2) * width * 3 + std::size_t(width / 2) * 3 + 1],
                rgb[std::size_t(height / 2) * width * 3 + std::size_t(width / 2) * 3 + 2]);
    std::printf("non background pixels %d of %d\n", nonBackground, width * height);
    std::printf("closest linear depth %.6f\n", minimumDepth);

    // ---- second scenario: orthographic camera, no MSAA, depth prepass ------
    RenderSettings alternativeSettings = settings;
    alternativeSettings.msaaSamples = 0;
    alternativeSettings.depthPrepass = true;
    alternativeSettings.frustumCulling = false;
    alternativeSettings.toneMapping = ToneMapping::Aces;
    alternativeSettings.exposure = 1.2f;

    SceneRenderer alternativeRenderer;
    if (!alternativeRenderer.initialize(*gl, alternativeSettings))
    {
        logError("second renderer initialisation failed");
        return 8;
    }

    OffscreenTarget alternativeTarget;
    if (!alternativeTarget.create(*gl, width, height, alternativeSettings))
    {
        logError("second render target failed");
        return 9;
    }

    alternativeRenderer.uploadGeometry(*gl, 1, positions.data(), int(positions.size() / 3),
                                       normals.data(), int(normals.size() / 3), uvs.data(),
                                       int(uvs.size() / 2), indices.data(), int(indices.size()));
    alternativeRenderer.uploadGeometry(*gl, 2, groundPositions, 4, groundNormals, 6, groundUvs, 6,
                                       groundIndices, 6);

    FrameData alternativeFrame = frame;
    alternativeFrame.camera = makeCamera(width, height, Vec3(0.0f, 10.0f, 10.0f), Vec3(0.0f, 0.0f, 0.0f),
                                         Vec3(0.0f, 1.0f, 0.0f), false, 45.0f);

    std::vector<unsigned char> alternativeRgb(std::size_t(width) * std::size_t(height) * 3, 0);
    alternativeRenderer.renderFrame(*gl, alternativeTarget, alternativeFrame,
                                    alternativeRgb.data(), nullptr, true, false);

    writeFlippedPng("smokeOrtho.png", alternativeRgb, width, height);

    const int alternativeNonBackground = countNonBackground(alternativeRgb, width, height);
    std::printf("ortho / no-MSAA / prepass: non background pixels %d of %d\n",
                alternativeNonBackground, width * height);

    alternativeRenderer.shutdown(*gl);

    // ---- third scenario: colour coded frame --------------------------------
    // The handle bits must reach the read-back image unchanged, which is what a
    // vision sensor in "color coded" mode decodes.
    RenderSettings codedSettings = settings;
    codedSettings.shadowsEnabled = false;

    SceneRenderer codedRenderer;
    if (!codedRenderer.initialize(*gl, codedSettings))
        return 11;

    OffscreenTarget codedTarget;
    if (!codedTarget.create(*gl, width, height, codedSettings))
        return 12;

    codedRenderer.uploadGeometry(*gl, 2, groundPositions, 4, groundNormals, 6, groundUvs, 6,
                                 groundIndices, 6);

    FrameData codedFrame;
    codedFrame.camera = makeCamera(width, height, Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f),
                                   Vec3(0.0f, 0.0f, 1.0f), true, 45.0f);
    codedFrame.camera.colorCoded = true;

    const int codedHandle = 0x123456;
    DrawItem quad;
    quad.meshKey = 2;
    quad.textureId = 0;
    quad.textured = false;
    quad.applyMode = simTextureModulate;
    quad.cullBackFaces = true;
    quad.handle = codedHandle;
    quad.model.setToIdentity();
    quad.normalMatrix = quad.model.normalMatrix();
    quad.viewDepth = 0.0f;
    codedFrame.items.push_back(quad);

    std::vector<unsigned char> codedRgb(std::size_t(width) * std::size_t(height) * 3, 0);
    codedRenderer.renderFrame(*gl, codedTarget, codedFrame, codedRgb.data(), nullptr, true, false);

    const std::size_t codedCentre = (std::size_t(height) / 2 * width + width / 2) * 3;
    const unsigned char expectedR = static_cast<unsigned char>(codedHandle & 0xFF);
    const unsigned char expectedG = static_cast<unsigned char>((codedHandle >> 8) & 0xFF);
    const unsigned char expectedB = static_cast<unsigned char>((codedHandle >> 16) & 0xFF);
    std::printf("colour coded centre pixel %u %u %u (expect %u %u %u)\n", codedRgb[codedCentre],
                codedRgb[codedCentre + 1], codedRgb[codedCentre + 2], expectedR, expectedG,
                expectedB);

    if (codedRgb[codedCentre] != expectedR || codedRgb[codedCentre + 1] != expectedG ||
        codedRgb[codedCentre + 2] != expectedB)
    {
        logError("colour coded frame did not encode the mesh handle");
        return 13;
    }

    // Same frame, but with a marker covering the middle: markers must be colour
    // coded with their own handle too.
    {
        FrameData markerFrame;
        markerFrame.camera = codedFrame.camera;

        MarkerItem square;
        square.type = markerTypeSquares;
        square.handle = 0x00C0DE;
        square.points = {0.0f, 0.0f, 0.0f};
        // (x, y, z, w), the layout the callback uses: -90 deg about X, so the
        // square (which lies in local XY) faces the camera from above.
        square.quats = {-0.70710678f, 0.0f, 0.0f, 0.70710678f};
        square.sizes = {40.0f, 40.0f, 1.0f};
        markerFrame.markers.push_back(std::move(square));

        std::vector<unsigned char> markerRgb(std::size_t(width) * std::size_t(height) * 3, 0);
        codedRenderer.renderFrame(*gl, codedTarget, markerFrame, markerRgb.data(), nullptr, true,
                                  false);

        const unsigned char markerR = static_cast<unsigned char>(0xC0DE & 0xFF);
        const unsigned char markerG = static_cast<unsigned char>((0xC0DE >> 8) & 0xFF);
        const unsigned char markerB = 0;
        std::printf("colour coded marker pixel %u %u %u (expect %u %u %u)\n",
                    markerRgb[codedCentre], markerRgb[codedCentre + 1],
                    markerRgb[codedCentre + 2], markerR, markerG, markerB);

        if (markerRgb[codedCentre] != markerR || markerRgb[codedCentre + 1] != markerG ||
            markerRgb[codedCentre + 2] != markerB)
        {
            logError("colour coded frame did not encode the marker handle");
            return 14;
        }
    }

    codedRenderer.shutdown(*gl);

    renderer.shutdown(*gl);
    context.doneCurrent();

    if (nonBackground < (width * height) / 10)
    {
        logError("the rendered image is almost empty");
        return 6;
    }
    if (minimumDepth >= 1.0f)
    {
        logError("the depth buffer is empty");
        return 7;
    }
    if (alternativeNonBackground < (width * height) / 10)
    {
        logError("the orthographic image is almost empty");
        return 10;
    }
    std::printf("SMOKE TEST PASSED\n");
    return 0;
}
