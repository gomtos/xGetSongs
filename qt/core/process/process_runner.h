#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <stop_token>

// Running an external program (the port of engine/.../process/ProcessRunner.kt). Blocking, with a std::stop_token in place of
// coroutine cancellation (an author decision of the Qt port).
namespace xgs::process {

/// Exited: the program ran to its end. Cancelled: a stop was requested (and only then). StartFailed: nothing was started.
/// Failed: the program was started, but the runner could no longer wait for it although no stop was requested (a Win32 wait
/// failed); the whole process tree was killed. Callers treat Failed like StartFailed (Kotlin would see an IOException), never
/// like a cancel.
enum class RunStatus { Exited, Cancelled, StartFailed, Failed };

struct RunResult {
    RunStatus status;
    int exitCode = -1;   // Exited only: GetExitCodeProcess's value as it is
    QString error;       // StartFailed and Failed only: Windows' message (or why the program was not started)
};

using LineHandler = std::function<void(const QString& line)>;

class ProcessRunner {
public:
    virtual ~ProcessRunner() = default;

    /// Runs command (command[0] is the executable), never through a shell. Calls a handler once per output line. The two
    /// handlers may be called from different threads, and may be called on the caller's thread (the test fake does that);
    /// a handler must not block (the real runner reads that pipe only after the handler returns, and a stop waits for it).
    /// When run returns, neither handler is called again. When stop is requested, the whole process tree is killed and the
    /// result is Cancelled; the last line handed over after such a kill may be cut short.
    /// When a handler throws (as Kotlin, where the exception leaves run), the whole process tree is killed, both handlers have
    /// stopped being called, and run rethrows that exception on the caller's thread; no further line is handed to the handler
    /// that threw. When both handlers threw, the stdout handler's exception is rethrown. The exception wins over Cancelled
    /// when a stop and a throw meet.
    virtual RunResult run(const QStringList& command, const LineHandler& onStdout, const LineHandler& onStderr,
                          std::stop_token stop) = 0;
};

}  // namespace xgs::process
