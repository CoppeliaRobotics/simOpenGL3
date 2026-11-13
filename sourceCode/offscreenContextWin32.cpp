// Windows off-screen context: a hidden window plus WGL.
//
// A window is needed because WGL draws from a device context; the window is
// never shown and everything is rendered into framebuffer objects, so no
// pbuffer or swap chain is involved.  MSAA happens in the FBO, therefore the
// window's pixel format only has to be a valid accelerated OpenGL format.
//
// The ARB entry points (wglChoosePixelFormatARB, wglCreateContextAttribsARB)
// are only reachable through a current context, so a throw-away window and
// context are created first, queried, and destroyed.  That also lets the real
// window get its final pixel format chosen by wglChoosePixelFormatARB, which
// has to happen before SetPixelFormat is called for the first time.

#if defined(_WIN32)

#include "offscreenContext.h"
#include "log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

namespace modern
{
namespace
{

constexpr int kRequiredMajor = 3;
constexpr int kRequiredMinor = 3;

const char* kWindowClass = "simOpenGL3Offscreen";

using PfnWglCreateContextAttribsARB = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PfnWglChoosePixelFormatARB = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*,
                                                 UINT*);
using PfnWglSwapIntervalEXT = BOOL(WINAPI*)(int);

// WGL_ARB_create_context constants.
constexpr int kWglContextMajorVersionArb = 0x2091;
constexpr int kWglContextMinorVersionArb = 0x2092;
constexpr int kWglContextProfileMaskArb = 0x9126;
constexpr int kWglContextCoreProfileBitArb = 0x0001;
constexpr int kWglContextFlagsArb = 0x2094;

// WGL_ARB_pixel_format constants.
constexpr int kWglDrawToWindowArb = 0x2001;
constexpr int kWglSupportOpenGlArb = 0x2010;
constexpr int kWglRedBitsArb = 0x2015;
constexpr int kWglGreenBitsArb = 0x2017;
constexpr int kWglBlueBitsArb = 0x2019;
constexpr int kWglPixelTypeArb = 0x2013;
constexpr int kWglTypeRgbaArb = 0x202B;

HMODULE openGlModule()
{
    static HMODULE module = LoadLibraryA("opengl32.dll");
    return module;
}

void* wglLookup(const char* name)
{
    HMODULE module = openGlModule();
    if (module == nullptr)
        return nullptr;

    // wglGetProcAddress only returns extensions; the GL 1.1 entry points have
    // to come from the module itself (GLFuncs::load does that fallback too).
    PROC address = wglGetProcAddress(name);
    if (address != nullptr)
        return reinterpret_cast<void*>(address);
    return reinterpret_cast<void*>(GetProcAddress(module, name));
}

WNDCLASSEXA windowClass()
{
    WNDCLASSEXA description = {};
    description.cbSize = sizeof(description);
    description.style = CS_OWNDC;
    description.lpfnWndProc = DefWindowProcA;
    description.hInstance = GetModuleHandleA(nullptr);
    description.lpszClassName = kWindowClass;
    return description;
}

HWND createHiddenWindow(const char* title)
{
    static bool classRegistered = false;
    if (!classRegistered)
    {
        WNDCLASSEXA description = windowClass();
        if (RegisterClassExA(&description) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            logError("RegisterClassEx failed (%lu)", GetLastError());
            return nullptr;
        }
        classRegistered = true;
    }

    HWND window = CreateWindowExA(0, kWindowClass, title, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                  GetModuleHandleA(nullptr), nullptr);
    if (window == nullptr)
        logError("CreateWindowEx failed (%lu); a desktop session is required for WGL",
                 GetLastError());
    return window;
}

// Chooses the pixel format of the hidden window.
//
// The window's format is only there to make the context creatable: every pixel
// is rendered into a framebuffer object with its own depth attachment, so no
// depth/stencil/multisample request is made here.  Asking for depth bits makes
// some drivers (including the one this was tested on) return no matching format
// at all on a plain hidden window, while RGB888 always matches.
bool choosePixelFormat(HDC deviceContext, PfnWglChoosePixelFormatARB choose, int* format)
{
    if (choose != nullptr)
    {
        const int attributes[] = {
            kWglDrawToWindowArb, 1,
            kWglSupportOpenGlArb, 1,
            kWglPixelTypeArb, kWglTypeRgbaArb,
            kWglRedBitsArb, 8,
            kWglGreenBitsArb, 8,
            kWglBlueBitsArb, 8,
            0, 0,
        };
        UINT count = 0;
        if (choose(deviceContext, attributes, nullptr, 1, format, &count) != FALSE && count > 0)
            return true;

        logWarning("wglChoosePixelFormatARB found no format, falling back to ChoosePixelFormat");
    }

    PIXELFORMATDESCRIPTOR description = {};
    description.nSize = sizeof(description);
    description.nVersion = 1;
    description.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
    description.iPixelType = PFD_TYPE_RGBA;
    description.cColorBits = 32;
    description.cDepthBits = 24;

    *format = ChoosePixelFormat(deviceContext, &description);
    if (*format == 0)
    {
        logError("ChoosePixelFormat failed (%lu)", GetLastError());
        return false;
    }
    return true;
}

} // namespace

