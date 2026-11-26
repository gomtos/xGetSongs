#include "playlist_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>

#include <mutex>

#include "fs/file_ops.h"
#include "paths/paths.h"

using namespace Qt::StringLiterals;

namespace xgs::ui {

namespace {

std::mutex writeLock;

struct Reading {
    bool exists = false;     // there is a file to look at
    bool clean = true;       // everything in it was used
    QList<Playlist> list;
};

Reading read(const QString& path) {
    Reading reading;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // A folder or an unreadable file has to be kept out of the way too; a plain missing file is just empty.
        reading.exists = QFileInfo::exists(path);
        reading.clean = !reading.exists;
        return reading;
    }
    reading.exists = true;
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        reading.clean = false;
        return reading;
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    QString text = decoder.decode(bytes);
    if (decoder.hasError()) {
        reading.clean = false;
        return reading;
    }
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        reading.clean = false;
        return reading;
    }
    const QJsonObject object = document.object();
    const QJsonValue version = object.value(u"version"_s);
    const QJsonValue entries = object.value(u"playlists"_s);
    if (!entries.isArray() || (!version.isUndefined() && !(version.isDouble() && version.toDouble() == 1.0))) {
        reading.clean = false;
        return reading;
    }
    for (const QJsonValue& value : entries.toArray()) {
        const QJsonObject entry = value.toObject();
        const QJsonValue name = entry.value(u"name"_s);
        const QJsonValue pid = entry.value(u"pid"_s);
        if (!value.isObject() || !name.isString() || !pid.isString() || name.toString().isEmpty() || pid.toString().isEmpty()) {
            reading.clean = false;
            continue;
        }
        reading.list.append(Playlist{name.toString(), pid.toString()});
    }
    return reading;
}

}  // namespace

PlaylistStore::PlaylistStore(QString filePath) : m_filePath(std::move(filePath)) {}

QList<Playlist> PlaylistStore::load() {
    if (m_filePath.isEmpty() || m_filePath.contains(QChar(0))) return {};
    return read(m_filePath).list;
}

bool PlaylistStore::save(const QList<Playlist>& playlists) {
    const std::optional<QString> path = paths::fromText(m_filePath);
    if (!path || paths::fileName(*path).value_or(QString()).isEmpty()) return false;

    const std::lock_guard lock(writeLock);
    if (!QDir().mkpath(QFileInfo(*path).path())) return false;

    // A file that cannot be used is set aside before it is written over (an earlier .bad is replaced), so nothing the user had
    // is lost to a save. If it cannot be moved, nothing is written.
    const Reading before = read(*path);
    if (before.exists && !before.clean) {
        if (fs::moveFile(*path, *path + u".bad"_s, true).status != fs::MoveStatus::Moved) return false;
    }

    QJsonArray entries;
    for (const Playlist& playlist : playlists) {
        entries.append(QJsonObject{{u"name"_s, playlist.name}, {u"pid"_s, playlist.pid}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"playlists"_s, entries}}).toJson(QJsonDocument::Indented);

    // Next to the target, then moved over it (not QSaveFile: a plain replace failed once when another program held the file).
    const QString temp = *path + u".tmp"_s;
    if (!fs::writeBytes(temp, bytes).isOk() || fs::moveFile(temp, *path, true).status != fs::MoveStatus::Moved) {
        fs::removeQuietly(temp);
        return false;
    }
    return true;
}

}  // namespace xgs::ui
