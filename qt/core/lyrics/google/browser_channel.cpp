#include "lyrics/google/browser_channel.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace xgs::lyrics::google {

namespace {

DriverAnswer unavailable(const QString& reason) {
    DriverAnswer answer;
    answer.status = DriverAnswer::Status::Unavailable;
    answer.reason = reason;
    return answer;
}

DriverAnswer stopped() {
    DriverAnswer answer;
    answer.status = DriverAnswer::Status::Stopped;
    return answer;
}

}  // namespace

BrowserChannel::BrowserChannel(std::chrono::nanoseconds answerLimit) : m_answerLimit(answerLimit) {}

BrowserChannel::~BrowserChannel() {
    close();
}

void BrowserChannel::attach(std::function<void()> wake) {
    std::unique_lock lock(m_mutex);
    if (m_closed) return;
    if (m_wake) endAgent(lock);
    if (wake) m_wake = std::make_shared<const std::function<void()>>(std::move(wake));
}

void BrowserChannel::detach() {
    std::unique_lock lock(m_mutex);
    endAgent(lock);
}

void BrowserChannel::close() {
    std::unique_lock lock(m_mutex);
    m_closed = true;
    endAgent(lock);
}

void BrowserChannel::endAgent(std::unique_lock<std::mutex>& lock) {
    m_wake.reset();
    m_queue.clear();
    for (auto& [id, waiter] : m_waiting) waiter->closed = true;
    m_waiting.clear();
    m_changed.notify_all();
    // A wake that has taken its copy of the function before m_wake was dropped is still running (or about to run) outside the
    // lock; the agent must outlive it. One on this very thread is this thread's own caller up the stack (detach called from
    // inside wake): waiting for it would never end.
    const std::thread::id self = std::this_thread::get_id();
    m_changed.wait(lock, [this, self] {
        return std::all_of(m_waking.begin(), m_waking.end(), [self](std::thread::id id) { return id == self; });
    });
}

bool BrowserChannel::hasAgent() const {
    std::lock_guard lock(m_mutex);
    return !m_closed && m_wake != nullptr;
}

std::optional<BrowserCommand> BrowserChannel::takeCommand() {
    std::lock_guard lock(m_mutex);
    while (!m_queue.empty()) {
        BrowserCommand command = std::move(m_queue.front());
        m_queue.pop_front();
        if (m_waiting.contains(command.id)) return command;
    }
    return std::nullopt;
}

bool BrowserChannel::answer(const BrowserAnswer& answer) {
    std::lock_guard lock(m_mutex);
    const auto found = m_waiting.find(answer.id);
    if (found == m_waiting.end()) return false;
    found->second->answer = answer;
    m_waiting.erase(found);
    m_changed.notify_all();
    return true;
}

DriverAnswer BrowserChannel::load(const QString& url, const Wait& wait) {
    return call(BrowserCommand::Type::Load, url, wait);
}

DriverAnswer BrowserChannel::evaluate(const QString& script, const Wait& wait) {
    return call(BrowserCommand::Type::Evaluate, script, wait);
}

DriverAnswer BrowserChannel::reset(const Wait& wait) {
    return call(BrowserCommand::Type::Reset, QString(), wait);
}

DriverAnswer BrowserChannel::call(BrowserCommand::Type type, const QString& text, const Wait& wait) {
    using std::chrono::nanoseconds;
    std::unique_lock lock(m_mutex);
    if (m_closed) return unavailable(u"closed"_s);
    if (!m_wake) return unavailable(u"no-agent"_s);
    if (wait.stop.stop_requested()) return stopped();
    const nanoseconds limit = std::min(m_answerLimit, wait.limit);
    // Kotlin's withTimeoutOrNull with no time left gives up at once: nothing is queued that nobody would wait for.
    if (limit <= nanoseconds::zero()) return unavailable(u"no-answer"_s);

    // The deadline, saturated: a limit near nanoseconds::max() would overflow now() + limit.
    using SteadyClock = std::chrono::steady_clock;
    const SteadyClock::time_point start = SteadyClock::now();
    const SteadyClock::time_point latest = SteadyClock::time_point::max();
    const SteadyClock::time_point deadline =
        limit >= latest - start ? latest : start + std::chrono::duration_cast<SteadyClock::duration>(limit);

    const quint64 id = m_nextId++;
    Waiter waiter;
    m_waiting.emplace(id, &waiter);
    m_queue.push_back(BrowserCommand{id, type, text});

    // Wake the agent outside the lock, with a copy of the function that stays valid however detach and attach go meanwhile;
    // detach waits until this thread's entry in m_waking is gone. A wake must not throw (the agent's contract); one that does is
    // taken as having woken nobody, and the call waits for its answer as usual.
    const std::shared_ptr<const std::function<void()>> wake = m_wake;
    const std::thread::id self = std::this_thread::get_id();
    m_waking.push_back(self);
    lock.unlock();
    try {
        (*wake)();
    } catch (...) {
    }
    lock.lock();
    m_waking.erase(std::find(m_waking.begin(), m_waking.end(), self));
    m_changed.notify_all();

    // Waits in pieces of at most a day: a wait_until far in the future is turned into a relative wait that may overflow.
    const auto done = [&waiter] { return waiter.answer.has_value() || waiter.closed; };
    while (!done() && !wait.stop.stop_requested()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        const auto step = std::min<std::chrono::steady_clock::duration>(deadline - now, std::chrono::hours(24));
        m_changed.wait_until(lock, wait.stop, now + step, done);
    }

    if (!done()) {
        // Given up: nobody waits for the command any more, so it leaves the queue (takeCommand would skip it anyway) and a late
        // answer is refused.
        m_waiting.erase(id);
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [id](const BrowserCommand& command) { return command.id == id; }),
                      m_queue.end());
        if (wait.stop.stop_requested()) return stopped();
        return unavailable(u"no-answer"_s);
    }
    if (waiter.closed) return unavailable(u"closed"_s);

    const BrowserAnswer& answer = *waiter.answer;
    if (!answer.ok) return unavailable(answer.error.isEmpty() ? u"failed"_s : answer.error);
    DriverAnswer result;
    result.status = DriverAnswer::Status::Ok;
    if (type == BrowserCommand::Type::Evaluate) result.value = answer.value;
    return result;
}

}  // namespace xgs::lyrics::google
