#pragma once

#include <QString>
#include <QtGlobal>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "lyrics/google/page_driver.h"

// Port of engine/.../lyrics/google/BrowserChannel.kt and of the commands and answers of shared/.../ApiModels.kt
// (BrowserCommand, BrowserAnswer). The channel used to join two processes by HTTP long polling; here both ends are in one
// process (Q3, decision 1): worker threads call it as a PageDriver, and the agent (BrowserAgent on the GUI thread, which owns
// the hidden browser) takes the commands and answers them.
namespace xgs::lyrics::google {

/// One thing for the hidden browser to do. `text` is the address of a Load and the script of an Evaluate (empty for Reset).
/// Ids are a running number of the channel that made the command (Kotlin used a random UUID).
struct BrowserCommand {
    enum class Type { Load, Evaluate, Reset };
    quint64 id = 0;
    Type type = Type::Load;
    QString text;
};

/// The report on one command. `value` is what a script gave (nothing when it gave nothing); `error` is the short name of what
/// went wrong when `ok` is false (empty when ok).
struct BrowserAnswer {
    quint64 id = 0;
    bool ok = false;
    std::optional<QString> value;
    QString error;
};

/// The worker threads' end of the hidden browser. The lookup calls load, evaluate and reset as a PageDriver, from any thread;
/// each becomes a BrowserCommand in a queue. The agent attaches with a `wake` function, takes commands with takeCommand and
/// reports each with answer.
///
/// Nothing waits for a browser that is not there: a call fails at once with `no-agent` while no agent is attached (Kotlin: the
/// shell had not been in touch within the freshness time), so a lookup without a browser ends at once with "no lyrics". A call
/// waits for its answer at most min(answerLimit, wait.limit) and fails with `no-answer` after that; it ends at once with
/// Stopped on its stop request. A command whose caller has given up (no answer in time, stopped) is never handed out. No call
/// throws.
///
/// Threads. Every function may be called from any thread. `wake` is called once for every command, on the thread that made the
/// call, outside the channel's lock; the agent then calls takeCommand until it gives nothing (typically wake posts an event to
/// the agent's thread). `wake` must return without waiting for the agent's thread: detach waits for a wake in progress on
/// another thread, so a wake that waited for the thread that is in detach would never return. A wake that runs on the thread
/// that calls detach (or close) is that thread's own caller further up the stack: it is not waited for, so detach from inside
/// wake does not deadlock (and the wake function must then not touch the agent after detach returns).
class BrowserChannel final : public PageDriver {
public:
    explicit BrowserChannel(std::chrono::nanoseconds answerLimit = std::chrono::seconds(15));
    /// close().
    ~BrowserChannel() override;

    BrowserChannel(const BrowserChannel&) = delete;
    BrowserChannel& operator=(const BrowserChannel&) = delete;

    /// The agent is here: from now on calls are queued and `wake` is called for each. An agent that is attached already is
    /// detached first (its waiting calls end with `closed`). Does nothing on a closed channel.
    void attach(std::function<void()> wake);

    /// The agent is gone: every waiting call ends with `closed`, the queued commands are dropped, and later calls fail with
    /// `no-agent` until an agent attaches again. When it returns, `wake` is not running on any other thread and will never be
    /// called again, so the agent may be destroyed right after.
    void detach();

    /// Whether an agent is attached (and the channel is open). Kotlin: whether the agent had been in touch within the freshness
    /// time.
    bool hasAgent() const;

    /// The next command somebody still waits for; nothing when there is none or the channel is closed. Never waits.
    std::optional<BrowserCommand> takeCommand();

    /// The agent's report on a command. False when nobody waits for it any more (given up, answered already, not ours).
    bool answer(const BrowserAnswer& answer);

    /// Fails the waiting calls (with `closed`), drops the queue and detaches the agent (as detach does); every later call fails
    /// with `closed` and takeCommand gives nothing.
    void close();

    DriverAnswer load(const QString& url, const Wait& wait) override;
    DriverAnswer evaluate(const QString& script, const Wait& wait) override;
    DriverAnswer reset(const Wait& wait) override;

private:
    struct Waiter {
        std::optional<BrowserAnswer> answer;
        bool closed = false;
    };

    /// Queues the command, wakes the agent and waits for the answer: Ok with the answer's value, or why not.
    DriverAnswer call(BrowserCommand::Type type, const QString& text, const Wait& wait);
    /// Under the lock: ends the waiting calls with `closed`, drops the queue and the wake function, and waits until no wake runs
    /// on another thread.
    void endAgent(std::unique_lock<std::mutex>& lock);

    const std::chrono::nanoseconds m_answerLimit;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::deque<BrowserCommand> m_queue;
    std::map<quint64, Waiter*> m_waiting;   // the waiters live on their callers' stacks, removed before the caller returns
    std::shared_ptr<const std::function<void()>> m_wake;
    std::vector<std::thread::id> m_waking;   // one entry per wake in progress, by the thread it runs on
    quint64 m_nextId = 1;
    bool m_closed = false;
};

}  // namespace xgs::lyrics::google
