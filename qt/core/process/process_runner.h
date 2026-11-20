#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <stop_token>

// Running an external program (the port of engine/.../process/ProcessRunner.kt). Blocking, with a std::stop_token in place of
// coroutine cancellation (an author decision of the Qt port).
namespace xgs::process {

enum class RunStatus { Exited, Cancelled, StartFailed };

struct RunResult {
    RunStatus status;
    int exitCode = -1;   // Exited only: GetExitCodeProcess's value as it is
    QString error;       // StartFailed only: Windows' message (or why the program was not started)
};

using LineHandler = std::function<void(const QString& line)>;

class ProcessRunner {
public:
    virtual ~ProcessRunner() = default;

    /// Runs command (command[0] is the executable), never through a shell. Calls a handler once per output line (the two
    /// handlers may be called from different threads, never from the caller's). When run returns, neither handler is called
    /// again. When stop is requested, the whole process tree is killed and the result is Cancelled.
    virtual RunResult run(const QStringList& command, const LineHandler& onStdout, const LineHandler& onStderr,
                          std::stop_token stop) = 0;
};

}  // namespace xgs::process
