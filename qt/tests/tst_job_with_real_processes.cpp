#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include "filename/filename_formatter.h"
#include "helpers/own_processes.h"
#include "job/default_download_service.h"
#include "job/download_service.h"
#include "job/item_downloader.h"
#include "output/local_folder_sink.h"
#include "paths/paths.h"
#include "process/system_process_runner.h"
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
using xgs::Progress;
using xgs::ResolvedItem;
using xgs::Stage;
using xgs::job::DefaultDownloadService;
using xgs::job::DownloadRequest;
using xgs::job::ItemDownloader;
using xgs::job::JobHandle;
using xgs::output::LocalFolderSink;
using xgs::process::LineHandler;
using xgs::process::RunResult;
using xgs::process::SystemProcessRunner;
using xgs::testsupport::FakeMetadata;
using xgs::testsupport::FixedTools;
using xgs::testsupport::testTools;

// The download job (DefaultDownloadService, ItemDownloader) through the real SystemProcessRunner and the real LocalFolderSink.
// Not a port: the Kotlin job is only tested with fakes. xgs-test-child (qt/tests/helpers/test_child.cpp) plays yt-dlp and
// ffmpeg (modes fakeytdlp, fakeytdlpspawn and fakeffmpeg): no network, no real tool, no window. The real runner refuses .cmd
// and .bat files, so the tools are not imitated by batch files: ChildToolRunner swaps the executable of each command for
// xgs-test-child and its mode and hands the command to a real SystemProcessRunner.
namespace {

// The helper executable, with `\` as the engine's paths have them.
QString child() {
    QString path = QString::fromUtf8(XGS_TEST_CHILD);
    return path.replace(u'/', u'\\');
}

QString windowsPath(const QString& path) {
    return xgs::paths::fromText(QDir::toNativeSeparators(path)).value_or(QString());
}

QString childOf(const QString& dir, const QString& name) {
    return xgs::paths::child(dir, name).value_or(QString());
}

/// Every name in a directory (files and folders, hidden ones too), sorted.
QStringList namesIn(const QString& dir) {
    QStringList names = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Unsorted);
    std::sort(names.begin(), names.end());
    return names;
}

/// Kotlin's `item(rank, artist = "A$rank", track = "T$rank")` of the job tests; videoId is vidNNNNNNNN unless given.
ResolvedItem item(int rank, std::optional<QString> videoId = std::nullopt, std::optional<QString> artist = std::nullopt,
                  std::optional<QString> track = std::nullopt) {
    ResolvedItem result;
    result.rank = rank;
    result.videoId = videoId.value_or(u"vid%1"_s.arg(rank, 8, 10, QChar(u'0')));
    result.artist = artist.value_or(u"A%1"_s.arg(rank));
    result.track = track.value_or(u"T%1"_s.arg(rank));
    result.title = result.artist + u" - "_s + result.track;
    result.channel = u"ch"_s;
    result.expectedFileName = xgs::filename::format(rank, result.artist, result.track);
    return result;
}

QString describe(const JobEvent& event) {
    if (const auto* e = std::get_if<ItemStarted>(&event)) return u"ItemStarted(%1, %2)"_s.arg(e->rank).arg(e->fileName);
    if (const auto* e = std::get_if<Progress>(&event)) {
        return u"Progress(%1, %2)"_s.arg(e->rank).arg(e->stage == Stage::Downloading ? u"Downloading"_s : u"Finishing"_s);
    }
    if (const auto* e = std::get_if<ItemDone>(&event)) return u"ItemDone(%1, %2)"_s.arg(e->rank).arg(e->fileName);
    if (const auto* e = std::get_if<ItemSkipped>(&event)) return u"ItemSkipped(%1, %2)"_s.arg(e->rank).arg(e->reason);
    if (const auto* e = std::get_if<ItemFailed>(&event)) return u"ItemFailed(%1, %2)"_s.arg(e->rank).arg(e->message);
    const auto& e = std::get<JobDone>(event);
    const QString status = e.status == JobStatus::Completed ? u"Completed"_s
                           : e.status == JobStatus::Cancelled ? u"Cancelled"_s
                                                              : u"Failed"_s;
    return u"JobDone(%1, %2/%3/%4)"_s.arg(status).arg(e.summary.succeeded).arg(e.summary.skipped).arg(e.summary.failed);
}

