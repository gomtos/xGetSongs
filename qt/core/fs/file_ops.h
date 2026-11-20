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
    QString error;   // Failed only: Windows' message
};

/// Moves a file (MoveFileExW). Onto another drive it copies, then deletes. When replace is false and the target exists, the
/// result is TargetExists and neither file changes.
MoveResult moveFile(const QString& from, const QString& to, bool replace);

/// Writes bytes into a file, created or truncated.
Outcome<std::monostate> writeBytes(const QString& path, const QByteArray& bytes);

/// Writes text as UTF-8 without a byte order mark. Text with an unpaired surrogate is not written at all, as Kotlin's
/// `Files.writeString(path, text, UTF_8)` (observed: it throws before it touches the file, so no file is created and an
/// existing one keeps its content): the result is Failed with the message Kotlin's exception carries, `Input length = 1`.
Outcome<std::monostate> writeUtf8(const QString& path, const QString& text);

/// true when path names an existing file that is not a directory.
bool isRegularFile(const QString& path);

/// true when path names an existing file or directory (Kotlin's `Files.exists`).
bool exists(const QString& path);

/// Deletes a file; a missing file or a failure is ignored.
void removeQuietly(const QString& path);

/// Creates a directory and the missing ones above it. true when the directory exists afterwards.
bool makeDirectories(const QString& path);

}  // namespace xgs::fs
