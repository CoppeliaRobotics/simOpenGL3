// Linux off-screen context: EGL.
//
// EGL is used instead of GLX on purpose: it works on X11, on Wayland, and with
// no display server at all (EGL_MESA_platform_surfaceless), which is what makes
// a headless CoppeliaSim build possible.  Everything is rendered into
// framebuffer objects, so a 1x1 pbuffer is a sufficient drawable.

#if defined(__linux__) && !defined(__ANDROID__)

#include "offscreenContext.h"
#include "log.h"

#include <cstring>

#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace modern
{
namespace
{

constexpr int kRequiredMajor = 3;
constexpr int kRequiredMinor = 3;

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#endif
#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#endif
#ifndef EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#endif

using PfnEglGetPlatformDisplayEXT = EGLDisplay(EGLAPIENTRY*)(EGLenum, void*, const EGLint*);

void* eglLookup(const char* name)
{
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

EGLDisplay openDisplay()
{
    // Prefer the surfaceless platform: it needs no X server and no Wayland
    // compositor, so the same plugin works on a desktop and in a container.
    const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (clientExtensions != nullptr &&
        std::strstr(clientExtensions, "EGL_MESA_platform_surfaceless") != nullptr)
    {
        auto getPlatformDisplay = reinterpret_cast<PfnEglGetPlatformDisplayEXT>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (getPlatformDisplay != nullptr)
        {
            EGLDisplay display =
                getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
            if (display != EGL_NO_DISPLAY)
                return display;
        }
    }

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    return display;
}

} // namespace

struct OffscreenContext::PlatformContext
{
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;

    // What was current before we bound ours, so doneCurrent() can restore it.
    EGLDisplay previousDisplay = EGL_NO_DISPLAY;
    EGLSurface previousDraw = EGL_NO_SURFACE;
    EGLSurface previousRead = EGL_NO_SURFACE;
    EGLContext previousContext = EGL_NO_CONTEXT;
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

    m_platform->display = openDisplay();
    if (m_platform->display == EGL_NO_DISPLAY)
    {
        logError("eglGetDisplay failed (0x%x)", eglGetError());
        destroy();
        return false;
    }

    EGLint majorVersion = 0;
    EGLint minorVersion = 0;
    if (eglInitialize(m_platform->display, &majorVersion, &minorVersion) == EGL_FALSE)
    {
        logError("eglInitialize failed (0x%x)", eglGetError());
        destroy();
        return false;
    }

    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE)
    {
        logError("the EGL implementation does not support the desktop OpenGL API");
        destroy();
        return false;
    }

    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE,
    };

    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(m_platform->display, configAttributes, &config, 1, &configCount) ==
            EGL_FALSE ||
        configCount < 1)
    {
        logError("no EGL config with a pbuffer surface and 24 bit depth is available");
        destroy();
        return false;
    }

    const EGLint surfaceAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    m_platform->surface =
        eglCreatePbufferSurface(m_platform->display, config, surfaceAttributes);
    if (m_platform->surface == EGL_NO_SURFACE)
    {
        logError("eglCreatePbufferSurface failed (0x%x)", eglGetError());
        destroy();
        return false;
    }

    const EGLint contextAttributes[] = {
        EGL_CONTEXT_MAJOR_VERSION, kRequiredMajor,
        EGL_CONTEXT_MINOR_VERSION, kRequiredMinor,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE,
    };

    m_platform->context =
        eglCreateContext(m_platform->display, config, EGL_NO_CONTEXT, contextAttributes);
    if (m_platform->context == EGL_NO_CONTEXT)
    {
        logError("eglCreateContext for OpenGL %d.%d core failed (0x%x)", kRequiredMajor,
                 kRequiredMinor, eglGetError());
        destroy();
        return false;
    }

    m_platform->previousContext = eglGetCurrentContext();
    m_platform->previousDisplay = eglGetCurrentDisplay();
    m_platform->previousDraw = eglGetCurrentSurface(EGL_DRAW);
    m_platform->previousRead = eglGetCurrentSurface(EGL_READ);

    if (eglMakeCurrent(m_platform->display, m_platform->surface, m_platform->surface,
                       m_platform->context) == EGL_FALSE)
    {
        logError("eglMakeCurrent failed (0x%x)", eglGetError());
        destroy();
        return false;
    }

    if (!finishCreation(&eglLookup, "egl"))
    {
        eglMakeCurrent(m_platform->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
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
        if (m_platform->display != EGL_NO_DISPLAY)
        {
            eglMakeCurrent(m_platform->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_platform->context != EGL_NO_CONTEXT)
                eglDestroyContext(m_platform->display, m_platform->context);
            if (m_platform->surface != EGL_NO_SURFACE)
                eglDestroySurface(m_platform->display, m_platform->surface);
            eglTerminate(m_platform->display);
        }
        m_platform.reset();
    }

    m_functions = GLFuncs();
    m_created = false;
}

bool OffscreenContext::makeCurrentPlatform()
{
    if (eglGetCurrentContext() == m_platform->context)
        return true; // already ours

    m_platform->previousContext = eglGetCurrentContext();
    m_platform->previousDisplay = eglGetCurrentDisplay();
    m_platform->previousDraw = eglGetCurrentSurface(EGL_DRAW);
    m_platform->previousRead = eglGetCurrentSurface(EGL_READ);

    return eglMakeCurrent(m_platform->display, m_platform->surface, m_platform->surface,
                          m_platform->context) != EGL_FALSE;
}

void OffscreenContext::doneCurrentPlatform()
{
    if (eglGetCurrentContext() != m_platform->context)
        return; // somebody else took over; do not disturb it

    if (m_platform->previousContext != EGL_NO_CONTEXT)
    {
        eglMakeCurrent(m_platform->previousDisplay, m_platform->previousDraw,
                       m_platform->previousRead, m_platform->previousContext);
    }
    else
    {
        eglMakeCurrent(m_platform->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    m_platform->previousContext = EGL_NO_CONTEXT;
    m_platform->previousDisplay = EGL_NO_DISPLAY;
}

} // namespace modern

#endif // __linux__