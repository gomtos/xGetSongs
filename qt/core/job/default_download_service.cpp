#include "job/default_download_service.h"

#include <QDir>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

#include "fs/file_ops.h"
#include "paths/paths.h"
#include "text/kt_text.h"

namespace xgs::job {

namespace {

using namespace Qt::StringLiterals;
using output::PutResult;
using output::PutStatus;
using ytdlp::FailureKind;

const QString kAlreadyExists = u"이미 존재"_s;
const QString kNoPermission = u"출력 폴더에 쓸 권한이 없습니다."_s;
const QString kDiskFull = u"디스크 공간이 부족합니다."_s;
const QString kFileError = u"파일 처리 중 오류: "_s;
const QString kUnknownError = u"알 수 없는 오류"_s;

constexpr quint32 kErrorAccessDenied = 5;      // ERROR_ACCESS_DENIED
constexpr quint32 kErrorHandleDiskFull = 39;   // ERROR_HANDLE_DISK_FULL
constexpr quint32 kErrorDiskFull = 112;        // ERROR_DISK_FULL

const QStringView kDiskFullMarkers[] = {u"no space left", u"not enough space", u"disk full", u"공간이 부족"};

// Kotlin's isDiskFull reads the reason of a FileSystemException or the message of any other IOException; the C++ results carry
// that text alone (and, from Windows, the error code).
bool isDiskFull(const QString& text) {
    const QString lowered = kt::lowercase(text);
    return std::any_of(std::begin(kDiskFullMarkers), std::end(kDiskFullMarkers),
                       [&](QStringView marker) { return lowered.contains(marker); });
}

bool isDiskFull(const PutResult& result) {
    return result.code == kErrorDiskFull || result.code == kErrorHandleDiskFull || isDiskFull(result.error);
}

// Waits for delay unless stop comes first. true when the whole delay passed without a stop.
bool waitUnlessStopped(std::chrono::milliseconds delay, std::stop_token stop) {
    std::mutex mutex;
    std::condition_variable_any never;
    std::unique_lock lock(mutex);
    never.wait_for(lock, stop, delay, [] { return false; });
    return !stop.stop_requested();
}

// `Files.createTempDirectory(tempRoot, "job-")` after `Files.createDirectories(tempRoot)`: a new folder whose name no other job
// has. Nothing when either cannot be made.
std::optional<QString> makeWorkRoot(const QString& tempRoot) {
    if (!fs::makeDirectories(tempRoot)) return std::nullopt;
    const std::optional<QString> pattern = paths::child(tempRoot, u"job-XXXXXX"_s);
    if (!pattern) return std::nullopt;
    QTemporaryDir dir(QDir::fromNativeSeparators(*pattern));
    if (!dir.isValid()) return std::nullopt;
    dir.setAutoRemove(false);   // removed by removeWorkRoot, which tries again
    return paths::fromText(QDir::toNativeSeparators(dir.path()));
}

// Kotlin's `deleteRecursively()`, tried again (author decision 12): a process that was just killed, or a virus scanner, can
// hold a file for a moment. The pauses are those of fs::moveFile (1.52 s in all). What is still left after them stays, as in
// Kotlin.
void removeWorkRoot(const QString& workRoot) {
    using namespace std::chrono_literals;
    static constexpr std::chrono::milliseconds kPauses[] = {20ms, 50ms, 100ms, 150ms, 200ms, 200ms, 200ms, 200ms, 200ms, 200ms};
    const auto removed = [&] {
        QDir(QDir::fromNativeSeparators(workRoot)).removeRecursively();
        return !fs::exists(workRoot);
    };
    if (removed()) return;
    for (const std::chrono::milliseconds pause : kPauses) {
        std::this_thread::sleep_for(pause);
        if (removed()) return;
    }
}

// What one job shares between its workers.
struct Job {
    const DownloadRequest& request;
    ItemDownloader& downloader;
    const std::vector<std::chrono::milliseconds>& retryDelays;
    const QString& workRoot;
    JobEvents& events;
    std::stop_source workStop;              // the user's stop (forwarded) or the first fatal failure
    std::atomic<qsizetype> nextItem{0};
    std::atomic<bool> fatal{false};
    std::atomic<int> succeeded{0};
    std::atomic<int> skipped{0};
    std::atomic<int> failed{0};
};

class ItemRun {
public:
    ItemRun(Job& job, const ResolvedItem& item) : m_job(job), m_item(item), m_stop(job.workStop.get_token()) {}

