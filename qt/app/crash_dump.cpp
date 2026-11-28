#include "crash_dump.h"

#include <QDir>

#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <thread>

#include "diag/file_logger.h"
#include "diag/log_sink.h"

namespace xgs::app {

namespace {

enum class Kind { Seh, Terminate, PureCall, InvalidParameter, Abort };

constexpr DWORD WriterWaitMs = 60'000;          // the longest the crashed thread waits for the dump
constexpr DWORD ExitCodeOfAbort = 3;             // what abort() ends a process with
constexpr DWORD ExitCodeOfCrtCheck = 0xC0000417; // STATUS_INVALID_CRUNTIME_PARAMETER, what the CRT ends a process with

const wchar_t* wide(const QString& text) {
    return reinterpret_cast<const wchar_t*>(text.utf16());
}

QString joined(const QString& dir, const QString& name) {
    QString base = QDir::toNativeSeparators(dir);
    while (base.endsWith(u'\\')) base.chop(1);
    return base + u'\\' + name;
}

/// A line built in a fixed buffer: no heap, no lock, no locale.
struct Line {
    char text[1024];
    int size = 0;

    void add(const char* part) {
        while (*part != 0 && size < static_cast<int>(sizeof text) - 3) text[size++] = *part++;
    }
    void addNumber(unsigned long value, int width) {
        char digits[16];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0 && count < 16);
        while (count < width && count < 16) digits[count++] = '0';
        while (count > 0) {
            const char one[2] = {digits[--count], 0};
            add(one);
        }
    }
    void addHex(unsigned long value) {
        add("0x");
        for (int shift = 28; shift >= 0; shift -= 4) {
            const char one[2] = {"0123456789ABCDEF"[(value >> shift) & 0xF], 0};
            add(one);
        }
    }
    void end() {
        text[size++] = '\r';
        text[size++] = '\n';
    }
};

bool setDeleteOnClose(HANDLE file, bool remove) {
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = remove ? TRUE : FALSE;
    return SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof disposition) != FALSE;
}

}  // namespace

/// Everything the crash path uses, made when the handler is installed.
struct CrashHandler::State {
    diag::FileLogger* logger = nullptr;
    HANDLE dumpFile = INVALID_HANDLE_VALUE;
    bool removeUnusedDump = false;     // the file was made by us: it goes at the end unless a dump went in
    HANDLE request = nullptr;          // the crashed thread asks for the dump
    HANDLE done = nullptr;             // the writer has written it (and the line)
    HANDLE quit = nullptr;             // the handler goes
    std::jthread writer;
    bool installed = false;

    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
    std::terminate_handler previousTerminate = nullptr;
    _purecall_handler previousPureCall = nullptr;
    _invalid_parameter_handler previousInvalidParameter = nullptr;
    _crt_signal_t previousAbort = SIG_DFL;

    // Filled in by the crashed thread before it wakes the writer.
    EXCEPTION_POINTERS* pointers = nullptr;
    DWORD threadId = 0;
    Line line;

    void writeDump();
};

namespace {

std::atomic<CrashHandler::State*> g_state{nullptr};
std::atomic<DWORD> g_crashingThread{0};

/// The crashed thread's part: what happened into the line (with its time and the thread's name), the writer woken and waited for,
/// the process ended. A second crash meanwhile waits for the first to end the process; a crash inside this path ends it at once.
[[noreturn]] void handleCrash(Kind kind, DWORD code, EXCEPTION_POINTERS* pointers) {
    const DWORD self = GetCurrentThreadId();
    DWORD expected = 0;
    if (!g_crashingThread.compare_exchange_strong(expected, self)) {
        if (expected == self) TerminateProcess(GetCurrentProcess(), code);
        for (;;) Sleep(INFINITE);
    }
    CrashHandler::State* const state = g_state.load();
    if (state != nullptr) {
        state->pointers = pointers;
        state->threadId = self;
        Line& line = state->line;
        SYSTEMTIME now{};
        GetLocalTime(&now);
        line.addNumber(now.wYear, 4);
        line.add("-");
        line.addNumber(now.wMonth, 2);
        line.add("-");
        line.addNumber(now.wDay, 2);
        line.add(" ");
        line.addNumber(now.wHour, 2);
        line.add(":");
        line.addNumber(now.wMinute, 2);
        line.add(":");
        line.addNumber(now.wSecond, 2);
        line.add(".");
        line.addNumber(now.wMilliseconds, 3);
        line.add(" ERROR [");
        char name[128];
        diag::currentThreadNameUtf8(name, sizeof name);
        line.add(name);
        line.add("] xgs.crash - ");
        switch (kind) {
            case Kind::Seh:
                line.add(reinterpret_cast<const char*>(u8"처리되지 않은 예외 "));
                line.addHex(code);
                break;
            case Kind::Terminate: line.add("std::terminate"); break;
            case Kind::PureCall: line.add(reinterpret_cast<const char*>(u8"순수 가상 함수 호출")); break;
            case Kind::InvalidParameter: line.add(reinterpret_cast<const char*>(u8"CRT 함수에 잘못된 인수")); break;
            case Kind::Abort: line.add("abort()"); break;
        }
        line.add(reinterpret_cast<const char*>(u8"로 끝남 "));
        if (state->request != nullptr && state->done != nullptr && SetEvent(state->request)) {
            WaitForSingleObject(state->done, WriterWaitMs);
        }
    }
    TerminateProcess(GetCurrentProcess(), code);
    for (;;) Sleep(INFINITE);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers) {
    const DWORD code = pointers != nullptr && pointers->ExceptionRecord != nullptr ? pointers->ExceptionRecord->ExceptionCode : 0;
    handleCrash(Kind::Seh, code, pointers);
}

[[noreturn]] void onTerminate() {
    handleCrash(Kind::Terminate, ExitCodeOfAbort, nullptr);
}

void __cdecl onPureCall() {
    handleCrash(Kind::PureCall, ExitCodeOfCrtCheck, nullptr);
}

void __cdecl onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    handleCrash(Kind::InvalidParameter, ExitCodeOfCrtCheck, nullptr);
}

