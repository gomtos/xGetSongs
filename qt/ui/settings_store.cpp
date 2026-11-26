#include "settings_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>

#include <mutex>

#include "fs/file_ops.h"
#include "paths/paths.h"

using namespace Qt::StringLiterals;

namespace xgs::ui {

namespace {

/// Two saves at once (the options change while the window closes) must not replace the file together.
std::mutex writeLock;

/// The value of a boolean key as the Rust shell read it: missing is the default, `true`/`false` or the quoted text of one,
/// anything else makes the file unusable (empty).
std::optional<bool> flag(const QJsonObject& object, const QString& key, bool fallback) {
    const auto it = object.constFind(key);
    if (it == object.constEnd()) return fallback;
    if (it->isBool()) return it->toBool();
    if (it->isString()) {
        if (it->toString() == u"true"_s) return true;
        if (it->toString() == u"false"_s) return false;
    }
    return std::nullopt;
}

/// The settings in the file, or nothing when it cannot be used (see SettingsStore).
std::optional<UserSettings> read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;  // missing, unreadable, or a folder
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return std::nullopt;
    // Strict: bytes that are not UTF-8 (a file saved in the ANSI code page) must give the defaults, not a folder name full of
    // U+FFFD. A byte order mark, which Notepad and PowerShell like to add, is not part of the JSON.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    QString text = decoder.decode(bytes);
    if (decoder.hasError()) return std::nullopt;
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const QJsonObject object = document.object();

    const UserSettings defaults;
    UserSettings settings;
    const auto dir = object.constFind(u"outputDir"_s);
    if (dir != object.constEnd() && !dir->isNull()) {
        if (!dir->isString()) return std::nullopt;
        settings.outputDir = dir->toString();
    }
    const std::optional<bool> overwrite = flag(object, u"overwrite"_s, defaults.overwrite);
    const std::optional<bool> includeRank = flag(object, u"includeRank"_s, defaults.includeRank);
    const std::optional<bool> searchLyricsOnline = flag(object, u"searchLyricsOnline"_s, defaults.searchLyricsOnline);
    if (!overwrite || !includeRank || !searchLyricsOnline) return std::nullopt;
    settings.overwrite = *overwrite;
    settings.includeRank = *includeRank;
    settings.searchLyricsOnline = *searchLyricsOnline;
    return settings;
}

}  // namespace

UserSettings normalizeSettings(const QJsonValue& raw) {
    if (!raw.isObject()) return UserSettings();
    const QJsonObject value = raw.toObject();
    const QJsonValue outputDir = value.value(u"outputDir"_s);
    const QJsonValue overwrite = value.value(u"overwrite"_s);
    const QJsonValue includeRank = value.value(u"includeRank"_s);
    const QJsonValue searchLyricsOnline = value.value(u"searchLyricsOnline"_s);
    // A missing key is Undefined, which is neither null nor a string: like TypeScript's undefined.
    if (!(outputDir.isNull() || outputDir.isString()) || !overwrite.isBool() || !includeRank.isBool() || !searchLyricsOnline.isBool()) {
        return UserSettings();
    }
    return UserSettings{outputDir.isString() ? std::optional<QString>(outputDir.toString()) : std::nullopt, overwrite.toBool(),
                        includeRank.toBool(), searchLyricsOnline.toBool()};
}

SettingsStore::SettingsStore(QString filePath, QString defaultOutputDir)
    : m_filePath(std::move(filePath)), m_defaultOutputDir(std::move(defaultOutputDir)) {}

QString SettingsStore::standardOutputDir() {
    return QDir::toNativeSeparators(QDir::homePath()) + u"\\Music\\xGetSongs"_s;
}

UserSettings SettingsStore::load() {
    if (m_filePath.isEmpty() || m_filePath.contains(QChar(0))) return UserSettings();
    return read(m_filePath).value_or(UserSettings());
}

bool SettingsStore::save(const UserSettings& settings) {
    // Not a path (a NUL, which Windows would cut the path at, included) or a path without a file name (a root such as C:\): there
    // is nothing to write.
    const std::optional<QString> path = paths::fromText(m_filePath);
    if (!path || paths::fileName(*path).value_or(QString()).isEmpty()) return false;

    QJsonObject object;
    object.insert(u"outputDir"_s, settings.outputDir ? QJsonValue(*settings.outputDir) : QJsonValue(QJsonValue::Null));
    object.insert(u"overwrite"_s, settings.overwrite);
    object.insert(u"includeRank"_s, settings.includeRank);
    object.insert(u"searchLyricsOnline"_s, settings.searchLyricsOnline);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);

    const std::lock_guard lock(writeLock);
    if (!QDir().mkpath(QFileInfo(*path).path())) return false;
    // Writes next to the target and moves it over, so a crash never leaves half a file behind. The move is tried again for a
    // moment when another program (a virus scanner, an indexer) holds the file (fs::moveFile); a leftover temporary file is
    // removed (and would be overwritten by the next save).
    const QString temp = *path + u".tmp"_s;
    if (!fs::writeBytes(temp, bytes).isOk() || fs::moveFile(temp, *path, true).status != fs::MoveStatus::Moved) {
        fs::removeQuietly(temp);
        return false;
    }
    return true;
}

}  // namespace xgs::ui
