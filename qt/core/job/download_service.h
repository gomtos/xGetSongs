#pragma once

#include <QList>
#include <QString>

#include <memory>
#include <optional>
#include <stop_token>
#include <thread>

#include "job/job_events.h"
#include "model/models.h"
#include "output/output_sink.h"

// Port of DownloadRequest, JobHandle and DownloadService of engine/.../Services.kt. Threads instead of coroutines: a job runs on
// std::jthreads of its own, and a std::stop_source stands in for the coroutine Job's cancellation.
namespace xgs::job {

/// items must already carry their final ranks. concurrency is how many items are downloaded at the same time; the service
/// raises anything below 1 to 1 and sets no upper limit (download_concurrency.h has the number for this machine). album is the
/// album tag of every file, the playlist title that its folder is named after: it wins over the video's own album. It is absent
/// for a single video, which then gets no album tag unless it has an album of its own. includeRank says whether the file names
/// start with the rank; the track number is the rank either way. Without the rank two items can end up with the same file
/// name: the first one to finish wins, the other is skipped (or, with overwrite, replaces it). searchLyricsOnline allows a
/// lookup on the internet (through the downloader's lyrics provider) for a song whose description has no lyrics; it is off by
/// default. albumOverride is an album name the user chose: when it is not blank every file gets it as its album tag. The lyrics
/// lookup still goes by the video's own album (else album).
struct DownloadRequest {
    QList<ResolvedItem> items;                 // already carry their final ranks
    output::OutputSink* sink = nullptr;        // referenced, not owned: must outlive the job
    bool overwrite = false;
    int concurrency = 1;                       // below 1 is 1; no upper limit of the service's own
    std::optional<QString> album;
    bool includeRank = true;
    bool searchLyricsOnline = false;
    std::optional<QString> albumOverride;
};

/// A running download. One reader. Destroying it cancels the job and waits for its threads.
class JobHandle {
public:
    /// events is the job's queue, stop the source whose stop cancels the job, job the thread that runs it (and joins every
    /// other thread of the job before it ends).
    JobHandle(std::unique_ptr<JobEvents> events, std::stop_source stop, std::jthread job);
    ~JobHandle();

    JobHandle(const JobHandle&) = delete;
    JobHandle& operator=(const JobHandle&) = delete;

    std::optional<JobEvent> nextEvent();       // blocks; nothing once JobDone has been taken
    std::optional<JobEvent> tryNextEvent();    // never blocks; nothing when no event is waiting
    bool finished() const;                     // JobDone has been queued (it may not have been taken yet)
    void cancel();                             // returns at once; safe from any thread
    void join();                               // until JobDone has been queued and every thread of the job has ended

private:
    std::unique_ptr<JobEvents> m_events;
    std::stop_source m_stop;
    std::jthread m_job;
};

class DownloadService {
public:
    virtual ~DownloadService() = default;
    /// Starts the job. A stop requested on stop (now or later) cancels it, as cancel() does.
    virtual std::unique_ptr<JobHandle> start(DownloadRequest request, std::stop_source stop = std::stop_source()) = 0;
};

}  // namespace xgs::job
