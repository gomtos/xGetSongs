#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include "app_api.h"
#include "diag/log_sink.h"
#include "job/download_service.h"
#include "output/local_folder_sink.h"
#include "tools/tool_manager.h"
#include "ytdlp/ytdlp_resolver.h"

// The AppApi of the app: the blocking calls of qt/core on std::jthreads of their own, each answer handed to the thread that made
// the EngineApi (the GUI thread) by a queued call. It stands where the Kotlin server's routes stood (server/.../Application.kt):
// startJob checks a request and builds the DownloadRequest as `POST /jobs` did, and one reader thread per job takes its events
// with nextEvent until the JobDone (the job's queue has one reader) and hands them over in order.
//
// Rules from the hand-over of the engine (docs/superpowers/specs/2026-11-21-qt-engine-job-report.md, "Q3·Q4에 넘길 것"):
//  - installYtDlp and updateYtDlp are refused while a job runs (yt-dlp would be replaced under it), and installs and updates run
//    one at a time;
//  - after stop() no answer is delivered (a ToolsStatus obtained after a stop looks like "no tools");
//  - teardown: stop() (or the destructor) cancels and waits for every job handle and thread; the EngineApi must go before the
//    engine objects it references (service, downloader, providers and runner, HTTP client), which go in that order.
//
// The job log (Kotlin: the `jobLog` lines of Application.kt, words from diag::JobLog) goes to jobLog under diag::JobLog::LOGGER_NAME,
// each line tagged `[<short id>]`: the start (on the GUI thread, once the job runs), each event that JobLog::describe logs (on the
// job's reader thread, as it takes them), a cancel of a job the bridge holds, and an event stream that ended without a JobDone
// while the bridge was not stopping. Every job gets a random id (a UUID, as the server's registry gave).
namespace xgs::ui {

class EngineApi final : public QObject, public AppApi {
    Q_OBJECT
public:
    /// Every engine object is referenced, not owned, and must outlive the EngineApi. jobLog (any thread; empty: no job log) gets
    /// the job lines.
    EngineApi(tools::ToolManager& tools, ytdlp::YtDlpResolver& resolver, job::DownloadService& downloads, diag::LogSink jobLog = {},
              QObject* parent = nullptr);
    /// stop().
    ~EngineApi() override;

    /// The Failed text of installYtDlp and updateYtDlp while a job runs.
    static QString refusedWhileDownloading();

    void tools(std::function<void(Outcome<ToolsStatus>)> done) override;
    void installYtDlp(std::function<void(Outcome<QString>)> done) override;
    void updateYtDlp(std::function<void(Outcome<QString>)> done) override;
    void resolve(const QString& input, std::function<void(Outcome<ResolveResult>)> done) override;
    /// Refuses (started with the message) what `POST /jobs` refused, with its texts: a single video's rank outside 1..999, no
    /// output folder, one that is not a path or not absolute, one that cannot be made, no item. Otherwise the job runs with a
    /// LocalFolderSink on the destination folder and job::automaticConcurrency(). Throws what DownloadService::start throws.
    void startJob(JobRequest request, JobCallbacks callbacks) override;
    void cancelJob() override;
    void stop() override;
    /// The jobs the bridge holds (running ones and finished ones whose end has not been delivered yet): the server's
    /// `runningJobs` for the exit record. GUI thread.
    int jobCount() const;

private:
    struct RunningJob;

    /// Runs work on a thread of its own; deliver (on the GUI thread, unless stopped) gets what work returned.
    template <class T>
    void runWorker(std::function<T(std::stop_token)> work, std::function<void(T)> deliver);
    void post(std::function<void()> call);
    void refuse(const JobCallbacks& callbacks, const QString& message);
    void toolAction(std::function<void(Outcome<QString>)> done, std::function<Outcome<QString>(std::stop_token)> action);
    bool aJobRuns() const;
    void finishJob(std::uint64_t id);
    void logJob(diag::Level level, const QString& jobId, const QString& text) const;

    tools::ToolManager& m_tools;
    ytdlp::YtDlpResolver& m_resolver;
    job::DownloadService& m_downloads;
    const diag::LogSink m_jobLog;

    // GUI thread only
    bool m_stopped = false;
    std::uint64_t m_nextId = 1;
    std::uint64_t m_lastJob = 0;
    std::map<std::uint64_t, std::jthread> m_workers;
    std::map<std::uint64_t, std::unique_ptr<RunningJob>> m_jobs;

    // any thread
    std::stop_source m_stop;
    std::mutex m_toolLock;  // one install or update at a time
};

}  // namespace xgs::ui
