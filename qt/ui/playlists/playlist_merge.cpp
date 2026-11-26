#include "playlist_merge.h"

#include <QSet>

#include <algorithm>

namespace xgs::ui {

MergeResult mergeRemote(const QList<Playlist>& current, QList<RemoteEntry> ko, QList<RemoteEntry> en) {
    const auto bySn = [](const RemoteEntry& a, const RemoteEntry& b) { return a.sn < b.sn; };
    std::stable_sort(ko.begin(), ko.end(), bySn);
    std::stable_sort(en.begin(), en.end(), bySn);

    MergeResult result;
    result.list = current;
    QSet<QString> known;
    for (const Playlist& playlist : current) known.insert(playlist.pid);

    for (const QList<RemoteEntry>* entries : {&ko, &en}) {
        for (const RemoteEntry& entry : *entries) {
            if (known.contains(entry.pid)) {
                ++result.alreadyThere;
                continue;
            }
            known.insert(entry.pid);
            result.list.append(Playlist{entry.name, entry.pid});
            ++result.added;
        }
    }
    return result;
}

}  // namespace xgs::ui
