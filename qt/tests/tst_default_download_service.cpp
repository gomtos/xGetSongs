#include <QtTest>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <variant>
#include <vector>

#include <windows.h>

#include "filename/filename_formatter.h"
#include "fs/file_ops.h"
#include "input/input_classifier.h"
#include "job/default_download_service.h"
#include "job/download_service.h"
#include "job/item_downloader.h"
#include "job/job_events.h"
#include "lyrics/lyrics_provider.h"
#include "output/local_folder_sink.h"
#include "paths/paths.h"
#include "support/fake_lyrics_provider.h"
#include "support/fake_output_sink.h"
#include "support/fake_process_runner.h"
#include "support/ffmetadata_reader.h"
#include "support/job_fakes.h"
#include "support/test_tools.h"
#include "ytdlp/ytdlp_commands.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::ItemDone;
using xgs::ItemFailed;
using xgs::ItemSkipped;
using xgs::ItemStarted;
using xgs::JobDone;
using xgs::JobEvent;
using xgs::JobStatus;
using xgs::JobSummary;
using xgs::LyricsOutcome;
using xgs::LyricsQuery;
using xgs::Progress;
using xgs::ResolvedItem;
using xgs::Stage;
using xgs::ToolPaths;
using xgs::job::DefaultDownloadService;
using xgs::job::Downloaded;
using xgs::job::DownloadRequest;
using xgs::job::DownloadResult;
using xgs::job::ItemDownloader;
using xgs::job::JobEvents;
using xgs::job::JobHandle;
using xgs::job::PreparedItem;
using xgs::output::LocalFolderSink;
using xgs::output::OutputSink;
using xgs::output::PutResult;
using xgs::output::PutStatus;
using xgs::process::LineHandler;
using xgs::testing::FakeLyricsProvider;
using xgs::testing::FakeOutputSink;
using xgs::testsupport::awaitStop;
using xgs::testsupport::downloadRunner;
using xgs::testsupport::endsWithFakeAudio;
using xgs::testsupport::FakeMetadata;
using xgs::testsupport::FakeProcessRunner;
using xgs::testsupport::ffmetadataTextOf;
using xgs::testsupport::ffmpegCommands;
using xgs::testsupport::FixedTools;
using xgs::testsupport::isFfmpegCommand;
using xgs::testsupport::kotlinLines;
using xgs::testsupport::outputDirOf;
using xgs::testsupport::readFfmetadata;
using xgs::testsupport::StartFailure;
using xgs::testsupport::StoppableProcessHandler;
using xgs::testsupport::testTools;
using xgs::testsupport::videoIdOf;
using xgs::testsupport::writeFakeCover;
using xgs::testsupport::writeFakeInfo;
using xgs::testsupport::writeFakeM4a;
using xgs::testsupport::writeFakeTagged;
using xgs::testsupport::ytDlpCommands;

namespace {

QString windowsPath(const QString& path) {
    QString out = path;
    return out.replace(u'/', u'\\');
}

QString videoIdFor(int number) {
    return u"vid%1"_s.arg(number, 8, 10, QChar(u'0'));
}

/// Busy-waits for duration (sleep_for on Windows rounds a short pause up to a timer tick of about 1-16 ms).
void spinFor(std::chrono::microseconds duration) {
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until) {
    }
}

QString watchUrl(const QString& videoId) {
    return xgs::ParsedInput{xgs::ParsedInput::Kind::Video, videoId, std::nullopt}.canonicalUrl();
}

/// Kotlin's `item(rank, artist = "A$rank", track = "T$rank", lowConfidence = false)`.
ResolvedItem item(int rank, std::optional<QString> artist = std::nullopt, std::optional<QString> track = std::nullopt,
                  bool lowConfidence = false) {
    ResolvedItem result;
    result.rank = rank;
    result.videoId = videoIdFor(rank);
    result.artist = artist.value_or(u"A%1"_s.arg(rank));
    result.track = track.value_or(u"T%1"_s.arg(rank));
    result.title = result.artist + u" - "_s + result.track;
    result.channel = u"ch"_s;
    result.lowConfidence = lowConfidence;
    result.expectedFileName = xgs::filename::format(rank, result.artist, result.track);
    return result;
}

/// item(1) .. item(count), Kotlin's `(1..count).map { item(it) }`.
QList<ResolvedItem> itemsUpTo(int count) {
    QList<ResolvedItem> items;
    for (int rank = 1; rank <= count; ++rank) items.append(item(rank));
    return items;
}

/// Kotlin's `succeeding`: one progress line, then the m4a.
FakeProcessRunner succeeding() {
    return downloadRunner([](const QStringList& command, const LineHandler& out, const LineHandler&) {
        out(u"XGSP|downloading|50|100|NA"_s);
        writeFakeM4a(command);
        return 0;
    });
}

/// Kotlin's `failingWith(vararg stderr)`: every command writes the lines to stderr and exits with 1.
FakeProcessRunner failingWith(QStringList stderrLines) {
    return FakeProcessRunner([stderrLines](const QStringList&, const LineHandler&, const LineHandler& err) {
        for (const QString& line : stderrLines) err(line);
        return 1;
    });
}

/// Kotlin's `events.receiveAsFlow().toList()`: every event until the queue is closed.
QList<JobEvent> collect(JobHandle& handle) {
    QList<JobEvent> events;
    while (std::optional<JobEvent> event = handle.nextEvent()) events.append(*event);
    return events;
}

QList<JobEvent> collect(const std::unique_ptr<JobHandle>& handle) {
    return collect(*handle);
}

/// Kotlin's `done(events)`: the last event, which must be the JobDone. Otherwise the test fails here and goes on with a JobDone
/// no job sends (an exception would end the whole Qt Test run, not only this test, as Kotlin's ClassCastException does).
JobDone done(const QList<JobEvent>& events) {
    if (events.isEmpty() || !std::holds_alternative<JobDone>(events.last())) {
        QTest::qFail("the last event is not JobDone", __FILE__, __LINE__);
        return JobDone{static_cast<JobStatus>(-1), JobSummary{-1, -1, -1}};
    }
    return std::get<JobDone>(events.last());
}

/// Kotlin's `filterIsInstance<T>()`.
template <typename T>
QList<T> ofType(const QList<JobEvent>& events) {
    QList<T> picked;
    for (const JobEvent& event : events) {
        if (const T* value = std::get_if<T>(&event)) picked.append(*value);
    }
    return picked;
}

template <typename T>
qsizetype countOf(const QList<JobEvent>& events) {
    return ofType<T>(events).size();
}

QList<int> startedRanks(const QList<JobEvent>& events) {
    QList<int> ranks;
    for (const ItemStarted& started : ofType<ItemStarted>(events)) ranks.append(started.rank);
    return ranks;
}

QList<int> failedRanks(const QList<JobEvent>& events) {
    QList<int> ranks;
    for (const ItemFailed& failed : ofType<ItemFailed>(events)) ranks.append(failed.rank);
    return ranks;
}

/// Kotlin's `awaitStarted`: waits in real time until count commands have started (10 s at most), then gives an unwanted extra
/// one 200 ms to show up. Polling and the 200 ms are Kotlin's; nothing signals "no further command".
void awaitStarted(const FakeProcessRunner& runner, int count) {
    QElapsedTimer timer;
    timer.start();
    while (runner.commands().size() < count && timer.elapsed() < 10'000) std::this_thread::sleep_for(10ms);
    std::this_thread::sleep_for(200ms);
}

/// A one-shot signal (Kotlin's CompletableDeferred<Unit>).
class Signal {
public:
    void raise() {
        {
            const std::lock_guard lock(m_mutex);
            m_raised = true;
        }
        m_changed.notify_all();
    }
    /// Time is only a safety net: a broken service that never reaches the signal fails the test instead of hanging it.
    bool waitFor(std::chrono::milliseconds limit) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, limit, [this] { return m_raised; });
    }
    /// false when stop came first.
    bool wait(std::stop_token stop) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait(lock, stop, [this] { return m_raised; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable_any m_changed;
    bool m_raised = false;
};

/// Counts arrivals until count have come.
class Arrivals {
public:
    explicit Arrivals(int count) : m_count(count) {}
    void arrive() {
        {
            const std::lock_guard lock(m_mutex);
            ++m_arrived;
        }
        m_changed.notify_all();
    }
    /// Time is only a safety net, as in Signal.
    bool waitFor(std::chrono::milliseconds limit) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, limit, [this] { return m_arrived >= m_count; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    const int m_count;
    int m_arrived = 0;
};

/// A lyrics provider whose answer gets the stop token (FakeLyricsProvider's does not).
class StoppableLyrics final : public xgs::lyrics::LyricsProvider {
public:
    using Answer = std::function<std::optional<QString>(const LyricsQuery&, std::stop_token)>;
    explicit StoppableLyrics(Answer answer) : m_answer(std::move(answer)) {}
    std::optional<QString> find(const LyricsQuery& query, std::stop_token stop) override { return m_answer(query, stop); }

private:
    Answer m_answer;
};

/// Opens path with no sharing, so nothing can delete, replace or move it, and lets it go after hold on a thread of its own;
/// before that, beforeRelease runs on that thread. Destroying it waits for the release.
class HeldFile {
public:
    HeldFile(const QString& path, std::chrono::milliseconds hold, std::function<void()> beforeRelease = {}) {
        const HANDLE file = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        m_opened = file != INVALID_HANDLE_VALUE;
        if (!m_opened) return;
        m_thread = std::jthread([file, hold, beforeRelease = std::move(beforeRelease)] {
            std::this_thread::sleep_for(hold);
            if (beforeRelease) beforeRelease();
            CloseHandle(file);
        });
    }
    bool opened() const { return m_opened; }

private:
    bool m_opened = false;
    std::jthread m_thread;
};

/// Kotlin's `service(runner, tools, metadata, workDir, lyrics)`: the downloader and the service, owned together. The retry
/// delays are 1 ms (Kotlin's tests run them in virtual time).
struct ServiceOptions {
    ToolPaths tools = testTools();
    xgs::ytdlp::VideoMetadataSource* metadata = nullptr;   // nullptr: answers nothing (Kotlin's `VideoMetadataSource { null }`)
    std::optional<QString> workDir;                        // absent: the test's tempRoot
    xgs::lyrics::LyricsProvider* lyrics = nullptr;         // nullptr: NoLyricsProvider
    std::vector<std::chrono::milliseconds> retryDelays = {1ms, 1ms};
};

class Service {
public:
    Service(xgs::process::ProcessRunner& runner, const QString& workDir, ServiceOptions options)
        : m_tools(options.tools),
          m_downloader(options.lyrics ? std::make_unique<ItemDownloader>(runner, m_tools, metadataOf(options), *options.lyrics)
                                      : std::make_unique<ItemDownloader>(runner, m_tools, metadataOf(options))),
          m_service(*m_downloader, workDir, options.retryDelays) {}

    std::unique_ptr<JobHandle> start(DownloadRequest request, std::stop_source stop = std::stop_source()) {
        return m_service.start(std::move(request), std::move(stop));
    }

private:
    xgs::ytdlp::VideoMetadataSource& metadataOf(const ServiceOptions& options) {
        return options.metadata ? *options.metadata : m_noMetadata;
    }

    FixedTools m_tools;
    FakeMetadata m_noMetadata;
    std::unique_ptr<ItemDownloader> m_downloader;
    DefaultDownloadService m_service;
};

/// Kotlin's named arguments of `request(...)`. sink nullptr is a new LocalFolderSink(outDir), as Kotlin's default.
struct RequestOptions {
    bool overwrite = false;
    int concurrency = 1;
    OutputSink* sink = nullptr;
    std::optional<QString> album;
    bool includeRank = true;
    bool searchLyricsOnline = false;
    std::optional<QString> albumOverride;
};

const QString kDiskFull = u"OSError: [Errno 28] No space left on device"_s;
const QString kHttp503 = u"ERROR: unable to download video data: HTTP Error 503: Service Unavailable"_s;

// ---- helpers of the tests from `// ---- tagging ----` on (Task 3) ----
// (The JSON texts are plain string literals: with a raw string literal in the file moc finds no test class at all.)

/// The file name part of a path (Kotlin's `Path.of(path).fileName.toString()`).
QString fileNameOf(const QString& path) {
    return path.mid(std::max(path.lastIndexOf(u'\\'), path.lastIndexOf(u'/')) + 1);
}

/// Kotlin's `mutableListOf<String>()` that a runner fills with the ffmetadata texts it is handed. Safe from several threads.
class MetadataTexts {
public:
    void add(const QString& text) {
        const std::lock_guard lock(m_mutex);
        m_texts.append(text);
    }
    QStringList all() const {
        const std::lock_guard lock(m_mutex);
        return m_texts;
    }
    /// Kotlin's `single()`: the one text. With none or several the test fails here and goes on with an empty text (as done()).
    QString single() const {
        const QStringList texts = all();
        if (texts.size() != 1) {
            QTest::qFail(qPrintable(u"expected one metadata text, got %1"_s.arg(texts.size())), __FILE__, __LINE__);
            return {};
        }
        return texts.first();
    }

private:
    mutable std::mutex m_mutex;
    QStringList m_texts;
};

/// Kotlin's `taggingRunner(metadataTexts, withCover)`: yt-dlp writes the m4a (and a cover when withCover), ffmpeg records its
/// ffmetadata. texts nullptr stands for Kotlin's throwaway `mutableListOf()`.
FakeProcessRunner taggingRunner(MetadataTexts* texts, bool withCover = false) {
    return FakeProcessRunner([texts, withCover](const QStringList& command, const LineHandler&, const LineHandler&) {
        if (isFfmpegCommand(command)) {
            const QString text = ffmetadataTextOf(command);
            if (texts) texts->add(text);
            writeFakeTagged(command);
        } else {
            writeFakeM4a(command);
            if (withCover) writeFakeCover(command);
        }
        return 0;
    });
}

/// Kotlin's `infoRunner(metadataTexts, infoJson)`: like taggingRunner, and yt-dlp also writes the video's info file holding
/// infoJson (no file when absent).
FakeProcessRunner infoRunner(MetadataTexts* texts, std::optional<QByteArray> infoJson) {
    return FakeProcessRunner([texts, infoJson](const QStringList& command, const LineHandler&, const LineHandler&) {
        if (isFfmpegCommand(command)) {
            const QString text = ffmetadataTextOf(command);
            if (texts) texts->add(text);
            writeFakeTagged(command);
        } else {
            writeFakeM4a(command);
            if (infoJson) writeFakeInfo(command, *infoJson);
        }
        return 0;
    });
}

/// Kotlin's `albumLines(metadataText)`.
QStringList albumLines(const QString& metadataText) {
    QStringList lines;
    for (const QString& line : kotlinLines(metadataText)) {
        if (line.startsWith(u"album="_s)) lines.append(line);
    }
    return lines;
}

/// Kotlin's `JsonPrimitive(text).toString()`: text as a JSON string literal.
QByteArray jsonString(const QString& text) {
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);   // ["..."]
    return array.mid(1, array.size() - 2);
}

/// Kotlin's `infoWithDescription(description, album)`: an info file whose `description` is description (JSON-escaped here, so
/// the tests can use plain text).
QByteArray infoWithDescription(const QString& description, const std::optional<QString>& album = std::nullopt) {
    const QByteArray albumPart = album ? "\"album\":" + jsonString(*album) + "," : QByteArray();
    return "{\"id\":\"vid00000001\"," + albumPart + "\"description\":" + jsonString(description) + "}";
}

/// A made-up description: a heading, separators around the block, then links, a copyright line and hashtags.
const QString describedLyrics =
    u"Example Artist - Example Song\n=======\n[Lyrics]\n=======\n첫 번째 줄\n두 번째 줄\n\nLa la la\n=======\n"_s
    u"Instagram: https://example.invalid/artist\n© 2024 Example Label\n#example"_s;

/// A made-up answer of the lookup: Korean and English lines.
const QString onlineLyrics = u"온라인 첫 줄\nLa la online\n온라인 셋째 줄"_s;

/// A description that has no lyrics section: a title, a link and a hashtag.
const QString descriptionWithoutLyrics = u"Example Artist - Example Song\nListen everywhere\nhttps://example.invalid\n\n#example"_s;

/// Kotlin's `infoRunnerWith(infoJson, interceptor)`: like infoRunner without recording; interceptor may answer a command first
/// (with an exit code, after writing to stderr) and returns nothing to leave the command to the usual handling.
using Interceptor = std::function<std::optional<int>(const QStringList& command, const LineHandler& err)>;
FakeProcessRunner infoRunnerWith(std::optional<QByteArray> infoJson, Interceptor interceptor) {
    return FakeProcessRunner([infoJson, interceptor = std::move(interceptor)](const QStringList& command, const LineHandler&,
                                                                              const LineHandler& err) {
        if (const std::optional<int> answer = interceptor(command, err)) return *answer;
        if (isFfmpegCommand(command)) {
            writeFakeTagged(command);
        } else {
            writeFakeM4a(command);
            if (infoJson) writeFakeInfo(command, *infoJson);
        }
        return 0;
    });
}

/// Kotlin's `itemDone(events)`: the single ItemDone. Otherwise the test fails here and goes on with one no job sends.
ItemDone itemDone(const QList<JobEvent>& events) {
    const QList<ItemDone> all = ofType<ItemDone>(events);
    if (all.size() != 1) {
        QTest::qFail(qPrintable(u"expected one ItemDone, got %1"_s.arg(all.size())), __FILE__, __LINE__);
        return ItemDone{-1, {}, std::nullopt};
    }
    return all.first();
}

/// Kotlin's `DownloaderCase`.
struct DownloaderCase {
    std::optional<QString> answer;
    QByteArray infoJson;
    bool online = false;
    xgs::LyricsOutcome expected = xgs::LyricsOutcome::Description;
};

/// Kotlin's `filesIn(dir)`: the names in dir, sorted; none when dir is not a directory.
QStringList filesIn(const QString& dir) {
    if (!xgs::fs::isDirectory(dir)) return {};
    QStringList names = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    names.sort();
    return names;
}

/// Two entries that parse to the same artist and title: without the rank their file names are equal.
ResolvedItem firstTwin() {
    return item(1, u"Same"_s, u"Song"_s);
}
ResolvedItem secondTwin() {
    return item(2, u"Same"_s, u"Song"_s);
}

/// Kotlin's `heldRunner(gate)`: every yt-dlp call waits for gate, so every item has passed its existence check before any file
/// is put. A stop ends the wait (a failed test cancels the job instead of leaving it waiting).
FakeProcessRunner heldRunner(Signal& gate) {
    return downloadRunner(StoppableProcessHandler(
        [&gate](const QStringList& command, const LineHandler&, const LineHandler&, std::stop_token stop) {
            if (!gate.wait(stop)) return 0;
            writeFakeM4a(command);
            return 0;
        }));
}

}  // namespace

