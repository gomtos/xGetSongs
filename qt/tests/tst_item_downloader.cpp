#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <variant>
#include <vector>

#include "filename/filename_formatter.h"
#include "fs/file_ops.h"
#include "input/input_classifier.h"
#include "job/item_downloader.h"
#include "paths/paths.h"
#include "support/fake_lyrics_provider.h"
#include "support/fake_process_runner.h"
#include "support/ffmetadata_reader.h"
#include "support/job_fakes.h"
#include "support/test_tools.h"
#include "ytdlp/error_classifier.h"
#include "ytdlp/ytdlp_commands.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::JobEvent;
using xgs::LyricsOutcome;
using xgs::LyricsQuery;
using xgs::Progress;
using xgs::ResolvedItem;
using xgs::Stage;
using xgs::ToolPaths;
using xgs::job::DownloadCancelled;
using xgs::job::Downloaded;
using xgs::job::DownloadFailed;
using xgs::job::DownloadIoError;
using xgs::job::DownloadResult;
using xgs::job::ItemDownloader;
using xgs::job::PreparedItem;
using xgs::process::LineHandler;
using xgs::testing::FakeLyricsProvider;
using xgs::testsupport::awaitStop;
using xgs::testsupport::downloadRunner;
using xgs::testsupport::endsWithFakeAudio;
using xgs::testsupport::FakeMetadata;
using xgs::testsupport::FakeProcessRunner;
using xgs::testsupport::ffmetadataTextOf;
using xgs::testsupport::ffmpegCommands;
using xgs::testsupport::FixedTools;
using xgs::testsupport::isFfmpegCommand;
using xgs::testsupport::readFfmetadata;
using xgs::testsupport::RunFailure;
using xgs::testsupport::StartFailure;
using xgs::testsupport::testTools;
using xgs::testsupport::videoIdOf;
using xgs::testsupport::writeFakeInfo;
using xgs::testsupport::writeFakeM4a;
using xgs::testsupport::writeFakeTagged;
using xgs::testsupport::ytDlpCommands;
using xgs::ytdlp::Failure;
using xgs::ytdlp::FailureKind;