    // Kotlin's processItem. No exception leaves it except one the catch blocks themselves raise (an allocation failure),
    // which the worker swallows.
    void run() {
        try {
            process();
        } catch (const std::exception& e) {
            failUnlessCancelled(QString::fromUtf8(e.what()));
        } catch (...) {
            failUnlessCancelled(kUnknownError);
        }
    }

private:
    void process() {
        const DownloadRequest& request = m_job.request;
        const PreparedItem prepared = m_job.downloader.prepare(m_item, request.album, request.includeRank,
                                                               request.searchLyricsOnline, request.albumOverride, m_stop);
        if (cancelled()) return;
        m_job.events.push(ItemStarted{m_item.rank, m_item.videoId, prepared.fileName});
        if (!request.overwrite && request.sink->exists(prepared.fileName)) {
            if (!cancelled()) skip(kAlreadyExists);
            return;
        }

        const std::optional<QString> itemDir = paths::child(m_job.workRoot, QString::number(m_item.rank));
        quint32 errorCode = 0;
        if (!itemDir || !fs::makeDirectories(*itemDir, errorCode)) {
            // Kotlin's Files.createDirectories throws an IOException: read like a failed put (author decision 5).
            if (!cancelled()) readFailure(PutResult{PutStatus::Failed, fs::errorText(errorCode), errorCode});
            return;
        }

        const Emit emitEvent = [this](const JobEvent& event) {
            if (!m_stop.stop_requested()) m_job.events.push(event);
        };
        for (std::size_t retries = 0;;) {
            const DownloadResult result = m_job.downloader.download(prepared, *itemDir, emitEvent, m_stop);
            if (const auto* downloaded = std::get_if<Downloaded>(&result)) {
                deliver(prepared, *downloaded);
                return;
            }
            if (cancelled()) return;   // DownloadCancelled, or any other ending once the stop is requested
            if (const auto* ioError = std::get_if<DownloadIoError>(&result)) {
                // Kotlin: the runner's IOException (author decision 6).
                if (isDiskFull(ioError->message)) {
                    failFatally(kDiskFull);
                } else {
                    fail(kFileError + ioError->message);
                }
                return;
            }
            const ytdlp::Failure& failure = std::get<DownloadFailed>(result).failure;
            switch (failure.kind) {
            case FailureKind::Unavailable:
                skip(failure.message);
                return;
            case FailureKind::Transient:
                if (retries < m_job.retryDelays.size()) {
                    if (!waitUnlessStopped(m_job.retryDelays[retries], m_stop)) return;
                    ++retries;
                    continue;
                }
                fail(failure.message);
                return;
            case FailureKind::Fatal:
                failFatally(failure.message);
                return;
            case FailureKind::Other:
                fail(failure.message);
                return;
            }
            return;
        }
    }

    // Author decision 10: a stop requested before the put leaves the file where it is; once the put has started its result
    // counts.
    void deliver(const PreparedItem& prepared, const Downloaded& downloaded) {
        if (cancelled()) return;
        m_putStarted = true;
        const DownloadRequest& request = m_job.request;
        const PutResult put = request.sink->put(prepared.fileName, downloaded.file, request.overwrite);
        switch (put.status) {
        case PutStatus::Stored:
            m_job.events.push(ItemDone{m_item.rank, prepared.fileName, downloaded.lyrics});
            ++m_job.succeeded;
            return;
        case PutStatus::AlreadyExists:
            // Free when the item started, taken now: another item of this job with the same name (the rank is not part of
            // it) or another program finished first; the first one to finish wins. Same outcome as finding it taken up front.
            // Otherwise a failure that names the file, never a full disk (Kotlin's FileAlreadyExistsException).
            if (!request.overwrite && request.sink->exists(prepared.fileName)) {
                skip(kAlreadyExists);
            } else {
                fail(kFileError + (put.error.isEmpty() ? prepared.fileName : put.error));
            }
            return;
        case PutStatus::Failed:
            readFailure(put);
            return;
        }
    }

    // Author decision 5: Kotlin's AccessDeniedException, a full disk, any other IOException.
    void readFailure(const PutResult& result) {
        if (result.code == kErrorAccessDenied) {
            failFatally(kNoPermission);
        } else if (isDiskFull(result)) {
            failFatally(kDiskFull);
        } else {
            fail(kFileError + result.error);
        }
    }

    // A stop requested before the put makes the item a cancelled one, which emits nothing more (author decision 2).
    bool cancelled() const { return !m_putStarted && m_stop.stop_requested(); }