QString eventsText(const QList<JobEvent>& events) {
    QStringList parts;
    for (const JobEvent& event : events) parts.append(describe(event));
    return u"["_s + parts.join(u", "_s) + u"]"_s;
}

/// True when both ItemStarted events of a two-item job come before the first event that ends an item (ItemDone, ItemSkipped
/// or ItemFailed): the two items were in the job at the same time.
bool bothStartedBeforeEitherEnded(const QList<JobEvent>& events) {
    int started = 0;
    for (const JobEvent& event : events) {
        if (std::holds_alternative<ItemStarted>(event)) {
            if (++started == 2) return true;
        } else if (std::holds_alternative<ItemDone>(event) || std::holds_alternative<ItemSkipped>(event) ||
                   std::holds_alternative<ItemFailed>(event)) {
            return false;
        }
    }
    return false;
}

std::optional<JobDone> lastDone(const QList<JobEvent>& events) {
    if (events.isEmpty() || !std::holds_alternative<JobDone>(events.last())) return std::nullopt;
    return std::get<JobDone>(events.last());
}

/// Hands each command to a real SystemProcessRunner with its executable swapped for xgs-test-child: the yt-dlp of testTools()
/// becomes `fakeytdlp <ms>` (or, with a pid folder, `fakeytdlpspawn <pid file> <ms>`, a new pid file per run), the ffmpeg
/// becomes `fakeffmpeg`; the arguments follow as the engine built them. Anything else becomes the unknown mode `notatool`
/// (exit code 2).
///
/// With meetCount, each yt-dlp run first waits until meetCount yt-dlp runs have begun (or the stop is requested), so that
/// many items are known to be downloading at once: the job pushes ItemStarted before it calls the downloader, so every one of
/// those ItemStarted events is queued before any of the items can end.
class ChildToolRunner final : public xgs::process::ProcessRunner {
public:
    explicit ChildToolRunner(int ytDlpMilliseconds, std::optional<QString> pidDir = std::nullopt, int meetCount = 0)
        : m_ytDlpMilliseconds(ytDlpMilliseconds), m_pidDir(std::move(pidDir)), m_meetCount(meetCount) {}

    RunResult run(const QStringList& command, const LineHandler& onStdout, const LineHandler& onStderr,
                  std::stop_token stop) override {
        const xgs::ToolPaths tools = testTools();
        QStringList real{child()};
        if (!command.isEmpty() && command.first() == tools.ytDlp) {
            if (m_meetCount > 0) {
                // The runs meet in well under a second; 30 s is a safety net only. When it passes, the run goes on alone
                // and the test's overlap assertion fails (it does not hang).
                std::unique_lock lock(m_mutex);
                ++m_arrived;
                m_met.notify_all();
                m_met.wait_for(lock, stop, 30s, [this] { return m_arrived >= m_meetCount; });
            }
            if (m_pidDir) {
                const QString pidFile = childOf(*m_pidDir, u"run-%1.pid"_s.arg(m_runs.fetch_add(1) + 1));
                {
                    const std::lock_guard lock(m_mutex);
                    m_pidFiles.append(pidFile);
                }
                real << u"fakeytdlpspawn"_s << pidFile;
            } else {
                real << u"fakeytdlp"_s;
            }
            real << QString::number(m_ytDlpMilliseconds);
        } else if (!command.isEmpty() && command.first() == tools.ffmpeg) {
            real << u"fakeffmpeg"_s;
        } else {
            real << u"notatool"_s;
        }
        real += command.mid(1);
        return m_real.run(real, onStdout, onStderr, stop);
    }

