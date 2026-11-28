#pragma once

#include <QString>

#include <memory>

namespace xgs::diag {
class FileLogger;
}

// What is left behind when the process dies of something nobody handled: a minidump `xgetsongs-crash.dmp` in the log folder
// (overwritten by the next crash; it never leaves the PC) and one ERROR line in the log. The Kotlin app logged an uncaught
// exception with its stack trace; a C++ crash has no such text, so the dump stands in for it (plan, "사용자가 정할 것").
//
// Covered: an unhandled SEH exception (access violation, stack overflow, ...: SetUnhandledExceptionFilter), std::terminate (an
// exception that leaves a thread or main), a pure virtual call, an invalid parameter to the CRT, abort() (SIGABRT). Not covered:
// __fastfail (a /GS stack-buffer overrun, some CRT checks), which ends the process without running any handler, and a crash under
// a debugger (the debugger gets it).
//
// The moment of the crash is the worst time to take a lock or touch the heap: the crashed thread may hold the heap's lock or the
// logger's. So everything is prepared when the handlers are installed: the dump file is opened, the dump is written by a thread of
// its own that only waits (so a stack overflow still has a stack to write it with), and the line goes to the log by one WriteFile on
// the logger's file handle (FileLogger::writeRaw), formatted into a static buffer. The crashed thread only fills in a few numbers,
// wakes the writer, waits for it (at most 60 s) and ends the process. After the dump every path, the SEH filter included, ends the
// process at once with TerminateProcess and the exception's code (the filter never returns): Windows' error reporting is not asked,
// so no error box appears and no report is sent anywhere.
namespace xgs::app {

/// The dump file names, in the log folder. They do not match the names FileLogger rolls or deletes (`xgetsongs.log`,
/// `xgetsongs.<date>.<N>.log`).
inline const QString CrashDumpFileName = QStringLiteral("xgetsongs-crash.dmp");
inline const QString HangDumpFileName = QStringLiteral("xgetsongs-hang.dmp");
/// The logger name of the crash line.
inline const QString CrashLoggerName = QStringLiteral("xgs.crash");

/// The crash handlers of the process while it lives. One at a time; making a second one while one is installed installs nothing
/// (installed() is false).
class CrashHandler final {
public:
    /// Opens the dump file in logDir (without emptying a dump an earlier crash left), starts the writer thread and installs the
    /// handlers. logger (may be null) gets the line; it must outlive the handler. Never throws: what fails leaves installed() false
    /// or the dump file out (the line still says so).
    CrashHandler(const QString& logDir, diag::FileLogger* logger) noexcept;
    /// Puts back the handlers that were there before and ends the writer thread. A dump file opened but never written is removed.
    ~CrashHandler();
    CrashHandler(const CrashHandler&) = delete;
    CrashHandler& operator=(const CrashHandler&) = delete;

    bool installed() const;

    struct State;

private:
    std::unique_ptr<State> m_state;
};

/// Writes a minidump (MiniDumpNormal) of this process to path, replacing the file. For the hang dump: not for use inside a crash.
/// false when the file cannot be made or the dump fails (the file is then removed).
bool writeMiniDump(const QString& path) noexcept;

}  // namespace xgs::app
