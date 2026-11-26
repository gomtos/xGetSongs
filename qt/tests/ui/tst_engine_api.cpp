#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <variant>

#include "engine_api.h"
#include "fake_app_api.h"
#include "job/download_concurrency.h"
#include "job/download_service.h"
#include "job/job_events.h"
#include "output/local_folder_sink.h"
#include "paths/paths.h"
#include "support/fake_process_runner.h"
#include "support/job_fakes.h"
#include "support/test_tools.h"
#include "tools/tool_manager.h"
#include "ytdlp/ytdlp_resolver.h"

// The bridge from the blocking engine of qt/core to the GUI thread (no Kotlin or TypeScript pair: the Tauri shell had a server in
// between). The engine parts are real (ToolManager, YtDlpResolver) on a fake process runner; the download service is a fake whose
// job is a script. Every wait is for an event, bounded by a generous limit (5 s, or what the test says) so that a broken bridge
// fails the test instead of hanging it; the two short sleeps say why they are there.

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::InputKind;
using xgs::JobDone;
using xgs::JobEvent;
using xgs::JobStatus;
using xgs::JobSummary;
using xgs::Outcome;
using xgs::OutcomeStatus;
using xgs::ResolveResult;
using xgs::ToolsStatus;
using xgs::job::DownloadRequest;
using xgs::job::DownloadService;
using xgs::job::JobEvents;
using xgs::job::JobHandle;
using xgs::process::LineHandler;
using xgs::testsupport::FakeProcessRunner;
using xgs::testsupport::FixedTools;
using xgs::ui::EngineApi;
using xgs::ui::JobCallbacks;
using xgs::ui::JobRequest;
using xgs::ui::test::item;

namespace {

constexpr int kLimitMs = 5000;

/// What the scripted service saw of one request when it started the job (the sink is gone once the job has ended).
struct SeenRequest {
    DownloadRequest request;
    QString sinkDirectory;
};

/// A download service whose job runs `script` on a thread of its own and then closes the queue. The script must queue a JobDone.
class ScriptedDownloadService final : public DownloadService {
public:
    using Script = std::function<void(JobEvents& events, std::stop_token stop)>;

    Script script = [](JobEvents& events, std::stop_token) { events.push(JobDone{JobStatus::Completed, JobSummary{}}); };

    std::unique_ptr<JobHandle> start(DownloadRequest request, std::stop_source stop) override {
        auto* sink = dynamic_cast<xgs::output::LocalFolderSink*>(request.sink);
        {
            const std::lock_guard lock(m_mutex);
            m_seen.append(SeenRequest{request, sink ? sink->directory() : QString()});
        }
        auto events = std::make_unique<JobEvents>();
        JobEvents* queue = events.get();
        std::jthread job([this, queue, token = stop.get_token(), run = script] {
            alive.fetch_add(1);
            run(*queue, token);
            queue->close();
            alive.fetch_sub(1);
        });
        return std::make_unique<JobHandle>(std::move(events), std::move(stop), std::move(job));
    }

    QList<SeenRequest> seen() const {
        const std::lock_guard lock(m_mutex);
        return m_seen;
    }

    std::atomic<int> alive{0};

private:
    mutable std::mutex m_mutex;
    QList<SeenRequest> m_seen;
};

/// Blocks until stop is requested (or 30 s have passed: a safety net only).
void waitForStop(std::stop_token stop) {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::unique_lock lock(mutex);
    changed.wait_for(lock, stop, std::chrono::seconds(30), [] { return false; });
}

/// The engine around one EngineApi. What a tool run prints is `onRun` (by default every tool prints a version that is found).
class Engine {
public:
    Engine()
        : runner([this](const QStringList& command, const LineHandler& out, const LineHandler& err, std::stop_token stop) {
              return onRun(command, out, err, stop);
          }),
          toolManager(tools, runner, binDir(), [this](const QString& url, std::stop_token stop) { return onFetch(url, stop); }),
          resolver(runner, tools) {
        api = std::make_unique<EngineApi>(runner, toolManager, resolver, downloads, dir.filePath(u"work"_s));
    }

