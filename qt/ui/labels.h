#pragma once

#include <QString>

#include <optional>

#include "ui_types.h"

// The texts of the screen, ported from shell/src/state/labels.ts (itself the Compose app's Labels.kt), letter for letter.
namespace xgs::ui {

/// The label of the button that updates yt-dlp; formatFailureHelp points to it by this name.
QString updateYtDlpLabel();

/// The help shown next to the update button of the tools panel. "m4a 오디오 형식이 없습니다." (the engine's message) is also what
/// an outdated yt-dlp or a broken JavaScript runtime makes every song fail with, so the user is told to check those first.
QString formatFailureHelp();

/// The text shown in an item's status cell.
QString statusLabel(const ItemStatus& status);

/// The one-line result shown after a job ends, with how long it took when that is known; empty while there is nothing to report.
std::optional<QString> summaryText(const UiState& state);

/// elapsedMs in whole seconds as `42초`, `3분 05초` or `1시간 02분 05초`; never negative.
QString elapsedLabel(qint64 elapsedMs);

/// Where the files will be saved: a folder named after the album name the user typed inside the output folder, else a folder
/// named after the playlist for a playlist, else the output folder itself for a video (the same names the engine uses). Empty
/// until something is resolved and an output folder is typed.
std::optional<QString> destinationPath(const UiState& state);

/// What the album name field shows: the text the user typed, else the title of the playlist, which the files get as their album
/// when the field is left alone (a single video keeps the album of its own then), else nothing.
QString albumNameText(const UiState& state);

/// The line shown above the options: destinationPath with its caption; empty when there is no destination yet.
std::optional<QString> destinationLabel(const UiState& state);

/// The zero-padded rank shown in the list, e.g. `007`.
QString rankLabel(int rank);

/// What the rank text field may hold while the user types: ASCII digits only, at most three of them.
QString rankInputText(const QString& raw);

/// The rank typed so far, kept within the valid range; empty while the field holds no ASCII digits only.
std::optional<int> rankFromInput(const QString& text);

}  // namespace xgs::ui