namespace {

const QString kVideoId = u"vid00000001"_s;

QString windowsPath(const QString& path) {
    QString out = path;
    return out.replace(u'/', u'\\');
}

QString watchUrl(const QString& videoId) {
    return xgs::ParsedInput{xgs::ParsedInput::Kind::Video, videoId, std::nullopt}.canonicalUrl();
}

ResolvedItem itemOf(int rank, const QString& videoId) {
    ResolvedItem item;
    item.rank = rank;
    item.videoId = videoId;
    item.title = u"Artist - Song"_s;
    item.channel = u"Channel"_s;
    item.artist = u"Artist"_s;
    item.track = u"Song"_s;
    return item;
}

PreparedItem preparedOf(int rank, const QString& videoId, bool searchLyricsOnline = false) {
    return PreparedItem{itemOf(rank, videoId), xgs::filename::format(rank, u"Artist"_s, u"Song"_s), u"Artist"_s, u"Song"_s,
                        std::nullopt, searchLyricsOnline, std::nullopt};
}

/// The text downloadCommand gives a video ID it refuses (computed, so the test follows the command builder).
QString rejectedIdText(const QString& workDir, const QString& videoId) {
    try {
        xgs::ytdlp::downloadCommand(testTools(), watchUrl(videoId), workDir, videoId);
    } catch (const std::invalid_argument& e) {
        return QString::fromUtf8(e.what());
    }
    return u"<downloadCommand accepted the id>"_s;
}

/// Collects the events of one download (the fake runner calls the handlers on the caller's thread; the lock is for the
/// contract of Emit, which allows reader threads).
struct EventLog {
    mutable std::mutex mutex;
    QList<JobEvent> events;
    xgs::job::Emit emitter() {
        return [this](const JobEvent& event) {
            const std::lock_guard lock(mutex);
            events.append(event);
        };
    }
    QList<JobEvent> all() const {
        const std::lock_guard lock(mutex);
        return events;
    }
};

Progress progress(int rank, Stage stage, std::optional<double> percent) {
    return Progress{rank, stage, percent};
}

/// A runner whose ffmpeg pass records the ffmetadata text it is given and then succeeds; yt-dlp is handled by ytDlp.
FakeProcessRunner recordingRunner(std::optional<QString>& metadataText,
                                  std::function<int(const QStringList&, const LineHandler&, const LineHandler&)> ytDlp) {
    return FakeProcessRunner([&metadataText, ytDlp = std::move(ytDlp)](const QStringList& command, const LineHandler& out,
                                                                       const LineHandler& err) {
        if (isFfmpegCommand(command)) {
            metadataText = ffmetadataTextOf(command);
            writeFakeTagged(command);
            return 0;
        }
        return ytDlp(command, out, err);
    });
}

/// Requests a stop on source once the fake says its run has started (Kotlin's cancel of a job whose handler awaits
/// cancellation). Destroying it ends its thread even when the run never started, so a broken downloader fails the test on
/// its result instead of hanging it.
class StopOnceRunning {
public:
    explicit StopOnceRunning(std::stop_source source)
        : m_thread([this, source](std::stop_token own) mutable {
              std::unique_lock lock(m_mutex);
              if (m_changed.wait(lock, own, [this] { return m_running; })) source.request_stop();
          }) {}
    void running() {
        {
            const std::lock_guard lock(m_mutex);
            m_running = true;
        }
        m_changed.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable_any m_changed;
    bool m_running = false;
    std::jthread m_thread;   // last, so it starts after the members it uses and stops before they go
};

/// Lets `count` threads meet, as often as they like. Time is unavoidable only as a safety net: if the code under test
/// serialised the runs, the threads would never meet and the test would hang; after a generous 10 s each thread goes on and
/// the maxActive check fails instead.
class Meeting {
public:
    explicit Meeting(int count) : m_count(count) {}
    void arriveAndWait() {
        std::unique_lock lock(m_mutex);
        const int generation = m_generation;
        if (++m_arrived == m_count) {
            m_arrived = 0;
            ++m_generation;
            m_met.notify_all();
            return;
        }
        m_met.wait_for(lock, 10s, [&] { return m_generation != generation; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_met;
    const int m_count;
    int m_arrived = 0;
    int m_generation = 0;
};

}  // namespace

// Readable QCOMPARE failures (found by argument-dependent lookup).
namespace xgs {
char* toString(const JobEvent& event) {
    if (const auto* p = std::get_if<Progress>(&event)) {
        const QString percent = p->percent ? QString::number(*p->percent, 'g', 17) : u"unknown"_s;
        const QString text = u"Progress(%1, %2, %3)"_s.arg(p->rank).arg(p->stage == Stage::Downloading ? u"Downloading"_s
                                                                                                       : u"Finishing"_s, percent);
        return qstrdup(text.toUtf8().constData());
    }
    return qstrdup((QByteArray("event #") + QByteArray::number(static_cast<int>(event.index()))).constData());
}
namespace ytdlp {
char* toString(const Failure& failure) {
    return qstrdup((u"Failure(%1, %2)"_s.arg(static_cast<int>(failure.kind)).arg(failure.message)).toUtf8().constData());
}
}  // namespace ytdlp
}  // namespace xgs

// Tests of the C++ ItemDownloader that have no Kotlin counterpart (the Kotlin downloader is tested through
// DefaultDownloadServiceTest, Task 3). A cancelled coroutine is a stop requested on the token.
class ItemDownloaderTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_temp;
    QString m_root;
    QString m_workDir;

    QString makeDir(const QString& name) {
        const std::optional<QString> dir = xgs::paths::child(m_root, name);
        if (!dir || !QDir().mkpath(*dir)) return QString();
        return *dir;
    }

private slots:
    void init() {
        m_temp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp->isValid());
        m_root = *xgs::paths::fromText(windowsPath(m_temp->path()));
        m_workDir = makeDir(u"work"_s);
        QVERIFY(!m_workDir.isEmpty());
    }
    void cleanup() { m_temp.reset(); }

    void aStopDuringYtDlpIsCancelledAndClassifiesNothing() {
        std::stop_source source;
        StopOnceRunning stopper(source);
        auto runner = downloadRunner([&](const QStringList&, const LineHandler&, const LineHandler& err, std::stop_token stop) {
            err(u"ERROR: [youtube] vid00000001: Video unavailable"_s);
            stopper.running();
            awaitStop(stop);
            return 1;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        const DownloadResult result = downloader.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), source.get_token());

        QVERIFY(std::holds_alternative<DownloadCancelled>(result));
        QCOMPARE(ytDlpCommands(runner).size(), 1);
        QCOMPARE(ffmpegCommands(runner).size(), 0);
        QVERIFY2(log.all().isEmpty(), "a cancelled item emits nothing, not even Finishing");
    }