    ~Engine() { api.reset(); }

    QString binDir() const { return *xgs::paths::fromText(dir.filePath(u"bin"_s)); }

    QTemporaryDir dir;
    FixedTools tools;
    std::function<int(const QStringList&, const LineHandler&, const LineHandler&, std::stop_token)> onRun =
        [](const QStringList& command, const LineHandler& out, const LineHandler&, std::stop_token) {
            const QString tool = xgs::paths::fileName(command.first()).value_or(QString());
            if (tool.startsWith(u"yt-dlp"_s)) out(u"2025.10.22"_s);
            else if (tool.startsWith(u"ffmpeg"_s)) out(u"ffmpeg version 7.1 Copyright"_s);
            else out(u"v24.1.0"_s);
            return 0;
        };
    std::function<Outcome<QByteArray>(const QString&, std::stop_token)> onFetch = [](const QString&, std::stop_token) {
        return Outcome<QByteArray>::failed(u"no network in tests"_s);
    };
    FakeProcessRunner runner;
    xgs::tools::ToolManager toolManager;
    xgs::ytdlp::YtDlpResolver resolver;
    ScriptedDownloadService downloads;
    std::unique_ptr<EngineApi> api;
};

/// A request for the two sample items into a fresh folder below dir.
JobRequest requestInto(const QTemporaryDir& dir, bool isPlaylist = true) {
    JobRequest request;
    request.items = {item(1), item(2)};
    request.isPlaylist = isPlaylist;
    request.playlistTitle = isPlaylist ? std::optional<QString>(u"더미 목록"_s) : std::nullopt;
    request.options.outputDir = QDir::toNativeSeparators(dir.filePath(u"out"_s));
    return request;
}

/// What the callbacks of one job saw, in order, and on which threads.
struct Seen {
    QStringList log;
    QList<JobEvent> events;
    bool offThread = false;
    bool ended = false;
    std::optional<QString> startError;
    std::optional<QString> endError;
};

JobCallbacks recorder(Seen& seen, std::function<void(const JobEvent&)> also = {}) {
    QThread* const gui = QThread::currentThread();
    JobCallbacks callbacks;
    callbacks.started = [&seen, gui](std::optional<QString> error) {
        if (QThread::currentThread() != gui) seen.offThread = true;
        seen.log.append(error ? u"refused"_s : u"started"_s);
        seen.startError = error;
    };
    callbacks.event = [&seen, gui, also](const JobEvent& event) {
        if (QThread::currentThread() != gui) seen.offThread = true;
        seen.log.append(u"event"_s);
        seen.events.append(event);
        if (also) also(event);
    };
    callbacks.ended = [&seen, gui](std::optional<QString> error) {
        if (QThread::currentThread() != gui) seen.offThread = true;
        seen.log.append(u"ended"_s);
        seen.endError = error;
        seen.ended = true;
    };
    return callbacks;
}

}  // namespace

class EngineApiTest : public QObject {
    Q_OBJECT

private slots:
    void eventsArriveOnTheCallingThreadInOrder() {
        Engine engine;
        const QList<JobEvent> script = {
            xgs::ItemStarted{1, u"vid00000001"_s, u"001 A1 - T1.m4a"_s},
            xgs::Progress{1, xgs::Stage::Downloading, 50.0},
            xgs::ItemDone{1, u"001 A1 - T1.m4a"_s, xgs::LyricsOutcome::NotFound},
            xgs::ItemStarted{2, u"vid00000002"_s, u"002 A2 - T2.m4a"_s},
            xgs::ItemFailed{2, u"더미 실패"_s},
            JobDone{JobStatus::Completed, JobSummary{1, 0, 1}},
        };
        engine.downloads.script = [script](JobEvents& events, std::stop_token) {
            for (const JobEvent& event : script) events.push(event);
        };
        Seen seen;

        engine.api->startJob(requestInto(engine.dir), recorder(seen));
        QVERIFY2(seen.log.isEmpty(), "nothing is delivered from inside the call");

        QTRY_VERIFY_WITH_TIMEOUT(seen.ended, kLimitMs);
        QCOMPARE(seen.log, (QStringList{u"started"_s, u"event"_s, u"event"_s, u"event"_s, u"event"_s, u"event"_s, u"event"_s, u"ended"_s}));
        QVERIFY(seen.events == script);
        QVERIFY2(!seen.offThread, "every callback runs on the thread that made the EngineApi");
        QCOMPARE(seen.startError, std::optional<QString>());
        QCOMPARE(seen.endError, std::optional<QString>());
    }

