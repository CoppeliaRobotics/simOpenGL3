// The single off-screen OpenGL context used by the plugin.
//
// The legacy renderer created one QOffscreenSurface plus one QOpenGLContext per
// camera and per vision sensor, and then duplicated the shader programs, the
// mesh cache and the texture cache into each of them.  Rendering to a
// different framebuffer does not require a different context, so this plugin
// uses exactly one context for every view: all GPU resources are uploaded once
// and shared by all views.
//
// No Qt, and no window system dependency in this header: the platform specific
// part lives behind a pimpl and is implemented for WGL (Windows), EGL
// (Linux) and CGL (macOS) in offscreenContext<Platform>.cpp.

#pragma once

#include "glUtil.h"

#include <memory>
#include <string>
#include <thread>

namespace modern
{

class OffscreenContext
{
public:
    OffscreenContext();
    ~OffscreenContext();
    OffscreenContext(const OffscreenContext&) = delete;
    OffscreenContext& operator=(const OffscreenContext&) = delete;

    // Creates the context on first use.  Must be called from the thread that
    // will render; the window (or drawable) is created on that thread too.
    bool ensureCreated();

    void destroy();

    bool isCreated() const { return m_created; }

    // makeCurrent() remembers whatever context was current and doneCurrent()
    // restores it.  The external renderer runs in the middle of the host's
    // frame, on a thread that very likely has its own GL context current, so
    // clearing the binding instead of restoring it would pull the context out
    // from under CoppeliaSim (and out from under Qt's paint engine).
    bool makeCurrent();
    void doneCurrent();

    GLFuncs* functions() { return &m_functions; }
    const GLFuncs* functions() const { return &m_functions; }

    const std::string& versionString() const { return m_versionString; }
    const std::string& rendererString() const { return m_rendererString; }
    bool supportsRequiredVersion() const { return m_supportsRequiredVersion; }

    // What actually got used, for the log: "wgl", "egl", "cgl".
    const char* loaderName() const { return m_loaderName; }

private:
    // Loads the entry point table and reads GL_VERSION / GL_RENDERER.
    bool finishCreation(GLFuncs::Lookup lookup, const char* loaderName);

    // Implemented in offscreenContext<Platform>.cpp.
    bool makeCurrentPlatform();
    void doneCurrentPlatform();

    struct PlatformContext;
    std::unique_ptr<PlatformContext> m_platform;

    GLFuncs m_functions;
    std::string m_versionString;
    std::string m_rendererString;
    const char* m_loaderName = "none";
    std::thread::id m_ownerThread;
    bool m_threadWarningIssued = false;
    bool m_supportsRequiredVersion = false;
    bool m_created = false;
};

} // namespace modern
