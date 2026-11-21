#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

#include "model/models.h"

// The event queue of one download job (Kotlin's unlimited Channel<JobEvent> in DefaultDownloadService.kt): any number of
// writers, one reader. The job queues its JobDone last and then closes the queue.
namespace xgs::job {

class JobEvents final {
public:
    /// Queues event. Dropped once the queue is closed or a JobDone has been queued (nothing follows the JobDone). Never blocks
    /// for long: the reader holds the lock only to take an event.
    void push(JobEvent event);

    /// Closes the queue: later pushes are dropped and a blocked next returns once the queue is empty.
    void close();

    /// The next event, waiting for one; nothing once the queue is empty and either closed or past its JobDone.
    std::optional<JobEvent> next();

    /// The next event if one is waiting, else nothing. Never blocks.
    std::optional<JobEvent> tryNext();

    /// Whether a JobDone has been queued (it may not have been taken yet).
    bool doneQueued() const;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::deque<JobEvent> m_events;
    bool m_closed = false;
    bool m_doneQueued = false;
};

}  // namespace xgs::job