    void anAnswerAfterStopIsDropped() {
        Engine engine;
        std::atomic<int> entered{0};
        engine.onRun = [&entered](const QStringList&, const LineHandler& out, const LineHandler&, std::stop_token stop) {
            entered.fetch_add(1);
            xgs::testsupport::awaitStop(stop);
            out(u"2025.10.22"_s);  // what a tool prints after the stop must not reach the screen
            return 0;
        };
        int answers = 0;
        engine.api->resolve(u"PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"_s, [&answers](Outcome<ResolveResult>) { answers += 1; });
        engine.api->tools([&answers](Outcome<ToolsStatus>) { answers += 1; });
        QTRY_COMPARE_WITH_TIMEOUT(entered.load(), 2, kLimitMs);

        engine.api->stop();
        // Proving that nothing arrives needs a window of time: the answers would be queued right after the stop returned.
        QTest::qWait(300);

        QCOMPARE(answers, 0);
        engine.api->resolve(u"PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"_s, [&answers](Outcome<ResolveResult>) { answers += 1; });
        engine.api->tools([&answers](Outcome<ToolsStatus>) { answers += 1; });
        QTest::qWait(100);
        QVERIFY2(answers == 0, "a stopped bridge answers nothing at all");
        QCOMPARE(entered.load(), 2);
    }

    void toolInstallIsRefusedWhileAJobRuns() {
        Engine engine;
        std::atomic<int> fetches{0};
        engine.onFetch = [&fetches](const QString&, std::stop_token) {
            fetches.fetch_add(1);
            return Outcome<QByteArray>::failed(u"no network in tests"_s);
        };
        engine.downloads.script = [](JobEvents& events, std::stop_token stop) {
            waitForStop(stop);
            events.push(JobDone{JobStatus::Cancelled, JobSummary{}});
        };
        Seen seen;
        engine.api->startJob(requestInto(engine.dir), recorder(seen));
        QTRY_COMPARE_WITH_TIMEOUT(seen.log, QStringList{u"started"_s}, kLimitMs);

        std::optional<Outcome<QString>> install;
        std::optional<Outcome<QString>> update;
        engine.api->installYtDlp([&install](Outcome<QString> outcome) { install = outcome; });
        engine.api->updateYtDlp([&update](Outcome<QString> outcome) { update = outcome; });
        QTRY_VERIFY_WITH_TIMEOUT(install && update, kLimitMs);

        QCOMPARE(install->status, OutcomeStatus::Failed);
        QCOMPARE(install->error, EngineApi::refusedWhileDownloading());
        QCOMPARE(update->status, OutcomeStatus::Failed);
        QCOMPARE(update->error, EngineApi::refusedWhileDownloading());
        QVERIFY2(engine.runner.commands().isEmpty(), "yt-dlp was not run");
        QCOMPARE(fetches.load(), 0);

        engine.api->cancelJob();
        QTRY_VERIFY_WITH_TIMEOUT(seen.ended, kLimitMs);
        install.reset();
        engine.api->installYtDlp([&install](Outcome<QString> outcome) { install = outcome; });
        QTRY_VERIFY_WITH_TIMEOUT(install.has_value(), kLimitMs);

        QVERIFY2(install->error != EngineApi::refusedWhileDownloading(), "once the job has ended the install runs");
        QCOMPARE(fetches.load(), 1);
    }