// QCOMPARE and QVERIFY2 for a helper that returns a value (theirs return nothing): record the failure, return fallback.
#define XGS_COMPARE_OR(actual, expected, fallback)                                            \
    do {                                                                                      \
        if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__)) return fallback; \
    } while (false)
#define XGS_VERIFY2_OR(statement, description, fallback)                                                     \
    do {                                                                                                     \
        if (!QTest::qVerify(static_cast<bool>(statement), #statement, description, __FILE__, __LINE__)) return fallback; \
    } while (false)

// Readable QCOMPARE failures (found by argument-dependent lookup).
namespace xgs {
namespace {
QString statusName(JobStatus status) {
    switch (status) {
    case JobStatus::Completed: return u"Completed"_s;
    case JobStatus::Cancelled: return u"Cancelled"_s;
    case JobStatus::Failed: return u"Failed"_s;
    }
    return u"?"_s;
}
QString outcomeName(LyricsOutcome outcome) {
    switch (outcome) {
    case LyricsOutcome::Description: return u"Description"_s;
    case LyricsOutcome::Online: return u"Online"_s;
    case LyricsOutcome::NotFound: return u"NotFound"_s;
    case LyricsOutcome::SearchOff: return u"SearchOff"_s;
    }
    return u"?"_s;
}
QString describe(const JobEvent& event) {
    if (const auto* e = std::get_if<ItemStarted>(&event)) return u"ItemStarted(%1, %2, %3)"_s.arg(e->rank).arg(e->videoId, e->fileName);
    if (const auto* e = std::get_if<Progress>(&event)) {
        return u"Progress(%1, %2, %3)"_s.arg(e->rank)
            .arg(e->stage == Stage::Downloading ? u"Downloading"_s : u"Finishing"_s,
                 e->percent ? QString::number(*e->percent, 'g', 17) : u"unknown"_s);
    }
    if (const auto* e = std::get_if<ItemDone>(&event)) {
        return u"ItemDone(%1, %2, %3)"_s.arg(e->rank).arg(e->fileName, e->lyrics ? outcomeName(*e->lyrics) : u"null"_s);
    }
    if (const auto* e = std::get_if<ItemSkipped>(&event)) return u"ItemSkipped(%1, %2)"_s.arg(e->rank).arg(e->reason);
    if (const auto* e = std::get_if<ItemFailed>(&event)) return u"ItemFailed(%1, %2)"_s.arg(e->rank).arg(e->message);
    const auto& e = std::get<JobDone>(event);
    return u"JobDone(%1, %2/%3/%4)"_s.arg(statusName(e.status)).arg(e.summary.succeeded).arg(e.summary.skipped).arg(e.summary.failed);
}
char* text(const QString& value) {
    return qstrdup(value.toUtf8().constData());
}
}  // namespace
/// Kotlin's `events.toString()`, for failure messages.
QString eventsText(const QList<JobEvent>& events) {
    QStringList parts;
    for (const JobEvent& event : events) parts.append(describe(event));
    return u"["_s + parts.join(u", "_s) + u"]"_s;
}
char* toString(const JobEvent& event) { return text(describe(event)); }
char* toString(const ItemStarted& event) { return text(describe(event)); }
char* toString(const ItemDone& event) { return text(describe(event)); }
char* toString(const LyricsOutcome& outcome) { return text(outcomeName(outcome)); }
char* toString(const LyricsQuery& q) {
    return text(u"LyricsQuery(%1, %2, %3, %4)"_s.arg(q.artist, q.title, q.album.value_or(u"null"_s),
                                                     q.durationSeconds ? QString::number(*q.durationSeconds) : u"null"_s));
}
char* toString(const JobDone& event) { return text(describe(event)); }
char* toString(const ItemSkipped& event) { return text(describe(event)); }
char* toString(const ItemFailed& event) { return text(describe(event)); }
char* toString(const Progress& event) { return text(describe(event)); }
char* toString(const JobStatus& status) { return text(statusName(status)); }
char* toString(const JobSummary& s) { return text(u"%1/%2/%3"_s.arg(s.succeeded).arg(s.skipped).arg(s.failed)); }
}  // namespace xgs

// Port of engine/src/test/.../job/DefaultDownloadServiceTest.kt: its 103 tests in Kotlin's order (the 26 before
// `// ---- tagging ----` from Task 2 of Q2d, the 77 from there on from Task 3), then the C++ additions. A cancelled coroutine
// is a stop requested on the job's stop source; an exception a Kotlin sink or provider throws is a PutResult or a
// std::runtime_error (author decisions 4, 5 and 13).
class DefaultDownloadServiceTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_temp;
    QString m_root;
    QString m_outDir;
    QString m_tempRoot;
    std::vector<std::unique_ptr<OutputSink>> m_sinks;   // the default sinks of request(); outlive the jobs of the test

    QString pathIn(const QString& dir, const QString& name) const { return *xgs::paths::child(dir, name); }

    std::unique_ptr<Service> service(xgs::process::ProcessRunner& runner, ServiceOptions options = {}) {
        const QString workDir = options.workDir.value_or(m_tempRoot);
        return std::make_unique<Service>(runner, workDir, std::move(options));
    }

    DownloadRequest request(QList<ResolvedItem> items, RequestOptions options = {}) {
        OutputSink* sink = options.sink;
        if (!sink) {
            m_sinks.push_back(std::make_unique<LocalFolderSink>(m_outDir));
            sink = m_sinks.back().get();
        }
        DownloadRequest result;
        result.items = std::move(items);
        result.sink = sink;
        result.overwrite = options.overwrite;
        result.concurrency = options.concurrency;
        result.album = options.album;
        result.includeRank = options.includeRank;
        result.searchLyricsOnline = options.searchLyricsOnline;
        result.albumOverride = options.albumOverride;
        return result;
    }

    /// Kotlin's `Files.list(tempRoot).count()`, which also fails when tempRoot is missing.
    qsizetype entriesIn(const QString& dir) const {
        if (!xgs::fs::isDirectory(dir)) return -1;
        return QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).size();
    }

    QString readText(const QString& path) const {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return u"<unreadable>"_s;
        return QString::fromUtf8(file.readAll());
    }

    /// Kotlin's `deliveredTag(infoJson, texts, album)`: downloads item 1 with the info file infoJson and returns the tag values
    /// that were handed to ffmpeg; nothing when one of its assertions failed (the caller then returns). texts nullptr stands
    /// for Kotlin's default fresh list.
    std::optional<QMap<QString, QString>> deliveredTag(std::optional<QByteArray> infoJson, MetadataTexts* texts = nullptr,
                                                       std::optional<QString> album = u"My List"_s) {
        MetadataTexts fresh;
        MetadataTexts& recorded = texts ? *texts : fresh;
        auto runner = infoRunner(&recorded, std::move(infoJson));
        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)}, {.album = album})));

        XGS_COMPARE_OR(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 0}}), std::nullopt);
        XGS_VERIFY2_OR(countOf<ItemFailed>(events) == 0, qPrintable(xgs::eventsText(events)), std::nullopt);
        return readFfmetadata(recorded.single());
    }

    /// Kotlin's named arguments of `deliveredWith`.
    struct DeliveredOptions {
        bool online = true;
        std::optional<ResolvedItem> item;                  // absent: item(1)
        std::optional<QString> album = u"My List"_s;
        MetadataTexts* texts = nullptr;                    // nullptr: a fresh list
        xgs::ytdlp::VideoMetadataSource* metadata = nullptr;
    };

    /// Kotlin's `deliveredWith(provider, infoJson, ...)`: downloads the item with provider, the info file infoJson and the lookup
    /// switched online; the item must succeed. Returns the tag values that were handed to ffmpeg; nothing when one of its
    /// assertions failed (the caller then returns).
    std::optional<QMap<QString, QString>> deliveredWith(xgs::lyrics::LyricsProvider& provider, std::optional<QByteArray> infoJson,
                                                        DeliveredOptions options = {}) {
        MetadataTexts fresh;
        MetadataTexts& recorded = options.texts ? *options.texts : fresh;
        auto runner = infoRunner(&recorded, std::move(infoJson));
        const QList<JobEvent> events =
            collect(service(runner, {.metadata = options.metadata, .lyrics = &provider})
                        ->start(request({options.item.value_or(item(1))},
                                        {.album = options.album, .searchLyricsOnline = options.online})));

        XGS_COMPARE_OR(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 0}}), std::nullopt);
        XGS_VERIFY2_OR(countOf<ItemFailed>(events) == 0, qPrintable(xgs::eventsText(events)), std::nullopt);
        return readFfmetadata(recorded.single());
    }

    /// Kotlin's `eventsWith(provider, infoJson, online)`: the events of a job over item 1.
    QList<JobEvent> eventsWith(xgs::lyrics::LyricsProvider& provider, std::optional<QByteArray> infoJson, bool online) {
        auto runner = infoRunner(nullptr, std::move(infoJson));
        return collect(service(runner, {.lyrics = &provider})->start(request({item(1)}, {.searchLyricsOnline = online})));
    }

