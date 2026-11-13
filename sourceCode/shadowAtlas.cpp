#include "shadowAtlas.h"

#include "log.h"

namespace modern
{

bool ShadowAtlas::ensure(GLFuncs& gl, int requiredLayers, int mapSize, const RenderSettings& settings)
{
    if (!settings.shadowsEnabled || requiredLayers <= 0 || mapSize <= 0)
        return m_texture.isValid();

    requiredLayers = std::min(requiredLayers, settings.maxShadowLayers);
    mapSize = clampValue(mapSize, 128, 4096);

    const bool sizeChanged = (mapSize != m_size) && m_texture.isValid();
    const bool tooSmall = (requiredLayers > m_layers);

    if (m_texture.isValid() && !sizeChanged && !tooSmall)
        return true;

    // Grow by doubling so that one extra light does not reallocate the atlas
    // on every single frame, and clamp to the configured hard limit.
    int layers = (sizeChanged || !m_texture.isValid()) ? 8 : m_layers;
    while ((layers < requiredLayers) && (layers < settings.maxShadowLayers))
        layers = std::min(layers * 2, settings.maxShadowLayers);
    layers = std::min(layers, settings.maxShadowLayers);
    if (layers < requiredLayers)
    {
        logWarning("shadow atlas capped at %d layers; %d requested. Extra lights will not "
                   "cast shadows.",
                   layers, requiredLayers);
    }

    destroy(gl);

    m_size = mapSize;
    m_layers = layers;

    m_texture.createDepthArray(gl, mapSize, mapSize, layers, true);

    m_framebuffer.create(gl);
    m_framebuffer.disableColorAttachment(gl);
    m_framebuffer.attachDepthLayer(gl, m_texture.id(), 0);

    if (!m_framebuffer.isComplete(gl, "shadow atlas"))
    {
        destroy(gl);
        return false;
    }

    GpuFramebuffer::bindDefault(gl);
    logGLError(gl, "ShadowAtlas::ensure");
    return true;
}

void ShadowAtlas::destroy(GLFuncs& gl)
{
    m_framebuffer.destroy(gl);
    m_texture.destroy(gl);
    m_size = 0;
    m_layers = 0;
}

void ShadowAtlas::beginLayer(GLFuncs& gl, int layer)
{
    if (!m_texture.isValid() || layer < 0 || layer >= m_layers)
        return;

    m_framebuffer.bind(gl);
    gl.glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_texture.id(), 0, layer);
    gl.glViewport(0, 0, m_size, m_size);
    gl.glClearDepthf(1.0f);
    gl.glClear(GL_DEPTH_BUFFER_BIT);
}

void ShadowAtlas::end(GLFuncs& gl)
{
    GpuFramebuffer::bindDefault(gl);
}

} // namespace modern