    void closingWhileAJobRunsEndsEverything() {
        Engine engine;
        std::atomic<bool> resolving{false};
        engine.onRun = [&resolving](const QStringList&, const LineHandler&, const LineHandler&, std::stop_token stop) {
            resolving = true;
            xgs::testsupport::awaitStop(stop);
            return 1;
        };
        engine.downloads.script = [](JobEvents& events, std::stop_token stop) {
            events.push(xgs::ItemStarted{1, u"vid00000001"_s, u"001 A1 - T1.m4a"_s});
            waitForStop(stop);
            events.push(JobDone{JobStatus::Cancelled, JobSummary{}});
        };
        Seen seen;
        int answers = 0;
        engine.api->startJob(requestInto(engine.dir), recorder(seen));
        engine.api->resolve(u"PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"_s, [&answers](Outcome<ResolveResult>) { answers += 1; });
        QTRY_COMPARE_WITH_TIMEOUT(seen.log, (QStringList{u"started"_s, u"event"_s}), kLimitMs);
        QTRY_VERIFY_WITH_TIMEOUT(resolving.load(), kLimitMs);
        QCOMPARE(engine.downloads.alive.load(), 1);

        QElapsedTimer clock;
        clock.start();
        engine.api->stop();
        const qint64 took = clock.elapsed();

        QVERIFY2(took < 5000, qPrintable(u"stop took %1 ms"_s.arg(took)));
        QVERIFY2(engine.downloads.alive.load() == 0, "the job's thread has ended");
        QTest::qWait(200);  // a window for anything that would still be delivered (see anAnswerAfterStopIsDropped)
        QVERIFY2(seen.log == (QStringList{u"started"_s, u"event"_s}), "nothing is delivered after the stop, not even the end");
        QCOMPARE(answers, 0);
        engine.api.reset();  // the bridge goes before the engine it uses, which is what the app does as well
    }

    void aCancelRightAfterTheLastItemStillEndsWithJobDone() {
        // This pins the bridge only: the service is a script that ends with a JobDone whichever way the race goes. Whether the
        // real DefaultDownloadService does so is tst_default_download_service's business.
        Engine engine;
        engine.downloads.script = [](JobEvents& events, std::stop_token stop) {
            events.push(xgs::ItemDone{2, u"002 A2 - T2.m4a"_s, std::nullopt});
            // the moment between the last item and the end of the job, where the cancel lands
            std::mutex mutex;
            std::condition_variable_any changed;
            std::unique_lock lock(mutex);
            changed.wait_for(lock, stop, std::chrono::milliseconds(20), [] { return false; });
            events.push(JobDone{stop.stop_requested() ? JobStatus::Cancelled : JobStatus::Completed, JobSummary{1, 0, 0}});
        };
        for (int round = 0; round < 10; ++round) {
            Seen seen;
            EngineApi* api = engine.api.get();
            engine.api->startJob(requestInto(engine.dir), recorder(seen, [api](const JobEvent& event) {
                if (std::holds_alternative<xgs::ItemDone>(event)) api->cancelJob();
            }));

            QTRY_VERIFY_WITH_TIMEOUT(seen.ended, kLimitMs);

            QVERIFY2(!seen.events.isEmpty() && std::holds_alternative<JobDone>(seen.events.last()), qPrintable(seen.log.join(u',')));
            QCOMPARE(seen.log.last(), u"ended"_s);
            QCOMPARE(seen.endError, std::optional<QString>());
        }
    }

    // ---- not named by the plan ----

    void destroyingTheApiWhileAJobRunsEndsEverything() {
        Engine engine;
        std::atomic<bool> resolving{false};
        engine.onRun = [&resolving](const QStringList&, const LineHandler&, const LineHandler&, std::stop_token stop) {
            resolving = true;
            xgs::testsupport::awaitStop(stop);
            return 1;
        };
        engine.downloads.script = [](JobEvents& events, std::stop_token stop) {
            for (int rank = 1; rank <= 50; ++rank) events.push(xgs::ItemSkipped{rank, u"이미 존재"_s});
            waitForStop(stop);
            events.push(JobDone{JobStatus::Cancelled, JobSummary{}});
        };
        Seen seen;
        int answers = 0;
        engine.api->startJob(requestInto(engine.dir), recorder(seen));
        engine.api->resolve(u"PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"_s, [&answers](Outcome<ResolveResult>) { answers += 1; });
        QTRY_VERIFY_WITH_TIMEOUT(resolving.load(), kLimitMs);
        // A moment without the event loop, so that the reader queues the fifty events as answers nobody has delivered yet (a
        // sleep: there is no event for "posted but not delivered").
        std::this_thread::sleep_for(300ms);
        const QStringList before = seen.log;

        QElapsedTimer clock;
        clock.start();
        engine.api.reset();  // no stop() first: the destructor does it
        const qint64 took = clock.elapsed();

        QVERIFY2(took < 5000, qPrintable(u"destroying took %1 ms"_s.arg(took)));
        QVERIFY2(engine.downloads.alive.load() == 0, "the job's thread has ended");
        QTest::qWait(200);  // spin the event loop: the queued answers of the destroyed object must never run
        QVERIFY2(seen.log == before, qPrintable(seen.log.join(u',')));
        QCOMPARE(answers, 0);
    }

