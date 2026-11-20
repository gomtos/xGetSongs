#include "process/system_process_runner.h"

#include <QList>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <windows.h>

#include "process/command_line.h"
#include "process/line_splitter.h"

namespace xgs::process {

namespace {

using namespace Qt::StringLiterals;

// A Win32 handle closed at the end of its scope (null and INVALID_HANDLE_VALUE are no handle).
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : m_handle(handle) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return m_handle; }
    HANDLE* out() {
        close();
        return &m_handle;
    }
    bool valid() const { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }
    void close() {
        if (valid()) CloseHandle(m_handle);
        m_handle = nullptr;
    }

private:
    HANDLE m_handle = nullptr;
};

const wchar_t* wide(const QString& text) {
    return reinterpret_cast<const wchar_t*>(text.utf16());
}

// Windows' message for an error code, without the line break at its end.
QString errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    QString text = length > 0 ? QString::fromWCharArray(buffer, qsizetype(length)) : QString();
    if (buffer) LocalFree(buffer);
    while (!text.isEmpty() && (text.back() == u'\r' || text.back() == u'\n' || text.back() == u' ')) text.chop(1);
    if (text.isEmpty()) text = u"Windows error %1"_s.arg(code);
    return text;
}

RunResult startFailure(const QString& error) {
    return {RunStatus::StartFailed, -1, error};
}

// ASCII case-insensitive comparison of a short ASCII suffix.
bool endsWithAsciiIgnoringCase(const QString& text, QStringView suffix) {
    if (text.size() < suffix.size()) return false;
    const QStringView tail = QStringView(text).last(suffix.size());
    for (qsizetype i = 0; i < suffix.size(); ++i) {
        char16_t a = tail.at(i).unicode();
        if (a >= u'A' && a <= u'Z') a = char16_t(a + (u'a' - u'A'));
        if (a != suffix.at(i).unicode()) return false;
    }
    return true;
}

// true for a program that Windows would run through cmd.exe: a name ending in .bat or .cmd, in any case, once the trailing
// dots and spaces that Win32 drops from a path are dropped.
bool isBatchFile(const QString& program) {
    QString name = program;
    while (!name.isEmpty() && (name.back() == u'.' || name.back() == u' ')) name.chop(1);
    return endsWithAsciiIgnoringCase(name, u".bat") || endsWithAsciiIgnoringCase(name, u".cmd");
}

// The name of an environment entry `name=value` (an entry may start with `=`, as the per-drive `=C:=C:\dir`).
QStringView entryName(QStringView entry) {
    const qsizetype equals = entry.indexOf(u'=', 1);
    return equals < 0 ? entry : entry.first(equals);
}

bool sameNameIgnoringCase(QStringView a, QStringView b) {
    return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(a.utf16()), int(a.size()), reinterpret_cast<const wchar_t*>(b.utf16()),
                                int(b.size()), TRUE) == CSTR_EQUAL;
}

