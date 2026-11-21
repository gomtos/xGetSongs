#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <windows.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <vector>

#include "ffprobe.h"
#include "fs/file_ops.h"
#include "helpers/own_processes.h"
#include "integration_support.h"
#include "job/default_download_service.h"
#include "job/download_service.h"
#include "job/item_downloader.h"
#include "lyrics/lrclib_lyrics_provider.h"
#include "net/qt_http_client.h"
#include "output/local_folder_sink.h"
#include "paths/paths.h"
#include "process/system_process_runner.h"
#include "tools/tool_locator.h"
#include "ytdlp/ytdlp_resolver.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::ItemDone;
using xgs::ItemFailed;
using xgs::ItemSkipped;
using xgs::ItemStarted;
using xgs::JobDone;
using xgs::JobEvent;
using xgs::JobStatus;
using xgs::Progress;
using xgs::Stage;
using xgs::integration::Deadline;
using xgs::integration::Ffprobe;

namespace {

/// The names in a directory, sorted.
QStringList namesIn(const QString& dir) {
    QStringList names = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Unsorted);
    std::sort(names.begin(), names.end());
    return names;
}

/// The events of a job without any text a service or a tool returned (file names, skip reasons and failure messages can hold
/// the song's name): kinds, ranks, stages, text lengths and the end status only. Kotlin puts `events.toString()` into its failure messages; here they must not carry a song name.
QString eventsShape(const QList<JobEvent>& events) {
    QStringList parts;
    for (const JobEvent& event : events) {
        if (const auto* e = std::get_if<ItemStarted>(&event)) parts << u"ItemStarted(%1)"_s.arg(e->rank);
        else if (const auto* p = std::get_if<Progress>(&event))
            parts << u"Progress(%1, %2)"_s.arg(p->rank).arg(p->stage == Stage::Downloading ? u"Downloading"_s : u"Finishing"_s);
        else if (const auto* d = std::get_if<ItemDone>(&event)) parts << u"ItemDone(%1)"_s.arg(d->rank);
        // A skip reason or a failure message can hold tool stderr or a path with the song's name: only its length is shown.
        else if (const auto* s = std::get_if<ItemSkipped>(&event))
            parts << u"ItemSkipped(%1, reason of %2 characters)"_s.arg(s->rank).arg(s->reason.size());
        else if (const auto* f = std::get_if<ItemFailed>(&event))
            parts << u"ItemFailed(%1, message of %2 characters)"_s.arg(f->rank).arg(f->message.size());
        else {
            const auto& j = std::get<JobDone>(event);
            parts << u"JobDone(%1, %2/%3/%4)"_s.arg(int(j.status)).arg(j.summary.succeeded).arg(j.summary.skipped).arg(j.summary.failed);
        }
    }
    return u'[' + parts.join(u", "_s) + u']';
}

QString shown(const std::optional<QString>& value) { return value ? u'"' + *value + u'"' : u"null"_s; }

QString stageName(Stage stage) { return stage == Stage::Downloading ? u"DOWNLOADING"_s : u"FINISHING"_s; }

}  // namespace

// The real-download integration tests of the download job (Q2d). They talk to the real YouTube through the real yt-dlp and
// ffmpeg in %APPDATA%\xGetSongs\bin (run only, never written to) and download the audio of a 19-second video (about 300 KB),
// so they run only when both XGS_INTEGRATION=1 and XGS_INTEGRATION_AUDIO=1 are set, and skip otherwise (and, like Kotlin's
// assumeTrue, when a tool is missing). As in tst_real_ytdlp, XDG_CACHE_HOME, TEMP and TMP point into one QTemporaryDir for the
// whole class, so everything yt-dlp writes stays in it; the old values are put back at the end.
//
// Nothing a service returned is printed: no lyrics (only how many lyrics tags there are), and no song or file name (the
// Kotlin test prints the file name and the tags; here only sizes, tag keys, stages and streams are printed).
class RealDownloadIntegrationTest : public QObject {
    Q_OBJECT