    void theDownloadRequestIsBuiltLikeTheServerBuiltIt() {
        Engine engine;
        JobRequest request = requestInto(engine.dir);
        request.playlistTitle = u"더미: 목록"_s;
        request.options.albumName = u" 내 앨범 "_s;
        request.options.overwrite = true;
        request.options.includeRank = false;
        request.options.searchLyricsOnline = true;
        Seen playlist;
        engine.api->startJob(request, recorder(playlist));
        QTRY_VERIFY_WITH_TIMEOUT(playlist.ended, kLimitMs);

        JobRequest single = requestInto(engine.dir, false);
        single.items = {item(7)};
        single.options.singleRank = 7;
        single.options.searchLyricsOnline = false;
        Seen video;
        engine.api->startJob(single, recorder(video));
        QTRY_VERIFY_WITH_TIMEOUT(video.ended, kLimitMs);

        const QList<SeenRequest> seen = engine.downloads.seen();
        QCOMPARE(seen.size(), 2);
        const QString outputDir = *xgs::paths::fromText(*request.options.outputDir);
        QCOMPARE(seen[0].sinkDirectory, *xgs::paths::child(outputDir, u"내 앨범"_s));
        QVERIFY(QFileInfo(seen[0].sinkDirectory).isDir());
        QCOMPARE(seen[0].request.items, request.items);
        QCOMPARE(seen[0].request.album, std::optional<QString>(u"더미: 목록"_s));
        QCOMPARE(seen[0].request.albumOverride, std::optional<QString>(u"내 앨범"_s));
        QCOMPARE(seen[0].request.overwrite, true);
        QCOMPARE(seen[0].request.includeRank, false);
        QCOMPARE(seen[0].request.searchLyricsOnline, true);
        QCOMPARE(seen[0].request.concurrency, xgs::job::automaticConcurrency());
        QCOMPARE(seen[1].sinkDirectory, outputDir);
        QCOMPARE(seen[1].request.items, single.items);
        QCOMPARE(seen[1].request.album, std::optional<QString>());
        QCOMPARE(seen[1].request.albumOverride, std::optional<QString>());
        QCOMPARE(seen[1].request.searchLyricsOnline, false);
    }

