#include "glApi.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace modern
{
namespace gl
{
namespace
{

// Last resort resolver, used for the GL 1.1 entry points that WGL/GLX will not
// hand out through their extension lookup.  On macOS the symbols live in the
// OpenGL framework and dlsym finds all of them.
void* platformDefault(const char* name)
{
#if defined(_WIN32)
    static HMODULE module = LoadLibraryA("opengl32.dll");
    if (module == nullptr)
        return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(module, name));
#elif defined(__APPLE__)
    static void* handle =
        dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    return handle != nullptr ? dlsym(handle, name) : nullptr;
#else
    static void* handle = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (handle == nullptr)
        handle = dlopen("libGL.so", RTLD_LAZY | RTLD_GLOBAL);
    return handle != nullptr ? dlsym(handle, name) : nullptr;
#endif
}

} // namespace

bool GLFuncs::load(Lookup lookup, const char** missing)
{
    const char* firstMissing = nullptr;

    const auto resolve = [&](const char* name) -> void* {
        void* address = (lookup != nullptr) ? lookup(name) : nullptr;
        if (address == nullptr)
            address = platformDefault(name);
        if ((address == nullptr) && (firstMissing == nullptr))
            firstMissing = name;
        return address;
    };

#define LOAD(name) gl##name = reinterpret_cast<PFN_gl##name>(resolve("gl" #name));
    MODERN_GL_FUNCTIONS(LOAD)
#undef LOAD

    if (missing != nullptr)
        *missing = firstMissing;
    return firstMissing == nullptr;
}

} // namespace gl
} // namespace modern