struct OffscreenContext::PlatformContext
{
    HWND window = nullptr;
    HDC deviceContext = nullptr;
    HGLRC renderContext = nullptr;
    PfnWglSwapIntervalEXT swapInterval = nullptr;

    // Whatever was current before we bound ours, so doneCurrent() can put it
    // back instead of leaving the thread without a context.
    HDC previousDeviceContext = nullptr;
    HGLRC previousContext = nullptr;
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

    // --- bootstrap window and context --------------------------------------
    // wglChoosePixelFormatARB and wglCreateContextAttribsARB are only reachable
    // through a current context, and some drivers additionally require a
    // context to be current while choosing a pixel format, so the throw-away
    // context stays current until the real one has been created.
    HWND bootstrapWindow = createHiddenWindow("simOpenGL3 bootstrap");
    if (bootstrapWindow == nullptr)
        return false;

    HDC bootstrapDeviceContext = GetDC(bootstrapWindow);
    int bootstrapFormat = 0;
    if (!choosePixelFormat(bootstrapDeviceContext, nullptr, &bootstrapFormat))
    {
        ReleaseDC(bootstrapWindow, bootstrapDeviceContext);
        DestroyWindow(bootstrapWindow);
        return false;
    }

    PIXELFORMATDESCRIPTOR bootstrapDescription = {};
    DescribePixelFormat(bootstrapDeviceContext, bootstrapFormat, sizeof(bootstrapDescription),
                        &bootstrapDescription);
    if (SetPixelFormat(bootstrapDeviceContext, bootstrapFormat, &bootstrapDescription) == FALSE)
    {
        logError("SetPixelFormat failed (%lu)", GetLastError());
        ReleaseDC(bootstrapWindow, bootstrapDeviceContext);
        DestroyWindow(bootstrapWindow);
        return false;
    }

    HGLRC bootstrapContext = wglCreateContext(bootstrapDeviceContext);
    if (bootstrapContext == nullptr ||
        wglMakeCurrent(bootstrapDeviceContext, bootstrapContext) == FALSE)
    {
        logError("cannot create the bootstrap OpenGL context (%lu)", GetLastError());
        if (bootstrapContext != nullptr)
            wglDeleteContext(bootstrapContext);
        ReleaseDC(bootstrapWindow, bootstrapDeviceContext);
        DestroyWindow(bootstrapWindow);
        return false;
    }

    const auto createContextAttribs = reinterpret_cast<PfnWglCreateContextAttribsARB>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    const auto chooseFormat = reinterpret_cast<PfnWglChoosePixelFormatARB>(
        wglGetProcAddress("wglChoosePixelFormatARB"));

    if (createContextAttribs == nullptr)
    {
        logError("wglCreateContextAttribsARB is not available; the driver does not support "
                 "OpenGL 3.3 core");
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(bootstrapContext);
        ReleaseDC(bootstrapWindow, bootstrapDeviceContext);
        DestroyWindow(bootstrapWindow);
        return false;
    }

