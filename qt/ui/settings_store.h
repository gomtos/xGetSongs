#pragma once

#include <QJsonValue>
#include <QString>

#include "ui_types.h"

// The remembered options: the settings part of shell/src/desktop.ts and the settings file the Tauri shell kept for the screen
// (sidecar-host/src/settings.rs, `settings.json` in the app data folder, the file the Compose app wrote and read).
namespace xgs::ui {

/// desktop.ts normalizeSettings: the settings as the Rust side handed them over, all four keys with the right types (outputDir
/// a string or null, the others booleans), else the defaults.
UserSettings normalizeSettings(const QJsonValue& raw);

/// Keeps the options in a small UTF-8 JSON file. Neither function throws or fails, whatever goes wrong. load reads the file as
/// the Rust shell read it: a key that is missing takes its default, a key of the wrong type makes the whole file unusable,
/// unknown keys are ignored, a boolean may be quoted (`"true"`), a UTF-8 byte order mark is skipped, and a file that is missing,
/// unreadable, not valid UTF-8 or not a JSON object means the defaults. (What it hands out is always something normalizeSettings
/// would take as it is.) save writes all four keys to `<file>.tmp` next to the target and moves that over the file (tried again
/// for about 1.5 s while another program holds the file, see fs::moveFile), creating the folders above it; a place that cannot be
/// written means the options are not kept (false), and no temporary file stays. Saves from several threads are taken one at a
/// time. load and save are virtual so that tests can stand in for the file.
class SettingsStore {
public:
    /// filePath: where the file is. defaultOutputDir: the folder the screen uses while the settings name none.
    explicit SettingsStore(QString filePath, QString defaultOutputDir = standardOutputDir());
    virtual ~SettingsStore() = default;

    SettingsStore(const SettingsStore&) = default;
    SettingsStore& operator=(const SettingsStore&) = default;

    /// `Music\xGetSongs` in the user's home folder (%USERPROFILE%), with backslashes: the folder the app saved into before.
    static QString standardOutputDir();

    QString filePath() const { return m_filePath; }
    QString defaultOutputDir() const { return m_defaultOutputDir; }

    virtual UserSettings load();
    /// True when the file now holds settings.
    virtual bool save(const UserSettings& settings);

private:
    QString m_filePath;
    QString m_defaultOutputDir;
};

}  // namespace xgs::ui
