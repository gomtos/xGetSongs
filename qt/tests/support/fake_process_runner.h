#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <mutex>
#include <stop_token>

#include "process/process_runner.h"

// The process part of engine/src/test/.../testutil/Fakes.kt.
namespace xgs::testsupport {

/// What a FakeProcessRunner does for one command: writes lines to the handlers and returns the exit code.
using ProcessHandler = std::function<int(const QStringList& command, const process::LineHandler& out,
                                         const process::LineHandler& err)>;

/// The same with the stop token of the run, for a handler that waits for a stop (Kotlin's `awaitCancellation()`): it returns
/// once the token is stopped, and the run is then Cancelled.
using StoppableProcessHandler = std::function<int(const QStringList& command, const process::LineHandler& out,
                                                  const process::LineHandler& err, std::stop_token stop)>;

/// Kotlin's String.lines(): text split at \r\n, \n and \r (an empty text is one empty line).
QStringList kotlinLines(const QString& text);

/// Thrown by a handler to make run return StartFailed with this message.
struct StartFailure {
    QString message;
};

/// Thrown by a handler to make run return Failed (started, then lost by the runner) with this message.
struct RunFailure {
    QString message;
};

/// A ProcessRunner whose behaviour is a function; records every command and the peak concurrency. Safe to call from several
/// threads at once. The handler runs on the caller's thread, so it may call the line handlers directly.
class FakeProcessRunner final : public process::ProcessRunner {
public:
    explicit FakeProcessRunner(ProcessHandler handler);
    explicit FakeProcessRunner(StoppableProcessHandler handler);

    /// A stop requested before the call runs nothing (nothing is recorded) and is Cancelled. Otherwise the command is
    /// recorded and the handler runs: a StartFailure it throws is StartFailed, a RunFailure Failed, any other exception propagates, a stop
    /// requested by the time it returns is Cancelled, else Exited with its return value.
    process::RunResult run(const QStringList& command, const process::LineHandler& onStdout,
                           const process::LineHandler& onStderr, std::stop_token stop) override;

    QList<QStringList> commands() const;
    int maxActive() const;

private:
    StoppableProcessHandler m_handler;
    mutable std::mutex m_mutex;
    QList<QStringList> m_commands;
    int m_active = 0;
    int m_maxActive = 0;
};

}  // namespace xgs::testsupport
