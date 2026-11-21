#pragma once

#include <QString>
#include <QStringView>

#include <functional>
#include <optional>
#include <stop_token>
#include <variant>

#include "lyrics/lyrics_provider.h"
#include "model/models.h"
#include "process/process_runner.h"
#include "tags/m4a_tagger.h"
#include "tools/tool_locator.h"
#include "ytdlp/error_classifier.h"
#include "ytdlp/ytdlp_resolver.h"

// Port of engine/.../job/ItemDownloader.kt: downloads one video's audio with yt-dlp and writes its tags. Knows nothing about
// concurrency, retries or sinks. Blocking, with a std::stop_token in place of coroutine cancellation: where Kotlin throws a
// CancellationException, download returns DownloadCancelled and prepare returns normally (the caller looks at the token).
namespace xgs::job {

/// An item whose final file name is settled. artist and track are the parsed originals the file name was made from (the tags
/// use them as they are); album is the album tag the files get unless albumOverride is set: the playlist title, or absent for
/// a single video, which then gets the album of its own video if it has one. searchLyricsOnline says whether the lyrics may
/// be looked up on the internet when the video description has none. albumOverride is the album name the user chose, if any:
/// the album tag gets it instead of album.
struct PreparedItem {
    ResolvedItem item;
    QString fileName;
    QString artist;
    QString track;
    std::optional<QString> album;
    bool searchLyricsOnline = false;
    std::optional<QString> albumOverride;          // never blank: a blank one is absent
    bool operator==(const PreparedItem&) const = default;
};

/// The finished m4a, still inside the job's work directory. lyrics says what the file got as lyrics, or why it got none: it
/// describes what the tag step wrote, so it exists only for a file whose tags were written.
struct Downloaded {
    QString file;
    LyricsOutcome lyrics = LyricsOutcome::Description;
};
struct DownloadFailed {
    ytdlp::Failure failure;
};
/// The runner could not start the program or lost it (RunStatus::StartFailed or Failed; Kotlin: an IOException). message is
/// the runner's text.
struct DownloadIoError {
    QString message;
};
/// A stop was requested; the item emits no further event.
struct DownloadCancelled {};

using DownloadResult = std::variant<Downloaded, DownloadFailed, DownloadIoError, DownloadCancelled>;

/// Receives the events of one download. May be called from the runner's reader threads; must not block.
using Emit = std::function<void(const JobEvent&)>;

/// The album artist of every file: one value for all tracks, so a player groups a playlist into one album.
inline constexpr QStringView AlbumArtist = u"Various Artists";

class ItemDownloader final {
public:
    /// Everything is referenced, not owned, and must outlive the downloader. Safe to call from several threads at once.
    /// Without a lyrics provider nothing is looked up (Kotlin's default NoLyricsProvider).
    ItemDownloader(process::ProcessRunner& runner, const tools::ToolPathProvider& tools, ytdlp::VideoMetadataSource& metadata);
    ItemDownloader(process::ProcessRunner& runner, const tools::ToolPathProvider& tools, ytdlp::VideoMetadataSource& metadata,
                   lyrics::LyricsProvider& lyrics);

    ItemDownloader(const ItemDownloader&) = delete;
    ItemDownloader& operator=(const ItemDownloader&) = delete;

    /// Settles the final file name. Items whose artist came from the channel name (lowConfidence) get a second chance: the
    /// full metadata (yt-dlp `artist`/`track`) is fetched and the title is parsed again; when the metadata source answers
    /// nothing (a stop included) the first parse stays. album is the fallback for the album tag (the playlist title, or absent
    /// for a single video). includeRank puts the rank in front of the file name; the tags keep the rank as the track number
    /// either way. searchLyricsOnline and albumOverride are carried to download. A rank outside filename::MinRank..MaxRank
    /// throws std::invalid_argument (filename::format, as Kotlin). The caller looks at stop afterwards.
    PreparedItem prepare(const ResolvedItem& item, const std::optional<QString>& album, bool includeRank,
                         bool searchLyricsOnline, const std::optional<QString>& albumOverride, std::stop_token stop);

    /// Runs yt-dlp once into workDir (a result of paths::fromText; a `%` in it is escaped for yt-dlp's output template), then
    /// writes the tags and the cover (the thumbnail yt-dlp left next to the m4a) into the m4a. In order:
    ///  - a stop already requested: DownloadCancelled, nothing run;
    ///  - no yt-dlp: DownloadFailed(Fatal, `yt-dlp를 찾을 수 없습니다.`); no ffmpeg: DownloadFailed(Fatal, `ffmpeg를 찾을 수 없습니다.`);
    ///  - a video ID with a `%` or one downloadCommand rejects: DownloadFailed(Other, the text of downloadCommand), nothing run;
    ///  - the run: throttled Progress(Downloading) events (the first one, then only when the whole percent changes; a percent
    ///    that is not finite is sent as unknown); Cancelled is DownloadCancelled, StartFailed or Failed is DownloadIoError, an
    ///    exit code other than 0 is DownloadFailed(classifyError(stderr));
    ///  - one Progress(Finishing) event;
    ///  - no `<id>.m4a`: DownloadFailed(Other, `받은 m4a 파일을 찾을 수 없습니다.`);
    ///  - the album tag is albumOverride, else album, else the video's own album from the info file; the lyrics tag is the
    ///    lyrics section of the description, else (when searchLyricsOnline) what the provider finds (a provider that throws
    ///    finds nothing), else none; a stop seen after the lookup is DownloadCancelled;
    ///  - the tags: a failure is DownloadFailed, a cancellation DownloadCancelled, else Downloaded.
    /// An exception the runner or a handler throws propagates. (The parameter is not named `emit`, Kotlin's name: Qt defines
    /// `emit` as an empty macro.)
    DownloadResult download(const PreparedItem& prepared, const QString& workDir, const Emit& emitEvent, std::stop_token stop);

private:
    std::optional<QString> lookUpLyrics(const PreparedItem& prepared, const std::optional<QString>& album,
                                        std::optional<int> durationSeconds, std::stop_token stop);

    lyrics::NoLyricsProvider m_noLyrics;   // the provider of the three-argument constructor
    process::ProcessRunner& m_runner;
    const tools::ToolPathProvider& m_tools;
    ytdlp::VideoMetadataSource& m_metadata;
    lyrics::LyricsProvider& m_lyrics;
    tags::M4aTagger m_tagger;
};

}  // namespace xgs::job
