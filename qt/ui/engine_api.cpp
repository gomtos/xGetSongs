#include "engine_api.h"

#include <QFileInfo>
#include <QMetaObject>

#include <exception>
#include <variant>

#include "app_state_holder.h"
#include "filename/filename_formatter.h"
#include "job/download_concurrency.h"
#include "paths/paths.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::ui {

namespace {

/// java.nio's Path.isAbsolute on Windows for a result of paths::fromText: a drive with its root (`C:\`) or a UNC root. A
/// drive-relative path (`C:x`) and the root of the current drive (`\x`) are not absolute.
bool isAbsolute(const QString& path) {
    if (path.startsWith(u"\\\\"_s)) return true;
    if (path.size() < 3) return false;
    const char16_t drive = path.at(0).unicode();
    const bool letter = (drive >= u'A' && drive <= u'Z') || (drive >= u'a' && drive <= u'z');
    return letter && path.at(1) == u':' && path.at(2) == u'\\';
}

/// What an exception on a worker thread says (it would otherwise leave the thread).
QString exceptionText(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return failureMessage(QString::fromUtf8(e.what()));
    } catch (...) {
        return failureMessage(u"알 수 없는 오류"_s);
    }
}

}  // namespace

/// One job: the sink it saves into, its handle, and the thread that reads its events. The reader is declared last so that it is
/// joined before the handle (which cancels and joins the job) and the sink (which the job uses) go.
struct EngineApi::RunningJob {
    JobCallbacks callbacks;
    std::unique_ptr<output::LocalFolderSink> sink;
    std::unique_ptr<job::JobHandle> handle;
    std::jthread reader;
};

EngineApi::EngineApi(process::ProcessRunner& runner, tools::ToolManager& tools, ytdlp::YtDlpResolver& resolver,
                     job::DownloadService& downloads, QString tempRoot, QObject* parent)
    : QObject(parent), m_runner(runner), m_tools(tools), m_resolver(resolver), m_downloads(downloads), m_tempRoot(std::move(tempRoot)) {}

EngineApi::~EngineApi() {
    stop();
}

QString EngineApi::refusedWhileDownloading() {
    return u"다운로드 중에는 yt-dlp를 설치하거나 업데이트할 수 없습니다. 다운로드가 끝난 뒤 다시 시도하세요."_s;
}

void EngineApi::post(std::function<void()> call) {
    QMetaObject::invokeMethod(this, std::move(call), Qt::QueuedConnection);
}

template <class T>
void EngineApi::runWorker(std::function<T(std::stop_token)> work, std::function<void(T)> deliver) {
    const std::uint64_t id = m_nextId++;
    std::jthread worker([this, id, work = std::move(work), deliver = std::move(deliver), stop = m_stop.get_token()] {
        // Nothing may leave the thread: a failure of the work is its answer, and a failure to post the answer (memory) leaves the
        // worker to stop(), which joins it.
        try {
            std::optional<T> result;
            try {
                result = work(stop);
            } catch (...) {
                result = T::failed(exceptionText(std::current_exception()));
            }
            post([this, id, deliver, answer = std::move(*result)] {
                if (m_stopped) return;  // stop() has joined the worker already
                auto node = m_workers.extract(id);
                if (!node.empty()) node.mapped().join();  // the worker's last step was the post that brought us here
                deliver(answer);
            });
        } catch (...) {
        }
    });
    m_workers.emplace(id, std::move(worker));
}

void EngineApi::tools(std::function<void(Outcome<ToolsStatus>)> done) {
    if (m_stopped) return;
    runWorker<Outcome<ToolsStatus>>([this](std::stop_token stop) { return Outcome<ToolsStatus>::ok(m_tools.status(stop)); },
                                    std::move(done));
}

void EngineApi::installYtDlp(std::function<void(Outcome<QString>)> done) {
    toolAction(std::move(done), [this](std::stop_token stop) { return m_tools.installYtDlp(stop); });
}

void EngineApi::updateYtDlp(std::function<void(Outcome<QString>)> done) {
    toolAction(std::move(done), [this](std::stop_token stop) { return m_tools.updateYtDlp(stop); });
}

void EngineApi::toolAction(std::function<void(Outcome<QString>)> done, std::function<Outcome<QString>(std::stop_token)> action) {
    if (m_stopped) return;
    if (aJobRuns()) {
        post([this, done = std::move(done)] {
            if (!m_stopped) done(Outcome<QString>::failed(refusedWhileDownloading()));
        });
        return;
    }
    runWorker<Outcome<QString>>(
        [this, action = std::move(action)](std::stop_token stop) {
            const std::lock_guard lock(m_toolLock);  // an install waits for the one before it (which a stop ends promptly)
            if (stop.stop_requested()) return Outcome<QString>::cancelled();
            return action(stop);
        },
        std::move(done));
}

void EngineApi::resolve(const QString& input, std::function<void(Outcome<ResolveResult>)> done) {
    if (m_stopped) return;
    runWorker<Outcome<ResolveResult>>([this, input](std::stop_token stop) { return m_resolver.resolve(input, stop); },
                                      std::move(done));
}

bool EngineApi::aJobRuns() const {
    for (const auto& [id, job] : m_jobs) {
        if (!job->handle->finished()) return true;
    }
    return false;
}

void EngineApi::refuse(const JobCallbacks& callbacks, const QString& message) {
    post([this, started = callbacks.started, message] {
        if (!m_stopped) started(message);
    });
}

