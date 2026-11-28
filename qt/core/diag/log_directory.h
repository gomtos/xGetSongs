#pragma once

#include <QString>
#include <QStringList>

#include <functional>

// Where the log files go. The Tauri shell passes no log folder, so the candidates are `<appData>\logs` (appData is the app's
// own folder, %APPDATA%\xGetSongs) and then `<temp>\xgetsongs-logs` (a profile whose roaming folder cannot be written still
// has a temp folder). Port of Diagnostics.kt's ensureLogDirectory and chooseLogDirectory and of sidecarLogCandidates.
namespace xgs::diag {

/// Where a message about a failure goes; the default (an empty function) writes the line to stderr.
using Report = std::function<void(const QString&)>;

/// The folders to try, in order. Nothing is read from the environment.
QStringList logDirectoryCandidates(const QString& appData, const QString& tempDir);

/// Creates dir with its parents (the log files would be created on first use anyway). false on failure, which is only
/// reported (`로그 폴더를 만들지 못함: <dir> (<Windows' text>)`), never thrown.
bool ensureLogDirectory(const QString& dir, const Report& report = {});

/// The first of candidates that can be created; the last one when none can (the file logger then does nothing). Empty for an
/// empty list. Never throws.
QString chooseLogDirectory(const QStringList& candidates, const Report& report = {});

}  // namespace xgs::diag
