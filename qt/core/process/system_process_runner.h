#pragma once

#include "process/process_runner.h"

namespace xgs::process {

/// ProcessRunner on Win32 (CreateProcessW; no QProcess, an author decision of the Qt port).
///   - The child is born inside a new Job Object that kills every process in it when it is closed, so the whole tree goes
///     on a stop and also after a normal exit (descendants that outlive the child are killed: an author decision).
///   - The child inherits exactly its three pipe ends (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), so runs in parallel never hold
///     each other's pipes. Standard input is a pipe whose writing end is closed at once (the child reads end of file).
///   - No console window (CREATE_NO_WINDOW). The environment is this process's plus PYTHONIOENCODING=utf-8 and PYTHONUTF8=1.
///   - Never through a shell: an executable whose name ends in .cmd or .bat (in any case, trailing dots and spaces ignored) is
///     not started (Windows would run it through cmd.exe, which interprets `%`, `&` and `^`); the result is StartFailed.
///   - The output is split into lines by LineSplitter (UTF-8), stdout and stderr each on its own reading thread. An exception
///     on a reading thread (a throwing handler, bad_alloc) is caught there and rethrown by run (ProcessRunner's contract).
/// run may be called from several threads at once.
class SystemProcessRunner final : public ProcessRunner {
public:
    RunResult run(const QStringList& command, const LineHandler& onStdout, const LineHandler& onStderr,
                  std::stop_token stop) override;
};

}  // namespace xgs::process