    // --- the real window and context ---------------------------------------
    m_platform->window = createHiddenWindow("simOpenGL3");
    m_platform->deviceContext =
        m_platform->window != nullptr ? GetDC(m_platform->window) : nullptr;

    int format = 0;
    bool ready = (m_platform->deviceContext != nullptr) &&
                 choosePixelFormat(m_platform->deviceContext, chooseFormat, &format);

    if (ready)
    {
        PIXELFORMATDESCRIPTOR description = {};
        DescribePixelFormat(m_platform->deviceContext, format, sizeof(description), &description);
        ready = SetPixelFormat(m_platform->deviceContext, format, &description) != FALSE;
        if (!ready)
            logError("SetPixelFormat failed (%lu)", GetLastError());
    }

    if (ready)
    {
        const int contextAttributes[] = {
            kWglContextMajorVersionArb, kRequiredMajor,
            kWglContextMinorVersionArb, kRequiredMinor,
            kWglContextProfileMaskArb, kWglContextCoreProfileBitArb,
            kWglContextFlagsArb, 0,
            0, 0,
        };
        m_platform->renderContext =
            createContextAttribs(m_platform->deviceContext, nullptr, contextAttributes);
        if (m_platform->renderContext == nullptr)
        {
            logError("wglCreateContextAttribsARB failed for OpenGL %d.%d core (%lu)",
                     kRequiredMajor, kRequiredMinor, GetLastError());
            ready = false;
        }
    }

    // The bootstrap is no longer needed, whichever way the above went.
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(bootstrapContext);
    ReleaseDC(bootstrapWindow, bootstrapDeviceContext);
    DestroyWindow(bootstrapWindow);

    if (!ready)
    {
        destroy();
        return false;
    }
    m_platform->previousContext = wglGetCurrentContext();
    m_platform->previousDeviceContext = wglGetCurrentDC();
    if (wglMakeCurrent(m_platform->deviceContext, m_platform->renderContext) == FALSE)
    {
        logError("wglMakeCurrent failed (%lu)", GetLastError());
        destroy();
        return false;
    }

    if (!finishCreation(&wglLookup, "wgl"))
    {
        wglMakeCurrent(nullptr, nullptr);
        destroy();
        return false;
    }

    m_platform->swapInterval =
        reinterpret_cast<PfnWglSwapIntervalEXT>(wglGetProcAddress("wglSwapIntervalEXT"));

    m_created = true;
    return true;
}

void OffscreenContext::destroy()
{
    if (m_platform != nullptr)
    {
        if (m_platform->renderContext != nullptr)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_platform->renderContext);
            m_platform->renderContext = nullptr;
        }
        if (m_platform->deviceContext != nullptr && m_platform->window != nullptr)
            ReleaseDC(m_platform->window, m_platform->deviceContext);
        if (m_platform->window != nullptr)
            DestroyWindow(m_platform->window);
        m_platform.reset();
    }

    m_functions = GLFuncs();
    m_created = false;
}

bool OffscreenContext::makeCurrentPlatform()
{
    if (wglGetCurrentContext() == m_platform->renderContext)
        return true; // already ours; keep the saved previous context untouched

    m_platform->previousContext = wglGetCurrentContext();
    m_platform->previousDeviceContext = wglGetCurrentDC();
    return wglMakeCurrent(m_platform->deviceContext, m_platform->renderContext) != FALSE;
}

void OffscreenContext::doneCurrentPlatform()
{
    if (wglGetCurrentContext() != m_platform->renderContext)
        return; // somebody else already took over; do not disturb it

    if (m_platform->previousContext != nullptr)
        wglMakeCurrent(m_platform->previousDeviceContext, m_platform->previousContext);
    else
        wglMakeCurrent(nullptr, nullptr);

    m_platform->previousContext = nullptr;
    m_platform->previousDeviceContext = nullptr;
}

} // namespace modern

#endif // _WIN32