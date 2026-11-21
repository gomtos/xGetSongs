#include "job/job_events.h"

#include <utility>
#include <variant>

#include "job/download_service.h"

namespace xgs::job {

void JobEvents::push(JobEvent event) {
    {
        const std::lock_guard lock(m_mutex);
        if (m_closed || m_doneQueued) return;
        if (std::holds_alternative<JobDone>(event)) m_doneQueued = true;
        m_events.push_back(std::move(event));
    }
    m_changed.notify_one();
}

void JobEvents::close() {
    {
        const std::lock_guard lock(m_mutex);
        m_closed = true;
    }
    m_changed.notify_all();
}

std::optional<JobEvent> JobEvents::next() {
    std::unique_lock lock(m_mutex);
    m_changed.wait(lock, [this] { return !m_events.empty() || m_closed; });
    if (m_events.empty()) return std::nullopt;
    JobEvent event = std::move(m_events.front());
    m_events.pop_front();
    return event;
}

std::optional<JobEvent> JobEvents::tryNext() {
    const std::lock_guard lock(m_mutex);
    if (m_events.empty()) return std::nullopt;
    JobEvent event = std::move(m_events.front());
    m_events.pop_front();
    return event;
}

bool JobEvents::doneQueued() const {
    const std::lock_guard lock(m_mutex);
    return m_doneQueued;
}

// JobHandle is the reader's side of the queue plus the thread of the job.

JobHandle::JobHandle(std::unique_ptr<JobEvents> events, std::stop_source stop, std::jthread job)
    : m_events(std::move(events)), m_stop(std::move(stop)), m_job(std::move(job)) {}

JobHandle::~JobHandle() {
    cancel();
    join();
}

std::optional<JobEvent> JobHandle::nextEvent() {
    return m_events->next();
}

std::optional<JobEvent> JobHandle::tryNextEvent() {
    return m_events->tryNext();
}

bool JobHandle::finished() const {
    return m_events->doneQueued();
}

void JobHandle::cancel() {
    m_stop.request_stop();
}

void JobHandle::join() {
    if (m_job.joinable()) m_job.join();
}

}  // namespace xgs::job