// This process's environment with PYTHONIOENCODING=utf-8 and PYTHONUTF8=1 (replacing any value they had), as a
// CREATE_UNICODE_ENVIRONMENT block sorted by name, case-insensitively, as CreateProcessW's documentation asks.
std::wstring environmentBlock() {
    static constexpr QStringView added[] = {u"PYTHONIOENCODING=utf-8", u"PYTHONUTF8=1"};
    QList<QString> entries;
    if (wchar_t* strings = GetEnvironmentStringsW()) {
        for (const wchar_t* p = strings; *p; p += wcslen(p) + 1) {
            const QString entry = QString::fromWCharArray(p);
            const bool replaced = std::any_of(std::begin(added), std::end(added),
                                              [&](QStringView a) { return sameNameIgnoringCase(entryName(entry), entryName(a)); });
            if (!replaced) entries.append(entry);
        }
        FreeEnvironmentStringsW(strings);
    }
    for (QStringView a : added) entries.append(a.toString());
    std::stable_sort(entries.begin(), entries.end(), [](const QString& a, const QString& b) {
        const QStringView x = entryName(a), y = entryName(b);
        return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(x.utf16()), int(x.size()),
                                    reinterpret_cast<const wchar_t*>(y.utf16()), int(y.size()), TRUE) == CSTR_LESS_THAN;
    });
    std::wstring block;
    for (const QString& entry : entries) {
        block.append(reinterpret_cast<const wchar_t*>(entry.utf16()), size_t(entry.size()));
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

// One reading thread: the pipe's bytes into a LineSplitter until the pipe is broken (every writer gone), then finish().
void readLines(HANDLE pipe, const LineHandler& handler) {
    LineSplitter splitter([&handler](const QString& line) {
        if (handler) handler(line);
    });
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        DWORD read = 0;
        // ERROR_BROKEN_PIPE: the end; ERROR_OPERATION_ABORTED: cancelled after a stop (Readers::joinAfterKill)
        if (!ReadFile(pipe, buffer.data(), DWORD(buffer.size()), &read, nullptr)) break;
        if (read > 0) splitter.feed(QByteArrayView(buffer.data(), qsizetype(read)));
    }
    splitter.finish();
}

// The two reading threads. Should run leave early (an exception), the destructor kills the job first, so that the pipes break
// and the threads end, then joins them.
class Readers {
public:
    explicit Readers(HANDLE job) : m_job(job) {}
    ~Readers() {
        if (!m_out.joinable() && !m_err.joinable()) return;
        TerminateJobObject(m_job, 1);
        joinAfterKill();
    }
    Readers(const Readers&) = delete;
    Readers& operator=(const Readers&) = delete;
    void start(HANDLE out, const LineHandler& onOut, HANDLE err, const LineHandler& onErr) {
        m_out = std::thread(readLines, out, std::cref(onOut));
        m_err = std::thread(readLines, err, std::cref(onErr));
    }
    void join() {
        if (m_out.joinable()) m_out.join();
        if (m_err.joinable()) m_err.join();
    }
    // After the job was killed: the pipes break when every writer is gone, which is at once unless a process outside the job
    // holds a writing end (it could only have got one from code in this process that starts processes inheriting every
    // inheritable handle). So that a stop always returns, a reader still blocked after 2 s has its ReadFile cancelled
    // (CancelSynchronousIo, repeated until the thread is done, since a cancel finds nothing to cancel while a handler runs).
    void joinAfterKill() {
        for (std::thread* reader : {&m_out, &m_err}) {
            if (!reader->joinable()) continue;
            if (WaitForSingleObject(reader->native_handle(), 2000) == WAIT_TIMEOUT)
                while (WaitForSingleObject(reader->native_handle(), 100) == WAIT_TIMEOUT) CancelSynchronousIo(reader->native_handle());
            reader->join();
        }
    }
    // Waits until both threads have ended (true) or stop is signalled first (false).
    bool waitUnlessStopped(HANDLE stop) {
        for (;;) {
            HANDLE waits[3];
            DWORD count = 0;
            if (m_out.joinable() && WaitForSingleObject(m_out.native_handle(), 0) == WAIT_TIMEOUT) waits[count++] = m_out.native_handle();
            if (m_err.joinable() && WaitForSingleObject(m_err.native_handle(), 0) == WAIT_TIMEOUT) waits[count++] = m_err.native_handle();
            if (count == 0) return true;
            waits[count] = stop;
            const DWORD woke = WaitForMultipleObjects(count + 1, waits, FALSE, INFINITE);
            if (woke == WAIT_OBJECT_0 + count) return false;
            if (woke == WAIT_FAILED) return false;
        }
    }

private:
    HANDLE m_job;
    std::thread m_out;
    std::thread m_err;
};

}  // namespace

