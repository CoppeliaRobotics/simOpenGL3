#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace modern
{
namespace
{

LogSink g_sink = nullptr;

// Messages produced while the plugin is still initialising, see log.h.
bool g_deferred = false;
std::vector<std::pair<LogLevel, std::string>> g_pending;

void write(LogLevel level, const char* text)
{
    if (g_sink != nullptr)
    {
        g_sink(level, text);
        return;
    }

    const char* prefix =
        (level == LogLevel::Error) ? "[simOpenGL3] error: " : "[simOpenGL3] warning: ";
    std::fprintf(stderr, "%s%s\n", prefix, text);
    std::fflush(stderr);
}

void emit(LogLevel level, const char* format, va_list arguments)
{
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);

    if (g_deferred)
        g_pending.emplace_back(level, std::string(buffer));
    else
        write(level, buffer);
}

} // namespace

void setLogSink(LogSink sink)
{
    g_sink = sink;
}

void deferMessages(bool defer)
{
    if (g_deferred == defer)
        return;
    g_deferred = defer;
    if (!defer)
        flushDeferredMessages();
}

void flushDeferredMessages()
{
    if (g_pending.empty())
        return;

    std::vector<std::pair<LogLevel, std::string>> pending;
    pending.swap(g_pending);
    for (const auto& message : pending)
        write(message.first, message.second.c_str());
}


void logWarning(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    emit(LogLevel::Warning, format, arguments);
    va_end(arguments);
}

void logError(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    emit(LogLevel::Error, format, arguments);
    va_end(arguments);
}

} // namespace modern