    xgs::process::SystemProcessRunner m_runner;
    std::unique_ptr<xgs::tools::ToolLocator> m_locator;
    std::unique_ptr<QTemporaryDir> m_sandbox;
    struct SavedVariable {
        const wchar_t* name;
        std::optional<std::wstring> value;
    };
    std::vector<SavedVariable> m_saved;

    static bool enabled() {
        return xgs::integration::enabled() && qEnvironmentVariable("XGS_INTEGRATION_AUDIO") == u"1"_s;
    }

    void setVariable(const wchar_t* name, const QString& value) {
        const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
        std::optional<std::wstring> old;
        if (length > 0) {
            std::wstring buffer(length, L'\0');
            buffer.resize(GetEnvironmentVariableW(name, buffer.data(), length));
            old = buffer;
        }
        m_saved.push_back({name, old});
        QVERIFY(SetEnvironmentVariableW(name, reinterpret_cast<const wchar_t*>(value.utf16())));
    }

    /// A new temporary directory inside the sandbox (Kotlin's Files.createTempDirectory(prefix)).
    std::unique_ptr<QTemporaryDir> tempDir(const QString& prefix) {
        auto dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + u'/' + prefix + u"-XXXXXX"_s);
        return dir->isValid() ? std::move(dir) : nullptr;
    }

