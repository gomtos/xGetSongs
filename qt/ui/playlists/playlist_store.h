#pragma once

#include <QList>
#include <QString>

namespace xgs::ui {

/// One entry of the user's list: a name and a YouTube playlist ID (design spec §5).
struct Playlist {
    QString name;
    QString pid;
    bool operator==(const Playlist&) const = default;
};

/// Keeps the playlist list in `playlists.json` (spec §5.3): `{ "version": 1, "playlists": [ { "name", "pid" } ] }`, the order of
/// the array being the order on screen. A missing file is an empty list. A file that cannot be used (not JSON, not an object,
/// no `playlists` array, a version other than 1, or an entry that is not a name and a pid, both non-empty strings) gives the
/// entries that are fine (none when the structure is wrong) and is moved to `<file>.bad` by the next save, before anything is
/// written over it. Writing goes to `<file>.tmp` first and is moved over the file (tried again while another program holds it,
/// see fs::moveFile). Saves from several threads are taken one at a time.
class PlaylistStore {
public:
    explicit PlaylistStore(QString filePath);

    QString filePath() const { return m_filePath; }

    QList<Playlist> load();
    /// false when nothing could be written (or the broken file could not be set aside); the old file is then still there.
    bool save(const QList<Playlist>& playlists);

private:
    QString m_filePath;
};

}  // namespace xgs::ui
