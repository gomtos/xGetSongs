#pragma once

#include <QString>

#include <chrono>
#include <memory>
#include <stop_token>
#include <vector>

#include "job/download_service.h"
#include "job/item_downloader.h"

// Port of engine/.../job/DefaultDownloadService.kt with threads instead of coroutines (author decision 1 of the Q2d plan).
//
// A job runs on one thread of its own, which creates `<tempRoot>` and a `job-…` folder in it, then starts
// min(max(concurrency, 1), number of items) worker threads. The workers take the items in list order. Per item: prepare,
// ItemStarted, ItemSkipped(`이미 존재`) when overwrite is off and the sink has the name, the folder `<job>\<rank>`, download
// (Transient failures are tried again after each of retryDelays), put, ItemDone. When the workers have ended the job folder is
// removed (tried again for about 1.5 s when something holds a file in it), JobDone is queued once and the queue is closed.
//
// The end status (author decision 3): Failed when an item failed fatally (disk full, no permission, no yt-dlp or ffmpeg) or the
// job folder could not be made or a worker thread could not be started (the summary then counts the items that the started
// workers finished); else Cancelled when a stop was requested on the job's stop source; else Completed. A fatal
// failure stops the other workers through an inner stop source, so their items end as cancelled and are not counted.
//
// A cancelled item emits nothing more (author decision 2): once a stop is requested an item emits no event until it ends,
// unless its put had already started (author decision 10: that put runs to its end and its result is reported). An exception a
// collaborator throws on a worker thread fails that item only (author decision 13): what() of a std::exception (UTF-8), else
// `알 수 없는 오류`; one thrown after the stop, before the put, ends the item as cancelled.
namespace xgs::job {

class DefaultDownloadService final : public DownloadService {
public:
    // downloader is referenced and must outlive every job. tempRoot is a result of paths::fromText.
    DefaultDownloadService(ItemDownloader& downloader, QString tempRoot,
                           std::vector<std::chrono::milliseconds> retryDelays = {std::chrono::seconds(2), std::chrono::seconds(4)});
    std::unique_ptr<JobHandle> start(DownloadRequest request, std::stop_source stop = std::stop_source()) override;
    const std::vector<std::chrono::milliseconds>& retryDelays() const;

private:
    ItemDownloader& m_downloader;
    QString m_tempRoot;
    std::vector<std::chrono::milliseconds> m_retryDelays;
};

}  // namespace xgs::job
