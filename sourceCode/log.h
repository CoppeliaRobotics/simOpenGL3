// Logging that does not depend on Qt.
//
// Warnings and errors only: the renderer says nothing while it is working, so
// anything that appears in the console is something that went wrong.

#pragma once

namespace modern
{

enum class LogLevel
{
    Warning,
    Error
};

using LogSink = void (*)(LogLevel level, const char* message);

// Not thread safe on purpose: the external renderer protocol is single
// threaded, and an atomic would be pointless overhead.
void setLogSink(LogSink sink);

void logWarning(const char* format, ...);
void logError(const char* format, ...);

// simAddLog must not be called while the plugin is still being initialised
// (CoppeliaSim has not registered the plugin name yet, and the lookup takes the
// whole application down), but that is exactly when the configuration is read.
// Messages are therefore buffered between deferMessages(true) and
// deferMessages(false), and flushDeferredMessages() emits them once the plugin
// is running.  With no sink installed they go to stderr, which is safe at any
// time.
void deferMessages(bool defer);
void flushDeferredMessages();

} // namespace modern
