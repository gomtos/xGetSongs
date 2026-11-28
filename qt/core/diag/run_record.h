#pragma once

#include <QString>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

#include "diag/exit_record.h"
#include "diag/log_sink.h"
#include "diag/run_marker.h"

// What Diagnostics.start does apart from the JVM parts (the uncaught-exception handler, the shutdown hook, the watchdog
// thread): prepare the log folder, write the startup record, say what the marker of the previous run says, write the marker of
// this run and set up the exit record whose onExit rewrites the marker. The app (qt/app) makes one RunRecord and calls logExit
// from the ends of the process.
namespace xgs::diag {

/// The marker of the last run, next to the log.
inline const QString MARKER_FILE_NAME = QStringLiteral("last-run.txt");

/// The logger name of the lines the start writes (the startup record, what the marker says).
inline const QString DIAGNOSTICS_LOGGER_NAME = QStringLiteral("xgs.diagnostics");

/// Tells what the marker of the previous run says and writes the marker of this one. A marker that was edited by hand, a process
/// table that fails, a start time that cannot be read: none of that may keep the app from starting, so each step is guarded and
/// a failure is a WARN line. nullptr when the marker of this run cannot be set up at all (nothing is rewritten at exit then).
std::unique_ptr<RunMarkerFile> startMarker(const QString& logDir, const Processes& processes, const LogSink& log);

class RunRecord {
public:
    /// Prepares logDir (a failure is only reported on stderr), logs startupText, runs startMarker and sets up the exit record.
    /// uptime is the clock of the exit record's uptime (empty: time since this constructor, on a clock that only goes forward).
    RunRecord(const QString& logDir, const LogSink& log, const Processes& processes, const QString& startupText,
              std::function<std::chrono::milliseconds()> uptime = {});

    /// The job count source for the exit record (-1 until set).
    void setRunningJobs(std::function<int()> runningJobs);
    int runningJobs() const;

    /// The window's close handler calls this: the exit that follows is the user's own.
    void markUserExit();

    /// Something unhandled was logged (a crash, an exception no one caught): the exit record mentions it.
    void noteUncaught() noexcept;

    /// Writes the exit record and rewrites the marker. Never throws.
    void logExit() noexcept;

private:
    LogSink m_log;
    std::unique_ptr<RunMarkerFile> m_marker;
    std::atomic<bool> m_uncaughtSeen{false};
    std::unique_ptr<ExitRecord> m_exit;
};

}  // namespace xgs::diag