RunResult SystemProcessRunner::run(const QStringList& command, const LineHandler& onStdout, const LineHandler& onStderr,
                                   std::stop_token stop) {
    if (stop.stop_requested()) return {RunStatus::Cancelled, -1, QString()};
    if (command.isEmpty() || command.first().isEmpty()) return startFailure(u"실행할 프로그램이 없습니다"_s);
    if (isBatchFile(command.first())) return startFailure(u"배치 파일(.cmd, .bat)은 실행하지 않습니다"_s);

    // A stop request sets this event, so that one wait can watch the process (or the readers) and the stop together.
    Handle stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stopEvent.valid()) return startFailure(errorText(GetLastError()));
    const std::stop_callback onStop(stop, [&stopEvent] { SetEvent(stopEvent.get()); });

    // The job: every process in it dies when the last handle to it is closed (also when this process dies).
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.valid()) return startFailure(errorText(GetLastError()));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return startFailure(errorText(GetLastError()));

    // The pipes, all ends created not inheritable. Standard input: the writing end is closed at once.
    Handle inRead, inWrite, outRead, outWrite, errRead, errWrite;
    if (!CreatePipe(inRead.out(), inWrite.out(), nullptr, 0) || !CreatePipe(outRead.out(), outWrite.out(), nullptr, 0) ||
        !CreatePipe(errRead.out(), errWrite.out(), nullptr, 0))
        return startFailure(errorText(GetLastError()));
    inWrite.close();

    // The child's three ends become inheritable just before CreateProcessW and are closed right after it. Even meanwhile no
    // other child of this runner can inherit them: every CreateProcessW here passes its own handle list, and a child
    // inherits only the handles in its list.
    HANDLE inherited[] = {inRead.get(), outWrite.get(), errWrite.get()};
    for (HANDLE handle : inherited)
        if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) return startFailure(errorText(GetLastError()));
    HANDLE jobs[] = {job.get()};

    SIZE_T listSize = 0;
    InitializeProcThreadAttributeList(nullptr, 2, 0, &listSize);
    std::unique_ptr<char[]> listBuffer(new char[listSize]);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listBuffer.get());
    if (!InitializeProcThreadAttributeList(list, 2, 0, &listSize)) return startFailure(errorText(GetLastError()));
    const std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, decltype(&DeleteProcThreadAttributeList)> listGuard(
        list, &DeleteProcThreadAttributeList);
    // (UpdateProcThreadAttribute keeps pointers to the arrays; they live until the end of run.)
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) ||
        !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs, sizeof(jobs), nullptr, nullptr))
        return startFailure(errorText(GetLastError()));

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inRead.get();
    startup.StartupInfo.hStdOutput = outWrite.get();
    startup.StartupInfo.hStdError = errWrite.get();
    startup.lpAttributeList = list;

    const QString program = command.first();
    std::wstring commandLine = windowsCommandLine(command).toStdWString();
    std::wstring environment = environmentBlock();
    PROCESS_INFORMATION info{};
    const BOOL started = CreateProcessW(wide(program), commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                                        environment.data(), nullptr, &startup.StartupInfo, &info);
    const DWORD startError = GetLastError();
    // This process keeps no child end: the pipes break when the last process of the job that holds them is gone.
    inRead.close();
    outWrite.close();
    errWrite.close();
    if (!started) return startFailure(errorText(startError));
    const Handle process(info.hProcess);
    CloseHandle(info.hThread);

    Readers readers(job.get());
    readers.start(outRead.get(), onStdout, errRead.get(), onStderr);

    HANDLE waits[] = {process.get(), stopEvent.get()};
    const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (woke != WAIT_OBJECT_0 || !readers.waitUnlessStopped(stopEvent.get())) {
        // Stopped (or the wait failed): the whole tree dies, so every pipe breaks and the readers end.
        TerminateJobObject(job.get(), 1);
        readers.joinAfterKill();
        return {RunStatus::Cancelled, -1, QString()};
    }
    readers.join();
    DWORD exitCode = 0;
    GetExitCodeProcess(process.get(), &exitCode);
    // Closing the job (at the end of the scope) kills what the child left running.
    return {RunStatus::Exited, int(exitCode), QString()};
}

}  // namespace xgs::process
