#pragma once

#include <QList>
#include <QString>

#include "playlist_store.h"

namespace xgs::ui {

/// An entry of the remote list, after cleaning (see RemotePlaylists). `sn` only orders the entries; it is never stored.
struct RemoteEntry {
    int sn = 0;
    QString name;
    QString pid;
    bool operator==(const RemoteEntry&) const = default;
};

struct MergeResult {
    QList<Playlist> list;
    int added = 0;
    int alreadyThere = 0;   // entries skipped because their pid was in the list already or was added a moment before
};

/// Spec §5.4: the Korean entries by `sn`, then the English ones by `sn` (equal `sn` keep their order), each added to the end of
/// `current` when its pid (case-sensitive; the name is not looked at) is not there yet. What is there is left as it is.
MergeResult mergeRemote(const QList<Playlist>& current, QList<RemoteEntry> ko, QList<RemoteEntry> en);

}  // namespace xgs::ui
