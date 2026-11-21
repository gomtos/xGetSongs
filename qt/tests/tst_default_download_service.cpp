#include <QtTest>

#include <QDir>
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
#include "job/default_download_service.h"
#include "job/download_service.h"
#include "job/item_downloader.h"
#include "lyrics/lyrics_provider.h"
#include "output/local_folder_sink.h"
#include "paths/paths.h"
#include "support/fake_output_sink.h"
#include "support/fake_process_runner.h"
#include "support/job_fakes.h"
#include "support/test_tools.h"

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
using xgs::LyricsQuery;
using xgs::Progress;
using xgs::ResolvedItem;
using xgs::Stage;
using xgs::ToolPaths;
using xgs::job::DefaultDownloadService;
using xgs::job::DownloadRequest;
using xgs::job::ItemDownloader;
using xgs::job::JobHandle;
using xgs::output::LocalFolderSink;
using xgs::output::OutputSink;
using xgs::output::PutResult;
using xgs::output::PutStatus;
using xgs::process::LineHandler;
using xgs::testing::FakeOutputSink;
using xgs::testsupport::awaitStop;
using xgs::testsupport::downloadRunner;
using xgs::testsupport::endsWithFakeAudio;
using xgs::testsupport::FakeMetadata;
using xgs::testsupport::FakeProcessRunner;
using xgs::testsupport::ffmpegCommands;
using xgs::testsupport::FixedTools;
using xgs::testsupport::isFfmpegCommand;
using xgs::testsupport::outputDirOf;
using xgs::testsupport::StartFailure;
using xgs::testsupport::StoppableProcessHandler;
using xgs::testsupport::testTools;
using xgs::testsupport::videoIdOf;
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

}  // namespace

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
QString describe(const JobEvent& event) {
    if (const auto* e = std::get_if<ItemStarted>(&event)) return u"ItemStarted(%1, %2, %3)"_s.arg(e->rank).arg(e->videoId, e->fileName);
    if (const auto* e = std::get_if<Progress>(&event)) {
        return u"Progress(%1, %2, %3)"_s.arg(e->rank)
            .arg(e->stage == Stage::Downloading ? u"Downloading"_s : u"Finishing"_s,
                 e->percent ? QString::number(*e->percent, 'g', 17) : u"unknown"_s);
    }
    if (const auto* e = std::get_if<ItemDone>(&event)) return u"ItemDone(%1, %2)"_s.arg(e->rank).arg(e->fileName);
    if (const auto* e = std::get_if<ItemSkipped>(&event)) return u"ItemSkipped(%1, %2)"_s.arg(e->rank).arg(e->reason);
    if (const auto* e = std::get_if<ItemFailed>(&event)) return u"ItemFailed(%1, %2)"_s.arg(e->rank).arg(e->message);
    const auto& e = std::get<JobDone>(event);
    return u"JobDone(%1, %2/%3/%4)"_s.arg(statusName(e.status)).arg(e.summary.succeeded).arg(e.summary.skipped).arg(e.summary.failed);
}
char* text(const QString& value) {
    return qstrdup(value.toUtf8().constData());
}
}  // namespace
char* toString(const JobEvent& event) { return text(describe(event)); }
char* toString(const JobDone& event) { return text(describe(event)); }
char* toString(const ItemSkipped& event) { return text(describe(event)); }
char* toString(const ItemFailed& event) { return text(describe(event)); }
char* toString(const Progress& event) { return text(describe(event)); }
char* toString(const JobStatus& status) { return text(statusName(status)); }
char* toString(const JobSummary& s) { return text(u"%1/%2/%3"_s.arg(s.succeeded).arg(s.skipped).arg(s.failed)); }
}  // namespace xgs

// Port of engine/src/test/.../job/DefaultDownloadServiceTest.kt: the 26 tests before `// ---- tagging ----` (Task 2 of Q2d;
// Task 3 adds the rest) and the C++ additions after them. A cancelled coroutine is a stop requested on the job's stop source.
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

        // 2 s is generous for a wake-up that takes microseconds; the delay itself is 60 s.
        QVERIFY2(timer.elapsed() < 2000, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
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
        // not put the file. Time is unavoidable here (100 ms); on a machine so slow that the tagger has not reached the
        // replacement by then the tagger reports the cancellation itself, with the same outcome: never flaky, only weaker.
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
};

QTEST_GUILESS_MAIN(DefaultDownloadServiceTest)
#include "tst_default_download_service.moc"
