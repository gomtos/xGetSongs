#include "job/item_downloader.h"

#include <QStringList>

#include <climits>
#include <cmath>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

#include "filename/filename_formatter.h"
#include "fs/file_ops.h"
#include "input/input_classifier.h"
#include "lyrics/lyrics_extractor.h"
#include "paths/paths.h"
#include "text/kt_text.h"
#include "title/title_parser.h"
#include "ytdlp/progress_parser.h"
#include "ytdlp/video_info_file.h"
#include "ytdlp/ytdlp_commands.h"

namespace xgs::job {

namespace {

using namespace Qt::StringLiterals;
using ytdlp::Failure;
using ytdlp::FailureKind;

// downloadCommand's std::invalid_argument text for an id that is not a plain file name part. A `%` is refused here with the
// same text (author decision 9): the id would be read as an output template field. tst_item_downloader compares the two.
constexpr const char* kIdNotAFileNamePart = "video id is not a plain file name part";

QString watchUrl(const QString& videoId) {
    return ParsedInput{ParsedInput::Kind::Video, videoId, std::nullopt}.canonicalUrl();
}

std::optional<QString> blankToEmpty(const std::optional<QString>& text) {
    return text && !kt::isBlank(*text) ? text : std::nullopt;
}

// Kotlin's Double-to-Int conversion (toInt): NaN is 0, a value beyond the Int range is the nearest end, anything else drops its fraction.
int kotlinToInt(double value) {
    if (std::isnan(value)) return 0;
    if (value >= static_cast<double>(INT_MAX)) return INT_MAX;
    if (value <= static_cast<double>(INT_MIN)) return INT_MIN;
    return static_cast<int>(value);
}

// Lets a download update through only when it is the first one or the whole percent advances. Used by the stdout handler
// only, whose calls never overlap.
class ProgressThrottle {
public:
    bool accept(const ytdlp::DownloadProgress& update) {
        const int whole = update.percent ? kotlinToInt(*update.percent) : -1;
        if (m_reported && whole == m_lastPercent) return false;
        m_reported = true;
        m_lastPercent = whole;
        return true;
    }

private:
    bool m_reported = false;
    int m_lastPercent = -1;
};

// What M4aTagger wrote as lyrics: the description's, else the lookup's, else nothing, which is "off" when the lookup was not
// allowed and "not found" when it ran (or failed) and gave nothing.
LyricsOutcome lyricsOutcome(const std::optional<QString>& fromDescription, const std::optional<QString>& found,
                            bool lookupAllowed) {
    if (fromDescription) return LyricsOutcome::Description;
    if (found) return LyricsOutcome::Online;
    if (!lookupAllowed) return LyricsOutcome::SearchOff;
    return LyricsOutcome::NotFound;
}

}  // namespace

ItemDownloader::ItemDownloader(process::ProcessRunner& runner, const tools::ToolPathProvider& tools,
                               ytdlp::VideoMetadataSource& metadata)
    : m_runner(runner), m_tools(tools), m_metadata(metadata), m_lyrics(m_noLyrics), m_tagger(runner, tools) {}

ItemDownloader::ItemDownloader(process::ProcessRunner& runner, const tools::ToolPathProvider& tools,
                               ytdlp::VideoMetadataSource& metadata, lyrics::LyricsProvider& lyrics)
    : m_runner(runner), m_tools(tools), m_metadata(metadata), m_lyrics(lyrics), m_tagger(runner, tools) {}

PreparedItem ItemDownloader::prepare(const ResolvedItem& item, const std::optional<QString>& album, bool includeRank,
                                     bool searchLyricsOnline, const std::optional<QString>& albumOverride,
                                     std::stop_token stop) {
    QString artist = item.artist;
    QString track = item.track;
    if (item.lowConfidence) {
        if (const std::optional<ytdlp::VideoMeta> meta = m_metadata.fetch(item.videoId, std::move(stop))) {
            const ParsedTrack parsed = parseTitle(meta->title.value_or(item.title), meta->channel ? meta->channel : item.channel,
                                                  meta->artist, meta->track);
            artist = parsed.artist;
            track = parsed.title;
        }
    }
    QString fileName = filename::format(item.rank, artist, track, includeRank);
    return PreparedItem{item, std::move(fileName), std::move(artist), std::move(track), album, searchLyricsOnline,
                        blankToEmpty(albumOverride)};
}

DownloadResult ItemDownloader::download(const PreparedItem& prepared, const QString& workDir, const Emit& emitEvent,
                                        std::stop_token stop) {
    if (stop.stop_requested()) return DownloadCancelled{};
    const ToolPaths paths = m_tools.current();
    if (!paths.ytDlp) return DownloadFailed{Failure{FailureKind::Fatal, u"yt-dlp를 찾을 수 없습니다."_s}};
    // The tags are written with ffmpeg after the download, so stop before spending a download that cannot be finished.
    if (!paths.ffmpeg) return DownloadFailed{Failure{FailureKind::Fatal, u"ffmpeg를 찾을 수 없습니다."_s}};

    const QString& videoId = prepared.item.videoId;
    const QString url = watchUrl(videoId);
    if (videoId.contains(u'%')) return DownloadFailed{Failure{FailureKind::Other, QString::fromUtf8(kIdNotAFileNamePart)}};
    QStringList command;
    try {
        command = ytdlp::downloadCommand(paths, url, ytdlp::escapeOutputTemplateDirectory(workDir), videoId);
    } catch (const std::invalid_argument& e) {
        return DownloadFailed{Failure{FailureKind::Other, QString::fromUtf8(e.what())}};
    }

    const int rank = prepared.item.rank;
    std::mutex stderrLock;
    QStringList stderrLines;
    ProgressThrottle throttle;
    const process::RunResult run = m_runner.run(
        command,
        [&](const QString& line) {
            const std::optional<ytdlp::DownloadProgress> update = ytdlp::parseProgress(line);
            if (!update || !throttle.accept(*update)) return;
            // Author decision 8: a percent that is not finite is unknown; the throttle above still counted it as Kotlin does.
            std::optional<double> percent = update->percent;
            if (percent && !std::isfinite(*percent)) percent.reset();
            emitEvent(Progress{rank, Stage::Downloading, percent});
        },
        [&](const QString& line) {
            const std::lock_guard lock(stderrLock);
            stderrLines.append(line);
        },
        stop);

    switch (run.status) {
    case process::RunStatus::Cancelled:
        return DownloadCancelled{};
    case process::RunStatus::StartFailed:
    case process::RunStatus::Failed:
        return DownloadIoError{run.error};
    case process::RunStatus::Exited:
        break;
    }
    if (run.exitCode != 0) {
        const std::lock_guard lock(stderrLock);
        return DownloadFailed{ytdlp::classifyError(stderrLines)};
    }
    // From here on the work is ours (tags, cover, lyrics), so we say so ourselves instead of reading yt-dlp's output.
    emitEvent(Progress{rank, Stage::Finishing, std::nullopt});

    const std::optional<QString> file = paths::child(workDir, videoId + u".m4a"_s);
    if (!file || !fs::isRegularFile(*file)) {
        return DownloadFailed{Failure{FailureKind::Other, u"받은 m4a 파일을 찾을 수 없습니다."_s}};
    }
    std::optional<QString> cover = paths::child(workDir, videoId + u".jpg"_s);
    if (cover && !fs::isRegularFile(*cover)) cover.reset();
    const std::optional<QString> infoFile = paths::child(workDir, videoId + u".info.json"_s);
    const ytdlp::VideoInfo info = infoFile ? ytdlp::readVideoInfo(*infoFile) : ytdlp::VideoInfo{};

    // The lookup goes by the album the video really has: a name the user made up, or a playlist title, would only hurt the
    // match.
    const std::optional<QString> ownAlbum = info.album ? info.album : prepared.album;
    const std::optional<QString> fromDescription = lyrics::extract(info.description);
    const std::optional<QString> found =
        fromDescription ? std::nullopt : lookUpLyrics(prepared, ownAlbum, info.duration, stop);
    // Kotlin's cancelled lookup throws here; the provider returns nothing instead, so the token tells.
    if (stop.stop_requested()) return DownloadCancelled{};

    // The tag follows the folder: the album name the user chose, else the playlist title; the video's own album only when
    // there is neither (a single video).
    const std::optional<QString> folderAlbum = prepared.albumOverride ? prepared.albumOverride : blankToEmpty(prepared.album);
    const tags::TrackTags trackTags{
        .title = prepared.track,
        .artist = prepared.artist,
        .album = folderAlbum ? folderAlbum : info.album,
        .albumArtist = AlbumArtist.toString(),
        .trackNumber = rank,
        .comment = url,
        .lyrics = fromDescription ? fromDescription : found,
    };
    const tags::TagResult tagged = m_tagger.tag(*file, cover, trackTags, stop);
    switch (tagged.status) {
    case tags::TagStatus::Failed:
        return DownloadFailed{tagged.failure};
    case tags::TagStatus::Cancelled:
        return DownloadCancelled{};
    case tags::TagStatus::Tagged:
        break;
    }
    return Downloaded{*file, lyricsOutcome(fromDescription, found, prepared.searchLyricsOnline)};
}

std::optional<QString> ItemDownloader::lookUpLyrics(const PreparedItem& prepared, const std::optional<QString>& album,
                                                    std::optional<int> durationSeconds, std::stop_token stop) {
    if (!prepared.searchLyricsOnline) return std::nullopt;
    try {
        return blankToEmpty(m_lyrics.find(LyricsQuery{prepared.artist, prepared.track, album, durationSeconds}, std::move(stop)));
    } catch (const std::bad_alloc&) {
        throw;   // Kotlin catches Exception, not an OutOfMemoryError
    } catch (...) {
        // A song without lyrics is no reason to fail the item (author decision 13).
        return std::nullopt;
    }
}

}  // namespace xgs::job