    void failUnlessCancelled(const QString& message) {
        if (!cancelled()) fail(message);
    }

    void skip(const QString& reason) {
        m_job.events.push(ItemSkipped{m_item.rank, reason});
        ++m_job.skipped;
    }

    void fail(const QString& message) {
        m_job.events.push(ItemFailed{m_item.rank, message});
        ++m_job.failed;
    }

    // Kotlin's FatalJobException: the item fails, then the job stops every other worker. Only the first one is recorded; the
    // stop of the first makes any later one a cancelled item, except one whose put had already started.
    void failFatally(const QString& message) {
        fail(message);
        m_job.fatal = true;
        m_job.workStop.request_stop();
    }

    Job& m_job;
    const ResolvedItem& m_item;
    std::stop_token m_stop;
    bool m_putStarted = false;
};

// One worker: takes the next item in list order until there is none or the job is stopped.
void work(Job& job) noexcept {
    try {
        const std::stop_token stop = job.workStop.get_token();
        while (!stop.stop_requested()) {
            const qsizetype index = job.nextItem.fetch_add(1);
            if (index >= job.request.items.size()) return;
            ItemRun(job, job.request.items.at(index)).run();
        }
    } catch (...) {
        // Only an allocation failure while reporting an item gets here; the worker ends, the job goes on (no std::terminate).
    }
}

// Runs the workers and returns once every one has ended.
void runWorkers(Job& job) {
    const qsizetype items = job.request.items.size();
    const qsizetype count = std::min<qsizetype>(std::max(job.request.concurrency, 1), items);
    std::vector<std::jthread> workers;
    workers.reserve(static_cast<std::size_t>(count));
    try {
        for (qsizetype i = 0; i < count; ++i) workers.emplace_back([&job] { work(job); });
    } catch (...) {
        job.workStop.request_stop();   // a thread that cannot start: the started ones end, then the job fails
        throw;
    }
}   // the jthreads join here

// The thread of a job: Kotlin's runJob.
void runJob(ItemDownloader& downloader, const QString& tempRoot, const std::vector<std::chrono::milliseconds>& retryDelays,
            const DownloadRequest& request, std::stop_token userStop, JobEvents& events) noexcept {
    JobStatus status = JobStatus::Failed;
    JobSummary summary;
    std::optional<QString> workRoot;
    try {
        if (userStop.stop_requested()) {
            status = JobStatus::Cancelled;   // cancelled before it started: no folder is made
        } else if ((workRoot = makeWorkRoot(tempRoot))) {
            Job job{request, downloader, retryDelays, *workRoot, events};
            {
                const std::stop_callback forward(userStop, [&job] { job.workStop.request_stop(); });
                runWorkers(job);
            }
            summary = JobSummary{job.succeeded.load(), job.skipped.load(), job.failed.load()};
            // Author decision 3: a fatal failure wins over the user's stop.
            status = job.fatal ? JobStatus::Failed : userStop.stop_requested() ? JobStatus::Cancelled : JobStatus::Completed;
        }
        // else: the work folder could not be made; no item was started, so there is no item event (Failed, 0/0/0).
    } catch (...) {
        status = JobStatus::Failed;   // a worker thread that could not start
    }
    try {
        if (workRoot) removeWorkRoot(*workRoot);
    } catch (...) {
    }
    events.push(JobDone{status, summary});
    events.close();
}

}  // namespace

DefaultDownloadService::DefaultDownloadService(ItemDownloader& downloader, QString tempRoot,
                                               std::vector<std::chrono::milliseconds> retryDelays)
    : m_downloader(downloader), m_tempRoot(std::move(tempRoot)), m_retryDelays(std::move(retryDelays)) {}

std::unique_ptr<JobHandle> DefaultDownloadService::start(DownloadRequest request, std::stop_source stop) {
    auto events = std::make_unique<JobEvents>();
    std::jthread job([&downloader = m_downloader, tempRoot = m_tempRoot, retryDelays = m_retryDelays, request = std::move(request),
                      userStop = stop.get_token(), events = events.get()] {
        runJob(downloader, tempRoot, retryDelays, request, userStop, *events);
    });
    try {
        return std::make_unique<JobHandle>(std::move(events), stop, std::move(job));
    } catch (...) {
        stop.request_stop();   // the job ends promptly and `job` joins it before the queue goes
        throw;
    }
}

const std::vector<std::chrono::milliseconds>& DefaultDownloadService::retryDelays() const {
    return m_retryDelays;
}

}  // namespace xgs::job
