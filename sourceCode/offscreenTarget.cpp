#include "offscreenTarget.h"

#include "log.h"

namespace modern
{

bool OffscreenTarget::create(GLFuncs& gl, int width, int height, const RenderSettings& settings)
{
    if (width <= 0 || height <= 0)
        return false;

    if (createWithSampleCount(gl, width, height, settings.msaaSamples))
        return true;

    // A driver can refuse the multisampled formats even though it advertises
    // GL_MAX_SAMPLES, and a failed target means a black image with no obvious
    // cause, so fall back to a single-sampled target before giving up.
    if (settings.msaaSamples > 1)
    {
        logWarning("cannot create a %dx%d anti-aliased render target, retrying without MSAA",
                   width, height);
        destroy(gl);
        if (createWithSampleCount(gl, width, height, 1))
            return true;
    }

    destroy(gl);
    return false;
}

bool OffscreenTarget::createWithSampleCount(GLFuncs& gl, int width, int height, int requestedSamples)
{
    destroy(gl);

    GLint maxSamples = 1;
    gl.glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_samples = clampValue(requestedSamples, 1, int(std::max(1, maxSamples)));
    if (m_samples < 2)
        m_samples = 1;

    m_width = width;
    m_height = height;

    // --- multisampled scene target -----------------------------------------
    m_msFramebuffer.create(gl);

    m_msColor.allocate(gl, GL_RGBA16F, width, height, m_samples);
    m_msDepth.allocate(gl, GL_DEPTH_COMPONENT32F, width, height, m_samples);

    m_msFramebuffer.attachColorRenderbuffer(gl, m_msColor.id());
    m_msFramebuffer.attachDepthRenderbuffer(gl, m_msDepth.id());
    if (!m_msFramebuffer.isComplete(gl, "scene (MSAA)"))
        return false;

    // --- single-sampled resolve target -------------------------------------
    m_resolveColor.create2D(gl, width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, nullptr, false);
    m_resolveDepth.create2D(gl, width, height, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT,
                            GL_FLOAT, nullptr, false);

    m_resolveFramebuffer.create(gl);
    m_resolveFramebuffer.attachColorTexture(gl, GL_TEXTURE_2D, m_resolveColor.id());
    m_resolveFramebuffer.attachDepthTexture(gl, GL_TEXTURE_2D, m_resolveDepth.id());
    if (!m_resolveFramebuffer.isComplete(gl, "resolve"))
        return false;

    // --- 8 bit present target ----------------------------------------------
    m_presentColor.create2D(gl, width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr,
                            false);
    m_presentFramebuffer.create(gl);
    m_presentFramebuffer.attachColorTexture(gl, GL_TEXTURE_2D, m_presentColor.id());
    if (!m_presentFramebuffer.isComplete(gl, "present"))
        return false;

    GpuFramebuffer::bindDefault(gl);
    return true;
}

void OffscreenTarget::destroy(GLFuncs& gl)
{
    m_presentFramebuffer.destroy(gl);
    m_presentColor.destroy(gl);
    m_resolveFramebuffer.destroy(gl);
    m_resolveDepth.destroy(gl);
    m_resolveColor.destroy(gl);
    m_msFramebuffer.destroy(gl);
    m_msDepth.destroy(gl);
    m_msColor.destroy(gl);
    m_width = m_height = 0;
    m_samples = 1;
}

void OffscreenTarget::beginScene(GLFuncs& gl, const Vec3& backgroundColor)
{
    m_msFramebuffer.bind(gl);
    gl.glViewport(0, 0, m_width, m_height);
    gl.glClearColor(backgroundColor.x(), backgroundColor.y(), backgroundColor.z(), 1.0f);
    gl.glClearDepthf(1.0f);
    gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OffscreenTarget::resolve(GLFuncs& gl)
{
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msFramebuffer.id());
    gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFramebuffer.id());

    // Colour and depth are resolved with separate calls on purpose: a combined
    // blit is rejected as a whole if any part of it is invalid, and the depth
    // half is the fragile one (the formats have to match exactly), so a driver
    // that dislikes the depth resolve would otherwise also throw away the
    // colour and leave a black image.
    gl.glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT,
                         GL_NEAREST);

    gl.glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_DEPTH_BUFFER_BIT,
                         GL_NEAREST);
    if (!m_depthResolveChecked)
    {
        m_depthResolveChecked = true;
        if (checkGLError(gl, "resolving the depth buffer"))
        {
            logWarning("the depth buffer could not be resolved from the multisampled target; "
                       "depth reads will be wrong, the colour image is unaffected");
        }
    }

    gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OffscreenTarget::readColor(GLFuncs& gl, PixelReader& reader, unsigned char* destination)
{
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    reader.readPixels(gl, m_presentFramebuffer.id(), m_width, m_height, GL_RGB, GL_UNSIGNED_BYTE,
                      GLsizeiptr(m_width) * GLsizeiptr(m_height) * 3, destination);
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
}

void OffscreenTarget::readColorAndDepth(GLFuncs& gl, PixelReader& reader, unsigned char* color,
                                        float* depth)
{
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // Queue both transfers first: the depth one also waits for the colour one,
    // so the second map is free.
    const int colorSlot =
        reader.queue(gl, m_presentFramebuffer.id(), m_width, m_height, GL_RGB, GL_UNSIGNED_BYTE,
                     GLsizeiptr(m_width) * GLsizeiptr(m_height) * 3);
    const int depthSlot =
        reader.queue(gl, m_resolveFramebuffer.id(), m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT,
                     GLsizeiptr(m_width) * GLsizeiptr(m_height) * GLsizeiptr(sizeof(float)));

    gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);

    if (color != nullptr)
        reader.fetch(gl, colorSlot, GLsizeiptr(m_width) * GLsizeiptr(m_height) * 3, color);
    if (depth != nullptr)
        reader.fetch(gl, depthSlot, GLsizeiptr(m_width) * GLsizeiptr(m_height) * sizeof(float),
                     depth);
}
void OffscreenTarget::readDepth(GLFuncs& gl, PixelReader& reader, float* destination)
{
    reader.readPixels(gl, m_resolveFramebuffer.id(), m_width, m_height, GL_DEPTH_COMPONENT,
                      GL_FLOAT, GLsizeiptr(m_width) * GLsizeiptr(m_height) * sizeof(float),
                      destination);
}

} // namespace modern