private slots:
    void initTestCase() {
        m_locator = std::make_unique<xgs::tools::ToolLocator>(xgs::integration::appBinDir());
        if (!enabled()) return;   // every test skips in init(); nothing is set up
        m_sandbox = std::make_unique<QTemporaryDir>(QDir::tempPath() + u"/xgs-download-integration-XXXXXX"_s);
        QVERIFY(m_sandbox->isValid());
        const QString root = xgs::integration::windowsPath(m_sandbox->path());
        const QString cache = *xgs::paths::child(root, u"cache"_s);
        const QString temp = *xgs::paths::child(root, u"tmp"_s);
        QVERIFY(xgs::fs::makeDirectories(cache) && xgs::fs::makeDirectories(temp));
        XGS_CHECKED(setVariable(L"XDG_CACHE_HOME", cache));
        XGS_CHECKED(setVariable(L"TEMP", temp));
        XGS_CHECKED(setVariable(L"TMP", temp));
    }

    void cleanupTestCase() {
        for (auto it = m_saved.rbegin(); it != m_saved.rend(); ++it) {
            SetEnvironmentVariableW(it->name, it->value ? it->value->c_str() : nullptr);
        }
        m_saved.clear();
        m_sandbox.reset();
    }

    // Kotlin's @BeforeTest requireTools, after the two variables: a QSKIP here skips the test function.
    void init() {
        if (!enabled()) {
            QSKIP("downloads the audio of a 19-second video: set XGS_INTEGRATION=1 and XGS_INTEGRATION_AUDIO=1 to run it");
        }
        const xgs::ToolPaths tools = m_locator->current();
        if (!(tools.ytDlp && tools.ffmpeg && tools.jsRuntime)) QSKIP("yt-dlp, ffmpeg and Node 22+/Deno 2.3+ must be installed");
    }

    // Port of RealYtDlpIntegrationTest.downloadsAShortVideoAsM4aWithoutReencoding (every assertion of it).
    void downloadsAShortVideoAsM4aWithoutReencoding() {
        const std::optional<QString> ffprobePath = Ffprobe::besides(m_locator->current().ffmpeg);
        if (!ffprobePath) QSKIP("ffprobe must be installed next to ffmpeg");
        const Ffprobe ffprobe(*ffprobePath, m_runner);
        const Deadline deadline(180s);
        // "Me at the zoo": the first video ever uploaded to YouTube, 19 seconds long.
        xgs::ytdlp::YtDlpResolver resolver(m_runner, *m_locator);
        const xgs::Outcome<xgs::ResolveResult> resolved =
            resolver.resolve(u"https://www.youtube.com/watch?v=jNQXAC9IVRw"_s, deadline.token());
        QVERIFY2(resolved.isOk(), qPrintable(resolved.status == xgs::OutcomeStatus::Cancelled ? u"cancelled (timeout)"_s
                                                                                             : u"failed: "_s + resolved.error));
        const auto temp = tempDir(u"xgs-integration"_s);
        QVERIFY(temp);
        const QString root = xgs::integration::windowsPath(temp->path());
        const QString outDir = *xgs::paths::child(root, u"out"_s);

        // The lyrics lookup is on and the real LRCLIB is asked: this video has no lyrics in its description and none may be
        // found anywhere else, so the file must come out without any lyrics tag.
        xgs::net::QtHttpClient http;
        xgs::lyrics::LrclibLyricsProvider lyrics(http);
        xgs::job::ItemDownloader downloader(m_runner, *m_locator, resolver, lyrics);
        xgs::job::DefaultDownloadService service(downloader, *xgs::paths::child(root, u"work"_s));
        xgs::output::LocalFolderSink sink(outDir);
        xgs::job::DownloadRequest request;
        request.items = resolved.value->items;
        request.sink = &sink;
        request.overwrite = false;
        request.concurrency = 1;
        request.album = u"Test Album"_s;
        request.searchLyricsOnline = true;
        // Kotlin's withTimeout: the deadline cancels the job.
        std::stop_source jobStop;
        const std::stop_callback onTimeout(deadline.token(), [&jobStop] { jobStop.request_stop(); });
        const std::unique_ptr<xgs::job::JobHandle> handle = service.start(std::move(request), jobStop);
        QList<JobEvent> events;
        while (std::optional<JobEvent> event = handle->nextEvent()) events.append(*event);
        QVERIFY2(!deadline.expired(), qPrintable(u"timed out: "_s + eventsShape(events)));

        QVERIFY2(!events.isEmpty() && std::holds_alternative<JobDone>(events.last()), qPrintable(eventsShape(events)));
        const JobDone done = std::get<JobDone>(events.last());
        QVERIFY2(done.status == JobStatus::Completed, qPrintable(eventsShape(events)));
        QVERIFY2(done.summary.succeeded == 1, qPrintable(eventsShape(events)));
        const QStringList files = namesIn(outDir);
        QVERIFY2(files.size() == 1, qPrintable(u"files in the output folder: "_s + QString::number(files.size())));
        const QString file = *xgs::paths::child(outDir, files.first());
        const qint64 size = QFileInfo(file).size();
        qInfo().noquote() << u"downloaded: one file (%1 bytes)"_s.arg(size);
        const QRegularExpression name(QRegularExpression::anchoredPattern(u"001 .+ - .+[.]m4a"_s));
        QVERIFY2(name.match(files.first()).hasMatch(), "the file name is not `001 <artist> - <title>.m4a`");
        QVERIFY(size > 50'000);

        QList<Stage> stages;
        for (const JobEvent& event : events) {
            if (const auto* progress = std::get_if<Progress>(&event)) stages << progress->stage;
        }
        QStringList stageNames, folded;
        for (const Stage stage : stages) {
            stageNames << stageName(stage);
            if (folded.isEmpty() || folded.last() != stageName(stage)) folded << stageName(stage);
        }
        qInfo().noquote() << u"reported stages: ["_s + folded.join(u", "_s) + u']';
        QVERIFY2(stages.contains(Stage::Downloading), qPrintable(u"no download progress was reported: "_s + stageNames.join(u", "_s)));
        QVERIFY2(!stages.isEmpty() && stages.last() == Stage::Finishing,
                 qPrintable(u"the finishing stage comes last: "_s + stageNames.join(u", "_s)));
        QVERIFY2(stages.count(Stage::Finishing) == 1, qPrintable(stageNames.join(u", "_s)));

        xgs::integration::Probed probed;
        try {
            probed = ffprobe.probe(file, deadline.token());
        } catch (const std::exception& e) {
            QFAIL(e.what());
        }
        const QStringList keys = probed.tags.keys();
        QStringList shownKeys;
        qsizetype lyricsTags = 0;
        for (const QString& key : keys) {
            if (key.startsWith(u"lyrics"_s)) ++lyricsTags;
            else shownKeys << key;
        }
        // Never print a lyrics tag, nor the values of the other tags (the title and the artist come from the service).
        qInfo().noquote() << u"tag keys: [%1]; lyrics tags: %2; streams: %3"_s.arg(shownKeys.join(u", "_s))
                                 .arg(lyricsTags)
                                 .arg(xgs::integration::describe(probed.streams));
        QVERIFY2(lyricsTags == 0, qPrintable(u"the video has no lyrics anywhere; tag keys: "_s + keys.join(u", "_s)));
        QCOMPARE(shown(probed.tag(u"major_brand"_s)), shown(u"M4A "_s));
        QVERIFY2(probed.tag(u"title"_s) == u"Me at the zoo"_s, "the title tag is not the video's title");
        QVERIFY2(probed.tag(u"artist"_s) == u"jawed"_s, "the artist tag is not the video's uploader");
        QCOMPARE(shown(probed.tag(u"album_artist"_s)), shown(u"Various Artists"_s));
        QCOMPARE(shown(probed.tag(u"album"_s)), shown(u"Test Album"_s));
        QCOMPARE(shown(probed.tag(u"track"_s)), shown(u"1"_s));
        QVERIFY2(probed.tag(u"comment"_s).value_or(QString()).startsWith(u"https://www.youtube.com/watch?v="_s),
                 "the comment tag is not the video's address");
        QStringList codecTypes;
        for (const xgs::integration::ProbedStream& stream : probed.streams) codecTypes << stream.codecType;
        std::sort(codecTypes.begin(), codecTypes.end());
        QVERIFY2(codecTypes == (QStringList{u"audio"_s, u"video"_s}), qPrintable(xgs::integration::describe(probed.streams)));
        const auto audio = std::find_if(probed.streams.cbegin(), probed.streams.cend(),
                                        [](const xgs::integration::ProbedStream& s) { return s.codecType == u"audio"_s; });
        const auto cover = std::find_if(probed.streams.cbegin(), probed.streams.cend(),
                                        [](const xgs::integration::ProbedStream& s) { return s.codecType == u"video"_s; });
        QVERIFY2(audio->codecName == u"aac"_s, "YouTube's AAC stream is copied as it is");
        QVERIFY2(cover->attachedPic, "the picture must be an attached cover");
        QVERIFY(cover->width.has_value());
        QVERIFY2(cover->width == cover->height, "the cover is cropped to a square");
    }

    // Not in Kotlin. Cancelling while the real yt-dlp downloads ends the job as Cancelled, leaves no process of that job behind
    // (no process created after the job started whose command line holds this test's work folder: yt-dlp, the copy of itself
    // the one-file yt-dlp.exe starts, an ffmpeg it starts), and removes the job's work folder. Only processes of this test are
    // looked at, and none is killed by the test.
    void cancellingARealDownloadLeavesNoToolProcessBehind() {
        const Deadline deadline(180s);
        xgs::ytdlp::YtDlpResolver resolver(m_runner, *m_locator);
        const xgs::Outcome<xgs::ResolveResult> resolved =
            resolver.resolve(u"https://www.youtube.com/watch?v=jNQXAC9IVRw"_s, deadline.token());
        QVERIFY2(resolved.isOk(), qPrintable(resolved.status == xgs::OutcomeStatus::Cancelled ? u"cancelled (timeout)"_s
                                                                                             : u"failed: "_s + resolved.error));
        const auto temp = tempDir(u"xgs-download-cancel"_s);
        QVERIFY(temp);
        const QString root = xgs::integration::windowsPath(temp->path());
        const QString token = QFileInfo(temp->path()).fileName();   // the random name of this test's own folder
        const QString workRoot = *xgs::paths::child(root, u"work"_s);
        const QString outDir = *xgs::paths::child(root, u"out"_s);

        // No lyrics lookup: the job is cancelled before it could ask, and LRCLIB is not to be asked for nothing.
        xgs::job::ItemDownloader downloader(m_runner, *m_locator, resolver);
        xgs::job::DefaultDownloadService service(downloader, workRoot);
        xgs::output::LocalFolderSink sink(outDir);
        xgs::job::DownloadRequest request;
        request.items = resolved.value->items;
        request.sink = &sink;
        request.concurrency = 1;
        std::stop_source jobStop;
        const std::stop_callback onTimeout(deadline.token(), [&jobStop] { jobStop.request_stop(); });
        const FILETIME since = xgs::testsupport::nowAsFileTime();
        const std::unique_ptr<xgs::job::JobHandle> handle = service.start(std::move(request), jobStop);

        QList<JobEvent> events;
        bool sawProgress = false;
        qsizetype runningAtCancel = -1;
        while (std::optional<JobEvent> event = handle->nextEvent()) {
            events.append(*event);
            if (!sawProgress && std::holds_alternative<Progress>(*event)) {
                sawProgress = true;
                // The first progress event comes from a line yt-dlp wrote, so yt-dlp is running now.
                runningAtCancel = xgs::testsupport::processesMentioning(token, since).size();
                handle->cancel();
            }
        }
        QVERIFY2(!deadline.expired(), qPrintable(u"timed out: "_s + eventsShape(events)));
        QVERIFY2(!events.isEmpty() && std::holds_alternative<JobDone>(events.last()), qPrintable(eventsShape(events)));
        const JobStatus status = std::get<JobDone>(events.last()).status;
        // Author decision 3: a cancel that comes after the last item finished may end the job as Completed. That says nothing
        // about the cancel; the source is too short for this machine and connection.
        if (status == JobStatus::Completed) {
            QFAIL(qPrintable(u"the download finished before the cancel took hold, so nothing was cancelled; a longer source is "
                             u"needed (a progress event was %1seen): "_s.arg(sawProgress ? u""_s : u"not "_s) +
                             eventsShape(events)));
        }
        QVERIFY2(sawProgress, qPrintable(u"no progress event was seen, so the job was never cancelled: "_s + eventsShape(events)));
        QVERIFY2(runningAtCancel >= 1,
                 qPrintable(u"no running process of the job was seen at the first progress event: "_s + eventsShape(events)));
        QVERIFY2(status == JobStatus::Cancelled, qPrintable(eventsShape(events)));
        // The runner kills each run's Job Object before it returns; 10 s is only a bound for Windows to finish them.
        const QList<xgs::testsupport::SeenProcess> left = xgs::testsupport::waitForNoneMentioning(token, since, 10'000);
        QStringList leftImages;
        for (const xgs::testsupport::SeenProcess& process : left) {
            leftImages << u"%1 (%2)"_s.arg(QFileInfo(process.image).fileName()).arg(process.pid);
        }
        qInfo().noquote() << u"processes of the job running at the cancel: %1; left after it: %2"_s.arg(runningAtCancel).arg(left.size());
        QVERIFY2(left.isEmpty(), qPrintable(u"left behind: "_s + leftImages.join(u", "_s)));
        QVERIFY2(namesIn(workRoot).isEmpty(), qPrintable(u"the work folder still holds: "_s + namesIn(workRoot).join(u", "_s)));
    }
};

QTEST_GUILESS_MAIN(RealDownloadIntegrationTest)
#include "tst_real_download.moc"
