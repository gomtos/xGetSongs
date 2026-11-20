#include "fs/file_ops.h"

#include <QList>

#include <algorithm>

#include <windows.h>

namespace xgs::fs {

namespace {

using namespace Qt::StringLiterals;

const wchar_t* wide(const QString& text) {
    return reinterpret_cast<const wchar_t*>(text.utf16());
}

// GetFullPathNameW: the absolute form of path (it also turns `/` into `\` and resolves `.` and `..`, as every Win32 call does
// to a path in its ordinary form). path itself when it fails.
QString fullPath(const QString& path) {
    QList<wchar_t> buffer(MAX_PATH);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const DWORD length = GetFullPathNameW(wide(path), DWORD(buffer.size()), buffer.data(), nullptr);
        if (length == 0) return path;
        if (length < DWORD(buffer.size())) return QString::fromWCharArray(buffer.data(), qsizetype(length));
        buffer.resize(qsizetype(length) + 1);
    }
    return path;
}

// The form of path that Win32 is given. Win32 refuses an ordinary path of MAX_PATH (260) units or more (CreateDirectoryW
// already from MAX_PATH - 12) unless the system's long path setting is on; the extended form `\\?\` + the full path is
// accepted at any length. Shorter paths stay as they are, so that Win32 cleans them as usual.
QString win32Path(const QString& path) {
    if (path.size() < MAX_PATH - 12 || path.startsWith(u"\\\\?\\") || path.startsWith(u"\\\\.\\")) return path;
    const QString full = fullPath(path);
    if (full.startsWith(u"\\\\?\\") || full.startsWith(u"\\\\.\\")) return full;
    if (full.startsWith(u"\\\\")) return u"\\\\?\\UNC\\"_s + full.sliced(2);
    return u"\\\\?\\"_s + full;
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

bool hasUnpairedSurrogate(const QString& text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            ++i;
            continue;
        }
        if (c.isSurrogate()) return true;
    }
    return false;
}

DWORD attributesOf(const QString& path) {
    return GetFileAttributesW(wide(win32Path(path)));
}

bool isDirectory(const QString& path) {
    const DWORD attributes = attributesOf(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

}  // namespace

MoveResult moveFile(const QString& from, const QString& to, bool replace) {
    const DWORD flags = MOVEFILE_COPY_ALLOWED | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (MoveFileExW(wide(win32Path(from)), wide(win32Path(to)), flags)) return {MoveStatus::Moved, QString()};
    const DWORD code = GetLastError();
    if (!replace && (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS)) return {MoveStatus::TargetExists, QString()};
    return {MoveStatus::Failed, errorText(code)};
}

Outcome<std::monostate> writeBytes(const QString& path, const QByteArray& bytes) {
    const HANDLE file = CreateFileW(wide(win32Path(path)), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return Outcome<std::monostate>::failed(errorText(GetLastError()));
    qsizetype written = 0;
    while (written < bytes.size()) {
        const DWORD chunk = DWORD(std::min<qsizetype>(bytes.size() - written, 1 << 30));
        DWORD done = 0;
        if (!WriteFile(file, bytes.constData() + written, chunk, &done, nullptr)) {
            const DWORD code = GetLastError();
            CloseHandle(file);
            return Outcome<std::monostate>::failed(errorText(code));
        }
        written += qsizetype(done);
    }
    if (!CloseHandle(file)) return Outcome<std::monostate>::failed(errorText(GetLastError()));
    return Outcome<std::monostate>::ok(std::monostate());
}

Outcome<std::monostate> writeUtf8(const QString& path, const QString& text) {
    // Kotlin's encoder refuses the text before the file is opened (Step 3 of the Q2b Task 1 report)
    if (hasUnpairedSurrogate(text)) return Outcome<std::monostate>::failed(u"Input length = 1"_s);
    return writeBytes(path, text.toUtf8());
}

bool isRegularFile(const QString& path) {
    const DWORD attributes = attributesOf(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool exists(const QString& path) {
    return attributesOf(path) != INVALID_FILE_ATTRIBUTES;
}

void removeQuietly(const QString& path) {
    DeleteFileW(wide(win32Path(path)));
}

bool makeDirectories(const QString& path) {
    if (isDirectory(path)) return true;
    // every directory from the top down; creating the drive or a UNC host fails and is ignored, the last check decides
    const QString full = fullPath(path);
    for (qsizetype at = full.indexOf(u'\\', 3); at >= 0; at = full.indexOf(u'\\', at + 1))
        CreateDirectoryW(wide(win32Path(full.first(at))), nullptr);
    CreateDirectoryW(wide(win32Path(full)), nullptr);
    return isDirectory(full);
}

}  // namespace xgs::fs
