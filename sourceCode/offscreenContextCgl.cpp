// macOS off-screen context: CGL.
//
// CGL is the only native way to get a core profile context on macOS without a
// window (NSOpenGLContext would drag AppKit in).  It is deprecated since 10.14
// but still functional, which is acceptable for a renderer that deliberately
// targets OpenGL 3.3 and not Metal.
//
// Everything is rendered into framebuffer objects, so no drawable is required:
// a pixel format plus a context is enough.

#if defined(__APPLE__)

#include "offscreenContext.h"
#include "log.h"

#include <OpenGL/OpenGL.h>

namespace modern
{
namespace
{


// Every symbol lives in the OpenGL framework, and GLFuncs::load falls back to
// dlsym on it for any name the lookup returns nullptr for, so no lookup
// function is needed: nullptr is passed to finishCreation().

} // namespace

struct OffscreenContext::PlatformContext
{
    CGLPixelFormatObj pixelFormat = nullptr;
    CGLContextObj context = nullptr;

    // The engine and Qt both keep their own CGL context current on this
    // thread; doneCurrent() must put theirs back rather than clear the binding.
    CGLContextObj previousContext = nullptr;
};

OffscreenContext::OffscreenContext() = default;

OffscreenContext::~OffscreenContext()
{
    destroy();
}

bool OffscreenContext::ensureCreated()
{
    if (m_created)
        return true;

    m_platform = std::make_unique<PlatformContext>();

    const CGLPixelFormatAttribute attributes[] = {
        kCGLPFAAccelerated,
        kCGLPFAColorSize, CGLPixelFormatAttribute(24),
        kCGLPFAAlphaSize, CGLPixelFormatAttribute(8),
        kCGLPFADepthSize, CGLPixelFormatAttribute(24),
        kCGLPFAOpenGLProfile, CGLPixelFormatAttribute(kCGLOGLPVersion_GL3_Core),
        CGLPixelFormatAttribute(0),
    };

    GLint pixelFormatCount = 0;
    if (CGLChoosePixelFormat(attributes, &m_platform->pixelFormat, &pixelFormatCount) != kCGLNoError ||
        m_platform->pixelFormat == nullptr)
    {
        logError("CGLChoosePixelFormat failed: no accelerated OpenGL 3.2 core pixel format");
        destroy();
        return false;
    }

    CGLError error = CGLCreateContext(m_platform->pixelFormat, nullptr, &m_platform->context);
    if (error != kCGLNoError || m_platform->context == nullptr)
    {
        logError("CGLCreateContext failed (%d)", int(error));
        destroy();
        return false;
    }

    m_platform->previousContext = CGLGetCurrentContext();
    error = CGLSetCurrentContext(m_platform->context);
    if (error != kCGLNoError)
    {
        logError("CGLSetCurrentContext failed (%d)", int(error));
        destroy();
        return false;
    }

    if (!finishCreation(nullptr, "cgl"))
    {
        CGLSetCurrentContext(nullptr);
        destroy();
        return false;
    }


    m_created = true;
    return true;
}

void OffscreenContext::destroy()
{
    if (m_platform != nullptr)
    {
        CGLSetCurrentContext(nullptr);
        if (m_platform->context != nullptr)
            CGLDestroyContext(m_platform->context);
        if (m_platform->pixelFormat != nullptr)
            CGLDestroyPixelFormat(m_platform->pixelFormat);
        m_platform.reset();
    }

    m_functions = GLFuncs();
    m_created = false;
}

bool OffscreenContext::makeCurrentPlatform()
{
    if (CGLGetCurrentContext() == m_platform->context)
        return true; // already ours

    m_platform->previousContext = CGLGetCurrentContext();
    return CGLSetCurrentContext(m_platform->context) == kCGLNoError;
}

void OffscreenContext::doneCurrentPlatform()
{
    if (CGLGetCurrentContext() != m_platform->context)
        return; // somebody else took over; do not disturb it

    CGLSetCurrentContext(m_platform->previousContext);
    m_platform->previousContext = nullptr;
}

} // namespace modern

#endif // __APPLE__