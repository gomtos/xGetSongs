// crash_probe <folder> <mode>: the exe that crashes on purpose for tst_diagnostics (gate D5). It opens the log file in folder
// (the app's FileLogger), installs the app's crash handlers (CrashHandler) on that folder, writes `probe ready`, and crashes:
//   access-violation   writes through a null pointer (an unhandled SEH exception)
//   terminate          lets an exception leave a noexcept function (std::terminate)
// A console exe without Qt's GUI parts and without a window. SetErrorMode keeps Windows from showing an error box if the handler
// did not take the crash (the product does not set it: its handlers end the process themselves, without Windows' error reporting).

#include <windows.h>

#include <QString>

#include <cstdio>
#include <stdexcept>

#include "crash_dump.h"
#include "diag/file_logger.h"
#include "diag/log_sink.h"

namespace {

[[noreturn]] void throwOutOf() {
    throw std::runtime_error("probe");
}

// What a thread's function or a destructor does when an exception leaves it: noexcept turns that into std::terminate.
void throwNobodyCatches() noexcept {
    throwOutOf();
}

}  // namespace

int main(int argc, char* argv[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 3) {
        std::fputs("usage: crash_probe <folder> <access-violation|terminate>\n", stderr);
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const QString mode = QString::fromLatin1(argv[2]);
    xgs::diag::setCurrentThreadName(QStringLiteral("main"));
    static xgs::diag::FileLogger logger;   // lives until the process ends, as in the app
    logger.open(dir);
    static xgs::app::CrashHandler handler(dir, &logger);
    if (!handler.installed()) {
        std::fputs("the crash handler could not be installed\n", stderr);
        return 3;
    }
    logger.write(xgs::diag::Level::Info, QStringLiteral("main"), QStringLiteral("xgs.probe"), QStringLiteral("probe ready"));

    if (mode == QStringLiteral("access-violation")) {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    } else if (mode == QStringLiteral("terminate")) {
        throwNobodyCatches();
    }
    std::fputs("unknown mode\n", stderr);
    return 2;
}