    QStringList pidFiles() const {
        const std::lock_guard lock(m_mutex);
        return m_pidFiles;
    }

private:
    SystemProcessRunner m_real;
    const int m_ytDlpMilliseconds;
    const std::optional<QString> m_pidDir;
    const int m_meetCount;
    std::atomic<int> m_runs{0};
    mutable std::mutex m_mutex;
    std::condition_variable_any m_met;
    int m_arrived = 0;
    QStringList m_pidFiles;
};

/// The downloader and the service, owned together; the metadata source answers nothing and no lyrics are looked up.
class Service {
public:
    Service(xgs::process::ProcessRunner& runner, const QString& tempRoot)
        : m_downloader(runner, m_tools, m_metadata), m_service(m_downloader, tempRoot, {1ms, 1ms}) {}

    std::unique_ptr<JobHandle> start(DownloadRequest request) { return m_service.start(std::move(request)); }

private:
    FixedTools m_tools;
    FakeMetadata m_metadata;
    ItemDownloader m_downloader;
    DefaultDownloadService m_service;
};

/// Reads the events of a job on a thread of its own, so the test can wait for one with a limit instead of blocking in
/// nextEvent. Destroying it cancels the job (so a failed test never hangs on a job still running) and joins the thread.
class EventReader {
public:
    explicit EventReader(JobHandle& handle)
        : m_handle(handle), m_thread([this] {
              while (std::optional<JobEvent> event = m_handle.nextEvent()) {
                  {
                      const std::lock_guard lock(m_mutex);
                      m_events.append(*event);
                  }
                  m_changed.notify_all();
              }
              {
                  const std::lock_guard lock(m_mutex);
                  m_ended = true;
              }
              m_changed.notify_all();
          }) {}
    ~EventReader() { m_handle.cancel(); }
    EventReader(const EventReader&) = delete;
    EventReader& operator=(const EventReader&) = delete;

    /// Waits until predicate holds for the events so far; false when limit passes first. The limit is a safety net only.
    bool waitUntil(const std::function<bool(const QList<JobEvent>&)>& predicate, std::chrono::milliseconds limit) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, limit, [&] { return predicate(m_events); });
    }

    /// Waits until the queue has ended (JobDone taken); false when limit passes first.
    bool waitForEnd(std::chrono::milliseconds limit) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, limit, [&] { return m_ended; });
    }

    QList<JobEvent> events() const {
        const std::lock_guard lock(m_mutex);
        return m_events;
    }

private:
    JobHandle& m_handle;
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    QList<JobEvent> m_events;
    bool m_ended = false;
    std::jthread m_thread;   // last: starts after the members it uses
};

// A whole job with fakes this quick ends in well under a second; 60 s only keeps a broken job from hanging the run.
constexpr auto JobLimit = 60s;

/// Runs a job to its end and returns its events (the test fails when it does not end within JobLimit).
QList<JobEvent> runToEnd(Service& service, DownloadRequest request) {
    const std::unique_ptr<JobHandle> handle = service.start(std::move(request));
    EventReader reader(*handle);
    const bool ended = reader.waitForEnd(JobLimit);
    QList<JobEvent> events = reader.events();
    if (!ended) QTest::qFail(qPrintable(u"the job did not end: "_s + eventsText(events)), __FILE__, __LINE__);
    return events;
}

DownloadRequest request(const QList<ResolvedItem>& items, LocalFolderSink& sink, int concurrency, bool overwrite = false,
                        bool includeRank = true) {
    DownloadRequest result;
    result.items = items;
    result.sink = &sink;
    result.concurrency = concurrency;
    result.overwrite = overwrite;
    result.includeRank = includeRank;
    return result;
}

