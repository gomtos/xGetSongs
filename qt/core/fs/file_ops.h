#pragma once

#include <QByteArray>
#include <QString>

#include <variant>

#include "outcome.h"

// File operations of the engine on Windows (Win32 directly). A path of 248 units or more is handed to Win32 in its extended
// form (`\\?\` in front of the full path), so long paths work without the system's long path setting. The error texts are
// Windows' own messages (FormatMessageW, in the user's language).
namespace xgs::fs {

enum class MoveStatus { Moved, TargetExists, Failed };
struct MoveResult {
    MoveStatus status;
    QString error;      // Failed only: Windows' message
    quint32 code = 0;   // Failed only: the Win32 error code (GetLastError), whatever the language of the message
};

/// Moves a file (MoveFileExW). Onto another drive it copies, then deletes. When replace is false and the target exists, the
/// result is TargetExists and neither file changes.
///
/// A deliberate difference from Kotlin's Files.move, which tries once: a move that fails with ERROR_SHARING_VIOLATION,
/// ERROR_ACCESS_DENIED or ERROR_LOCK_VIOLATION is tried again, up to 10 more times after pauses of 20, 50, 100, 150 and then
/// 200 ms (1.52 s of pauses in all), because another program (a virus scanner, an indexer) often holds a file for a moment
/// right after it was written. Then the result is Failed with the last error's text and code. Any other failure (a missing
/// source, TargetExists) is answered at once. A failure that lasts (a read-only target, a running yt-dlp.exe) is therefore
/// reported about 1.5 s later than in Kotlin.
MoveResult moveFile(const QString& from, const QString& to, bool replace);

/// Writes bytes into a file, created or truncated. A write that reports success without writing anything is Failed (Windows'
/// text for ERROR_WRITE_FAULT) instead of being tried forever.
Outcome<std::monostate> writeBytes(const QString& path, const QByteArray& bytes);

/// Writes text as UTF-8 without a byte order mark. Text with an unpaired surrogate is not written at all, as Kotlin's
/// `Files.writeString(path, text, UTF_8)` (observed: it throws before it touches the file, so no file is created and an
/// existing one keeps its content): the result is Failed with the message Kotlin's exception carries, `Input length = 1`.
Outcome<std::monostate> writeUtf8(const QString& path, const QString& text);

/// true when path names an existing file that is not a directory.
bool isRegularFile(const QString& path);

/// true when path names an existing file or directory (Kotlin's `Files.exists`).
bool exists(const QString& path);

/// true when path names an existing directory.
bool isDirectory(const QString& path);

/// Removes path when it is an empty directory (RemoveDirectoryW, at any length). true when it was removed.
bool removeEmptyDirectory(const QString& path);

/// Windows' message for a Win32 error code (FormatMessageW, in the user's language) without the line break at its end;
/// `Windows error N` when Windows has none.
QString errorText(quint32 code);

/// Deletes a file; a missing file or a failure is ignored.
void removeQuietly(const QString& path);

/// Creates a directory and the missing ones above it. true when the directory exists afterwards.
bool makeDirectories(const QString& path);

}  // namespace xgs::fs