private slots:
    void init() {
        m_temp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp->isValid());
        m_root = *xgs::paths::fromText(windowsPath(m_temp->path()));
        m_outDir = pathIn(m_root, u"out"_s);
        m_tempRoot = pathIn(m_root, u"work"_s);
    }
    void cleanup() {
        m_sinks.clear();
        m_temp.reset();
    }

    void downloadsEveryItemAndMovesFilesIntoTheSink() {
        auto runner = succeeding();
        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2)})));

        QVERIFY(xgs::fs::exists(pathIn(m_outDir, u"001 A1 - T1.m4a"_s)));
        QVERIFY(xgs::fs::exists(pathIn(m_outDir, u"002 A2 - T2.m4a"_s)));
        const QList<int> started = startedRanks(events);
        QCOMPARE(QSet<int>(started.begin(), started.end()), (QSet<int>{1, 2}));
        QSet<QString> doneNames;
        for (const ItemDone& itemDone : ofType<ItemDone>(events)) doneNames.insert(itemDone.fileName);
        QCOMPARE(doneNames, (QSet<QString>{u"001 A1 - T1.m4a"_s, u"002 A2 - T2.m4a"_s}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{2, 0, 0}}));
    }

    void reportsDownloadAndFinishingProgress() {
        auto runner = succeeding();
        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)})));

        const QList<Progress> progress = ofType<Progress>(events);
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.first(), (Progress{1, Stage::Downloading, 50.0}));
        QCOMPARE(progress.last(), (Progress{1, Stage::Finishing, std::nullopt}));
    }

    void progressEventsAreThrottledToWholePercents() {
        auto noisy = downloadRunner([](const QStringList& command, const LineHandler& out, const LineHandler&) {
            for (int i = 0; i < 1000; ++i) out(u"XGSP|downloading|%1|1000|NA"_s.arg(i));
            writeFakeM4a(command);
            return 0;
        });

        const QList<JobEvent> events = collect(service(noisy)->start(request({item(1)})));

        const QList<Progress> progress = ofType<Progress>(events);
        QCOMPARE(std::count_if(progress.begin(), progress.end(), [](const Progress& p) { return p.stage == Stage::Downloading; }), 100);
        QCOMPARE(std::count_if(progress.begin(), progress.end(), [](const Progress& p) { return p.stage == Stage::Finishing; }), 1);
    }

    void existingFilesAreSkippedUnlessOverwriteIsOn() {
        QVERIFY(xgs::fs::makeDirectories(m_outDir));
        const QString existing = pathIn(m_outDir, u"001 A1 - T1.m4a"_s);
        QVERIFY(xgs::fs::writeUtf8(existing, u"old"_s).isOk());
        auto runner = succeeding();

        const QList<JobEvent> skipped = collect(service(runner)->start(request({item(1)})));
        QCOMPARE(ofType<ItemSkipped>(skipped), (QList<ItemSkipped>{ItemSkipped{1, u"이미 존재"_s}}));
        QCOMPARE(readText(existing), u"old"_s);
        QCOMPARE(done(skipped).summary.skipped, 1);

        collect(service(runner)->start(request({item(1)}, {.overwrite = true})));
        QVERIFY2(endsWithFakeAudio(existing), "the old file must be replaced by the tagged one");
    }

    void transientFailuresAreRetriedWithBackoff() {
        std::atomic<int> calls{0};
        auto flaky = downloadRunner([&](const QStringList& command, const LineHandler&, const LineHandler& err) {
            if (++calls == 1) {
                err(kHttp503);
                return 1;
            }
            writeFakeM4a(command);
            return 0;
        });

        const QList<JobEvent> events = collect(service(flaky)->start(request({item(1)})));

        QCOMPARE(ytDlpCommands(flaky).size(), 2);
        QVERIFY2(ffmpegCommands(flaky).size() == 1, "only the successful attempt is tagged");
        QCOMPARE(countOf<ItemFailed>(events), 0);
        QCOMPARE(done(events).summary, (JobSummary{1, 0, 0}));
    }

    void givesUpAfterTwoRetries() {
        auto runner = failingWith({u"ERROR: HTTP Error 429: Too Many Requests"_s});

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)})));

        QCOMPARE(runner.commands().size(), 3);
        QCOMPARE(countOf<ItemFailed>(events), 1);
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{0, 0, 1}}));
    }

    void unavailableVideosAreSkippedWithoutRetry() {
        auto runner = failingWith({u"ERROR: [youtube] x: Private video. Sign in if you've been granted access"_s});

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)})));

        QCOMPARE(runner.commands().size(), 1);
        QCOMPARE(ofType<ItemSkipped>(events), (QList<ItemSkipped>{ItemSkipped{1, u"비공개 영상"_s}}));
        QCOMPARE(done(events).summary, (JobSummary{0, 1, 0}));
    }

    void otherFailuresDoNotStopTheRemainingItems() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler& err) {
            if (command.join(u' ').contains(u"vid00000001"_s)) {
                err(u"ERROR: something odd"_s);
                return 1;
            }
            writeFakeM4a(command);
            return 0;
        });

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2)})));

        QCOMPARE(failedRanks(events), (QList<int>{1}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 1}}));
    }

    void fatalErrorAbortsTheWholeJob() {
        auto runner = failingWith({kDiskFull});

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2), item(3)})));

        QCOMPARE(done(events).status, JobStatus::Failed);
        QCOMPARE(startedRanks(events), (QList<int>{1}));
        QCOMPARE(runner.commands().size(), 1);
    }

    void missingYtDlpAbortsTheJob() {
        auto runner = succeeding();
        ToolPaths noYtDlp = testTools();
        noYtDlp.ytDlp.reset();

        const QList<JobEvent> events = collect(service(runner, {.tools = noYtDlp})->start(request({item(1)})));

        QCOMPARE(done(events).status, JobStatus::Failed);
        const QList<ItemFailed> failed = ofType<ItemFailed>(events);
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().message.contains(u"yt-dlp"_s));
    }

    void cancellingStopsRunningDownloadsAndReportsCancelled() {
        Signal started;
        FakeProcessRunner hanging(StoppableProcessHandler(
            [&](const QStringList&, const LineHandler&, const LineHandler&, std::stop_token stop) {
                started.raise();
                awaitStop(stop);
                return 0;
            }));
        auto svc = service(hanging);
        auto handle = svc->start(request({item(1)}));
        QVERIFY(started.waitFor(10s));

        handle->cancel();
        const QList<JobEvent> events = collect(handle);

        QCOMPARE(done(events).status, JobStatus::Cancelled);
        QCOMPARE(hanging.commands().size(), 1);
    }

    void workDirectoriesAreRemovedAfterTheJob() {
        auto runner = succeeding();
        collect(service(runner)->start(request({item(1), item(2)})));

        QCOMPARE(entriesIn(m_tempRoot), 0);
    }

    void cancellingAlsoRemovesWorkDirectories() {
        Signal started;
        FakeProcessRunner hanging(StoppableProcessHandler(
            [&](const QStringList& command, const LineHandler&, const LineHandler&, std::stop_token stop) {
                writeFakeM4a(command);
                started.raise();
                awaitStop(stop);
                return 0;
            }));
        auto svc = service(hanging);
        auto handle = svc->start(request({item(1)}));
        QVERIFY(started.waitFor(10s));

        handle->cancel();
        collect(handle);

        QCOMPARE(entriesIn(m_tempRoot), 0);
    }

    void concurrencyIsLimitedToTheRequestedNumber() {
        Signal gate;
        auto held = downloadRunner(StoppableProcessHandler(
            [&](const QStringList& command, const LineHandler&, const LineHandler&, std::stop_token stop) {
                if (!gate.wait(stop)) return 0;   // a failed test cancels the job instead of leaving it waiting
                writeFakeM4a(command);
                return 0;
            }));
        auto svc = service(held);
        auto handle = svc->start(request(itemsUpTo(6), {.concurrency = 2}));

        awaitStarted(held, 2);
        QVERIFY2(held.commands().size() == 2, "the other four items must wait for a free slot");
        gate.raise();
        collect(handle);

        QCOMPARE(held.maxActive(), 2);
        QCOMPARE(ytDlpCommands(held).size(), 6);
        QCOMPARE(ffmpegCommands(held).size(), 6);
    }

    void theServiceSetsNoUpperLimitOfItsOwn() {
        Signal gate;
        auto held = downloadRunner(StoppableProcessHandler(
            [&](const QStringList& command, const LineHandler&, const LineHandler&, std::stop_token stop) {
                if (!gate.wait(stop)) return 0;
                writeFakeM4a(command);
                return 0;
            }));
        auto svc = service(held);
        auto handle = svc->start(request(itemsUpTo(10), {.concurrency = 8}));

        awaitStarted(held, 8);
        QVERIFY2(held.commands().size() == 8, "eight parallel downloads were asked for, and the other two wait");
        gate.raise();
        collect(handle);

        QCOMPARE(held.maxActive(), 8);
        QCOMPARE(ytDlpCommands(held).size(), 10);
    }

    void aConcurrencyBelowOneRunsOneAtATime() {
        auto runner = succeeding();
        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2), item(3)}, {.concurrency = 0})));

        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{3, 0, 0}}));
    }

    void lowConfidenceItemsAreRenamedFromFullMetadata() {
        FakeMetadata metadata([](const QString&, std::stop_token) {
            return std::optional<xgs::ytdlp::VideoMeta>(
                xgs::ytdlp::VideoMeta{u"Dynamite"_s, u"BTS - Topic"_s, u"BTS"_s, u"Dynamite"_s});
        });
        const ResolvedItem lowConfidence = item(1, u"BTS - Topic"_s, u"Dynamite"_s, true);
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner, {.metadata = &metadata})->start(request({lowConfidence})));

        const QList<ItemStarted> started = ofType<ItemStarted>(events);
        QCOMPARE(started.size(), 1);
        QCOMPARE(started.first().fileName, u"001 BTS - Dynamite.m4a"_s);
        QVERIFY(xgs::fs::exists(pathIn(m_outDir, u"001 BTS - Dynamite.m4a"_s)));
    }

    void sinkFailuresAreReportedPerItem() {
        // Kotlin: put throws IOException("disk on fire").
        FakeOutputSink brokenSink({}, [](const QString&, const QString&, bool) {
            return PutResult{PutStatus::Failed, u"disk on fire"_s, 0};
        });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)}, {.sink = &brokenSink})));

        const QList<ItemFailed> failed = ofType<ItemFailed>(events);
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().message.contains(u"disk on fire"_s));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{0, 0, 1}}));
    }

    void cancellingRightAfterStartStillEndsWithJobDoneCancelled() {
        // Author decision 4: with threads "right after start" is a race, so the stop is already requested when the job starts.
        auto runner = succeeding();
        std::stop_source stopped;
        stopped.request_stop();
        auto svc = service(runner);
        auto handle = svc->start(request({item(1)}), stopped);
        handle->cancel();

        const QList<JobEvent> events = collect(handle);

        QCOMPARE(done(events).status, JobStatus::Cancelled);
        QVERIFY2(!xgs::fs::exists(m_tempRoot), "no work folder is created when the job is cancelled before it starts");
    }

    void anUnusableWorkFolderEndsTheJobWithJobDoneFailed() {
        const QString blocker = pathIn(m_root, u"blocker"_s);
        QVERIFY(xgs::fs::writeUtf8(blocker, u"a file, not a folder"_s).isOk());
        auto runner = succeeding();

        const QList<JobEvent> events =
            collect(service(runner, {.workDir = pathIn(blocker, u"work"_s)})->start(request({item(1)})));

        QCOMPARE(done(events), (JobDone{JobStatus::Failed, JobSummary{0, 0, 0}}));
        QCOMPARE(runner.commands().size(), 0);
    }

    void sameVideoTwiceUsesSeparateWorkFolders() {
        const ResolvedItem first = item(1);
        ResolvedItem second = item(2);
        second.videoId = first.videoId;
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request({first, second}, {.concurrency = 2})));

        QSet<int> doneRanks;
        for (const ItemDone& itemDone : ofType<ItemDone>(events)) doneRanks.insert(itemDone.rank);
        QCOMPARE(doneRanks, (QSet<int>{1, 2}));
        QVERIFY(xgs::fs::exists(pathIn(m_outDir, u"001 A1 - T1.m4a"_s)));
        QVERIFY(xgs::fs::exists(pathIn(m_outDir, u"002 A2 - T2.m4a"_s)));
        QSet<QString> dirs;
        for (const QStringList& command : ytDlpCommands(runner)) dirs.insert(outputDirOf(command));
        QCOMPARE(dirs.size(), 2);
    }

    void accessDeniedFromTheSinkAbortsTheJob() {
        // Kotlin: AccessDeniedException("x"); the text of a C++ result is anything, the code is ERROR_ACCESS_DENIED.
        FakeOutputSink sink({}, [](const QString&, const QString&, bool) { return PutResult{PutStatus::Failed, u"x"_s, 5}; });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2), item(3)}, {.sink = &sink})));

        QCOMPARE(done(events).status, JobStatus::Failed);
        QCOMPARE(countOf<ItemFailed>(events), 1);
        QCOMPARE(ytDlpCommands(runner).size(), 1);
    }

    void diskFullFromTheSinkAbortsTheJob() {
        // Kotlin: IOException("No space left on device").
        FakeOutputSink sink({}, [](const QString&, const QString&, bool) {
            return PutResult{PutStatus::Failed, u"No space left on device"_s, 0};
        });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2), item(3)}, {.sink = &sink})));

        QCOMPARE(done(events).status, JobStatus::Failed);
        QCOMPARE(countOf<ItemFailed>(events), 1);
        QCOMPARE(ytDlpCommands(runner).size(), 1);
    }

    void jobDoneIsSentExactlyOnce() {
        auto runner = succeeding();
        const QList<JobEvent> completed = collect(service(runner)->start(request({item(1), item(2)})));
        // A second sink: the first job already put "001 A1 - T1.m4a" into outDir, so the items would be skipped there.
        LocalFolderSink abortedSink(pathIn(m_root, u"out-aborted"_s));
        auto failing = failingWith({kDiskFull});
        const QList<JobEvent> aborted =
            collect(service(failing)->start(request({item(1), item(2)}, {.sink = &abortedSink})));

        QCOMPARE(done(completed).status, JobStatus::Completed);
        QCOMPARE(done(aborted).status, JobStatus::Failed);
        for (const QList<JobEvent>& events : {completed, aborted}) {
            QCOMPARE(countOf<JobDone>(events), 1);
            QVERIFY(std::holds_alternative<JobDone>(events.last()));
        }
    }

    void aFileNamedLikeADiskFullErrorDoesNotAbortTheJob() {
        // FileAlreadyExistsException's message is just the path: a file name containing disk-full words is not a full disk.
        FakeOutputSink sink({}, [](const QString&, const QString&, bool) {
            return PutResult{PutStatus::AlreadyExists, u"C:/out/001 A - Disk Full No Space Left.m4a"_s, 0};
        });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2)}, {.sink = &sink})));

        QCOMPARE(countOf<ItemFailed>(events), 2);
        QCOMPARE(done(events).status, JobStatus::Completed);
        QCOMPARE(ytDlpCommands(runner).size(), 2);
    }

    void aRealDiskFullReasonInsideAFileSystemExceptionAbortsTheJob() {
        // Kotlin: FileSystemException("C:/out/x.m4a", null, "No space left on device"): the reason is the error text.
        FakeOutputSink sink({}, [](const QString&, const QString&, bool) {
            return PutResult{PutStatus::Failed, u"No space left on device"_s, 0};
        });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2), item(3)}, {.sink = &sink})));

        QCOMPARE(done(events).status, JobStatus::Failed);
        QCOMPARE(ytDlpCommands(runner).size(), 1);
    }

    // ---- tagging ----

    void taggingRunsOncePerItemRightAfterItsDownload() {
        auto runner = taggingRunner(nullptr);

        collect(service(runner)->start(request({item(1), item(2)})));

        QList<bool> kinds;
        for (const QStringList& command : runner.commands()) kinds.append(isFfmpegCommand(command));
        QCOMPARE(kinds, (QList<bool>{false, true, false, true}));
        QCOMPARE(ffmpegCommands(runner).size(), 2);
        QStringList tagged;
        for (const QStringList& command : ffmpegCommands(runner)) tagged.append(fileNameOf(command.at(command.indexOf(u"-i"_s) + 1)));
        QCOMPARE(tagged, (QStringList{u"vid00000001.m4a"_s, u"vid00000002.m4a"_s}));
    }

    void theTaggedFileIsWhatReachesTheSink() {
        auto runner = succeeding();
        collect(service(runner)->start(request({item(1)})));

        QVERIFY(endsWithFakeAudio(pathIn(m_outDir, u"001 A1 - T1.m4a"_s)));
    }

    void finishingIsReportedOnceAfterTheDownloadWhateverYtDlpPrintsAroundIt() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler& out, const LineHandler&) {
            out(u"XGSPP|started|ThumbnailsConvertor"_s);   // yt-dlp's post-processor lines are not read any more
            out(u"XGSP|downloading|50|100|NA"_s);
            out(u"XGSPP|started|MoveFiles"_s);
            writeFakeM4a(command);
            return 0;
        });

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)})));

        QCOMPARE(ofType<Progress>(events),
                 (QList<Progress>{Progress{1, Stage::Downloading, 50.0}, Progress{1, Stage::Finishing, std::nullopt}}));
    }

    void aFailedDownloadReportsNoFinishingStage() {
        auto runner = failingWith({u"ERROR: something odd"_s});
        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)})));

        QCOMPARE(ofType<Progress>(events), QList<Progress>());
    }

    void theMetadataFileCarriesTheParsedArtistTitleTrackAndAlbum() {
        MetadataTexts texts;
        auto runner = taggingRunner(&texts);

        collect(service(runner)->start(request({item(5, u"Artist Five"_s, u"Song Five"_s)}, {.album = u"My List"_s})));

        QString text = texts.single();
        if (text.endsWith(u'\n')) text.chop(1);
        QCOMPARE(text.split(u'\n'), (QStringList{
                                        u";FFMETADATA1"_s,
                                        u"title=Song Five"_s,
                                        u"artist=Artist Five"_s,
                                        u"album_artist=Various Artists"_s,
                                        u"album=My List"_s,
                                        u"track=5"_s,
                                        u"comment=https://www.youtube.com/watch?v\\=vid00000005"_s,
                                    }));
    }

    void theUrlIsTheCommentInTheMetadataFileAndNeverOnTheCommandLine() {
        MetadataTexts texts;
        auto runner = taggingRunner(&texts);

        collect(service(runner)->start(request({item(5)})));

        QCOMPARE(readFfmetadata(texts.single()).value(u"comment"_s), u"https://www.youtube.com/watch?v=vid00000005"_s);
        const QList<QStringList> commands = ffmpegCommands(runner);
        QCOMPARE(commands.size(), 1);
        QVERIFY2(std::none_of(commands.first().begin(), commands.first().end(),
                              [](const QString& argument) { return argument.contains(u"youtube"_s); }),
                 "tag text must not be on the command line");
    }

    void noAlbumLineIsWrittenWhenTheRequestHasNoAlbum() {
        MetadataTexts texts;
        auto runner = taggingRunner(&texts);

        collect(service(runner)->start(request({item(1)})));

        const QStringList lines = kotlinLines(texts.single());
        QVERIFY2(std::none_of(lines.begin(), lines.end(), [](const QString& line) { return line.startsWith(u"album="_s); }),
                 qPrintable(texts.single()));
        QVERIFY(lines.contains(u"track=1"_s));
    }

    void tagsKeepTheOriginalTextEvenWhenTheFileNameIsSanitized() {
        MetadataTexts texts;
        auto runner = taggingRunner(&texts);

        const QList<JobEvent> events =
            collect(service(runner)->start(request({item(1, u"AC/DC"_s, u"Who Made Who?"_s)})));

        const QStringList lines = kotlinLines(texts.single());
        QVERIFY2(lines.contains(u"title=Who Made Who?"_s), qPrintable(texts.single()));
        QVERIFY2(lines.contains(u"artist=AC/DC"_s), qPrintable(texts.single()));
        const QList<ItemStarted> started = ofType<ItemStarted>(events);
        QCOMPARE(started.size(), 1);
        const QString fileName = started.first().fileName;
        QCOMPARE(fileName, xgs::filename::format(1, u"AC/DC"_s, u"Who Made Who?"_s));
        QVERIFY2(!(fileName.contains(u'/') || fileName.contains(u'?')), qPrintable(fileName));
    }

    void lowConfidenceItemsAreTaggedWithTheSecondParse() {
        MetadataTexts texts;
        FakeMetadata metadata([](const QString&, std::stop_token) {
            return std::optional<xgs::ytdlp::VideoMeta>(
                xgs::ytdlp::VideoMeta{u"Dynamite"_s, u"BTS - Topic"_s, u"BTS"_s, u"Dynamite"_s});
        });
        const ResolvedItem lowConfidence = item(1, u"BTS - Topic"_s, u"Dynamite (Official)"_s, true);
        auto runner = taggingRunner(&texts);

        collect(service(runner, {.metadata = &metadata})->start(request({lowConfidence})));

        const QStringList lines = kotlinLines(texts.single());
        QVERIFY2(lines.contains(u"title=Dynamite"_s), qPrintable(texts.single()));
        QVERIFY2(lines.contains(u"artist=BTS"_s), qPrintable(texts.single()));
        QVERIFY2(lines.contains(u"album_artist=Various Artists"_s), qPrintable(texts.single()));
    }

    void aThumbnailLeftByYtDlpBecomesTheCover() {
        auto runner = taggingRunner(nullptr, true);

        collect(service(runner)->start(request({item(1)})));

        const QList<QStringList> commands = ffmpegCommands(runner);
        QCOMPARE(commands.size(), 1);
        const QStringList& command = commands.first();
        QStringList inputs;
        for (qsizetype i = 0; i + 1 < command.size(); ++i) {
            if (command.at(i) == u"-i"_s) inputs.append(fileNameOf(command.at(i + 1)));
        }
        QCOMPARE(inputs, (QStringList{u"vid00000001.m4a"_s, u"vid00000001.jpg"_s, u"vid00000001.ffmeta"_s}));
        QVERIFY(command.contains(u"attached_pic"_s));
    }

    void withoutAThumbnailTheTagsAreWrittenWithoutACover() {
        auto runner = taggingRunner(nullptr, false);

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)})));

        const QList<QStringList> commands = ffmpegCommands(runner);
        QCOMPARE(commands.size(), 1);
        QCOMPARE(commands.first().count(u"-i"_s), 2);
        QVERIFY(!commands.first().contains(u"-vf"_s));
        QCOMPARE(done(events).summary, (JobSummary{1, 0, 0}));
    }

    void aTaggingFailureFailsOnlyThatItem() {
        FakeProcessRunner runner([](const QStringList& command, const LineHandler&, const LineHandler& err) {
            if (!isFfmpegCommand(command)) {
                writeFakeM4a(command);
                return 0;
            }
            if (std::any_of(command.begin(), command.end(), [](const QString& a) { return a.contains(u"vid00000001"_s); })) {
                err(u"Error while writing the tag"_s);
                return 1;
            }
            writeFakeTagged(command);
            return 0;
        });

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2)})));

        QCOMPARE(ofType<ItemFailed>(events),
                 (QList<ItemFailed>{ItemFailed{1, u"태그를 쓰지 못했습니다: Error while writing the tag"_s}}));
        QList<int> doneRanks;
        for (const ItemDone& finished : ofType<ItemDone>(events)) doneRanks.append(finished.rank);
        QCOMPARE(doneRanks, (QList<int>{2}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 1}}));
        QVERIFY2(!xgs::fs::exists(pathIn(m_outDir, u"001 A1 - T1.m4a"_s)), "an untagged file must not reach the sink");
        QVERIFY(xgs::fs::exists(pathIn(m_outDir, u"002 A2 - T2.m4a"_s)));
        QVERIFY2(ytDlpCommands(runner).size() == 2, "a tagging failure is not retried");
    }

    void missingFfmpegAbortsTheJobBeforeAnyDownloadStarts() {
        auto runner = taggingRunner(nullptr);
        ToolPaths noFfmpeg = testTools();
        noFfmpeg.ffmpeg.reset();

        const QList<JobEvent> events =
            collect(service(runner, {.tools = noFfmpeg})->start(request({item(1), item(2), item(3)})));

        QCOMPARE(done(events).status, JobStatus::Failed);
        // Like a missing yt-dlp: one failed item, the others are never started.
        QCOMPARE(ofType<ItemFailed>(events), (QList<ItemFailed>{ItemFailed{1, u"ffmpeg를 찾을 수 없습니다."_s}}));
        QVERIFY2(runner.commands().isEmpty(), "no process may be started without ffmpeg");
        QCOMPARE(done(events).summary, (JobSummary{0, 0, 1}));
    }

    // ---- the album tag: the request's album (the playlist title), else the video's own album ----

    void thePlaylistTitleBeatsTheVideosOwnAlbum() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"id\":\"vid00000001\",\"album\":\"Palette\"}");

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)}, {.album = u"My List"_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=My List"_s}));
        QVERIFY2(kotlinLines(texts.single()).contains(u"album_artist=Various Artists"_s),
                 qPrintable(u"the album artist is always Various Artists: "_s + texts.single()));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 0}}));
        QVERIFY2(filesIn(m_outDir) == QStringList{u"001 A1 - T1.m4a"_s}, "the info file stays in the work folder");
    }

    void withoutAnInfoFileTheAlbumIsThePlaylistTitle() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, std::nullopt);

        collect(service(runner)->start(request({item(1)}, {.album = u"My List"_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=My List"_s}));
    }

    void anInfoFileWithoutAnAlbumStillGetsThePlaylistTitle() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"id\":\"vid00000001\",\"title\":\"x\",\"artist\":\"A1\"}");

        collect(service(runner)->start(request({item(1)}, {.album = u"My List"_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=My List"_s}));
    }

    void aBlankInfoAlbumStillGetsThePlaylistTitle() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"album\":\"   \"}");

        collect(service(runner)->start(request({item(1)}, {.album = u"My List"_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=My List"_s}));
    }

    void aBlankPlaylistTitleLeavesTheVideosOwnAlbum() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"album\":\"Palette\"}");

        collect(service(runner)->start(request({item(1)}, {.album = u"  "_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=Palette"_s}));
    }

    void aSingleVideoWithAnInfoAlbumGetsThatAlbum() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"album\":\"Palette\"}");

        collect(service(runner)->start(request({item(1)}, {.album = std::nullopt})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=Palette"_s}));
    }

    void aSingleVideoWithoutAnyAlbumGetsNoAlbumLine() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"id\":\"vid00000001\"}");

        collect(service(runner)->start(request({item(1)}, {.album = std::nullopt})));

        QCOMPARE(albumLines(texts.single()), QStringList());
        QVERIFY2(kotlinLines(texts.single()).contains(u"track=1"_s), qPrintable(texts.single()));
    }

    void anAlbumNameTheUserTypedBeatsTheVideosOwnAlbumAndThePlaylistTitle() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"album\":\"Palette\"}");

        collect(service(runner)->start(request({item(1)}, {.album = u"My List"_s, .albumOverride = u"내 앨범"_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=내 앨범"_s}));
        QVERIFY2(kotlinLines(texts.single()).contains(u"album_artist=Various Artists"_s),
                 qPrintable(u"the album artist is always Various Artists: "_s + texts.single()));
    }

    void anAlbumNameTheUserTypedGivesASingleVideoWithoutAnyAlbumAnAlbumLine() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"id\":\"vid00000001\"}");

        collect(service(runner)->start(request({item(1)}, {.album = std::nullopt, .albumOverride = u"내 앨범"_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=내 앨범"_s}));
    }

    void aBlankAlbumNameTheUserTypedChangesNothing() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"album\":\"Palette\"}");

        collect(service(runner)->start(request({item(1)}, {.album = u"My List"_s, .albumOverride = u"   "_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=My List"_s}));
    }

    void aCorruptInfoFileFallsBackAndTheItemStillSucceeds() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, "{\"album\":\"Pal");

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)}, {.album = u"My List"_s})));

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=My List"_s}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 0}}));
        QVERIFY2(countOf<ItemFailed>(events) == 0, qPrintable(xgs::eventsText(events)));
        QVERIFY(xgs::fs::exists(pathIn(m_outDir, u"001 A1 - T1.m4a"_s)));
    }

    // ---- lyrics: the description in the info file ----

    void lyricsFromTheInfoDescriptionAreInTheMetadataFile() {
        const auto tag = deliveredTag(infoWithDescription(describedLyrics));
        if (!tag) return;

        QCOMPARE(tag->value(u"lyrics"_s), u"첫 번째 줄\n두 번째 줄\n\nLa la la"_s);
        QVERIFY2(tag->value(u"comment"_s) == u"https://www.youtube.com/watch?v=vid00000001"_s, "the comment is unchanged");
    }

    void aSingleVideosAlbumAndLyricsComeFromTheSameInfoFile() {
        MetadataTexts texts;

        const auto tag = deliveredTag(infoWithDescription(u"Heading\nLyrics:\nLine one\nLine two\nLine three"_s, u"Palette"_s),
                                      &texts, std::nullopt);
        if (!tag) return;

        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=Palette"_s}));
        QCOMPARE(tag->value(u"lyrics"_s), u"Line one\nLine two\nLine three"_s);
    }

    void withoutAnInfoFileThereIsNoLyrics() {
        const auto tag = deliveredTag(std::nullopt);
        if (!tag) return;
        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    void anInfoFileWithoutADescriptionGivesNoLyrics() {
        const auto tag = deliveredTag("{\"id\":\"vid00000001\",\"album\":\"Palette\"}");
        if (!tag) return;
        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    void aDescriptionThatIsNotAStringGivesNoLyrics() {
        const auto tag = deliveredTag("{\"id\":\"vid00000001\",\"description\":[\"[Lyrics]\",\"a\",\"b\",\"c\"]}");
        if (!tag) return;
        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    void aDescriptionWithoutALyricsSectionGivesNoLyrics() {
        const QString description = u"Example Artist - Example Song\nListen everywhere\nhttps://example.invalid\n\n#example"_s;

        const auto tag = deliveredTag(infoWithDescription(description));
        if (!tag) return;
        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    void aLyricsSectionWithFewerThanThreeLinesGivesNoLyrics() {
        const QString description = u"[Lyrics]\nLine one\nLine two\n=======\nhttps://example.invalid"_s;

        const auto tag = deliveredTag(infoWithDescription(description));
        if (!tag) return;
        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    void aCorruptInfoFileGivesNoLyricsAndTheItemStillSucceeds() {
        // Cut off inside the description: the lyrics section itself is complete, but the file is not valid JSON.
        const auto tag = deliveredTag("{\"description\":\"[Lyrics]\\nLine one\\nLine two\\nLine three");
        if (!tag) return;

        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    // ---- lyrics: the internet lookup when the description has none ----

    void withoutLyricsInTheDescriptionAndTheOptionOnTheProviderIsAskedOnceAndItsLyricsReachTheFile() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        const auto tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics));
        if (!tag) return;

        QCOMPARE(provider.queries().size(), 1);
        QCOMPARE(tag->value(u"lyrics"_s), onlineLyrics);
    }

    void theQueryHoldsTheParsedArtistAndTitleTheTagAlbumAndTheLengthFromTheInfoFile() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        const QByteArray json =
            "{\"id\":\"vid00000007\",\"album\":\"Palette\",\"duration\":258.4,\"description\":" + jsonString(descriptionWithoutLyrics) + "}";

        const auto tag = deliveredWith(provider, json, {.item = item(7, u"Artist Seven"_s, u"Song Seven"_s), .album = u"My List"_s});
        if (!tag) return;

        QCOMPARE(provider.queries(), (QList<LyricsQuery>{LyricsQuery{u"Artist Seven"_s, u"Song Seven"_s, u"Palette"_s, 258}}));
    }

    void theQueryAlbumIsTheOneTheTagGetsSoThePlaylistTitleWhenTheVideoHasNone() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        if (!deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), {.album = u"My List"_s})) return;
        if (!deliveredWith(provider, "{\"album\":\"  \",\"duration\":200}", {.item = item(2), .album = u"My List"_s})) return;

        QList<std::optional<QString>> albums;
        for (const LyricsQuery& query : provider.queries()) albums.append(query.album);
        QCOMPARE(albums, (QList<std::optional<QString>>{u"My List"_s, u"My List"_s}));
    }

    void anAlbumNameTheUserTypedIsInTheTagButTheQueryKeepsTheAlbumTheTagWouldHaveHadWithoutIt() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        MetadataTexts texts;
        const QByteArray json =
            "{\"id\":\"vid00000001\",\"album\":\"Palette\",\"duration\":200,\"description\":" + jsonString(descriptionWithoutLyrics) + "}";
        auto runner = infoRunner(&texts, json);

        const QList<JobEvent> events =
            collect(service(runner, {.lyrics = &provider})
                        ->start(request({item(1)}, {.album = u"My List"_s, .searchLyricsOnline = true, .albumOverride = u"내 앨범"_s})));

        QVERIFY2(done(events) == (JobDone{JobStatus::Completed, JobSummary{1, 0, 0}}), qPrintable(xgs::eventsText(events)));
        QCOMPARE(albumLines(texts.single()), (QStringList{u"album=내 앨범"_s}));
        QList<std::optional<QString>> albums;
        for (const LyricsQuery& query : provider.queries()) albums.append(query.album);
        QVERIFY2(albums == (QList<std::optional<QString>>{u"Palette"_s}), "a name made up by the user would only hurt the match");
    }

    void aSingleVideoWithoutAnAlbumIsQueriedWithoutOne() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        if (!deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), {.album = std::nullopt})) return;

        QList<std::optional<QString>> albums;
        for (const LyricsQuery& query : provider.queries()) albums.append(query.album);
        QCOMPARE(albums, (QList<std::optional<QString>>{std::nullopt}));
    }

    void withoutAnInfoFileOrADurationTheQueryHasNoLength() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        const auto tag = deliveredWith(provider, std::nullopt, {.album = std::nullopt});
        if (!tag) return;
        if (!deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), {.item = item(2)})) return;

        QList<std::optional<int>> lengths;
        for (const LyricsQuery& query : provider.queries()) lengths.append(query.durationSeconds);
        QCOMPARE(lengths, (QList<std::optional<int>>{std::nullopt, std::nullopt}));
        QVERIFY2(tag->contains(u"lyrics"_s), "a missing info file still lets the lookup run");
    }

    void lowConfidenceItemsAreQueriedWithTheSecondParse() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        FakeMetadata metadata([](const QString&, std::stop_token) {
            return std::optional<xgs::ytdlp::VideoMeta>(
                xgs::ytdlp::VideoMeta{u"Dynamite"_s, u"BTS - Topic"_s, u"BTS"_s, u"Dynamite"_s});
        });
        const ResolvedItem lowConfidence = item(1, u"BTS - Topic"_s, u"Dynamite (Official)"_s, true);

        if (!deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), {.item = lowConfidence, .metadata = &metadata})) {
            return;
        }

        QList<std::pair<QString, QString>> asked;
        for (const LyricsQuery& query : provider.queries()) asked.append({query.artist, query.title});
        QCOMPARE(asked, (QList<std::pair<QString, QString>>{{u"BTS"_s, u"Dynamite"_s}}));
    }

    void lyricsInTheDescriptionAlwaysWinAndTheProviderIsNotCalled() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        const auto tag = deliveredWith(provider, infoWithDescription(describedLyrics));
        if (!tag) return;

        QVERIFY2(provider.queries().isEmpty(), "the description has lyrics, so nobody is asked");
        QCOMPARE(tag->value(u"lyrics"_s), u"첫 번째 줄\n두 번째 줄\n\nLa la la"_s);
    }

    void withTheOptionOffTheProviderIsNotCalledAndThereAreNoLyrics() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        const auto tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), {.online = false});
        if (!tag) return;

        QCOMPARE(provider.queries(), QList<LyricsQuery>());
        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    void theOptionIsOffUnlessTheRequestSaysOtherwise() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        auto runner = infoRunner(nullptr, infoWithDescription(descriptionWithoutLyrics));
        LocalFolderSink sink(m_outDir);
        // Kotlin's `DownloadRequest(listOf(item(1)), LocalFolderSink(outDir), overwrite = false, concurrency = 1)`.
        DownloadRequest plain;
        plain.items = {item(1)};
        plain.sink = &sink;
        plain.overwrite = false;
        plain.concurrency = 1;

        const QList<JobEvent> events = collect(service(runner, {.lyrics = &provider})->start(plain));

        QCOMPARE(done(events).summary, (JobSummary{1, 0, 0}));
        QCOMPARE(provider.queries(), QList<LyricsQuery>());
        DownloadRequest empty;
        empty.sink = &sink;
        empty.overwrite = false;
        empty.concurrency = 1;
        QVERIFY(!empty.searchLyricsOnline);
    }

    void withoutAProviderTheOptionChangesNothing() {
        MetadataTexts texts;
        auto runner = infoRunner(&texts, infoWithDescription(descriptionWithoutLyrics));

        collect(service(runner)->start(request({item(1)}, {.searchLyricsOnline = true})));

        QVERIFY2(!readFfmetadata(texts.single()).contains(u"lyrics"_s), "the engine's default provider finds nothing");
    }

    void aProviderThatFindsNothingLeavesNoLyrics() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(); });

        const auto tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics));
        if (!tag) return;

        QCOMPARE(provider.queries().size(), 1);
        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    void aBlankAnswerWritesNoLyrics() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(u"  \n "_s); });

        const auto tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics));
        if (!tag) return;

        QVERIFY(!tag->contains(u"lyrics"_s));
    }

    void aProviderThatThrowsGivesNoLyricsAndASuccessfulItem() {
        // Kotlin throws RuntimeException("boom"), IllegalStateException("bad state"), IOException("offline") and
        // UnsupportedOperationException(); here each is a std::runtime_error with that message.
        const QList<QByteArray> failures{"boom", "bad state", "offline", ""};
        for (qsizetype index = 0; index < failures.size(); ++index) {
            const QByteArray failure = failures.at(index);
            FakeLyricsProvider provider([failure](const LyricsQuery&) -> std::optional<QString> {
                throw std::runtime_error(failure.toStdString());
            });

            const auto tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics),
                                           {.item = item(static_cast<int>(index) + 1)});
            if (!tag) return;

            QVERIFY2(provider.queries().size() == 1, failure.constData());
            QVERIFY2(!tag->contains(u"lyrics"_s), failure.constData());
        }
    }

    void aFailingLookupDoesNotAffectTheOtherItems() {
        FakeLyricsProvider provider([](const LyricsQuery& query) -> std::optional<QString> {
            if (query.title == u"T1"_s) throw std::runtime_error("boom");
            return onlineLyrics;
        });
        MetadataTexts texts;
        auto runner = infoRunner(&texts, infoWithDescription(descriptionWithoutLyrics));

        const QList<JobEvent> events =
            collect(service(runner, {.lyrics = &provider})->start(request({item(1), item(2)}, {.searchLyricsOnline = true})));

        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{2, 0, 0}}));
        // One metadata file per item, in order (the request runs one item at a time).
        QList<std::optional<QString>> lyrics;
        for (const QString& text : texts.all()) {
            const QMap<QString, QString> tag = readFfmetadata(text);
            lyrics.append(tag.contains(u"lyrics"_s) ? std::optional<QString>(tag.value(u"lyrics"_s)) : std::nullopt);
        }
        QCOMPARE(lyrics, (QList<std::optional<QString>>{std::nullopt, onlineLyrics}));
    }

    void aCancellationExceptionFromTheProviderIsNotSwallowed() {
        // Author decision 4: Kotlin's provider throws a CancellationException; here it requests the stop of the job's source
        // and answers nothing.
        std::stop_source user;
        FakeLyricsProvider provider([&user](const LyricsQuery&) -> std::optional<QString> {
            user.request_stop();
            return std::nullopt;
        });
        auto runner = infoRunner(nullptr, infoWithDescription(descriptionWithoutLyrics));
        auto svc = service(runner, {.lyrics = &provider});

        const QList<JobEvent> events = collect(svc->start(request({item(1)}, {.searchLyricsOnline = true}), user));

        QCOMPARE(provider.queries().size(), 1);
        QVERIFY2(countOf<ItemDone>(events) == 0 && countOf<ItemFailed>(events) == 0,
                 qPrintable(u"the item is cancelled, not done and not failed: "_s + xgs::eventsText(events)));
        QVERIFY2(!xgs::fs::exists(pathIn(m_outDir, u"001 A1 - T1.m4a"_s)), "a cancelled item delivers nothing");
    }

    void cancellingTheJobWhileTheLookupRunsCancelsTheLookupAndReportsCancelled() {
        Signal inLookup;
        std::atomic<bool> cancelledInside{false};
        StoppableLyrics provider([&](const LyricsQuery&, std::stop_token stop) -> std::optional<QString> {
            inLookup.raise();
            awaitStop(stop);
            cancelledInside = true;
            return std::nullopt;
        });
        auto runner = infoRunner(nullptr, infoWithDescription(descriptionWithoutLyrics));
        auto svc = service(runner, {.lyrics = &provider});
        auto handle = svc->start(request({item(1)}, {.searchLyricsOnline = true}));
        // (10 s is only a safety net in place of Kotlin's unbounded wait: a working job reaches the lookup at once)
        QVERIFY(inLookup.waitFor(10s));

        handle->cancel();
        const QList<JobEvent> events = collect(handle);

        QCOMPARE(done(events).status, JobStatus::Cancelled);
        QVERIFY2(cancelledInside, "the lookup must be cancelled with the job");
        QVERIFY2(countOf<ItemFailed>(events) == 0 && countOf<ItemDone>(events) == 0, qPrintable(xgs::eventsText(events)));
        QCOMPARE(entriesIn(m_tempRoot), 0);
    }

    void noLookupIsMadeWhenTheDownloadFails() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        auto runner = failingWith({u"ERROR: something odd"_s});

        const QList<JobEvent> events =
            collect(service(runner, {.lyrics = &provider})->start(request({item(1)}, {.searchLyricsOnline = true})));

        QCOMPARE(provider.queries(), QList<LyricsQuery>());
        QCOMPARE(done(events).summary, (JobSummary{0, 0, 1}));
    }

    void theOptionIsPerRequest() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        auto runner = infoRunner(nullptr, infoWithDescription(descriptionWithoutLyrics));
        auto svc = service(runner, {.lyrics = &provider});

        collect(svc->start(request({item(1)}, {.searchLyricsOnline = false})));
        collect(svc->start(request({item(2)}, {.searchLyricsOnline = true})));

        QStringList titles;
        for (const LyricsQuery& query : provider.queries()) titles.append(query.title);
        QCOMPARE(titles, (QStringList{u"T2"_s}));
    }

    void prepareCarriesTheOption() {
        auto runner = succeeding();
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);

        // Kotlin's prepare(item(1)): album null, includeRank true, searchLyricsOnline false, albumOverride null. The C++ prepare
        // has no default arguments, so this first assertion passes false explicitly; that the option is off unless the request
        // turns it on is covered by theOptionIsOffUnlessTheRequestSaysOtherwise.
        QVERIFY2(!downloader.prepare(item(1), std::nullopt, true, false, std::nullopt, {}).searchLyricsOnline,
                 "off by default: the engine never looks anything up unasked");
        QVERIFY(downloader.prepare(item(1), u"My List"_s, true, true, std::nullopt, {}).searchLyricsOnline);
        QVERIFY(!downloader.prepare(item(1), u"My List"_s, true, false, std::nullopt, {}).searchLyricsOnline);
    }

    // ---- lyrics: the outcome reported with every finished item ----

    void lyricsFromTheDescriptionAreReportedAsDescriptionAndTheProviderIsNotAsked() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        const QList<JobEvent> events = eventsWith(provider, infoWithDescription(describedLyrics), true);

        QCOMPARE(itemDone(events), (ItemDone{1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Description}));
        QCOMPARE(provider.queries(), QList<LyricsQuery>());
        QCOMPARE(done(events).summary, (JobSummary{1, 0, 0}));
    }

    void lyricsFoundByTheProviderAreReportedAsOnline() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        const QList<JobEvent> events = eventsWith(provider, infoWithDescription(descriptionWithoutLyrics), true);

        QCOMPARE(itemDone(events), (ItemDone{1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Online}));
        QCOMPARE(provider.queries().size(), 1);
    }

    void aMissingOrCorruptInfoFileStillLetsTheLookupReportOnline() {
        for (const std::optional<QByteArray>& infoJson :
             {std::optional<QByteArray>(), std::optional<QByteArray>("{\"description\":\"[Lyrics]\\nLine one")}) {
            FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
            const QList<JobEvent> events = eventsWith(provider, infoJson, true);

            QVERIFY2(itemDone(events).lyrics == LyricsOutcome::Online, infoJson ? infoJson->constData() : "null");
            xgs::fs::removeQuietly(pathIn(m_outDir, u"001 A1 - T1.m4a"_s));
        }
    }

    void aProviderThatFindsNothingIsReportedAsNotFound() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(); });

        const QList<JobEvent> events = eventsWith(provider, infoWithDescription(descriptionWithoutLyrics), true);

        QCOMPARE(itemDone(events), (ItemDone{1, u"001 A1 - T1.m4a"_s, LyricsOutcome::NotFound}));
        QCOMPARE(provider.queries().size(), 1);
    }

    void aBlankAnswerIsReportedAsNotFound() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(u"  \n "_s); });

        const QList<JobEvent> events = eventsWith(provider, infoWithDescription(descriptionWithoutLyrics), true);

        QCOMPARE(itemDone(events).lyrics, std::optional<LyricsOutcome>(LyricsOutcome::NotFound));
    }

    void withTheLookupOnAndTheEnginesDefaultProviderTheOutcomeIsNotFound() {
        auto runner = infoRunner(nullptr, infoWithDescription(descriptionWithoutLyrics));

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)}, {.searchLyricsOnline = true})));

        QVERIFY2(itemDone(events).lyrics == LyricsOutcome::NotFound, "a lookup that ran and found nothing is not 'search off'");
    }

    void aProviderThatThrowsIsReportedAsNotFoundAndTheItemStillSucceeds() {
        // The four Kotlin exceptions, each a std::runtime_error with its message (see aProviderThatThrowsGivesNoLyrics...).
        const QList<QByteArray> failures{"boom", "bad state", "offline", ""};
        for (const QByteArray& failure : failures) {
            FakeLyricsProvider provider([failure](const LyricsQuery&) -> std::optional<QString> {
                throw std::runtime_error(failure.toStdString());
            });
            const QList<JobEvent> events = eventsWith(provider, infoWithDescription(descriptionWithoutLyrics), true);

            QVERIFY2(itemDone(events) == (ItemDone{1, u"001 A1 - T1.m4a"_s, LyricsOutcome::NotFound}), failure.constData());
            QVERIFY2(done(events).summary == (JobSummary{1, 0, 0}), failure.constData());
            QVERIFY2(countOf<ItemFailed>(events) == 0, failure.constData());
            xgs::fs::removeQuietly(pathIn(m_outDir, u"001 A1 - T1.m4a"_s));
        }
    }

    void withTheLookupOffAndNoLyricsInTheDescriptionTheOutcomeIsSearchOff() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        const QList<std::optional<QByteArray>> infos{infoWithDescription(descriptionWithoutLyrics), std::nullopt,
                                                     QByteArray("{\"id\":\"vid00000001\"}"),
                                                     QByteArray("{\"description\":\"[Lyrics]\\nLine one")};
        for (const std::optional<QByteArray>& infoJson : infos) {
            const QList<JobEvent> events = eventsWith(provider, infoJson, false);

            QVERIFY2(itemDone(events) == (ItemDone{1, u"001 A1 - T1.m4a"_s, LyricsOutcome::SearchOff}),
                     infoJson ? infoJson->constData() : "null");
            xgs::fs::removeQuietly(pathIn(m_outDir, u"001 A1 - T1.m4a"_s));
        }
        QVERIFY2(provider.queries().isEmpty(), "with the lookup off nobody is asked");
    }

    void withTheLookupOffButLyricsInTheDescriptionTheOutcomeIsDescription() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });

        const QList<JobEvent> events = eventsWith(provider, infoWithDescription(describedLyrics), false);

        QCOMPARE(itemDone(events), (ItemDone{1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Description}));
        QCOMPARE(provider.queries(), QList<LyricsQuery>());
    }

    void theRequestsOptionIsOffByDefaultSoTheOutcomeIsSearchOff() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        auto runner = infoRunner(nullptr, infoWithDescription(descriptionWithoutLyrics));
        LocalFolderSink sink(m_outDir);
        DownloadRequest plain;
        plain.items = {item(1)};
        plain.sink = &sink;
        plain.overwrite = false;
        plain.concurrency = 1;

        const QList<JobEvent> events = collect(service(runner, {.lyrics = &provider})->start(plain));

        QCOMPARE(itemDone(events).lyrics, std::optional<LyricsOutcome>(LyricsOutcome::SearchOff));
    }

    void everyItemOfAJobGetsItsOwnOutcome() {
        FakeLyricsProvider provider([](const LyricsQuery& query) -> std::optional<QString> {
            if (query.title == u"T1"_s) return std::nullopt;
            return onlineLyrics;
        });
        auto runner = infoRunner(nullptr, infoWithDescription(descriptionWithoutLyrics));

        const QList<JobEvent> events =
            collect(service(runner, {.lyrics = &provider})->start(request({item(1), item(2)}, {.searchLyricsOnline = true})));

        QMap<int, std::optional<LyricsOutcome>> outcomes;
        for (const ItemDone& finished : ofType<ItemDone>(events)) outcomes.insert(finished.rank, finished.lyrics);
        QCOMPARE(outcomes, (QMap<int, std::optional<LyricsOutcome>>{{1, LyricsOutcome::NotFound}, {2, LyricsOutcome::Online}}));
    }

    void aRetriedItemReportsTheOutcomeOfTheAttemptThatSucceeded() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        std::atomic<int> calls{0};
        auto flaky = infoRunnerWith(infoWithDescription(descriptionWithoutLyrics),
                                    [&](const QStringList& command, const LineHandler& err) -> std::optional<int> {
                                        if (!isFfmpegCommand(command) && ++calls == 1) {
                                            err(kHttp503);
                                            return 1;
                                        }
                                        return std::nullopt;
                                    });

        const QList<JobEvent> events =
            collect(service(flaky, {.lyrics = &provider})->start(request({item(1)}, {.searchLyricsOnline = true})));

        QCOMPARE(ytDlpCommands(flaky).size(), 2);
        QVERIFY2(provider.queries().size() == 1, "the failed attempt looked nothing up");
        QCOMPARE(itemDone(events), (ItemDone{1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Online}));
    }

    void aTaggingFailureFailsTheItemAndReportsNoOutcome() {
        // Lyrics are in the description, so a finished item would say Description: a failed one must say nothing.
        auto runner = infoRunnerWith(infoWithDescription(describedLyrics),
                                     [](const QStringList& command, const LineHandler& err) -> std::optional<int> {
                                         if (isFfmpegCommand(command) &&
                                             std::any_of(command.begin(), command.end(),
                                                         [](const QString& a) { return a.contains(u"vid00000001"_s); })) {
                                             err(u"Error while writing the tag"_s);
                                             return 1;
                                         }
                                         return std::nullopt;
                                     });

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1), item(2)})));

        QCOMPARE(ofType<ItemFailed>(events),
                 (QList<ItemFailed>{ItemFailed{1, u"태그를 쓰지 못했습니다: Error while writing the tag"_s}}));
        QVERIFY2(ofType<ItemDone>(events) == (QList<ItemDone>{ItemDone{2, u"002 A2 - T2.m4a"_s, LyricsOutcome::Description}}),
                 "no item-done at all for the item whose tag could not be written");
        QCOMPARE(done(events).summary, (JobSummary{1, 0, 1}));
    }

    void anExistingFileIsSkippedWithoutAnOutcomeAndWithoutALookup() {
        QVERIFY(xgs::fs::makeDirectories(m_outDir));
        const QString existing = pathIn(m_outDir, u"001 A1 - T1.m4a"_s);
        QVERIFY(xgs::fs::writeUtf8(existing, u"old"_s).isOk());
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        auto runner = infoRunner(nullptr, infoWithDescription(describedLyrics));

        const QList<JobEvent> events =
            collect(service(runner, {.lyrics = &provider})->start(request({item(1)}, {.searchLyricsOnline = true})));

        QCOMPARE(ofType<ItemSkipped>(events), (QList<ItemSkipped>{ItemSkipped{1, u"이미 존재"_s}}));
        QVERIFY2(countOf<ItemDone>(events) == 0, qPrintable(xgs::eventsText(events)));
        QCOMPARE(provider.queries(), QList<LyricsQuery>());
        QCOMPARE(readText(existing), u"old"_s);
    }

    void aFailedDownloadReportsNoOutcome() {
        FakeLyricsProvider provider([](const LyricsQuery&) { return std::optional<QString>(onlineLyrics); });
        auto runner = failingWith({u"ERROR: something odd"_s});

        const QList<JobEvent> events =
            collect(service(runner, {.lyrics = &provider})->start(request({item(1)}, {.searchLyricsOnline = true})));

        QVERIFY2(countOf<ItemDone>(events) == 0, qPrintable(xgs::eventsText(events)));
        QCOMPARE(countOf<ItemFailed>(events), 1);
    }

    void theDownloaderReturnsTheOutcomeWithTheFile() {
        const QList<DownloaderCase> cases{
            DownloaderCase{onlineLyrics, infoWithDescription(describedLyrics), true, LyricsOutcome::Description},
            DownloaderCase{onlineLyrics, infoWithDescription(descriptionWithoutLyrics), true, LyricsOutcome::Online},
            DownloaderCase{std::nullopt, infoWithDescription(descriptionWithoutLyrics), true, LyricsOutcome::NotFound},
            DownloaderCase{onlineLyrics, infoWithDescription(descriptionWithoutLyrics), false, LyricsOutcome::SearchOff},
        };
        for (qsizetype index = 0; index < cases.size(); ++index) {
            const DownloaderCase& testCase = cases.at(index);
            const QByteArray label = "case " + QByteArray::number(index);
            FakeLyricsProvider provider([answer = testCase.answer](const LyricsQuery&) { return answer; });
            auto runner = infoRunner(nullptr, testCase.infoJson);
            const FixedTools tools;
            FakeMetadata metadata;
            ItemDownloader downloader(runner, tools, metadata, provider);
            const QString workDir = pathIn(m_tempRoot, u"direct-%1"_s.arg(index));
            QVERIFY(xgs::fs::makeDirectories(workDir));

            const PreparedItem prepared = downloader.prepare(item(1), std::nullopt, true, testCase.online, std::nullopt, {});
            const DownloadResult result = downloader.download(prepared, workDir, [](const JobEvent&) {}, {});

            const auto* downloaded = std::get_if<Downloaded>(&result);
            QVERIFY2(downloaded, label.constData());
            QVERIFY2(downloaded->lyrics == testCase.expected, label.constData());
            QVERIFY2(xgs::fs::isRegularFile(downloaded->file), label.constData());
        }
    }

    // ---- file names without the rank ----

    void withoutTheRankTheFileIsNamedArtistDashTitleButTheTagKeepsTheRank() {
        MetadataTexts texts;
        std::mutex recordedLock;
        QStringList recorded;
        LocalFolderSink inner(m_outDir);
        FakeOutputSink recordingSink(
            [&](const QString& fileName) {
                {
                    const std::lock_guard lock(recordedLock);
                    recorded.append(u"exists:"_s + fileName);
                }
                return inner.exists(fileName);
            },
            [&](const QString& fileName, const QString& source, bool overwrite) {
                {
                    const std::lock_guard lock(recordedLock);
                    recorded.append(u"put:"_s + fileName);
                }
                return inner.put(fileName, source, overwrite);
            });
        auto runner = taggingRunner(&texts);

        const QList<JobEvent> events = collect(service(runner)->start(request(
            {item(5, u"Artist Five"_s, u"Song Five"_s)}, {.sink = &recordingSink, .album = u"My List"_s, .includeRank = false})));

        const QString name = u"Artist Five - Song Five.m4a"_s;
        QCOMPARE(recorded, (QStringList{u"exists:"_s + name, u"put:"_s + name}));
        QCOMPARE(filesIn(m_outDir), QStringList{name});
        QCOMPARE(ofType<ItemStarted>(events), (QList<ItemStarted>{ItemStarted{5, u"vid00000005"_s, name}}));
        QCOMPARE(ofType<ItemDone>(events), (QList<ItemDone>{ItemDone{5, name, LyricsOutcome::SearchOff}}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 0}}));
        const QStringList lines = kotlinLines(texts.single());
        QVERIFY2(lines.contains(u"track=5"_s),
                 qPrintable(u"the track number is the rank whatever the file name says: "_s + texts.single()));
        QVERIFY2(lines.contains(u"title=Song Five"_s), qPrintable(texts.single()));
        QVERIFY2(lines.contains(u"artist=Artist Five"_s), qPrintable(texts.single()));
    }

    void withoutTheRankLowConfidenceItemsAreRenamedFromFullMetadataToo() {
        FakeMetadata metadata([](const QString&, std::stop_token) {
            return std::optional<xgs::ytdlp::VideoMeta>(
                xgs::ytdlp::VideoMeta{u"Dynamite"_s, u"BTS - Topic"_s, u"BTS"_s, u"Dynamite"_s});
        });
        const ResolvedItem lowConfidence = item(1, u"BTS - Topic"_s, u"Dynamite"_s, true);
        auto runner = succeeding();

        const QList<JobEvent> events =
            collect(service(runner, {.metadata = &metadata})->start(request({lowConfidence}, {.includeRank = false})));

        const QList<ItemStarted> started = ofType<ItemStarted>(events);
        QCOMPARE(started.size(), 1);
        QCOMPARE(started.first().fileName, u"BTS - Dynamite.m4a"_s);
        QCOMPARE(filesIn(m_outDir), QStringList{u"BTS - Dynamite.m4a"_s});
    }

    void withTheRankTurnedOnTheNameKeepsTheRankPrefix() {
        auto runner = succeeding();

        const QList<JobEvent> events =
            collect(service(runner)->start(request({item(5, u"Artist Five"_s, u"Song Five"_s)}, {.includeRank = true})));

        const QList<ItemStarted> started = ofType<ItemStarted>(events);
        QCOMPARE(started.size(), 1);
        QCOMPARE(started.first().fileName, u"005 Artist Five - Song Five.m4a"_s);
        QCOMPARE(filesIn(m_outDir), QStringList{u"005 Artist Five - Song Five.m4a"_s});
    }

    // ---- two items of one job with the same file name ----

    void twoItemsWithTheSameNameOneAfterAnotherWriteOneFileAndSkipTheSecond() {
        auto runner = taggingRunner(nullptr);

        const QList<JobEvent> events =
            collect(service(runner)->start(request({firstTwin(), secondTwin()}, {.concurrency = 1, .includeRank = false})));

        QCOMPARE(filesIn(m_outDir), QStringList{u"Same - Song.m4a"_s});
        QCOMPARE(ofType<ItemDone>(events), (QList<ItemDone>{ItemDone{1, u"Same - Song.m4a"_s, LyricsOutcome::SearchOff}}));
        QCOMPARE(ofType<ItemSkipped>(events), (QList<ItemSkipped>{ItemSkipped{2, u"이미 존재"_s}}));
        QVERIFY2(countOf<ItemFailed>(events) == 0, qPrintable(xgs::eventsText(events)));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 1, 0}}));
    }

    void twoItemsWithTheSameNameDownloadedAtTheSameTimeWriteOneFileAndSkipTheOther() {
        Signal gate;
        auto held = heldRunner(gate);
        auto svc = service(held);
        auto handle = svc->start(request({firstTwin(), secondTwin()}, {.concurrency = 2, .includeRank = false}));
        awaitStarted(held, 2);
        gate.raise();

        const QList<JobEvent> events = collect(handle);

        QCOMPARE(filesIn(m_outDir), QStringList{u"Same - Song.m4a"_s});
        QVERIFY2(endsWithFakeAudio(pathIn(m_outDir, u"Same - Song.m4a"_s)), "the delivered file is the tagged one");
        QVERIFY2(countOf<ItemDone>(events) == 1, qPrintable(xgs::eventsText(events)));
        QVERIFY2(countOf<ItemSkipped>(events) == 1, qPrintable(xgs::eventsText(events)));
        const QList<ItemSkipped> skipped = ofType<ItemSkipped>(events);
        QCOMPARE(skipped.size(), 1);
        QCOMPARE(skipped.first().reason, u"이미 존재"_s);
        QVERIFY2(countOf<ItemFailed>(events) == 0, qPrintable(xgs::eventsText(events)));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 1, 0}}));
    }

    void withOverwriteTwoItemsWithTheSameNameOneAfterAnotherBothFinishAndOneFileRemains() {
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(
            request({firstTwin(), secondTwin()}, {.overwrite = true, .concurrency = 1, .includeRank = false})));

        QCOMPARE(filesIn(m_outDir), QStringList{u"Same - Song.m4a"_s});
        QVERIFY(endsWithFakeAudio(pathIn(m_outDir, u"Same - Song.m4a"_s)));
        QList<ItemDone> finished = ofType<ItemDone>(events);
        std::sort(finished.begin(), finished.end(), [](const ItemDone& a, const ItemDone& b) { return a.rank < b.rank; });
        QVERIFY2(finished == (QList<ItemDone>{ItemDone{1, u"Same - Song.m4a"_s, LyricsOutcome::SearchOff},
                                              ItemDone{2, u"Same - Song.m4a"_s, LyricsOutcome::SearchOff}}),
                 qPrintable(xgs::eventsText(events)));
        QVERIFY2(countOf<ItemSkipped>(events) == 0 && countOf<ItemFailed>(events) == 0, qPrintable(xgs::eventsText(events)));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{2, 0, 0}}));
    }

    void withOverwriteTwoItemsWithTheSameNameDownloadedAtTheSameTimeBothFinishAndOneFileRemains() {
        Signal gate;
        auto held = heldRunner(gate);
        auto svc = service(held);
        auto handle =
            svc->start(request({firstTwin(), secondTwin()}, {.overwrite = true, .concurrency = 2, .includeRank = false}));
        awaitStarted(held, 2);
        gate.raise();

        const QList<JobEvent> events = collect(handle);

        QCOMPARE(filesIn(m_outDir), QStringList{u"Same - Song.m4a"_s});
        QVERIFY(endsWithFakeAudio(pathIn(m_outDir, u"Same - Song.m4a"_s)));
        QList<ItemDone> finished = ofType<ItemDone>(events);
        std::sort(finished.begin(), finished.end(), [](const ItemDone& a, const ItemDone& b) { return a.rank < b.rank; });
        QVERIFY2(finished == (QList<ItemDone>{ItemDone{1, u"Same - Song.m4a"_s, LyricsOutcome::SearchOff},
                                              ItemDone{2, u"Same - Song.m4a"_s, LyricsOutcome::SearchOff}}),
                 qPrintable(xgs::eventsText(events)));
        QVERIFY2(countOf<ItemSkipped>(events) == 0 && countOf<ItemFailed>(events) == 0, qPrintable(xgs::eventsText(events)));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{2, 0, 0}}));
    }

    void aNameTakenBetweenTheCheckAndThePutIsSkippedNotFailed() {
        // What another item of the job, or another program, causes: the check says free, the put finds the name taken.
        // Kotlin: put throws FileAlreadyExistsException(outDir/fileName).
        std::atomic<bool> taken{false};
        FakeOutputSink racedSink([&](const QString&) { return taken.load(); },
                                 [&](const QString& fileName, const QString&, bool) {
                                     taken = true;
                                     return PutResult{PutStatus::AlreadyExists, pathIn(m_outDir, fileName), 0};
                                 });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)}, {.sink = &racedSink})));

        QCOMPARE(ofType<ItemSkipped>(events), (QList<ItemSkipped>{ItemSkipped{1, u"이미 존재"_s}}));
        QVERIFY2(countOf<ItemFailed>(events) == 0 && countOf<ItemDone>(events) == 0, qPrintable(xgs::eventsText(events)));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{0, 1, 0}}));
    }

    void withOverwriteAFileThatCannotBeReplacedStillFailsTheItem() {
        // Kotlin: exists is true, put throws FileAlreadyExistsException(outDir/fileName).
        FakeOutputSink sink([](const QString&) { return true; },
                            [&](const QString& fileName, const QString&, bool) {
                                return PutResult{PutStatus::AlreadyExists, pathIn(m_outDir, fileName), 0};
                            });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)}, {.overwrite = true, .sink = &sink})));

        QCOMPARE(failedRanks(events), (QList<int>{1}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{0, 0, 1}}));
    }

    // ---- C++ additions (no Kotlin counterpart) ----

    void theDefaultRetryDelaysAreTwoAndFourSeconds() {
        auto runner = succeeding();
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        const DefaultDownloadService defaults(downloader, m_tempRoot);

        QVERIFY(defaults.retryDelays() == (std::vector<std::chrono::milliseconds>{2s, 4s}));
    }

    void cancellingDuringTheRetryDelayReturnsPromptly() {
        Signal failedOnce;
        auto runner = downloadRunner([&](const QStringList&, const LineHandler&, const LineHandler& err) {
            err(kHttp503);
            failedOnce.raise();
            return 1;
        });
        auto svc = service(runner, {.retryDelays = {60s, 60s}});
        auto handle = svc->start(request({item(1)}));
        QVERIFY(failedOnce.waitFor(10s));
        // Time is unavoidable here: nothing signals that the worker has entered the delay. 200 ms lets it get there; a slower
        // machine still passes (the delay then sees the stop at once), it only tests less.
        std::this_thread::sleep_for(200ms);

        QElapsedTimer timer;
        timer.start();
        handle->cancel();
        const QList<JobEvent> events = collect(handle);

        // The wake-up takes microseconds and the delay itself is 60 s. The time also covers removing the work folder, which may
        // be tried again for 1.52 s (author decision 12), so the bound is 5 s like the neighbouring tests.
        QVERIFY2(timer.elapsed() < 5000, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
        QCOMPARE(done(events).status, JobStatus::Cancelled);
        QCOMPARE(ytDlpCommands(runner).size(), 1);
    }

    void cancellingWithItemsInEveryStageEndsPromptly() {
        // Review Focus 1: item 1 in the metadata lookup, 2 in yt-dlp, 3 in the lyrics lookup, 4 in the tagging, 5 in the retry
        // delay, 6 waiting for a worker.
        Arrivals arrivals(5);
        FakeMetadata metadata([&](const QString& videoId, std::stop_token stop) -> std::optional<xgs::ytdlp::VideoMeta> {
            if (videoId == videoIdFor(1)) {
                arrivals.arrive();
                awaitStop(stop);
            }
            return std::nullopt;
        });
        StoppableLyrics lyrics([&](const LyricsQuery& query, std::stop_token stop) -> std::optional<QString> {
            if (query.title == u"T3"_s) {
                arrivals.arrive();
                awaitStop(stop);
            }
            return std::nullopt;
        });
        FakeProcessRunner runner(StoppableProcessHandler(
            [&](const QStringList& command, const LineHandler&, const LineHandler& err, std::stop_token stop) {
                if (isFfmpegCommand(command)) {
                    if (command.join(u' ').contains(videoIdFor(4))) {
                        arrivals.arrive();
                        awaitStop(stop);
                        return 0;
                    }
                    writeFakeTagged(command);
                    return 0;
                }
                const QString videoId = videoIdOf(command);
                if (videoId == videoIdFor(2)) {
                    arrivals.arrive();
                    awaitStop(stop);
                    return 0;
                }
                if (videoId == videoIdFor(5)) {
                    err(kHttp503);
                    arrivals.arrive();
                    return 1;
                }
                writeFakeM4a(command);
                return 0;
            }));
        auto svc = service(runner, {.metadata = &metadata, .lyrics = &lyrics, .retryDelays = {60s, 60s}});
        QList<ResolvedItem> items = itemsUpTo(6);
        items[0].lowConfidence = true;
        auto handle = svc->start(request(items, {.concurrency = 5, .searchLyricsOnline = true}));
        QVERIFY(arrivals.waitFor(10s));
        // As in cancellingDuringTheRetryDelayReturnsPromptly: 200 ms for item 5 to enter its delay.
        std::this_thread::sleep_for(200ms);

        QElapsedTimer timer;
        timer.start();
        handle->cancel();
        const QList<JobEvent> events = collect(handle);

        // 5 s is generous: every stage wakes up on the stop at once.
        QVERIFY2(timer.elapsed() < 5000, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
        QCOMPARE(countOf<JobDone>(events), 1);
        QCOMPARE(done(events).status, JobStatus::Cancelled);
        QCOMPARE(countOf<ItemDone>(events), 0);
        QCOMPARE(countOf<ItemFailed>(events), 0);
        QCOMPARE(entriesIn(m_tempRoot), 0);
    }

    void destroyingTheHandleOfARunningJobCancelsAndJoins() {
        Signal started;
        std::atomic<bool> sawStop{false};
        FakeProcessRunner hanging(StoppableProcessHandler(
            [&](const QStringList&, const LineHandler&, const LineHandler&, std::stop_token stop) {
                started.raise();
                awaitStop(stop);
                sawStop = true;
                return 0;
            }));
        auto svc = service(hanging);
        auto handle = svc->start(request({item(1)}));
        QVERIFY(started.waitFor(10s));

        QElapsedTimer timer;
        timer.start();
        handle.reset();

        // 5 s is generous: the destructor only has to wake the fake and join.
        QVERIFY2(timer.elapsed() < 5000, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
        QVERIFY(sawStop);
    }

    void aJobNobodyReadsStillFinishes() {
        auto runner = succeeding();
        auto svc = service(runner);
        auto handle = svc->start(request(itemsUpTo(3), {.concurrency = 2}));

        handle->join();

        QVERIFY(handle->finished());
        const QList<JobEvent> events = collect(handle);
        QCOMPARE(startedRanks(events).size(), 3);
        QCOMPARE(countOf<ItemDone>(events), 3);
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{3, 0, 0}}));
    }

    void nextEventGivesNothingAfterJobDone() {
        auto runner = succeeding();
        auto svc = service(runner);
        auto handle = svc->start(request({item(1)}));

        bool jobDoneTaken = false;
        while (std::optional<JobEvent> event = handle->nextEvent()) {
            if (std::holds_alternative<JobDone>(*event)) {
                jobDoneTaken = true;
                break;
            }
        }
        QVERIFY(jobDoneTaken);
        QVERIFY(handle->finished());

        // A nextEvent that blocked here would hang the test (and fail it at Qt Test's function time limit).
        QVERIFY(!handle->nextEvent().has_value());
        QVERIFY(!handle->tryNextEvent().has_value());
        QVERIFY(!handle->nextEvent().has_value());

        // The queue alone, in the moment the job has not reached yet above: its JobDone queued and taken, the queue not yet
        // closed. next gives nothing at once (nothing can follow a JobDone). It runs on a thread of its own so that a next that
        // blocked cannot hang the run: closing the queue releases it. The 10 s are only that safety net.
        JobEvents queue;
        queue.push(JobDone{JobStatus::Completed, JobSummary{0, 0, 0}});
        QVERIFY(queue.next().has_value());
        Signal returned;
        std::optional<JobEvent> afterDone;
        std::jthread reader([&] {
            afterDone = queue.next();
            returned.raise();
        });
        const bool returnedAtOnce = returned.waitFor(10s);
        queue.close();
        reader.join();
        QVERIFY2(returnedAtOnce, "next blocked after the JobDone had been taken");
        QVERIFY(!afterDone.has_value());
    }

    void fiveThousandItemsUseNoMoreThreadsThanTheConcurrency() {
        // Review Focus 3. Ranks repeat (1..999, filename::MaxRank), video IDs do not.
        constexpr int kItems = 5000;
        std::mutex threadsLock;
        std::set<std::thread::id> threads;
        FakeOutputSink sink([&](const QString&) {
            {
                const std::lock_guard lock(threadsLock);
                threads.insert(std::this_thread::get_id());
            }
            // Keeps each worker busy for a moment: thread IDs are reused once a thread ends, so a service that started a
            // thread per item would otherwise get away with it whenever its first threads finished the list alone. (Windows
            // may round the pause up to ~16 ms: 5000 / 64 * 16 ms is still about 1.3 s.)
            std::this_thread::sleep_for(1ms);
            return true;
        });
        QList<ResolvedItem> items;
        for (int i = 0; i < kItems; ++i) {
            ResolvedItem next = item(i % xgs::filename::MaxRank + 1);
            next.videoId = videoIdFor(i + 1);
            items.append(next);
        }
        auto runner = succeeding();
        auto svc = service(runner);

        QElapsedTimer timer;
        timer.start();
        const QList<JobEvent> events = collect(svc->start(request(items, {.concurrency = 64, .sink = &sink})));

        // 30 s is generous: nothing is downloaded, every item is skipped.
        QVERIFY2(timer.elapsed() < 30'000, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
        QVERIFY2(threads.size() <= 64, qPrintable(u"%1 threads"_s.arg(threads.size())));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{0, kItems, 0}}));
        QCOMPARE(runner.commands().size(), 0);
        // Every item: one ItemStarted, then (later in the queue) one ItemSkipped of its rank.
        QSet<QString> startedIds;
        std::map<int, int> open;   // rank -> items started and not yet skipped
        for (const JobEvent& event : events) {
            if (const auto* started = std::get_if<ItemStarted>(&event)) {
                startedIds.insert(started->videoId);
                ++open[started->rank];
            } else if (const auto* skipped = std::get_if<ItemSkipped>(&event)) {
                QCOMPARE(skipped->reason, u"이미 존재"_s);
                QVERIFY2(open[skipped->rank] > 0, "an ItemSkipped came before the ItemStarted of its item");
                --open[skipped->rank];
            } else {
                QVERIFY(std::holds_alternative<JobDone>(event));
            }
        }
        QCOMPARE(startedIds.size(), kItems);
        for (const auto& [rank, count] : open) QCOMPARE(count, 0);
    }

    void aConcurrencyLargerThanTheItemCountIsFine() {
        auto runner = succeeding();
        const QList<JobEvent> events = collect(service(runner)->start(request(itemsUpTo(3), {.concurrency = 100000})));

        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{3, 0, 0}}));
    }

    void itemsStartInListOrder() {
        QList<ResolvedItem> items;
        QList<int> ranks;
        for (int i = 0; i < 20; ++i) {
            const int rank = (i * 7) % 20 + 1;   // a permutation of 1..20 that is not sorted
            items.append(item(rank));
            ranks.append(rank);
        }
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request(items, {.concurrency = 1})));

        QCOMPARE(startedRanks(events), ranks);
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{20, 0, 0}}));
    }

    void aFatalFailureStopsTheOtherRunningItemsWithoutFailingThem() {
        // Review Focus 4.
        Arrivals others(2);
        std::atomic<bool> othersMet{false};
        std::atomic<int> sawStop{0};
        auto runner = downloadRunner(StoppableProcessHandler(
            [&](const QStringList& command, const LineHandler&, const LineHandler& err, std::stop_token stop) {
                if (videoIdOf(command) == videoIdFor(1)) {
                    othersMet = others.waitFor(10s);
                    err(kDiskFull);
                    return 1;
                }
                others.arrive();
                awaitStop(stop);
                ++sawStop;
                return 0;
            }));

        const QList<JobEvent> events = collect(service(runner)->start(request(itemsUpTo(3), {.concurrency = 3})));

        QVERIFY2(othersMet, "items 2 and 3 were running when item 1 failed");
        QCOMPARE(failedRanks(events), (QList<int>{1}));
        QCOMPARE(countOf<ItemDone>(events), 0);
        QCOMPARE(countOf<JobDone>(events), 1);
        QCOMPARE(done(events), (JobDone{JobStatus::Failed, JobSummary{0, 0, 1}}));
        QCOMPARE(sawStop.load(), 2);
    }

    void aUserCancelAfterAFatalFailureIsStillFailed() {
        // Author decision 3: item 2 sees the stop of item 1's fatal failure, then the user cancels before the job ends.
        std::stop_source user;
        Signal secondRunning;
        auto runner = downloadRunner(StoppableProcessHandler(
            [&](const QStringList& command, const LineHandler&, const LineHandler& err, std::stop_token stop) {
                if (videoIdOf(command) == videoIdFor(1)) {
                    secondRunning.waitFor(10s);
                    err(kDiskFull);
                    return 1;
                }
                secondRunning.raise();
                awaitStop(stop);
                user.request_stop();
                return 0;
            }));
        auto svc = service(runner);

        const QList<JobEvent> events = collect(svc->start(request(itemsUpTo(2), {.concurrency = 2}), user));

        QVERIFY(user.stop_requested());
        QCOMPARE(done(events), (JobDone{JobStatus::Failed, JobSummary{0, 0, 1}}));
    }

    void anExceptionOnAWorkerThreadFailsOnlyThatItem() {
        // Review Focus 6, author decision 13.
        FakeMetadata metadata([](const QString& videoId, std::stop_token) -> std::optional<xgs::ytdlp::VideoMeta> {
            if (videoId == videoIdFor(1)) throw std::runtime_error("boom");
            return std::nullopt;
        });
        const std::invalid_argument badName("not a name this sink takes");
        FakeOutputSink sink([&](const QString& fileName) {
            if (fileName == u"002 A2 - T2.m4a"_s) throw badName;
            return false;
        });
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            if (videoIdOf(command) == videoIdFor(3)) throw 42;
            writeFakeM4a(command);
            return 0;
        });
        const QList<ResolvedItem> items{item(1, std::nullopt, std::nullopt, true), item(2), item(3), item(4)};

        const QList<JobEvent> events =
            collect(service(runner, {.metadata = &metadata})->start(request(items, {.sink = &sink})));

        QCOMPARE(ofType<ItemFailed>(events), (QList<ItemFailed>{ItemFailed{1, u"boom"_s},
                                                                ItemFailed{2, QString::fromUtf8(badName.what())},
                                                                ItemFailed{3, u"알 수 없는 오류"_s}}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 3}}));
    }

    void aProgramThatCannotBeStartedFailsTheItem() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            if (videoIdOf(command) == videoIdFor(1)) throw StartFailure{u"boom"_s};
            writeFakeM4a(command);
            return 0;
        });

        const QList<JobEvent> events = collect(service(runner)->start(request(itemsUpTo(2))));

        QCOMPARE(ofType<ItemFailed>(events), (QList<ItemFailed>{ItemFailed{1, u"파일 처리 중 오류: boom"_s}}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 1}}));
    }

    void aStartFailureThatSaysTheDiskIsFullAbortsTheJob() {
        auto runner = downloadRunner([](const QStringList&, const LineHandler&, const LineHandler&) -> int {
            throw StartFailure{u"There is not enough space on the disk."_s};
        });

        const QList<JobEvent> events = collect(service(runner)->start(request(itemsUpTo(2))));

        QCOMPARE(ofType<ItemFailed>(events), (QList<ItemFailed>{ItemFailed{1, u"디스크 공간이 부족합니다."_s}}));
        QCOMPARE(done(events), (JobDone{JobStatus::Failed, JobSummary{0, 0, 1}}));
        QCOMPARE(ytDlpCommands(runner).size(), 1);
    }

    void aDiskFullCodeFromTheSinkAbortsTheJob() {
        for (const quint32 code : {112u, 39u}) {   // ERROR_DISK_FULL, ERROR_HANDLE_DISK_FULL
            FakeOutputSink sink({}, [code](const QString&, const QString&, bool) { return PutResult{PutStatus::Failed, u"x"_s, code}; });
            auto runner = succeeding();

            const QList<JobEvent> events =
                collect(service(runner)->start(request({item(1), item(2), item(3)}, {.sink = &sink})));

            QCOMPARE(ofType<ItemFailed>(events), (QList<ItemFailed>{ItemFailed{1, u"디스크 공간이 부족합니다."_s}}));
            QCOMPARE(done(events).status, JobStatus::Failed);
            QCOMPARE(ytDlpCommands(runner).size(), 1);
        }
    }

    void aSharingViolationFromTheSinkFailsOnlyThatItem() {
        FakeOutputSink sink({}, [](const QString& fileName, const QString&, bool) {
            if (fileName.startsWith(u"001 "_s)) return PutResult{PutStatus::Failed, u"x"_s, 32};   // ERROR_SHARING_VIOLATION
            return PutResult{PutStatus::Stored, {}, 0};
        });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request(itemsUpTo(2), {.sink = &sink})));

        QCOMPARE(ofType<ItemFailed>(events), (QList<ItemFailed>{ItemFailed{1, u"파일 처리 중 오류: x"_s}}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 1}}));
    }

    void anItemFolderThatCannotBeMadeIsReadLikeASinkFailure() {
        // Right before the worker makes `<job>\1`, the sink's exists puts a file there.
        std::atomic<bool> planted{false};
        FakeOutputSink sink([&](const QString& fileName) {
            if (fileName != u"001 A1 - T1.m4a"_s) return false;
            const QStringList jobs = QDir(m_tempRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (jobs.size() == 1) planted = xgs::fs::writeUtf8(pathIn(pathIn(m_tempRoot, jobs.first()), u"1"_s), u"x"_s).isOk();
            return false;
        });
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request(itemsUpTo(2), {.sink = &sink})));

        QVERIFY(planted);
        const QList<ItemFailed> failed = ofType<ItemFailed>(events);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().rank, 1);
        QVERIFY2(failed.first().message.startsWith(u"파일 처리 중 오류: "_s), qPrintable(failed.first().message));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 1}}));
        QCOMPARE(ytDlpCommands(runner).size(), 1);
    }

    void anAlreadyExistsThatCannotBeASkipFailsWithTheFileName() {
        FakeOutputSink sink({}, [](const QString&, const QString&, bool) { return PutResult{PutStatus::AlreadyExists, {}, 0}; });
        auto runner = succeeding();

        const QList<JobEvent> events =
            collect(service(runner)->start(request({item(1)}, {.overwrite = true, .sink = &sink})));

        QCOMPARE(ofType<ItemFailed>(events), (QList<ItemFailed>{ItemFailed{1, u"파일 처리 중 오류: 001 A1 - T1.m4a"_s}}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{0, 0, 1}}));
    }

    void aStopBeforeThePutDeliversNothing() {
        // Author decision 10. The tagger looks at the token right up to replacing the m4a with the tagged file, so the fake
        // ffmpeg holds the m4a: the replacement waits (fs::moveFile tries again for about 1.5 s), and during that wait the
        // stop is requested and the m4a let go. The tagging then succeeds with the stop already requested, and the job must
        // not put the file. Time is unavoidable here (100 ms). When the tagger is slow and has not reached the replacement by
        // then, it reports the cancellation itself, with the same outcome: the test only proves less. When instead the thread
        // that holds the m4a stalls past fs::moveFile's 1.52 s of retries, the replacement gives up first, the tagging fails
        // before the stop and the Cancelled check below fails: the test depends on that thread waking within about 1.5 s.
        std::stop_source user;
        FakeOutputSink sink;
        std::optional<HeldFile> held;
        auto runner = FakeProcessRunner(StoppableProcessHandler(
            [&](const QStringList& command, const LineHandler&, const LineHandler&, std::stop_token) {
                if (!isFfmpegCommand(command)) {
                    writeFakeM4a(command);
                    return 0;
                }
                writeFakeTagged(command);
                QString m4a = command.last();
                m4a.replace(u".tagged.m4a"_s, u".m4a"_s);
                held.emplace(m4a, 100ms, [&] { user.request_stop(); });
                return 0;
            }));
        auto svc = service(runner);

        const QList<JobEvent> events = collect(svc->start(request({item(1)}, {.sink = &sink}), user));

        QVERIFY(held && held->opened());
        QCOMPARE(ffmpegCommands(runner).size(), 1);
        QCOMPARE(sink.putCalls().size(), 0);
        QCOMPARE(countOf<ItemDone>(events), 0);
        QCOMPARE(done(events).status, JobStatus::Cancelled);
    }

    void theWorkFolderIsRemovedEvenWhenAFileInItIsHeldForAMoment() {
        // Author decision 12: the removal is tried again for about 1.5 s; the file is held for 300 ms.
        std::optional<HeldFile> held;
        auto runner = downloadRunner([&](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeM4a(command);
            held.emplace(pathIn(outputDirOf(command), u"held.bin"_s), 300ms);
            return 0;
        });

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)})));

        QVERIFY(held && held->opened());
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 0}}));
        QCOMPARE(entriesIn(m_tempRoot), 0);
    }

    void aPercentThatIsNotFiniteReachesTheReaderAsUnknown() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler& out, const LineHandler&) {
            out(u"XGSP|downloading|NaN|100|NA"_s);
            writeFakeM4a(command);
            return 0;
        });

        const QList<JobEvent> events = collect(service(runner)->start(request({item(1)})));

        const QList<Progress> progress = ofType<Progress>(events);
        QCOMPARE(progress, (QList<Progress>{Progress{1, Stage::Downloading, std::nullopt}, Progress{1, Stage::Finishing, std::nullopt}}));
        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{1, 0, 0}}));
    }

    void everyItemsYtDlpCommandCarriesThatItemsOwnWatchUrl() {
        // The fakes take the video ID from the `-o` template only, so this is what notices another item's URL handed to yt-dlp.
        auto runner = succeeding();

        const QList<JobEvent> events = collect(service(runner)->start(request(itemsUpTo(3), {.concurrency = 2})));

        QCOMPARE(done(events), (JobDone{JobStatus::Completed, JobSummary{3, 0, 0}}));
        const QList<QStringList> commands = ytDlpCommands(runner);
        QSet<QString> videoIds;
        for (const QStringList& command : commands) {
            const QString videoId = videoIdOf(command);
            videoIds.insert(videoId);
            QCOMPARE(command.last(), watchUrl(videoId));
            QCOMPARE(command, xgs::ytdlp::downloadCommand(testTools(), watchUrl(videoId),
                                                          xgs::ytdlp::escapeOutputTemplateDirectory(outputDirOf(command)),
                                                          videoId));
        }
        QCOMPARE(commands.size(), 3);
        QCOMPARE(videoIds, (QSet<QString>{videoIdFor(1), videoIdFor(2), videoIdFor(3)}));
    }

    void aRequestWithoutASinkIsRefusedBeforeAnythingIsCreated() {
        auto runner = succeeding();
        auto svc = service(runner);
        DownloadRequest withoutSink = request({item(1)});
        withoutSink.sink = nullptr;

        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, svc->start(std::move(withoutSink)));

        QVERIFY2(!xgs::fs::exists(m_tempRoot), "no work folder is made for a request that is refused");
        QCOMPARE(runner.commands().size(), 0);
    }

    void startingAndCancellingAtOnceEndsWithOneJobDoneCancelled() {
        // No pre-stopped source: the stop races the job's own thread. Every yt-dlp run waits until it is stopped, so no item
        // can end before the stop and the end status is exactly Cancelled (author decision 3 lets a cancel look Completed only
        // after the last item has ended).
        auto runner = downloadRunner(StoppableProcessHandler(
            [](const QStringList&, const LineHandler&, const LineHandler&, std::stop_token stop) {
                awaitStop(stop);
                return 0;
            }));
        auto svc = service(runner);
        // The pause between start and cancel (0 to 2.95 ms) spreads the stop over the job's life: before its thread looks
        // at the stop, while the workers start, and while items run. (Measured once on this machine: about a quarter of the
        // rounds ended before any ItemStarted, and yt-dlp was run about 180 times in the 300 rounds.)
        for (int round = 0; round < 300; ++round) {
            auto handle = svc->start(request(itemsUpTo(3), {.concurrency = 2}));
            spinFor(std::chrono::microseconds(round % 60 * 50));
            handle->cancel();
            const QList<JobEvent> events = collect(handle);

            const QString context = u"round %1: %2"_s.arg(round).arg(xgs::eventsText(events));
            QVERIFY2(countOf<JobDone>(events) == 1, qPrintable(context));
            QVERIFY2(!events.isEmpty() && std::holds_alternative<JobDone>(events.last()), qPrintable(context));
            QVERIFY2(std::get<JobDone>(events.last()).status == JobStatus::Cancelled, qPrintable(context));
            QVERIFY2(countOf<ItemDone>(events) + countOf<ItemSkipped>(events) + countOf<ItemFailed>(events) == 0,
                     qPrintable(context));
            // The temp root is missing when every stop came before the job made it; otherwise it must be empty again.
            QVERIFY2(entriesIn(m_tempRoot) <= 0, qPrintable(context));
        }
    }
};

QTEST_GUILESS_MAIN(DefaultDownloadServiceTest)
#include "tst_default_download_service.moc"