void EngineApi::startJob(JobRequest request, JobCallbacks callbacks) {
    if (m_stopped) return;
    const JobOptions& options = request.options;
    if (!request.isPlaylist && (options.singleRank < filename::MinRank || options.singleRank > filename::MaxRank)) {
        refuse(callbacks, u"순위 번호는 1~999 사이여야 합니다."_s);
        return;
    }
    // A playlist goes into a folder named after it and its title is the album of its files (it wins over a video's own album in
    // the engine); a single video goes straight into the output folder. The album is the original title, not the sanitized
    // folder name. An album name the user typed wins over both of these, for a single video too: it is the album of every file
    // and the name of their folder.
    const std::optional<QString> folder = filename::destinationFolder(request.isPlaylist, request.playlistTitle, options.albumName);
    const std::optional<QString> album = request.isPlaylist ? request.playlistTitle : std::nullopt;
    std::optional<QString> albumOverride;
    if (options.albumName && !kt::trim(*options.albumName).isEmpty()) albumOverride = kt::trim(*options.albumName);

    if (!options.outputDir || kt::isBlank(*options.outputDir)) {
        refuse(callbacks, u"출력 폴더를 지정하세요."_s);
        return;
    }
    const std::optional<QString> path = paths::fromText(*options.outputDir);
    if (!path) {
        refuse(callbacks, u"출력 폴더 경로가 올바르지 않습니다."_s);
        return;
    }
    if (!isAbsolute(*path)) {
        refuse(callbacks, u"출력 폴더는 절대 경로여야 합니다."_s);
        return;
    }
    const std::optional<QString> target = folder ? paths::child(*path, *folder) : path;
    if (!target) {
        refuse(callbacks, u"출력 폴더 경로가 올바르지 않습니다."_s);
        return;
    }
    auto sink = std::make_unique<output::LocalFolderSink>(*target);
    if (!QFileInfo(*target).isDir()) {
        // Kotlin's LocalFolderSink threw here and the server added the exception's message, which for a folder that cannot be
        // made is the path; the C++ sink does not throw, so the path is added here.
        refuse(callbacks, u"출력 폴더를 만들 수 없습니다: "_s + *target);
        return;
    }
    if (request.items.isEmpty()) {
        refuse(callbacks, u"다운로드할 항목이 없습니다."_s);
        return;
    }

    auto running = std::make_unique<RunningJob>();
    running->callbacks = std::move(callbacks);
    running->sink = std::move(sink);
    // The engine decides how many items run at once: it is the machine that does the work.
    job::DownloadRequest download{request.items,  running->sink.get(), options.overwrite,          job::automaticConcurrency(),
                                  album,          options.includeRank, options.searchLyricsOnline, albumOverride};
    running->handle = m_downloads.start(std::move(download), std::stop_source());
    const std::uint64_t id = m_nextId++;
    job::JobHandle* const handle = running->handle.get();
    // Kept before its reader starts: if the reader cannot be started, taking the job out again cancels and joins it (the handle's
    // destructor), and no reader of an uncancelled job is left for the GUI thread to wait for.
    RunningJob& job = *m_jobs.emplace(id, std::move(running)).first->second;
    try {
        // The job's one reader: it says the job started, hands over every event up to the JobDone in order, waits for the job's
        // threads, and says the events are over. Nothing may leave the thread (a post that fails for lack of memory ends it;
        // the job then stays until stop()).
        job.reader = std::jthread([this, id, handle] {
            try {
                post([this, id] {
                    if (m_stopped) return;
                    const auto it = m_jobs.find(id);
                    if (it == m_jobs.end()) return;
                    const auto started = it->second->callbacks.started;
                    started(std::nullopt);
                });
                for (;;) {
                    std::optional<JobEvent> event = handle->nextEvent();
                    if (!event) break;
                    const bool last = std::holds_alternative<JobDone>(*event);
                    post([this, id, event = std::move(*event)] {
                        if (m_stopped) return;
                        const auto it = m_jobs.find(id);
                        if (it == m_jobs.end()) return;
                        const auto deliver = it->second->callbacks.event;  // a copy: the callback may call stop()
                        deliver(event);
                    });
                    if (last) break;
                }
                handle->join();
                post([this, id] { finishJob(id); });
            } catch (...) {
            }
        });
    } catch (...) {
        m_jobs.erase(id);
        throw;
    }
    m_lastJob = id;
}

void EngineApi::finishJob(std::uint64_t id) {
    if (m_stopped) return;
    auto node = m_jobs.extract(id);
    if (node.empty()) return;
    const auto ended = node.mapped()->callbacks.ended;
    node.mapped().reset();  // joins the reader (whose last step brought us here), then the handle and the sink go
    ended(std::nullopt);
}

void EngineApi::cancelJob() {
    if (m_stopped) return;
    const auto it = m_jobs.find(m_lastJob);
    if (it != m_jobs.end()) it->second->handle->cancel();
}

void EngineApi::stop() {
    if (m_stopped) return;
    m_stopped = true;
    m_stop.request_stop();
    for (const auto& [id, running] : m_jobs) running->handle->cancel();
    m_jobs.clear();     // per job: the reader (which ends with the JobDone), the handle (which joins the job), the sink
    m_workers.clear();  // each worker sees m_stop and returns promptly
}

}  // namespace xgs::ui
