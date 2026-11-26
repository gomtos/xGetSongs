#pragma once

#include <QList>
#include <QString>

#include <optional>

#include "playlist_store.h"

namespace xgs::ui {

/// A name as the lists keep it: control characters removed, white space at both ends cut off, and at most maxUnits UTF-16 units
/// (never cut inside a surrogate pair; maxUnits < 0 means no limit). Empty when nothing is left.
QString cleanName(const QString& raw, qsizetype maxUnits = -1);

/// The playlist ID in what the user typed or the remote list gave, by the rules of the main input field (xgs::classifyInput),
/// or nothing when it is not a playlist. A watch address that also names a playlist (`watch?v=…&list=PL…`) is a playlist: the
/// classifier takes it as one, and the lookup would list that playlist; a mix list, a video alone and anything else are not.
std::optional<QString> playlistIdOf(const QString& input);

struct EntryCheck {
    std::optional<Playlist> entry;   // present when the input is acceptable
    QString reason;                  // Korean text for under the fields when it is not (empty when entry is present)
};

/// The Add and Edit dialog's check: a name that is not empty, an ID or address that is a playlist, and an ID that no other entry
/// has. editingIndex is the entry being edited (it may keep its own ID), -1 when adding. The first problem found is the reason
/// (name first).
EntryCheck checkEntry(const QString& name, const QString& idOrUrl, const QList<Playlist>& current, int editingIndex = -1);

}  // namespace xgs::ui
