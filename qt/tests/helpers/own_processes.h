#pragma once

#include <QList>
#include <QString>

#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>

#include <vector>

// Finds the processes a test started by what their command lines hold (a test's own temporary folder), so a test can check
// that none of them is left without looking at, or touching, any other process on the machine (the user's own xGetSongs and
// its yt-dlp and ffmpeg may be running). It only reads: nothing here kills or changes a process.
namespace xgs::testsupport {

inline FILETIME nowAsFileTime() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return now;
}

/// The command line of a process opened with PROCESS_QUERY_LIMITED_INFORMATION ("" when it cannot be read), through
/// NtQueryInformationProcess(ProcessCommandLineInformation), which Windows 8.1 and later answer for that access right.
inline QString commandLineOf(HANDLE process) {
    using Query = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query =
        reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    constexpr ULONG ProcessCommandLineInformation = 60;
    if (!query) return QString();
    ULONG size = 0;
    query(process, ProcessCommandLineInformation, nullptr, 0, &size);
    if (size < sizeof(UNICODE_STRING)) return QString();
    std::vector<unsigned char> buffer(size);
    if (query(process, ProcessCommandLineInformation, buffer.data(), size, &size) < 0) return QString();
    const auto* text = reinterpret_cast<const UNICODE_STRING*>(buffer.data());
    return QString::fromWCharArray(text->Buffer, qsizetype(text->Length / sizeof(wchar_t)));
}

struct SeenProcess {
    DWORD pid = 0;
    QString image;   // the full path of its executable ("" when it cannot be read)
};

/// The processes created at or after since whose command line contains token (ignoring case), this process left out.
/// Processes that cannot be opened for PROCESS_QUERY_LIMITED_INFORMATION (protected ones, other users') are passed over.
inline QList<SeenProcess> processesMentioning(const QString& token, const FILETIME& since) {
    QList<SeenProcess> found;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return found;
    const ULONGLONG sinceValue = (ULONGLONG(since.dwHighDateTime) << 32) | since.dwLowDateTime;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == 0 || entry.th32ProcessID == GetCurrentProcessId()) continue;
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) continue;
        FILETIME created{}, exited{}, kernel{}, user{};
        const bool times = GetProcessTimes(process, &created, &exited, &kernel, &user);
        const ULONGLONG createdValue = (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        // A process that has exited but is still referenced by a handle somewhere has an exit time; it is not running.
        DWORD exitCode = 0;
        const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
        if (times && running && createdValue >= sinceValue && commandLineOf(process).contains(token, Qt::CaseInsensitive)) {
            wchar_t image[32768];
            DWORD length = DWORD(std::size(image));
            found.append({entry.th32ProcessID,
                          QueryFullProcessImageNameW(process, 0, image, &length) ? QString::fromWCharArray(image, length) : QString()});
        }
        CloseHandle(process);
    }
    CloseHandle(snapshot);
    return found;
}

/// Waits until processesMentioning(token, since) is empty, at most limitMs, and returns what is left (empty when none is).
/// Each process found is waited for through a handle of its own (an event, not a poll); the list is read again afterwards
/// because a process can start another before it ends.
inline QList<SeenProcess> waitForNoneMentioning(const QString& token, const FILETIME& since, DWORD limitMs) {
    const ULONGLONG end = GetTickCount64() + limitMs;
    QList<SeenProcess> left = processesMentioning(token, since);
    while (!left.isEmpty() && GetTickCount64() < end) {
        for (const SeenProcess& seen : left) {
            const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, seen.pid);
            if (!process) continue;
            const ULONGLONG now = GetTickCount64();
            WaitForSingleObject(process, now < end ? DWORD(end - now) : 0);
            CloseHandle(process);
        }
        left = processesMentioning(token, since);
    }
    return left;
}

}  // namespace xgs::testsupport
