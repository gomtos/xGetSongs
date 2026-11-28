#pragma once

#include <QString>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <utility>

#include "diag/log_sink.h"

// The record of an exit, whatever the reason: the line that tells a closed window from a process that was terminated from
// outside, which otherwise leaves no trace. Port of ExitLogger.kt. The wording is Kotlin's, `JVM 종료 시작` included, so that
// old and new logs read alike (the author kept it; changing the words is a one-line decision for the owner). The Java shutdown
// hook itself has no counterpart: the app calls logExit.
namespace xgs::diag {

/// The words of the exit record: warn says whether it is a warning (an exit nobody in the window asked for).
struct ExitDescription {
    bool warn = false;
    QString text;
    bool operator==(const ExitDescription&) const = default;
};

/// userRequested is the window's close button, the one exit the app knows about; uncaughtBefore says that an unhandled
/// exception or crash was logged earlier. runningJobs is the number of jobs the app has registered, or negative when nobody
/// can tell.
ExitDescription describeExit(bool userRequested, int runningJobs, std::chrono::milliseconds uptime, bool uncaughtBefore = false);

/// `3초`, `12분 3초` or `1시간 2분 3초`; never negative.
QString formatUptime(std::chrono::milliseconds uptime);

/// What an exit record asks at the moment of the exit. An empty function means "not known". Each may throw (it is caught).
struct ExitSources {
    std::function<int()> runningJobs;                      // empty: unknown (-1)
    std::function<std::chrono::milliseconds()> uptime;     // empty: 0
    std::function<std::pair<qint64, qint64>()> memoryMb;   // used and maximum, in MB; empty: no figure in the record
    std::function<bool()> uncaughtSeen;                    // empty: false
    std::function<void(bool userRequested)> onExit;        // told once the record is written (the marker is rewritten there)
};

class ExitRecord {
public:
    /// The record goes to log under the logger name `xgs.exit`.
    ExitRecord(ExitSources sources, LogSink log);

    /// The window's close handler calls this: the exit that follows is the user's own.
    void markUserExit() noexcept;

    /// Replaces the job count source (the server that knows the number starts after the diagnostics do).
    void setRunningJobs(std::function<int()> runningJobs);
    /// The job count now: -1 when unknown or when the source throws.
    int runningJobs() const;

    /// The words for an exit now: the user flag is this record's own; crashed says that an unhandled exception or crash was
    /// logged before.
    ExitDescription describe(int runningJobs, std::chrono::milliseconds uptime, bool crashed) const;
    QString text(int runningJobs, std::chrono::milliseconds uptime, bool crashed) const;

    /// Asks the sources, writes the one record (INFO for the user's exit, WARN for any other) and tells onExit. It never throws:
    /// it runs while the app is going down.
    void logExit() noexcept;

    static constexpr const char16_t* LoggerName = u"xgs.exit";

private:
    ExitSources m_sources;
    LogSink m_log;
    std::atomic<bool> m_userRequested{false};
    mutable std::mutex m_jobsMutex;
};

}  // namespace xgs::diag
