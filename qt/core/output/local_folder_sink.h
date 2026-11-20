#pragma once

#include <QString>

#include <mutex>

#include "output/output_sink.h"

// Port of engine/.../output/LocalFolderSink.kt: an OutputSink that is a folder on this PC.
namespace xgs::output {

/// The check of Kotlin's LocalFolderSink.resolve: fileName is not blank (Kotlin's isBlank), not `.` or `..`, and is a path
/// whose file name is itself (so it holds no separator, no drive and no character a path cannot have). Observed through the
/// differential (kind W, op fn). Names of dots and spaces only, such as `...` or `.. `, are accepted, as in Kotlin, although
/// Windows resolves them to the folder itself or to its parent.
bool isAcceptedFileName(const QString& fileName);

class LocalFolderSink final : public OutputSink {
public:
    /// directory is a result of paths::fromText (an absolute path). Creates the directory and the missing ones above it.
    /// Unlike Kotlin's constructor this one does not throw when that fails: while directory is not an existing directory, put
    /// is Failed (Windows' text for ERROR_PATH_NOT_FOUND, nothing moved) and exists answers false. (Windows resolves a name of
    /// dots and spaces only to the folder itself, so a put must not reach a regular file that has the folder's name.)
    explicit LocalFolderSink(QString directory);

    QString directory() const;

    /// Whether a file or a directory of that name is in the folder. Throws std::invalid_argument for a name
    /// isAcceptedFileName rejects, before any file access.
    bool exists(const QString& fileName) override;

    /// Moves source into the folder. Replacing a file is not atomic on Windows, so two overlapping puts of one name could fail
    /// each other; puts run one at a time (two playlist entries can share a name when the rank is left out of it, and a move
    /// is short next to a download). Throws std::invalid_argument for a name isAcceptedFileName rejects, before any file
    /// access.
    PutResult put(const QString& fileName, const QString& source, bool overwrite) override;

private:
    QString resolve(const QString& fileName) const;

    QString m_directory;
    std::mutex m_putLock;
};

}  // namespace xgs::output
