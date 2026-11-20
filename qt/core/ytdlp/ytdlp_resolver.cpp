#include "ytdlp/ytdlp_resolver.h"

#include <QStringList>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <variant>

#include "filename/filename_formatter.h"
#include "input/input_classifier.h"
#include "json/json.h"
#include "title/title_parser.h"
#include "ytdlp/error_classifier.h"
#include "ytdlp/ytdlp_commands.h"

using namespace Qt::StringLiterals;

namespace xgs::ytdlp {

namespace {

// Kotlin's RawEntry and RawPlaylist (every property defaults to null; entries to an empty list).
const json::ClassShape entryShape{{u"id"_s, u"title"_s, u"channel"_s, u"uploader"_s, u"availability"_s, u"artist"_s, u"track"_s}, {}};
const json::ClassShape playlistShape{{u"title"_s}, {{u"entries"_s, &entryShape}}};

struct RawEntry {
    std::optional<QString> id, title, channel, uploader, availability, artist, track;
};

std::optional<QString> stringOf(const json::Object& object, QStringView key) {
    const json::Value* value = json::find(object, key);
    if (!value) return std::nullopt;
    if (const json::Primitive* primitive = std::get_if<json::Primitive>(&value->data)) return primitive->content;
    return std::nullopt;   // Null
}

RawEntry entryOf(const json::Object& object) {
    return RawEntry{stringOf(object, u"id"), stringOf(object, u"title"), stringOf(object, u"channel"), stringOf(object, u"uploader"),
                    stringOf(object, u"availability"), stringOf(object, u"artist"), stringOf(object, u"track")};
}

QString missingYtDlp() {
    return u"yt-dlp를 찾을 수 없습니다. 도구 설치 후 다시 시도하세요."_s;
}

QString unreadableResponse() {
    return u"yt-dlp 응답을 해석할 수 없습니다."_s;
}

QString unknownItem() {
    return u"알 수 없는 항목"_s;
}

bool isUnavailableAvailability(const QString& availability) {
    return availability == u"private" || availability == u"needs_auth" || availability == u"premium_only" ||
           availability == u"subscriber_only";
}

std::optional<QString> unavailableReason(const std::optional<RawEntry>& entry) {
    if (!entry) return unknownItem();
    if (entry->title == u"[Private video]"_s) return u"비공개 영상"_s;
    if (entry->title == u"[Deleted video]"_s) return u"삭제된 영상"_s;
    if (entry->availability && isUnavailableAvailability(*entry->availability))
        return u"사용할 수 없는 영상 ("_s + *entry->availability + u")"_s;
    return std::nullopt;
}

ResolvedItem toItem(int rank, const std::optional<RawEntry>& entry) {
    const std::optional<QString> id = entry ? entry->id : std::nullopt;
    const QString title = entry ? entry->title.value_or(QString()) : QString();
    const std::optional<QString> reason = unavailableReason(entry);
    ResolvedItem item;
    item.rank = rank;
    item.title = title;
    if (!id || reason) {
        item.videoId = id.value_or(QString());
        item.available = false;
        item.unavailableReason = reason.value_or(unknownItem());
        return item;
    }
    const std::optional<QString> channel = entry->channel ? entry->channel : entry->uploader;
    const ParsedTrack parsed = parseTitle(title, channel, entry->artist, entry->track);
    item.videoId = *id;
    item.channel = channel;
    item.artist = parsed.artist;
    item.track = parsed.title;
    item.lowConfidence = parsed.confidence == Confidence::Low;
    item.expectedFileName = filename::format(rank, parsed.artist, parsed.title);
    return item;
}

// What a yt-dlp run gave: its stdout (every line followed by '\n', as Kotlin's runYtDlp collects it) or why there is none.
struct Run {
    OutcomeStatus status = OutcomeStatus::Failed;
    QString stdoutText;   // Ok
    QString error;        // Failed: the text of Kotlin's ResolveException
};

Run runYtDlp(process::ProcessRunner& runner, const QStringList& command, std::stop_token stop) {
    std::mutex mutex;
    Run run;
    QStringList stderrLines;
    const process::RunResult result = runner.run(
        command,
        [&](const QString& line) {
            const std::lock_guard lock(mutex);
            run.stdoutText.append(line).append(u'\n');
        },
        [&](const QString& line) {
            const std::lock_guard lock(mutex);
            stderrLines.append(line);
        },
        stop);
    if (result.status == process::RunStatus::Cancelled) {
        run.status = OutcomeStatus::Cancelled;
    } else if (result.status == process::RunStatus::StartFailed) {
        run.error = u"yt-dlp를 실행할 수 없습니다: "_s + result.error;
    } else if (result.exitCode != 0) {
        const std::lock_guard lock(mutex);
        run.error = classifyError(stderrLines).message;
    } else {
        run.status = OutcomeStatus::Ok;
    }
    return run;
}

// The command builders throw std::invalid_argument only without a yt-dlp path, which the callers check first.
std::optional<QStringList> commandOf(QStringList (*build)(const ToolPaths&, const QString&), const ToolPaths& paths, const QString& url) {
    try {
        return build(paths, url);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

}  // namespace

YtDlpResolver::YtDlpResolver(process::ProcessRunner& runner, const tools::ToolPathProvider& tools) : m_runner(runner), m_tools(tools) {}

Outcome<ResolveResult> YtDlpResolver::resolve(const QString& input, std::stop_token stop) {
    const ClassifyResult classified = classifyInput(input);
    if (!classified.ok()) return Outcome<ResolveResult>::failed(rejectMessage(classified.reason));
    const ParsedInput& parsed = *classified.input;
    const ToolPaths paths = m_tools.current();
    if (!paths.ytDlp) return Outcome<ResolveResult>::failed(missingYtDlp());
    const bool playlist = parsed.kind == ParsedInput::Kind::Playlist;
    const std::optional<QStringList> command =
        commandOf(playlist ? resolvePlaylistCommand : resolveVideoCommand, paths, parsed.canonicalUrl());
    if (!command) return Outcome<ResolveResult>::failed(missingYtDlp());

    const Run run = runYtDlp(m_runner, *command, stop);
    if (run.status == OutcomeStatus::Cancelled) return Outcome<ResolveResult>::cancelled();
    if (run.status == OutcomeStatus::Failed) return Outcome<ResolveResult>::failed(run.error);
    const std::optional<json::Value> decoded = json::parse(run.stdoutText, {playlist ? &playlistShape : &entryShape});
    if (!decoded) return Outcome<ResolveResult>::failed(unreadableResponse());
    const json::Object& root = std::get<json::Object>(decoded->data);

    ResolveResult result;
    if (!playlist) {
        result.kind = InputKind::Video;
        result.items.append(toItem(1, entryOf(root)));
        return Outcome<ResolveResult>::ok(std::move(result));
    }
    result.kind = InputKind::Playlist;
    result.playlistTitle = stringOf(root, u"title");
    result.alsoVideoId = parsed.alsoVideoId;
    if (const json::Value* entries = json::find(root, u"entries")) {
        const json::Array& list = std::get<json::Array>(entries->data);
        const qsizetype count = std::min<qsizetype>(qsizetype(list.size()), filename::MaxRank);
        result.items.reserve(count);
        for (qsizetype index = 0; index < count; ++index) {
            const json::Value& entry = list[size_t(index)];
            const json::Object* object = std::get_if<json::Object>(&entry.data);
            result.items.append(toItem(int(index) + 1, object ? std::optional<RawEntry>(entryOf(*object)) : std::nullopt));
        }
        result.truncated = qsizetype(list.size()) > filename::MaxRank;
    }
    return Outcome<ResolveResult>::ok(std::move(result));
}

std::optional<VideoMeta> YtDlpResolver::fetch(const QString& videoId, std::stop_token stop) {
    const ToolPaths paths = m_tools.current();
    if (!paths.ytDlp) return std::nullopt;
    const QString url = ParsedInput{ParsedInput::Kind::Video, videoId, std::nullopt}.canonicalUrl();
    const std::optional<QStringList> command = commandOf(resolveVideoCommand, paths, url);
    if (!command) return std::nullopt;
    const Run run = runYtDlp(m_runner, *command, stop);
    if (run.status != OutcomeStatus::Ok) return std::nullopt;
    const std::optional<json::Value> decoded = json::parse(run.stdoutText, {&entryShape});
    if (!decoded) return std::nullopt;
    const RawEntry entry = entryOf(std::get<json::Object>(decoded->data));
    return VideoMeta{entry.title, entry.channel ? entry.channel : entry.uploader, entry.artist, entry.track};
}

}  // namespace xgs::ytdlp
