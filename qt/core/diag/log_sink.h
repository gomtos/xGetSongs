#pragma once

#include <QString>

#include <exception>
#include <functional>

// What the diagnostics write and where it goes. Kotlin's logger levels (slf4j) that the app uses; Qt's message types map
// onto them (qDebug -> Debug, qInfo -> Info, qWarning -> Warn, qCritical and qFatal -> Error).
namespace xgs::diag {

enum class Level { Debug, Info, Warn, Error };

/// "DEBUG", "INFO", "WARN" or "ERROR" (not padded; FileLogger pads like logback's `%-5level`).
QString levelName(Level level);

/// Where a diagnostics component sends a record: the level, the logger's name (a category such as `xgs.exit`) and the text.
/// Called from any thread; it must not throw (the components catch what it throws anyway).
using LogSink = std::function<void(Level level, const QString& logger, const QString& message)>;

/// The name of the exception's type without `class `/`struct ` and namespaces (`runtime_error`): what Kotlin's
/// `javaClass.simpleName` is in the log lines that say what failed.
QString exceptionName(const std::exception& error);

/// exceptionName of the exception being handled, or `알 수 없는 예외` for one that is not a std::exception. Call it only inside
/// a catch handler.
QString currentExceptionName();

/// The name of the calling thread for the `[thread]` part of a line: what setCurrentThreadName gave it, else `t<number>`.
QString currentThreadName();
void setCurrentThreadName(const QString& name);
/// currentThreadName as UTF-8 into out (at most capacity - 1 bytes and a terminating zero; a name that does not fit is cut at a
/// character), without touching the heap: for a crash handler. Returns the number of bytes without the zero.
int currentThreadNameUtf8(char* out, int capacity) noexcept;

}  // namespace xgs::diag
