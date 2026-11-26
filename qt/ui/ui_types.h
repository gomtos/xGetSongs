#pragma once

#include <QList>
#include <QString>

#include <optional>

#include "model/models.h"

// The state of the screen, ported from shell/src/state/types.ts (itself the Compose app's UiState.kt and settings/*.kt): the same
// fields with the same meaning. A default-constructed UiState is the TypeScript `initialUiState()`.
namespace xgs::ui {

/// What an item's status cell says. Only the members of its kind are used: percent for Downloading, lyrics for Done, text for
/// Skipped (the reason) and Failed (the message).
struct ItemStatus {
    enum class Kind {
        Ready,        // shown in the preview before a download starts
        Waiting,      // part of a running job but not started yet
        Downloading,
        Finishing,
        Done,         // lyrics: what the finished file got as lyrics (or why it got none); empty when the engine did not say
        Skipped,
        Failed,
    };

    Kind kind = Kind::Ready;
    std::optional<double> percent;
    std::optional<LyricsOutcome> lyrics;
    QString text;

    static ItemStatus ready() { return ItemStatus{Kind::Ready, std::nullopt, std::nullopt, QString()}; }
    static ItemStatus waiting() { return ItemStatus{Kind::Waiting, std::nullopt, std::nullopt, QString()}; }
    static ItemStatus downloading(std::optional<double> percent) { return ItemStatus{Kind::Downloading, percent, std::nullopt, QString()}; }
    static ItemStatus finishing() { return ItemStatus{Kind::Finishing, std::nullopt, std::nullopt, QString()}; }
    static ItemStatus done(std::optional<LyricsOutcome> lyrics) { return ItemStatus{Kind::Done, std::nullopt, lyrics, QString()}; }
    static ItemStatus skipped(QString reason) { return ItemStatus{Kind::Skipped, std::nullopt, std::nullopt, std::move(reason)}; }
    static ItemStatus failed(QString message) { return ItemStatus{Kind::Failed, std::nullopt, std::nullopt, std::move(message)}; }

    bool operator==(const ItemStatus&) const = default;
};

struct ItemRow {
    ResolvedItem item;
    /// The file name shown to the user: the preview's guess, replaced by the real one once known.
    std::optional<QString> fileName;
    ItemStatus status;
    bool operator==(const ItemRow&) const = default;
};

enum class Phase { Idle, Resolving, Preview, Running, Finished };

struct UiState {
    Phase phase = Phase::Idle;
    QString input;
    /// outputDir, overwrite, includeRank and searchLyricsOnline are the options the app remembers between runs.
    QString outputDir;
    bool overwrite = false;
    /// Whether file names start with the rank (`001 `). Sent with the job; the preview names follow it.
    bool includeRank = true;
    /// Whether a song whose description has no lyrics is looked up on the internet. Sent with the job; the preview does not change.
    bool searchLyricsOnline = true;
    /// Not remembered: it belongs to the video that is on screen.
    int singleRank = 1;
    /// The album name the user typed, as typed; it is also the name of the folder the files are saved in. Empty means the user
    /// has not touched the field: it shows the default (see albumNameText) and sends nothing. Once touched, the text stays as it
    /// is, and blank means the default again. Not remembered, like singleRank; reset clears it.
    std::optional<QString> albumName;
    std::optional<ResolveResult> resolved;
    QList<ItemRow> rows;
    std::optional<QString> error;
    std::optional<JobStatus> jobStatus;
    std::optional<JobSummary> summary;
    /// How long the job that summary belongs to took, from the press of the button to the job-done event; empty while there is none.
    std::optional<qint64> elapsedMs;
    std::optional<ToolsStatus> tools;
    bool toolBusy = false;
    std::optional<QString> toolMessage;

    bool operator==(const UiState&) const = default;
};

/// The ranks of the rows whose item failed, in the order of the rows.
QList<int> failedRanks(const UiState& state);

bool canResolve(const UiState& state);

/// The options the app restores on the next start. The typed playlist or video address and the rank of a single video are not
/// part of it. outputDir is empty (or blank) when the user never chose a folder: the app then uses its default.
/// searchLyricsOnline is on for a settings file written before the option existed.
struct UserSettings {
    std::optional<QString> outputDir;
    bool overwrite = false;
    bool includeRank = true;
    bool searchLyricsOnline = true;
    bool operator==(const UserSettings&) const = default;
};

}  // namespace xgs::ui
