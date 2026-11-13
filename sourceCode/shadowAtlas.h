// Shadow map atlas.
//
// The legacy renderer declared five sampler2D plus five samplerCube uniforms
// and unrolled every light because "some graphics cards appear not to support
// arrays".  Modern GL 3.3 does support arrays, so all shadow maps live in a
// single depth texture array:
//
//   layer 0            : directional light 0
//   layer 1            : spot light 0
//   ...
//   layer n .. n+5     : the six faces of point light 0
//
// One sampler, one texture bind, no per-light uniforms, and the number of
// lights is bounded by the layer count instead of by the number of samplers
// the driver is willing to give us.

#pragma once

#include "glUtil.h"
#include "renderSettings.h"

namespace modern
{

class ShadowAtlas
{
public:
    ShadowAtlas() = default;
    ShadowAtlas(const ShadowAtlas&) = delete;
    ShadowAtlas& operator=(const ShadowAtlas&) = delete;

    // Makes sure the atlas can hold at least `requiredLayers` shadow maps of
    // `mapSize` pixels.  Grows by doubling; never shrinks, so a scene that
    // gains and loses a light does not reallocate every frame.
    bool ensure(GLFuncs& gl, int requiredLayers, int mapSize, const RenderSettings& settings);
    void destroy(GLFuncs& gl);

    bool isValid() const { return m_texture.isValid(); }
    int mapSize() const { return m_size; }
    int layers() const { return m_layers; }
    GLuint texture() const { return m_texture.id(); }

    void bind(GLFuncs& gl, GLuint unit) const { m_texture.bind(gl, unit); }

    // Starts rendering into one layer: binds the depth-only framebuffer,
    // attaches the layer, sets the viewport and clears depth.
    void beginLayer(GLFuncs& gl, int layer);
    void end(GLFuncs& gl);

private:
    GpuTexture m_texture;
    GpuFramebuffer m_framebuffer;
    int m_size = 0;
    int m_layers = 0;
};

} // namespace modern