    void aRequestTheServerWouldRefuseIsRefusedWithItsMessage() {
        Engine engine;
        const QString blocker = engine.dir.filePath(u"a-file"_s);
        {
            QFile file(blocker);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const QString blockedFolder = *xgs::paths::fromText(blocker);
        struct Case {
            const char* name;
            std::function<void(JobRequest&)> change;
            QString message;
        };
        const QList<Case> cases = {
            {"no folder", [](JobRequest& r) { r.options.outputDir.reset(); }, u"출력 폴더를 지정하세요."_s},
            {"a blank folder", [](JobRequest& r) { r.options.outputDir = u"   "_s; }, u"출력 폴더를 지정하세요."_s},
            {"not a path", [](JobRequest& r) { r.options.outputDir = u"C:\\a<b"_s; }, u"출력 폴더 경로가 올바르지 않습니다."_s},
            {"a relative path", [](JobRequest& r) { r.options.outputDir = u"music"_s; }, u"출력 폴더는 절대 경로여야 합니다."_s},
            {"the root of the current drive", [](JobRequest& r) { r.options.outputDir = u"\\music"_s; }, u"출력 폴더는 절대 경로여야 합니다."_s},
            {"a folder that cannot be made", [blocker](JobRequest& r) { r.options.outputDir = blocker; r.isPlaylist = false; },
             u"출력 폴더를 만들 수 없습니다: "_s + blockedFolder},
            {"nothing to download", [](JobRequest& r) { r.items.clear(); }, u"다운로드할 항목이 없습니다."_s},
            {"a rank out of range", [](JobRequest& r) { r.isPlaylist = false; r.options.singleRank = 0; }, u"순위 번호는 1~999 사이여야 합니다."_s},
        };
        for (const Case& c : cases) {
            JobRequest request = requestInto(engine.dir);
            c.change(request);
            Seen seen;

            engine.api->startJob(request, recorder(seen));

            QTRY_VERIFY2_WITH_TIMEOUT(!seen.log.isEmpty(), c.name, kLimitMs);
            QVERIFY2(seen.log == QStringList{u"refused"_s}, c.name);
            QVERIFY2(seen.startError == std::optional<QString>(c.message), qPrintable(seen.startError.value_or(u"<none>"_s)));
        }
        QVERIFY2(engine.downloads.seen().isEmpty(), "no job was started");
    }

    void resolveAndToolsAnswerOnTheCallingThread() {
        Engine engine;
        std::optional<Outcome<ToolsStatus>> tools;
        std::optional<Outcome<ResolveResult>> resolved;
        bool offThread = false;
        QThread* const gui = QThread::currentThread();
        engine.onRun = [](const QStringList& command, const LineHandler& out, const LineHandler& err, std::stop_token) {
            if (command.contains(u"--version"_s) || command.contains(u"-version"_s)) {
                const QString tool = xgs::paths::fileName(command.first()).value_or(QString());
                out(tool.startsWith(u"yt-dlp"_s) ? u"2025.10.22"_s : tool.startsWith(u"ffmpeg"_s) ? u"ffmpeg version 7.1 x"_s : u"v24.1.0"_s);
                return 0;
            }
            err(u"ERROR: [youtube:tab] 더미: This playlist does not exist"_s);
            return 1;
        };

        engine.api->tools([&](Outcome<ToolsStatus> outcome) {
            offThread |= QThread::currentThread() != gui;
            tools = outcome;
        });
        engine.api->resolve(u"PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"_s, [&](Outcome<ResolveResult> outcome) {
            offThread |= QThread::currentThread() != gui;
            resolved = outcome;
        });
        QTRY_VERIFY_WITH_TIMEOUT(tools && resolved, kLimitMs);

        QVERIFY(!offThread);
        QCOMPARE(tools->status, OutcomeStatus::Ok);
        QCOMPARE(tools->value->ytDlp.version, std::optional<QString>(u"2025.10.22"_s));
        QCOMPARE(resolved->status, OutcomeStatus::Failed);
        QVERIFY(!resolved->error.isEmpty());
    }

    void toolInstallsRunOneAtATime() {
        Engine engine;
        std::atomic<int> active{0};
        std::atomic<int> peak{0};
        engine.onFetch = [&](const QString&, std::stop_token) {
            const int now = active.fetch_add(1) + 1;
            int seen = peak.load();
            while (now > seen && !peak.compare_exchange_weak(seen, now)) {
            }
            // Held long enough that two installs running together would certainly overlap here.
            std::this_thread::sleep_for(100ms);
            active.fetch_sub(1);
            return Outcome<QByteArray>::failed(u"no network in tests"_s);
        };
        int answers = 0;

        engine.api->installYtDlp([&answers](Outcome<QString>) { answers += 1; });
        engine.api->installYtDlp([&answers](Outcome<QString>) { answers += 1; });
        QTRY_COMPARE_WITH_TIMEOUT(answers, 2, kLimitMs);

        QCOMPARE(peak.load(), 1);
    }
};

QTEST_GUILESS_MAIN(EngineApiTest)
#include "tst_engine_api.moc"
