// Modern off-screen render target chain.
//
//   multisampled FBO (RGBA16F + DEPTH32F renderbuffers)
//        |  geometry is rendered here
//        |  glBlitFramebuffer resolves colour *and* depth
//        v
//   resolve FBO (RGBA16F texture + DEPTH32F texture)
//        |  tone map / gamma / exposure, full screen
//        v
//   present FBO (RGBA8 texture)
//        |  glReadPixels through a PBO
//        v
//   CoppeliaSim's RGB buffer (+ optional linear depth buffer)
//
// This replaces the Qt QOpenGLFramebufferObject of the legacy plugin, which
// gave no control over the colour format, no MSAA and no explicit depth
// resolve.

#pragma once

#include "glUtil.h"
#include "renderSettings.h"

namespace modern
{

class OffscreenTarget
{
public:
    OffscreenTarget() = default;
    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    bool create(GLFuncs& gl, int width, int height, const RenderSettings& settings);
    void destroy(GLFuncs& gl);

    bool isValid() const { return m_width > 0 && m_height > 0; }
    bool matches(int width, int height) const { return m_width == width && m_height == height; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int samples() const { return m_samples; }

    // Binds the multisampled target, sets the viewport and clears it.
    void beginScene(GLFuncs& gl, const Vec3& backgroundColor);

    // Resolves colour and (when the scene used a depth buffer) depth into the
    // single-sampled resolve target.
    void resolve(GLFuncs& gl);

    GLuint msFramebuffer() const { return m_msFramebuffer.id(); }
    GLuint resolveFramebuffer() const { return m_resolveFramebuffer.id(); }
    GLuint presentFramebuffer() const { return m_presentFramebuffer.id(); }
    GLuint resolveColorTexture() const { return m_resolveColor.id(); }
    GLuint presentTexture() const { return m_presentColor.id(); }

    void readColor(GLFuncs& gl, PixelReader& reader, unsigned char* destination);
    // Reads colour and depth together: the two PBO reads are queued before the
    // first map, so the GPU is only waited for once.
    void readColorAndDepth(GLFuncs& gl, PixelReader& reader, unsigned char* color,
                           float* depth);
    void readDepth(GLFuncs& gl, PixelReader& reader, float* destination);

private:
    bool createWithSampleCount(GLFuncs& gl, int width, int height, int requestedSamples);

    int m_width = 0;
    int m_height = 0;
    int m_samples = 1;
    bool m_depthResolveChecked = false;

    GpuFramebuffer m_msFramebuffer;
    GpuRenderbuffer m_msColor;
    GpuRenderbuffer m_msDepth;

    GpuFramebuffer m_resolveFramebuffer;
    GpuTexture m_resolveColor;
    GpuTexture m_resolveDepth;

    GpuFramebuffer m_presentFramebuffer;
    GpuTexture m_presentColor;
};

} // namespace modern