/// The process id `fakeytdlpspawn` wrote, 0 when the file is not there.
DWORD readPid(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    bool ok = false;
    const DWORD pid = DWORD(QString::fromLatin1(file.readAll()).toULong(&ok));
    return ok ? pid : 0;
}

QString imageName(HANDLE process) {
    wchar_t buffer[32768];
    DWORD size = DWORD(std::size(buffer));
    return QueryFullProcessImageNameW(process, 0, buffer, &size) ? QString::fromWCharArray(buffer, qsizetype(size)) : QString();
}

/// A process this test started (a fakeytdlpspawn grandchild), opened by its recorded id. Should the test fail before that
/// process is gone, it is terminated here: it is ours, nothing else is ever touched.
class OwnProcess {
public:
    explicit OwnProcess(DWORD pid)
        : m_handle(pid ? OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr) {}
    ~OwnProcess() {
        if (!m_handle) return;
        if (WaitForSingleObject(m_handle, 0) == WAIT_TIMEOUT) {
            TerminateProcess(m_handle, 1);
            WaitForSingleObject(m_handle, 5000);
        }
        CloseHandle(m_handle);
    }
    OwnProcess(const OwnProcess&) = delete;
    OwnProcess& operator=(const OwnProcess&) = delete;

    HANDLE get() const { return m_handle; }

private:
    HANDLE m_handle;
};

}  // namespace

class JobWithRealProcessesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY2(QFile::exists(child()), qPrintable(child())); }

    void aWholeJobRunsThroughTheRealRunnerAndTheRealFolderSink() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString tempRoot = windowsPath(dir.filePath(u"work"_s));
        const QString outDir = windowsPath(dir.filePath(u"out"_s));
        ChildToolRunner runner(50);
        Service service(runner, tempRoot);
        LocalFolderSink sink(outDir);
        const QList<ResolvedItem> items{item(1), item(2), item(3)};

        const QList<JobEvent> events = runToEnd(service, request(items, sink, 2));
        if (QTest::currentTestFailed()) return;

        const std::optional<JobDone> done = lastDone(events);
        QVERIFY2(done, qPrintable(eventsText(events)));
        QVERIFY2(done->status == JobStatus::Completed, qPrintable(eventsText(events)));
        QVERIFY2(done->summary == (JobSummary{3, 0, 0}), qPrintable(eventsText(events)));
        QStringList expected;
        for (const ResolvedItem& i : items) expected.append(*i.expectedFileName);
        std::sort(expected.begin(), expected.end());
        QCOMPARE(namesIn(outDir), expected);
        for (const QString& name : expected) {
            QFile file(childOf(outDir, name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("fake tagged audio"));   // what fakeffmpeg wrote, moved by the real sink
        }
        QCOMPARE(namesIn(tempRoot), QStringList());
    }

    void cancellingKillsTheProcessTreesAndTheJobEndsPromptly() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString tempRoot = windowsPath(dir.filePath(u"work"_s));
        const QString outDir = windowsPath(dir.filePath(u"out"_s));
        const QString pidDir = windowsPath(dir.filePath(u"pids"_s));
        QVERIFY(QDir().mkpath(dir.filePath(u"pids"_s)));
        ChildToolRunner runner(60'000, pidDir);
        Service service(runner, tempRoot);
        LocalFolderSink sink(outDir);
        // The fake yt-dlp runs hold this test's own temporary folder (its random name) in their command lines.
        const QString token = QFileInfo(dir.path()).fileName();
        const FILETIME since = xgs::testsupport::nowAsFileTime();

        const std::unique_ptr<JobHandle> handle = service.start(request({item(1), item(2)}, sink, 2));
        EventReader reader(*handle);
        // fakeytdlpspawn writes its pid file before its progress lines, so a Downloading event of both items means both
        // grandchildren are running. Starting two processes takes far less than 30 s; the limit is a safety net.
        const bool bothDownloading = reader.waitUntil(
            [](const QList<JobEvent>& events) {
                bool seen[3] = {false, false, false};
                for (const JobEvent& event : events) {
                    if (const auto* p = std::get_if<Progress>(&event); p && p->stage == Stage::Downloading && p->rank >= 1 &&
                                                                        p->rank <= 2) {
                        seen[p->rank] = true;
                    }
                }
                return seen[1] && seen[2];
            },
            30s);
        QVERIFY2(bothDownloading, qPrintable(eventsText(reader.events())));

        const QStringList pidFiles = runner.pidFiles();
        QCOMPARE(pidFiles.size(), 2);
        std::vector<std::unique_ptr<OwnProcess>> grandchildren;
        for (const QString& pidFile : pidFiles) {
            const DWORD pid = readPid(pidFile);
            QVERIFY2(pid != 0, qPrintable(pidFile));
            grandchildren.push_back(std::make_unique<OwnProcess>(pid));
            QVERIFY2(grandchildren.back()->get(), qPrintable(u"cannot open process "_s + QString::number(pid)));
            QCOMPARE(WaitForSingleObject(grandchildren.back()->get(), 0), DWORD(WAIT_TIMEOUT));   // alive before the cancel
            QVERIFY(imageName(grandchildren.back()->get()).endsWith(u"\\xgs-test-child.exe"_s, Qt::CaseInsensitive));
        }
        // The two fake yt-dlp processes (the grandchildren's command lines name no folder of ours).
        const qsizetype running = xgs::testsupport::processesMentioning(token, since).size();
        QVERIFY2(running >= 2, qPrintable(QString::number(running)));

        QElapsedTimer timer;
        timer.start();
        handle->cancel();
        QVERIFY2(reader.waitForEnd(5s), qPrintable(eventsText(reader.events())));
        const qint64 elapsed = timer.elapsed();
        const QList<JobEvent> events = reader.events();
        const std::optional<JobDone> done = lastDone(events);
        QVERIFY2(done && done->status == JobStatus::Cancelled, qPrintable(eventsText(events)));
        QVERIFY2(elapsed < 5000, qPrintable(QString::number(elapsed)));
        // The runner kills each run's Job Object before it returns, so the grandchildren are gone by now; 5 s is a bound.
        for (const auto& grandchild : grandchildren) {
            QVERIFY2(WaitForSingleObject(grandchild->get(), 5000) == WAIT_OBJECT_0, "a grandchild outlived the cancel");
        }
        QVERIFY2(xgs::testsupport::waitForNoneMentioning(token, since, 5000).isEmpty(), "a fake yt-dlp outlived the cancel");
        QCOMPARE(namesIn(tempRoot), QStringList());
        QCOMPARE(namesIn(outDir), QStringList());
    }

    // Review Focus 5. yt-dlp reads its whole `-o` value as a template; fakeytdlp reads it the same way for `%%`, `%(id)s` and
    // `%(ext)s` (and creates the folders it names, as yt-dlp does), so an unescaped `%(id)s` in the work folder would make it
    // write into another folder, and the job would not find the m4a.
    void aWorkFolderAndAnOutputFolderWithPercentSpacesAndHangulWork() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString name = u"100% %(id)s 한글 폴더"_s;
        const QString workParent = windowsPath(dir.filePath(u"work"_s));
        const QString outParent = windowsPath(dir.filePath(u"out"_s));
        const QString tempRoot = childOf(workParent, name);
        const QString outDir = childOf(outParent, name);
        ChildToolRunner runner(0);
        Service service(runner, tempRoot);
        LocalFolderSink sink(outDir);
        const ResolvedItem one = item(1);

        const QList<JobEvent> events = runToEnd(service, request({one}, sink, 2));
        if (QTest::currentTestFailed()) return;

        const std::optional<JobDone> done = lastDone(events);
        QVERIFY2(done && done->status == JobStatus::Completed && done->summary == (JobSummary{1, 0, 0}),
                 qPrintable(eventsText(events)));
        QCOMPARE(namesIn(outDir), QStringList{*one.expectedFileName});
        QCOMPARE(namesIn(outParent), QStringList{name});
        QCOMPARE(namesIn(workParent), QStringList{name});   // no folder such as `100% vid00000001 한글 폴더` next to it
        QCOMPARE(namesIn(tempRoot), QStringList());
        QCOMPARE(namesIn(windowsPath(dir.path())), (QStringList{u"out"_s, u"work"_s}));
    }

    // Q2b hand-over "overlapping puts": two items with one file name at the same time, through the real LocalFolderSink.
    void twoItemsWithTheSameNameAtTheSameTimeLeaveOneFile() {
        for (const bool overwrite : {false, true}) {
            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            const QString tempRoot = windowsPath(dir.filePath(u"work"_s));
            const QString outDir = windowsPath(dir.filePath(u"out"_s));
            // The two yt-dlp runs wait for each other before they start (an event, see ChildToolRunner), so both items
            // have passed the job's `exists` check and are downloading at once; both then sleep 300 ms after writing, so
            // the two puts come close together.
            ChildToolRunner runner(300, std::nullopt, 2);
            Service service(runner, tempRoot);
            LocalFolderSink sink(outDir);
            const QList<ResolvedItem> items{item(1, std::nullopt, u"Same"_s, u"Song"_s), item(2, std::nullopt, u"Same"_s, u"Song"_s)};

            const QList<JobEvent> events = runToEnd(service, request(items, sink, 2, overwrite, false));
            if (QTest::currentTestFailed()) return;

            const std::optional<JobDone> done = lastDone(events);
            const JobSummary expected = overwrite ? JobSummary{2, 0, 0} : JobSummary{1, 1, 0};
            QVERIFY2(done && done->status == JobStatus::Completed && done->summary == expected,
                     qPrintable(u"overwrite=%1: "_s.arg(overwrite) + eventsText(events)));
            QVERIFY2(bothStartedBeforeEitherEnded(events), qPrintable(u"overwrite=%1: "_s.arg(overwrite) + eventsText(events)));
            const QString fileName = xgs::filename::format(1, u"Same"_s, u"Song"_s, false);
            QCOMPARE(namesIn(outDir), QStringList{fileName});
            QCOMPARE(namesIn(tempRoot), QStringList());
        }
    }

    // Q2b hand-over: every item has a work folder of its own, so one video twice in a job (two ranks) does not collide.
    void aSameVideoTwiceInOneJobDoesNotCollide() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString tempRoot = windowsPath(dir.filePath(u"work"_s));
        const QString outDir = windowsPath(dir.filePath(u"out"_s));
        ChildToolRunner runner(300, std::nullopt, 2);   // the two yt-dlp runs wait for each other: the downloads overlap
        Service service(runner, tempRoot);
        LocalFolderSink sink(outDir);
        const QList<ResolvedItem> items{item(1, u"sameVideo01"_s), item(2, u"sameVideo01"_s)};

        const QList<JobEvent> events = runToEnd(service, request(items, sink, 2));
        if (QTest::currentTestFailed()) return;

        const std::optional<JobDone> done = lastDone(events);
        QVERIFY2(done && done->status == JobStatus::Completed && done->summary == (JobSummary{2, 0, 0}),
                 qPrintable(eventsText(events)));
        QVERIFY2(bothStartedBeforeEitherEnded(events), qPrintable(eventsText(events)));
        QStringList expected{*items[0].expectedFileName, *items[1].expectedFileName};
        std::sort(expected.begin(), expected.end());
        QCOMPARE(namesIn(outDir), expected);
        QCOMPARE(namesIn(tempRoot), QStringList());
    }
};

QTEST_APPLESS_MAIN(JobWithRealProcessesTest)
#include "tst_job_with_real_processes.moc"