void __cdecl onAbort(int) {
    handleCrash(Kind::Abort, ExitCodeOfAbort, nullptr);
}

}  // namespace

/// The writer's part (its own thread, its own stack): the dump into the file opened at the start, emptied first, and the end of the
/// line through the logger's raw write.
void CrashHandler::State::writeDump() {
    bool written = false;
    if (dumpFile != INVALID_HANDLE_VALUE) {
        if (removeUnusedDump) setDeleteOnClose(dumpFile, false);
        LARGE_INTEGER start{};
        SetFilePointerEx(dumpFile, start, nullptr, FILE_BEGIN);
        SetEndOfFile(dumpFile);
        MINIDUMP_EXCEPTION_INFORMATION exception{};
        exception.ThreadId = threadId;
        exception.ExceptionPointers = pointers;
        exception.ClientPointers = FALSE;
        written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile, MiniDumpNormal,
                                    pointers != nullptr ? &exception : nullptr, nullptr, nullptr) != FALSE;
        FlushFileBuffers(dumpFile);
    }
    line.add(written ? reinterpret_cast<const char*>(u8"(덤프: xgetsongs-crash.dmp)") : reinterpret_cast<const char*>(u8"(덤프 실패)"));
    line.end();
    if (logger != nullptr) logger->writeRaw(line.text, static_cast<unsigned long>(line.size));
}

CrashHandler::CrashHandler(const QString& logDir, diag::FileLogger* logger) noexcept {
    try {
        if (g_state.load() != nullptr) return;   // one at a time: the installed one keeps its file and its handlers
        auto state = std::make_unique<State>();
        state->logger = logger;
        const QString path = joined(logDir, CrashDumpFileName);
        state->dumpFile = CreateFileW(wide(path), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (state->dumpFile != INVALID_HANDLE_VALUE && GetLastError() != ERROR_ALREADY_EXISTS) {
            // New and empty: it goes when the handle closes, unless a dump is written into it.
            state->removeUnusedDump = setDeleteOnClose(state->dumpFile, true);
        }
        state->request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        state->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        state->quit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (state->request == nullptr || state->done == nullptr || state->quit == nullptr) {
            m_state = std::move(state);
            return;
        }
        State* const raw = state.get();
        state->writer = std::jthread([raw] {
            diag::setCurrentThreadName(QStringLiteral("xgs-crash-writer"));
            const HANDLE waits[2] = {raw->request, raw->quit};
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0) {
                raw->writeDump();
                SetEvent(raw->done);
            }
        });
        m_state = std::move(state);

        State* expected = nullptr;
        if (!g_state.compare_exchange_strong(expected, raw)) return;   // another handler is installed
        raw->previousFilter = SetUnhandledExceptionFilter(onUnhandledException);
        raw->previousTerminate = std::set_terminate(onTerminate);
        raw->previousPureCall = _set_purecall_handler(onPureCall);
        raw->previousInvalidParameter = _set_invalid_parameter_handler(onInvalidParameter);
        raw->previousAbort = std::signal(SIGABRT, onAbort);
        raw->installed = true;
    } catch (...) {
        // Without memory or a thread there is no crash handler; the app runs without one.
    }
}

CrashHandler::~CrashHandler() {
    if (!m_state) return;
    State& s = *m_state;
    if (s.installed) {
        SetUnhandledExceptionFilter(s.previousFilter);
        std::set_terminate(s.previousTerminate);
        _set_purecall_handler(s.previousPureCall);
        _set_invalid_parameter_handler(s.previousInvalidParameter);
        std::signal(SIGABRT, s.previousAbort);
        g_state.store(nullptr);
    }
    if (s.quit != nullptr) SetEvent(s.quit);
    if (s.writer.joinable()) s.writer.join();
    for (HANDLE event : {s.request, s.done, s.quit}) {
        if (event != nullptr) CloseHandle(event);
    }
    if (s.dumpFile != INVALID_HANDLE_VALUE) CloseHandle(s.dumpFile);
}

bool CrashHandler::installed() const {
    return m_state && m_state->installed;
}

bool writeMiniDump(const QString& path) noexcept {
    try {
        const QString native = QDir::toNativeSeparators(path);
        const HANDLE file = CreateFileW(wide(native), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        const bool written =
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, nullptr, nullptr, nullptr) != FALSE;
        CloseHandle(file);
        if (!written) DeleteFileW(wide(native));
        return written;
    } catch (...) {
        return false;
    }
}

}  // namespace xgs::app
