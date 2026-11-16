#pragma once

#include <QList>
#include <QString>

#include <optional>
#include <variant>

// Plain data types of the engine, from shared/.../api/ApiModels.kt (ResolvedItem, ResolveResponse, JobOptions, Stage, JobStatus,
// JobSummary, LyricsOutcome, JobEvent, ToolInfo, ToolsStatus), engine/.../tools/ToolLocator.kt (ToolPaths) and
// engine/.../lyrics/LyricsProvider.kt (LyricsQuery). Field names, defaults and enum order are Kotlin's. The HTTP and JSON parts
// (requests, error bodies, SSE names, browser commands) do not exist in the Qt version.
namespace xgs {

enum class InputKind { Playlist, Video };

struct ResolvedItem {
    int rank;
    QString videoId;
    QString title;
    std::optional<QString> channel;
    QString artist;
    QString track;
    bool lowConfidence = false;
    bool available = true;
    std::optional<QString> unavailableReason;
    std::optional<QString> expectedFileName;
    bool operator==(const ResolvedItem&) const = default;
};

// Kotlin ResolveResponse.
struct ResolveResult {
    QString resolveId;
    InputKind kind;
    std::optional<QString> playlistTitle;
    QList<ResolvedItem> items;
    bool truncated = false;
    std::optional<QString> alsoVideoId;
    bool operator==(const ResolveResult&) const = default;
};

struct JobOptions {
    std::optional<QString> outputDir;
    bool overwrite = false;
    int singleRank = 1;
    bool includeRank = true;
    bool searchLyricsOnline = true;
    std::optional<QString> albumName;
    bool operator==(const JobOptions&) const = default;
};

enum class Stage { Downloading, Finishing };
enum class JobStatus { Completed, Cancelled, Failed };

struct JobSummary {
    int succeeded;
    int skipped;
    int failed;
    bool operator==(const JobSummary&) const = default;
};

enum class LyricsOutcome { Description, Online, NotFound, SearchOff };

struct ItemStarted {
    int rank;
    QString videoId;
    QString fileName;
    bool operator==(const ItemStarted&) const = default;
};

struct Progress {
    int rank;
    Stage stage;
    std::optional<double> percent;
    bool operator==(const Progress&) const = default;
};

struct ItemDone {
    int rank;
    QString fileName;
    std::optional<LyricsOutcome> lyrics;
    bool operator==(const ItemDone&) const = default;
};

struct ItemSkipped {
    int rank;
    QString reason;
    bool operator==(const ItemSkipped&) const = default;
};

struct ItemFailed {
    int rank;
    QString message;
    bool operator==(const ItemFailed&) const = default;
};

struct JobDone {
    JobStatus status;
    JobSummary summary;
    bool operator==(const JobDone&) const = default;
};

using JobEvent = std::variant<ItemStarted, Progress, ItemDone, ItemSkipped, ItemFailed, JobDone>;

struct ToolInfo {
    bool found;
    std::optional<QString> version;
    std::optional<QString> path;
    bool operator==(const ToolInfo&) const = default;
};

struct ToolsStatus {
    ToolInfo ytDlp;
    ToolInfo ffmpeg;
    ToolInfo jsRuntime;
    bool operator==(const ToolsStatus&) const = default;
};

struct ToolPaths {
    std::optional<QString> ytDlp;
    std::optional<QString> ffmpeg;
    std::optional<QString> jsRuntime;     // Deno or Node, which yt-dlp needs for YouTube's JavaScript challenges
    bool operator==(const ToolPaths&) const = default;
};

struct LyricsQuery {
    QString artist;
    QString title;
    std::optional<QString> album;
    std::optional<int> durationSeconds;
    bool operator==(const LyricsQuery&) const = default;
};

}  // namespace xgs