    void aStopBeforeTheRunRunsNothing() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeM4a(command);
            return 0;
        });
        FakeMetadata metadata;
        std::stop_source source;
        source.request_stop();

        const FixedTools tools;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;
        const DownloadResult result = downloader.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), source.get_token());
        QVERIFY(std::holds_alternative<DownloadCancelled>(result));

        // The stop is the very first check: it wins over a missing tool.
        const FixedTools noTools(ToolPaths{});
        ItemDownloader withoutTools(runner, noTools, metadata);
        const DownloadResult noToolsResult =
            withoutTools.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), source.get_token());
        QVERIFY(std::holds_alternative<DownloadCancelled>(noToolsResult));

        QCOMPARE(runner.commands().size(), 0);
        QVERIFY(log.all().isEmpty());
    }

    void aStopDuringTheLookupIsCancelledAndWritesNoTags() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeM4a(command);
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        std::stop_source source;
        // Author decision 4: the provider requests the stop itself and answers nothing (Kotlin's provider would throw).
        FakeLyricsProvider lyrics([&](const LyricsQuery&) -> std::optional<QString> {
            source.request_stop();
            return std::nullopt;
        });
        ItemDownloader downloader(runner, tools, metadata, lyrics);
        EventLog log;

        const DownloadResult result =
            downloader.download(preparedOf(1, kVideoId, true), m_workDir, log.emitter(), source.get_token());

        QVERIFY(std::holds_alternative<DownloadCancelled>(result));
        QCOMPARE(lyrics.queries().size(), 1);
        QCOMPARE(ffmpegCommands(runner).size(), 0);
        QVERIFY(!endsWithFakeAudio(*xgs::paths::child(m_workDir, kVideoId + u".m4a"_s)));
    }

    void aStopDuringTaggingIsCancelled() {
        std::stop_source source;
        FakeProcessRunner runner([&](const QStringList& command, const LineHandler&, const LineHandler&) {
            if (isFfmpegCommand(command)) {
                source.request_stop();
                return 0;
            }
            writeFakeM4a(command);
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        const DownloadResult result = downloader.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), source.get_token());

        QVERIFY(std::holds_alternative<DownloadCancelled>(result));
        QCOMPARE(ffmpegCommands(runner).size(), 1);
        QVERIFY(!endsWithFakeAudio(*xgs::paths::child(m_workDir, kVideoId + u".m4a"_s)));
    }

    void aProgramThatCannotBeStartedIsAnIoError() {
        const FixedTools tools;
        FakeMetadata metadata;
        {
            auto runner = downloadRunner([](const QStringList&, const LineHandler&, const LineHandler&) -> int {
                throw StartFailure{u"boom"_s};
            });
            ItemDownloader downloader(runner, tools, metadata);
            EventLog log;
            const DownloadResult result = downloader.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), {});
            QVERIFY(std::holds_alternative<DownloadIoError>(result));
            QCOMPARE(std::get<DownloadIoError>(result).message, u"boom"_s);
            QCOMPARE(ffmpegCommands(runner).size(), 0);
            QVERIFY(log.all().isEmpty());
        }
        {
            auto runner = downloadRunner([](const QStringList&, const LineHandler&, const LineHandler&) -> int {
                throw RunFailure{u"lost"_s};
            });
            ItemDownloader downloader(runner, tools, metadata);
            EventLog log;
            const DownloadResult result = downloader.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), {});
            QVERIFY(std::holds_alternative<DownloadIoError>(result));
            QCOMPARE(std::get<DownloadIoError>(result).message, u"lost"_s);
            QCOMPARE(ffmpegCommands(runner).size(), 0);
            QVERIFY(log.all().isEmpty());
        }
    }

    void aPercentSignInTheWorkFolderIsEscapedInTheCommand() {
        const QString workDir = makeDir(u"100% %(id)s 한글"_s);
        QVERIFY(!workDir.isEmpty());
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeM4a(command);   // where yt-dlp would write: the template's directory with `%%` read as `%`
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        const DownloadResult result = downloader.download(preparedOf(1, kVideoId), workDir, log.emitter(), {});

        const QStringList command = ytDlpCommands(runner).value(0);
        // The whole command, URL included: the fakes read only the `-o` template, so nothing else would notice another
        // video's URL, the bare ID or an empty URL here.
        QCOMPARE(command, xgs::ytdlp::downloadCommand(testTools(), watchUrl(kVideoId),
                                                      xgs::ytdlp::escapeOutputTemplateDirectory(workDir), kVideoId));
        const QString outputTemplate = command.value(command.indexOf(u"-o"_s) + 1);
        QCOMPARE(outputTemplate, xgs::ytdlp::escapeOutputTemplateDirectory(workDir) + u"\\"_s + kVideoId + u".%(ext)s"_s);
        QVERIFY(outputTemplate.contains(u"100%% %%(id)s 한글"_s));
        QVERIFY(std::holds_alternative<Downloaded>(result));
        QCOMPARE(std::get<Downloaded>(result).file, *xgs::paths::child(workDir, kVideoId + u".m4a"_s));
        QVERIFY(endsWithFakeAudio(std::get<Downloaded>(result).file));
    }

    void aVideoIdWithAPercentSignFailsTheItemWithoutRunningAnything() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeM4a(command);
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        const DownloadResult result = downloader.download(preparedOf(1, u"ab%cd"_s), m_workDir, log.emitter(), {});

        QVERIFY(std::holds_alternative<DownloadFailed>(result));
        QCOMPARE(std::get<DownloadFailed>(result).failure,
                 (Failure{FailureKind::Other, rejectedIdText(m_workDir, u"a*b"_s)}));
        QCOMPARE(runner.commands().size(), 0);
        QVERIFY(log.all().isEmpty());
    }

    void aVideoIdTheCommandRejectsFailsTheItem() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeM4a(command);
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        for (const QString& videoId : {u"a*b"_s, QString()}) {
            const DownloadResult result = downloader.download(preparedOf(1, videoId), m_workDir, log.emitter(), {});
            QVERIFY(std::holds_alternative<DownloadFailed>(result));
            QCOMPARE(std::get<DownloadFailed>(result).failure,
                     (Failure{FailureKind::Other, rejectedIdText(m_workDir, videoId)}));
        }
        QCOMPARE(runner.commands().size(), 0);
        QVERIFY(log.all().isEmpty());
    }

    void aPercentThatIsNotFiniteIsReportedAsUnknown() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler& out, const LineHandler&) {
            out(u"XGSP|downloading|NaN|100|NA"_s);              // NaN: whole percent 0 (Kotlin's toInt), the first: sent
            out(u"XGSP|downloading|0|100|NA"_s);                // 0: the same whole percent as the NaN: dropped
            out(u"XGSP|downloading|Infinity|Infinity|NA"_s);    // Infinity / Infinity is NaN: whole 0 again: dropped
            out(u"XGSP|downloading|50|100|NA"_s);               // 50: sent
            out(u"XGSP|downloading|-NaN|100|NA"_s);             // NaN again, whole 0 differs from 50: sent as unknown
            out(u"XGSP|downloading|Infinity|100|NA"_s);         // infinite, clamped to 100 by the parser: sent as 100
            writeFakeM4a(command);
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        const DownloadResult result = downloader.download(preparedOf(4, kVideoId), m_workDir, log.emitter(), {});

        QVERIFY(std::holds_alternative<Downloaded>(result));
        const QList<JobEvent> expected{
            progress(4, Stage::Downloading, std::nullopt), progress(4, Stage::Downloading, 50.0),
            progress(4, Stage::Downloading, std::nullopt), progress(4, Stage::Downloading, 100.0),
            progress(4, Stage::Finishing, std::nullopt)};
        QCOMPARE(log.all(), expected);
    }

    void aLineCutShortByTheKillIsIgnored() {
        std::stop_source source;
        StopOnceRunning stopper(source);
        auto runner = downloadRunner([&](const QStringList&, const LineHandler& out, const LineHandler&, std::stop_token stop) {
            stopper.running();
            awaitStop(stop);
            // What the reader hands over after the kill: the start of `XGSP|downloading|5000|10000|NA`, and less.
            out(u"XGSP|downloading|5"_s);
            out(u"XGSP|downl"_s);
            return 1;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        DownloadResult result = DownloadFailed{};
        QVERIFY_THROWS_NO_EXCEPTION(
            result = downloader.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), source.get_token()));

        QVERIFY(std::holds_alternative<DownloadCancelled>(result));
        QCOMPARE(ffmpegCommands(runner).size(), 0);
        // Both lines arrive after the stop: neither may become an event, not even one that parses as an update.
        QVERIFY2(log.all().isEmpty(), "no event once the stop is requested");
    }

    void theInfoFileIsReadOnlyAfterYtDlpHasExited() {
        std::optional<QString> metadataText;
        auto runner = recordingRunner(metadataText, [](const QStringList& command, const LineHandler& out, const LineHandler&) {
            writeFakeInfo(command, R"({"album":"Early Album"})");
            writeFakeM4a(command);
            out(u"XGSP|downloading|10|100|NA"_s);
            // The last thing before the exit: the file the downloader must read is this one.
            writeFakeInfo(command, R"({"album":"Late Album"})");
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        const DownloadResult result = downloader.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), {});

        QVERIFY(std::holds_alternative<Downloaded>(result));
        QVERIFY(metadataText.has_value());
        QCOMPARE(readFfmetadata(*metadataText).value(u"album"_s), u"Late Album"_s);
    }

    void aProviderThatThrowsIsNoLyrics() {
        std::optional<QString> metadataText;
        auto runner = recordingRunner(metadataText, [](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeM4a(command);
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        FakeLyricsProvider lyrics([](const LyricsQuery&) -> std::optional<QString> {
            throw std::runtime_error("the lookup broke");
        });
        ItemDownloader downloader(runner, tools, metadata, lyrics);
        EventLog log;

        DownloadResult result = DownloadFailed{};
        QVERIFY_THROWS_NO_EXCEPTION(result = downloader.download(preparedOf(1, kVideoId, true), m_workDir, log.emitter(), {}));

        QVERIFY(std::holds_alternative<Downloaded>(result));
        QCOMPARE(std::get<Downloaded>(result).lyrics, LyricsOutcome::NotFound);
        QCOMPARE(lyrics.queries().size(), 1);
        QVERIFY(metadataText.has_value());
        QVERIFY(!readFfmetadata(*metadataText).contains(u"lyrics"_s));
    }

    void aMetadataSourceThatAnswersNothingAfterAStopKeepsTheFirstParse() {
        auto runner = downloadRunner([](const QStringList&, const LineHandler&, const LineHandler&) { return 0; });
        const FixedTools tools;
        std::stop_source source;
        FakeMetadata metadata([&](const QString&, std::stop_token) -> std::optional<xgs::ytdlp::VideoMeta> {
            source.request_stop();
            return std::nullopt;
        });
        ItemDownloader downloader(runner, tools, metadata);
        ResolvedItem item = itemOf(7, kVideoId);
        item.title = u"Some Song"_s;
        item.artist = u"Channel"_s;
        item.track = u"Some Song"_s;
        item.lowConfidence = true;

        PreparedItem prepared;
        QVERIFY_THROWS_NO_EXCEPTION(
            prepared = downloader.prepare(item, u"My List"_s, true, true, u"  "_s, source.get_token()));

        QCOMPARE(metadata.videoIds(), QStringList{kVideoId});
        QVERIFY(source.stop_requested());
        const PreparedItem expected{item, xgs::filename::format(7, u"Channel"_s, u"Some Song"_s), u"Channel"_s,
                                    u"Some Song"_s, u"My List"_s, true, std::nullopt};
        QCOMPARE(prepared, expected);
        QCOMPARE(runner.commands().size(), 0);
    }

    void twoDownloadsOfTheSameDownloaderCanRunAtOnce() {
        Meeting meeting(2);
        bool fail = false;
        auto runner = downloadRunner([&](const QStringList& command, const LineHandler& out, const LineHandler& err) {
            const QString id = videoIdOf(command);
            const int base = id == u"vidAAAAAAAA" ? 10 : 60;
            out(u"XGSP|downloading|%1|100|NA"_s.arg(base));
            err(u"WARNING: first line of "_s + id);
            meeting.arriveAndWait();   // both runs are inside the runner now; their lines interleave from here on
            out(u"XGSP|downloading|%1|100|NA"_s.arg(base + 5));
            err(u"ERROR: [youtube] "_s + id + u": something went wrong"_s);
            meeting.arriveAndWait();
            if (fail) return 1;
            writeFakeM4a(command);
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        const QString dirA = makeDir(u"a"_s);
        const QString dirB = makeDir(u"b"_s);
        QVERIFY(!dirA.isEmpty() && !dirB.isEmpty());

        const auto runBoth = [&](EventLog& logA, EventLog& logB, std::optional<DownloadResult>& resultA,
                                 std::optional<DownloadResult>& resultB) {
            std::jthread a([&] { resultA = downloader.download(preparedOf(1, u"vidAAAAAAAA"_s), dirA, logA.emitter(), {}); });
            std::jthread b([&] { resultB = downloader.download(preparedOf(2, u"vidBBBBBBBB"_s), dirB, logB.emitter(), {}); });
        };

        {
            EventLog logA, logB;
            std::optional<DownloadResult> resultA, resultB;
            runBoth(logA, logB, resultA, resultB);
            QCOMPARE(runner.maxActive(), 2);
            QVERIFY(resultA && std::holds_alternative<Downloaded>(*resultA));
            QVERIFY(resultB && std::holds_alternative<Downloaded>(*resultB));
            QCOMPARE(std::get<Downloaded>(*resultA).file, *xgs::paths::child(dirA, u"vidAAAAAAAA.m4a"_s));
            QCOMPARE(std::get<Downloaded>(*resultB).file, *xgs::paths::child(dirB, u"vidBBBBBBBB.m4a"_s));
            QCOMPARE(logA.all(), (QList<JobEvent>{progress(1, Stage::Downloading, 10.0), progress(1, Stage::Downloading, 15.0),
                                                  progress(1, Stage::Finishing, std::nullopt)}));
            QCOMPARE(logB.all(), (QList<JobEvent>{progress(2, Stage::Downloading, 60.0), progress(2, Stage::Downloading, 65.0),
                                                  progress(2, Stage::Finishing, std::nullopt)}));
        }
        {
            fail = true;   // written before the threads start, read by them afterwards
            EventLog logA, logB;
            std::optional<DownloadResult> resultA, resultB;
            runBoth(logA, logB, resultA, resultB);
            QVERIFY(resultA && std::holds_alternative<DownloadFailed>(*resultA));
            QVERIFY(resultB && std::holds_alternative<DownloadFailed>(*resultB));
            const auto classified = [](const QString& id) {
                return xgs::ytdlp::classifyError(
                    {u"WARNING: first line of "_s + id, u"ERROR: [youtube] "_s + id + u": something went wrong"_s});
            };
            QCOMPARE(std::get<DownloadFailed>(*resultA).failure, classified(u"vidAAAAAAAA"_s));
            QCOMPARE(std::get<DownloadFailed>(*resultB).failure, classified(u"vidBBBBBBBB"_s));
            QVERIFY(std::get<DownloadFailed>(*resultA).failure != std::get<DownloadFailed>(*resultB).failure);
            // A failed run is not finished: no Finishing event.
            QCOMPARE(logA.all(), (QList<JobEvent>{progress(1, Stage::Downloading, 10.0), progress(1, Stage::Downloading, 15.0)}));
            QCOMPARE(logB.all(), (QList<JobEvent>{progress(2, Stage::Downloading, 60.0), progress(2, Stage::Downloading, 65.0)}));
        }
    }

    void theDownloaderReturnsTheFileInsideTheWorkFolder() {
        auto runner = downloadRunner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeM4a(command);
            return 0;
        });
        const FixedTools tools;
        FakeMetadata metadata;
        ItemDownloader downloader(runner, tools, metadata);
        EventLog log;

        const DownloadResult result = downloader.download(preparedOf(1, kVideoId), m_workDir, log.emitter(), {});

        QVERIFY(std::holds_alternative<Downloaded>(result));
        const Downloaded& downloaded = std::get<Downloaded>(result);
        QCOMPARE(downloaded.file, *xgs::paths::child(m_workDir, kVideoId + u".m4a"_s));
        QVERIFY2(endsWithFakeAudio(downloaded.file), "the returned file holds the tagged output");
        QCOMPARE(downloaded.lyrics, LyricsOutcome::SearchOff);
        QCOMPARE(log.all(), (QList<JobEvent>{progress(1, Stage::Finishing, std::nullopt)}));
    }
};

QTEST_GUILESS_MAIN(ItemDownloaderTest)
#include "tst_item_downloader.moc"
