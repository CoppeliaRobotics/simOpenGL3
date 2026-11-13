// getenv()/sscanf() are portable and fine here; silence MSVC's secure-CRT
// deprecation warnings without switching to the non-portable _s variants.
#define _CRT_SECURE_NO_WARNINGS
#include "offscreenContext.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace modern
{
namespace
{

constexpr int kRequiredMajor = 3;
constexpr int kRequiredMinor = 3;

} // namespace

// The constructor and the destructor cannot live here:
// unique_ptr<PlatformContext> needs the complete type for both, and only the
// platform source files have it.
bool OffscreenContext::finishCreation(GLFuncs::Lookup lookup, const char* loaderName)
{
    const char* missing = nullptr;
    if (!m_functions.load(lookup, &missing))
    {
        logError("missing OpenGL entry point %s", missing != nullptr ? missing : "?");
        return false;
    }
    m_functions.loaderName = loaderName;
    m_loaderName = loaderName;
    // GL contexts are owned by the thread that created them; a context used
    // from a second thread works on some drivers and crashes on others.
    m_ownerThread = std::this_thread::get_id();

    // Report the driver's own string; it is the only thing that is actually
    // authoritative.  Both the string and the queried numbers are used, and the
    // larger of the two wins, so a driver that answers one query oddly cannot
    // make the version look older than it is.
    const GLubyte* versionText = m_functions.glGetString(GL_VERSION);
    const std::string rawVersion =
        versionText != nullptr ? reinterpret_cast<const char*>(versionText) : "";

    GLint queriedMajor = 0;
    GLint queriedMinor = 0;
    m_functions.glGetIntegerv(GL_MAJOR_VERSION, &queriedMajor);
    m_functions.glGetIntegerv(GL_MINOR_VERSION, &queriedMinor);

    int parsedMajor = 0;
    int parsedMinor = 0;
    if (std::sscanf(rawVersion.c_str(), "%d.%d", &parsedMajor, &parsedMinor) < 2)
    {
        parsedMajor = 0;
        parsedMinor = 0;
    }

    const int major = std::max(int(queriedMajor), parsedMajor);
    const int minor = (int(queriedMajor) == major) ? std::max(int(queriedMinor), parsedMinor)
                                                   : parsedMinor;

    m_versionString = rawVersion.empty() ? std::string("unknown") : rawVersion;

    const GLubyte* renderer = m_functions.glGetString(GL_RENDERER);
    m_rendererString = renderer != nullptr ? reinterpret_cast<const char*>(renderer) : "";
    const GLubyte* shadingLanguage = m_functions.glGetString(GL_SHADING_LANGUAGE_VERSION);

    m_supportsRequiredVersion =
        major > kRequiredMajor || (major == kRequiredMajor && minor >= kRequiredMinor);

    // Only a warning: shader compilation is the authoritative test, and
    // disabling the whole renderer on a parsed version would turn a
    // mis-detection into a silently black image.
    if (!m_supportsRequiredVersion)
    {
        logWarning("the context reports OpenGL \"%s\", which the parser reads as %d.%d; "
                   "3.3 core is required, continuing anyway",
                   m_versionString.c_str(), major, minor);
    }

    (void)shadingLanguage;
    return true;
}

bool OffscreenContext::makeCurrent()
{
    if (!m_created)
        return false;

    if ((std::this_thread::get_id() != m_ownerThread) && !m_threadWarningIssued)
    {
        m_threadWarningIssued = true;
        logError("the OpenGL context was created by another thread; this is not supported and "
                 "usually crashes");
    }

    return makeCurrentPlatform();
}

void OffscreenContext::doneCurrent()
{
    if (m_created)
        doneCurrentPlatform();
}

} // namespace modern
