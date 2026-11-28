#pragma once

#include <QString>
#include <QtGlobal>

#include <functional>
#include <memory>

#include "diag/hang_detector.h"
#include "diag/log_sink.h"

// The diagnostics of the running app, put together from qt/core/diag (Kotlin: Diagnostics.start and SidecarDiagnostics.start):
// the log file, Qt's messages in it, the startup record, the marker of the last run, the exit record, the crash handlers and the
// hang watch of the GUI thread. main starts it right after the single-instance check (a second copy writes nothing to the first
// copy's files) and before everything else.
namespace xgs::app {

struct AppPaths;

/// What a test changes; the app uses the defaults (the Kotlin HangWatchdog's figures: a beat a second, a hang after 5 s).
struct DiagnosticsOptions {
    /// The temp folder of the second log folder candidate; empty: QDir::tempPath().
    QString tempDir;
    qint64 hangIntervalMs = 1'000;
    diag::HangWatchdogOptions hang;
    bool crashHandler = true;
    bool messageHandler = true;
};

class Diagnostics final {
public:
    /// Chooses the log folder (`<appData>\logs`, else `<temp>\xgetsongs-logs`), opens the log file, sends Qt's messages to it (their
    /// paths removed), installs the crash handlers, writes the startup record, says what the marker of the last run says, writes
    /// this run's marker and starts the hang watch. Call it on the GUI thread, which it names `main`. nullptr only when not even
    /// that could be made (no memory); every other failure (a folder or file that cannot be written) leaves an app that runs.
    static std::unique_ptr<Diagnostics> start(const AppPaths& paths, DiagnosticsOptions options = {}) noexcept;
    /// stopWatching, puts Qt's previous message handler and the previous crash handlers back, closes the log file.
    ~Diagnostics();
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;

    QString logDir() const;
    /// Writes to the log file with the calling thread's name, from any thread. The text is written as it is: it must hold no title,
    /// file name or path (the job lines of diag::JobLog are redacted already).
    diag::LogSink sink() const;

    /// The number of jobs for the exit record (-1 until set).
    void setRunningJobs(std::function<int()> runningJobs);
    /// The window's close: the exit that follows is the user's own.
    void markUserExit();
    /// Ends the hang watch and joins its thread (bounded: a dump that never ends is left behind after 2 s). main calls it as soon
    /// as the event loop has ended, so the teardown that follows (up to about 1.5 s for a job) is never taken for a hang.
    void stopWatching() noexcept;
    bool watching() const;
    /// The Windows id of the hang watch thread while it runs (0 when there is none), for a test.
    unsigned long watchThreadId() const;
    /// Writes the exit record and rewrites the marker, once: a second call does nothing.
    void logExit() noexcept;
    /// WM_QUERYENDSESSION (a line) and WM_ENDSESSION with wParam TRUE (the exit record at once: Windows may end the process as
    /// soon as the message returns). The native event filter of the app calls it; a test calls it directly.
    void handleWindowsMessage(unsigned message, quintptr wParam) noexcept;

    struct State;

private:
    explicit Diagnostics(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

}  // namespace xgs::app
