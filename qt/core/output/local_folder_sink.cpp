#include "output/local_folder_sink.h"

#include <stdexcept>
#include <utility>

#include "fs/file_ops.h"
#include "paths/paths.h"
#include "text/kt_text.h"

namespace xgs::output {

using namespace Qt::StringLiterals;

bool isAcceptedFileName(const QString& fileName) {
    if (kt::isBlank(fileName) || fileName == u"." || fileName == u"..") return false;
    const std::optional<QString> path = paths::fromText(fileName);   // Path.of throws InvalidPathException: rejected
    if (!path) return false;
    // A root only (`C:`, `\`) has no file name; Kotlin throws NullPointerException there (observed), which rejects it too.
    const std::optional<QString> name = paths::fileName(*path);
    return name && *name == fileName;
}

LocalFolderSink::LocalFolderSink(QString directory) : m_directory(std::move(directory)) {
    fs::makeDirectories(m_directory);   // a failure shows later, in put (see the header)
}

QString LocalFolderSink::directory() const {
    return m_directory;
}

bool LocalFolderSink::exists(const QString& fileName) {
    const QString target = resolve(fileName);
    return !target.isEmpty() && fs::exists(target);
}

PutResult LocalFolderSink::put(const QString& fileName, const QString& source, bool overwrite) {
    const QString target = resolve(fileName);
    if (target.isEmpty()) return {PutStatus::Failed, u"the output folder is not a path: "_s + m_directory};
    const std::lock_guard lock(m_putLock);
    const fs::MoveResult moved = fs::moveFile(source, target, overwrite);
    switch (moved.status) {
        case fs::MoveStatus::Moved: return {PutStatus::Stored, QString()};
        case fs::MoveStatus::TargetExists: return {PutStatus::AlreadyExists, QString()};
        case fs::MoveStatus::Failed: break;
    }
    return {PutStatus::Failed, moved.error};
}

// The target path of an accepted name; empty when the directory is not a path (it must be a result of paths::fromText).
QString LocalFolderSink::resolve(const QString& fileName) const {
    if (!isAcceptedFileName(fileName))
        throw std::invalid_argument((u"file name must not contain path segments: "_s + fileName).toStdString());
    return paths::child(m_directory, fileName).value_or(QString());
}

}  // namespace xgs::output
