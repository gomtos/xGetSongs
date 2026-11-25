#include <QtTest>

#include <QElapsedTimer>

#include <atomic>
#include <cstring>
#include <new>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <thread>
#include <vector>

#include "lyrics/google/browser_channel.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/BrowserChannelTest.kt. The channel joined two processes by
// HTTP long polling; here both ends are in one process (Q3, decision 1). The polling agent becomes a second thread of the test
// (AgentThread) that gets `wake` and calls takeCommand and answer; "the agent has been in touch" becomes "an agent is attached".
// Kotlin's virtual time becomes real time with short limits (50 ms) where a test is about a time limit, and generous limits
// (5 s and more) where a test only waits for something that happens at once: those only matter when the test fails.
// theAgentCountsAsPresentForTheFreshnessTimeOnly is not ported (Q3, decision 2: there is no freshness time); in its place
// theAgentCountsAsPresentOnlyWhileAttached.

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::lyrics::google::BrowserAnswer;
using xgs::lyrics::google::BrowserChannel;
using xgs::lyrics::google::BrowserCommand;
using xgs::lyrics::google::DriverAnswer;
using xgs::lyrics::google::Wait;
using Status = DriverAnswer::Status;

namespace {

/// Long enough for anything that happens at once on a loaded machine; only reached when the test fails.
constexpr auto Generous = 10s;

Wait waitFor(std::chrono::nanoseconds limit = 15s, std::stop_token stop = {}) {
    return Wait{std::move(stop), limit};
}

BrowserAnswer okAnswer(quint64 id, std::optional<QString> value = std::nullopt) {
    return BrowserAnswer{id, true, std::move(value), QString()};
}

BrowserAnswer failedAnswer(quint64 id, const QString& error) {
    return BrowserAnswer{id, false, std::nullopt, error};
}

/// A flag one thread raises and another waits for, with a limit.
class Signal {
public:
    void raise() {
        {
            std::lock_guard lock(m_mutex);
            ++m_count;
        }
        m_changed.notify_all();
    }

    /// Waits until the flag has been raised at least `count` times in all; false when the limit came first.
    bool waitFor(int count = 1, std::chrono::nanoseconds limit = Generous) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, limit, [&] { return m_count >= count; });
    }

    int count() const {
        std::lock_guard lock(m_mutex);
        return m_count;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    int m_count = 0;
};

/// The agent on a thread of its own: it attaches a wake that rouses its thread, which then takes commands until there is none
/// and hands each to `respond`; an answer that respond gives is sent at once (nothing: the command stays unanswered). It
/// records every command it took and what answer() said for each answer. Destroying it detaches it first.
class AgentThread {
public:
    using Respond = std::function<std::optional<BrowserAnswer>(const BrowserCommand&)>;

    AgentThread(BrowserChannel& channel, Respond respond) : m_channel(channel), m_respond(std::move(respond)) {
        m_thread = std::jthread([this](std::stop_token stop) { run(stop); });
        m_channel.attach([this] {
            wakes.raise();
            {
                std::lock_guard lock(m_mutex);
                m_woken = true;
            }
            m_changed.notify_all();
        });
    }

    ~AgentThread() {
        m_channel.detach();
        m_thread.request_stop();
        m_thread.join();
    }

    std::vector<BrowserCommand> taken() const {
        std::lock_guard lock(m_mutex);
        return m_taken;
    }

    /// What answer() said for each answer, once `count` answers have been recorded (the record is made after answer() has
    /// already woken the caller, so a caller that has its result must still wait for it); nothing after the generous limit.
    std::optional<std::vector<bool>> accepted(int count) {
        if (!m_answers.waitFor(count)) return std::nullopt;
        std::lock_guard lock(m_mutex);
        return m_accepted;
    }

    Signal wakes;
    Signal takes;

private:
    void run(std::stop_token stop) {
        while (true) {
            {
                std::unique_lock lock(m_mutex);
                m_changed.wait(lock, stop, [this] { return m_woken; });
                if (stop.stop_requested()) return;
                m_woken = false;
            }
            while (std::optional<BrowserCommand> command = m_channel.takeCommand()) {
                {
                    std::lock_guard lock(m_mutex);
                    m_taken.push_back(*command);
                }
                takes.raise();
                if (std::optional<BrowserAnswer> answer = m_respond(*command)) {
                    const bool accepted = m_channel.answer(*answer);
                    {
                        std::lock_guard lock(m_mutex);
                        m_accepted.push_back(accepted);
                    }
                    m_answers.raise();
                }
            }
        }
    }

    BrowserChannel& m_channel;
    Respond m_respond;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed;
    bool m_woken = false;
    std::vector<BrowserCommand> m_taken;
    std::vector<bool> m_accepted;
    Signal m_answers;
    std::jthread m_thread;
};

/// A call on a thread of its own, so that the test's thread can play the agent or end the call.
class CallThread {
public:
    explicit CallThread(std::function<DriverAnswer(std::stop_token)> call) {
        m_thread = std::jthread([this, call = std::move(call)](std::stop_token stop) {
            m_result = call(stop);
            m_done.raise();
        });
    }

    ~CallThread() {
        m_thread.request_stop();
        m_thread.join();
    }

    void requestStop() { m_thread.request_stop(); }

    /// The call's answer, once it has returned within the limit.
    std::optional<DriverAnswer> result(std::chrono::nanoseconds limit = Generous) {
        if (!m_done.waitFor(1, limit)) return std::nullopt;
        return m_result;
    }

private:
    Signal m_done;
    DriverAnswer m_result;
    std::jthread m_thread;
};

}  // namespace

class BrowserChannelTest : public QObject {
    Q_OBJECT

private slots:
    void aCallWithNoAgentFailsAtOnceWithNoAgent() {
        BrowserChannel channel;

        const DriverAnswer answer = channel.load(u"https://example.test/"_s, waitFor());

        QCOMPARE(answer.status, Status::Unavailable);
        QCOMPARE(answer.reason, u"no-agent"_s);
        QVERIFY2(!channel.takeCommand().has_value(), "nothing was queued");
    }

    void aCommandReachesThePollingAgentAndItsAnswerReturnsToTheCaller() {
        BrowserChannel channel;
        AgentThread agent(channel, [](const BrowserCommand& command) { return okAnswer(command.id, u"2"_s); });

        const DriverAnswer answer = channel.evaluate(u"1+1"_s, waitFor());

        const std::vector<BrowserCommand> taken = agent.taken();
        QCOMPARE(taken.size(), std::size_t(1));
        QCOMPARE(taken[0].type, BrowserCommand::Type::Evaluate);
        QCOMPARE(taken[0].text, u"1+1"_s);
        QCOMPARE(agent.accepted(1), std::optional<std::vector<bool>>(std::vector<bool>{true}));
        QCOMPARE(answer.status, Status::Ok);
        QCOMPARE(answer.value, std::optional<QString>(u"2"_s));
    }

    void anEvaluateAnsweredWithAnErrorFailsWithTheErrorText() {
        BrowserChannel channel;
        AgentThread agent(channel, [](const BrowserCommand& command) { return failedAnswer(command.id, u"script timed out"_s); });

        const DriverAnswer answer = channel.evaluate(u"document.body.innerText"_s, waitFor());

        QCOMPARE(answer.status, Status::Unavailable);
        QCOMPARE(answer.reason, u"script timed out"_s);
    }

    void anEvaluateThatGaveNothingGivesNull() {
        BrowserChannel channel;
        AgentThread agent(channel, [](const BrowserCommand& command) { return okAnswer(command.id, std::nullopt); });

        const DriverAnswer answer = channel.evaluate(u"undefined"_s, waitFor());

        QCOMPARE(answer.status, Status::Ok);
        QVERIFY(!answer.value.has_value());
    }

    void aLoadAnsweredWithAnErrorFailsWithTheErrorText() {
        BrowserChannel channel;
        AgentThread agent(channel, [](const BrowserCommand& command) { return failedAnswer(command.id, u"window gone"_s); });

        const DriverAnswer answer = channel.load(u"https://example.test/"_s, waitFor());

        QCOMPARE(answer.status, Status::Unavailable);
        QCOMPARE(answer.reason, u"window gone"_s);
    }

    void aCommandNobodyAnswersFailsWithNoAnswerAfterTheLimit() {
        // Kotlin: the answer limit is 15 s of virtual time, and the call fails exactly 15 s after the agent took the command. Here
        // the limit is 50 ms of real time, measured from the call's start (the deadline is counted from there): at least 50 ms,
        // and well under the generous limit.
        // The agent takes the command inside wake, on the calling thread, so the take has happened before the wait starts
        // whatever the scheduler does, and the test reads what it took on the same thread.
        BrowserChannel channel(50ms);
        std::vector<BrowserCommand> taken;
        channel.attach([&] {
            if (std::optional<BrowserCommand> command = channel.takeCommand()) taken.push_back(*command);
        });
        QElapsedTimer timer;
        timer.start();

        const DriverAnswer answer = channel.evaluate(u"x"_s, waitFor());
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(taken.size(), std::size_t(1));   // the agent took the command and never answers
        QCOMPARE(answer.status, Status::Unavailable);
        QCOMPARE(answer.reason, u"no-answer"_s);
        QVERIFY2(elapsed >= 50, qPrintable(QString::number(elapsed)));
        QVERIFY2(elapsed < 5000, qPrintable(QString::number(elapsed)));
        channel.detach();
    }

    void aCommandThatTimedOutIsNotHandedOutLater() {
        BrowserChannel channel(50ms);
        Signal wakes;
        channel.attach([&wakes] { wakes.raise(); });
        QVERIFY2(!channel.takeCommand().has_value(), "the agent was here, with nothing to do");

        const DriverAnswer answer = channel.evaluate(u"x"_s, waitFor());

        QCOMPARE(answer.status, Status::Unavailable);
        QCOMPARE(answer.reason, u"no-answer"_s);
        QCOMPARE(wakes.count(), 1);
        QVERIFY2(!channel.takeCommand().has_value(), "the caller gave up: nobody waits for what it asked");
        channel.detach();
    }

    void aCommandOfACancelledCallerIsNotHandedOutLater() {
        BrowserChannel channel;
        Signal wakes;
        channel.attach([&wakes] { wakes.raise(); });
        QVERIFY(!channel.takeCommand().has_value());
        CallThread call([&channel](std::stop_token stop) { return channel.evaluate(u"x"_s, waitFor(15s, stop)); });
        QVERIFY(wakes.waitFor());   // the command is queued

        call.requestStop();

        const std::optional<DriverAnswer> answer = call.result();
        QVERIFY(answer.has_value());
        QCOMPARE(answer->status, Status::Stopped);
        QVERIFY(!channel.takeCommand().has_value());
        channel.detach();
    }

    void anAnswerNobodyWaitsForIsRefused() {
        BrowserChannel channel;
        QVERIFY2(!channel.answer(okAnswer(987654321)), "an id the channel did not make");
        AgentThread agent(channel, [](const BrowserCommand& command) { return okAnswer(command.id, u"v"_s); });

        const DriverAnswer answer = channel.evaluate(u"x"_s, waitFor());

        QCOMPARE(agent.accepted(1), std::optional<std::vector<bool>>(std::vector<bool>{true}));
        QCOMPARE(answer.status, Status::Ok);
        QCOMPARE(answer.value, std::optional<QString>(u"v"_s));
        const quint64 id = agent.taken().at(0).id;
        QVERIFY2(!channel.answer(okAnswer(id, u"again"_s)), "answered already");
    }

    void closingFailsPendingCallsAndEndsPolls() {
        BrowserChannel channel;
        Signal wakes;
        channel.attach([&wakes] { wakes.raise(); });
        QVERIFY(!channel.takeCommand().has_value());
        CallThread pending([&channel](std::stop_token stop) { return channel.evaluate(u"x"_s, waitFor(15s, stop)); });
        QVERIFY(wakes.waitFor());

        channel.close();

        const std::optional<DriverAnswer> answer = pending.result();
        QVERIFY(answer.has_value());
        QCOMPARE(answer->status, Status::Unavailable);
        QCOMPARE(answer->reason, u"closed"_s);
        // Kotlin: a poll of a closed channel ends at once (it gives null without waiting). takeCommand never waits; a closed
        // channel gives nothing.
        QVERIFY(!channel.takeCommand().has_value());
        const DriverAnswer load = channel.load(u"https://example.test/"_s, waitFor());
        QCOMPARE(load.status, Status::Unavailable);
        QCOMPARE(load.reason, u"closed"_s);
        QCOMPARE(wakes.count(), 1);
        QVERIFY(!channel.hasAgent());
    }

    void everyCommandGetsItsOwnId() {
        BrowserChannel channel;
        AgentThread agent(channel, [](const BrowserCommand& command) { return okAnswer(command.id, u"v"_s); });

        QCOMPARE(channel.evaluate(u"a"_s, waitFor()).status, Status::Ok);
        QCOMPARE(channel.evaluate(u"b"_s, waitFor()).status, Status::Ok);

        const std::vector<BrowserCommand> taken = agent.taken();
        QCOMPARE(taken.size(), std::size_t(2));
        std::set<quint64> ids;
        for (const BrowserCommand& command : taken) ids.insert(command.id);
        QCOMPARE(ids.size(), std::size_t(2));
    }

    // ---- added: the agent is attached rather than fresh (decision 1), and the threads ----

    void theAgentCountsAsPresentOnlyWhileAttached() {
        BrowserChannel channel;
        QVERIFY2(!channel.hasAgent(), "nobody has attached yet");
        Signal wakes;

        channel.attach([&wakes] { wakes.raise(); });
        QVERIFY(channel.hasAgent());
        channel.detach();
        QVERIFY(!channel.hasAgent());
        const DriverAnswer answer = channel.evaluate(u"y"_s, waitFor());
        QCOMPARE(answer.status, Status::Unavailable);
        QCOMPARE(answer.reason, u"no-agent"_s);
        QCOMPARE(wakes.count(), 0);

        channel.attach([&wakes] { wakes.raise(); });
        QVERIFY2(channel.hasAgent(), "an agent may attach again");
        channel.close();
        QVERIFY(!channel.hasAgent());
        channel.attach([&wakes] { wakes.raise(); });
        QVERIFY2(!channel.hasAgent(), "a closed channel takes no agent");
    }

    void destroyingTheAgentWhileACallWaitsFailsItWithClosed() {
        BrowserChannel channel;
        auto wakes = std::make_unique<Signal>();
        std::atomic<int> wakesAfterDetach{0};
        std::atomic<bool> detached{false};
        channel.attach([&, raw = wakes.get()] {
            if (detached.load()) ++wakesAfterDetach;
            else raw->raise();
        });
        CallThread waiting([&channel](std::stop_token stop) { return channel.evaluate(u"x"_s, waitFor(15s, stop)); });
        QVERIFY(wakes->waitFor());

        channel.detach();
        detached = true;
        wakes.reset();   // the agent's own state is gone: a wake now would touch freed memory

        // One second: the call is woken by detach itself, not by a time limit (its limit is 15 s).
        const std::optional<DriverAnswer> answer = waiting.result(1s);
        QVERIFY2(answer.has_value(), "the waiting call returned within a second");
        QCOMPARE(answer->status, Status::Unavailable);
        QCOMPARE(answer->reason, u"closed"_s);
        QVERIFY(!channel.takeCommand().has_value());
        QCOMPARE(channel.evaluate(u"y"_s, waitFor()).reason, u"no-agent"_s);
        QCOMPARE(wakesAfterDetach.load(), 0);
    }

    void destroyingTheChannelWhileCallsWaitEndsThemWithClosed() {
        // The channel lives in memory of its own; once its destructor has returned the memory is overwritten, so a call that still
        // touched the channel (its lock, its map) would crash or hang instead of returning. Whether a woken call has left the
        // channel before a destructor that did not wait for it returns is up to the scheduler, so this goes round many times.
        constexpr int rounds = 20;
        constexpr int callers = 8;
        for (int round = 0; round < rounds; ++round) {
            void* storage = ::operator new(sizeof(BrowserChannel));
            auto* channel = new (storage) BrowserChannel;
            Signal wakes;
            channel->attach([&wakes] { wakes.raise(); });
            std::vector<std::unique_ptr<CallThread>> calls;
            for (int i = 0; i < callers; ++i) {
                calls.push_back(std::make_unique<CallThread>(
                    [channel](std::stop_token stop) { return channel->evaluate(u"x"_s, waitFor(15s, stop)); }));
            }
            // Every caller has queued its command and is past its wake: it waits for the answer, or is about to.
            QVERIFY(wakes.waitFor(callers));

            channel->~BrowserChannel();
            std::memset(storage, 0xDD, sizeof(BrowserChannel));

            for (const auto& call : calls) {
                const std::optional<DriverAnswer> answer = call->result();
                QVERIFY2(answer.has_value(), "every call returned");
                QCOMPARE(answer->status, Status::Unavailable);
                QCOMPARE(answer->reason, u"closed"_s);
            }
            calls.clear();
            ::operator delete(storage);
        }
    }

    void detachWaitsForAWakeThatIsRunningOnAnotherThread() {
        BrowserChannel channel;
        Signal inWake;
        Signal release;
        Signal detaching;
        Signal detached;
        channel.attach([&] {
            inWake.raise();
            release.waitFor();
        });
        CallThread call([&channel](std::stop_token stop) { return channel.evaluate(u"x"_s, waitFor(15s, stop)); });
        QVERIFY(inWake.waitFor());   // the wake runs on the caller's thread and holds there

        std::jthread detacher([&] {
            detaching.raise();
            channel.detach();
            detached.raise();
        });
        QVERIFY(detaching.waitFor());
        // detach must not return while the wake still runs. Half a second is far more than a detach that wrongly returned at
        // once would need to show; a correct one cannot return here at all.
        QVERIFY2(!detached.waitFor(1, 500ms), "detach returned while the wake was still running");

        release.raise();
        QVERIFY2(detached.waitFor(), "detach returned once the wake had");
        const std::optional<DriverAnswer> answer = call.result();
        QVERIFY(answer.has_value());
        QCOMPARE(answer->status, Status::Unavailable);
        QCOMPARE(answer->reason, u"closed"_s);
    }

    void detachFromInsideWakeDoesNotDeadlock() {
        // The wake detaches on the caller's own thread: detach must not wait for that wake (it is its own caller up the stack).
        // A detach that did would never return, and this test would hang (ctest's timeout ends it).
        BrowserChannel channel;
        std::atomic<int> wakes{0};
        channel.attach([&] {
            ++wakes;
            channel.detach();
        });
        CallThread call([&channel](std::stop_token stop) { return channel.evaluate(u"x"_s, waitFor(15s, stop)); });

        const std::optional<DriverAnswer> answer = call.result();
        QVERIFY2(answer.has_value(), "the call returned");
        QCOMPARE(answer->status, Status::Unavailable);
        QCOMPARE(answer->reason, u"closed"_s);
        QVERIFY(!channel.hasAgent());
        QCOMPARE(channel.evaluate(u"y"_s, waitFor()).reason, u"no-agent"_s);
        QCOMPARE(wakes.load(), 1);
    }

    void aStoppedCallerReturnsAtOnce() {
        BrowserChannel channel;   // the default answer limit, 15 s
        Signal wakes;
        channel.attach([&wakes] { wakes.raise(); });

        // A stop requested before the call: nothing is queued and nobody is woken.
        std::stop_source stopped;
        stopped.request_stop();
        QCOMPARE(channel.evaluate(u"x"_s, waitFor(15s, stopped.get_token())).status, Status::Stopped);
        QCOMPARE(wakes.count(), 0);
        QVERIFY(!channel.takeCommand().has_value());

        // A stop requested while the call waits: it returns well before its limit (five seconds against 15 s, generous for a
        // loaded machine; only the stop request can end it that early).
        CallThread call([&channel](std::stop_token stop) { return channel.evaluate(u"x"_s, waitFor(15s, stop)); });
        QVERIFY(wakes.waitFor());
        call.requestStop();
        const std::optional<DriverAnswer> answer = call.result(5s);
        QVERIFY2(answer.has_value(), "the stopped call returned within five seconds");
        QCOMPARE(answer->status, Status::Stopped);
        QVERIFY(!channel.takeCommand().has_value());
        channel.detach();
    }

    void aCallNeverWaitsLongerThanItsOwnLimit() {
        BrowserChannel channel;   // the default answer limit, 15 s
        Signal wakes;
        channel.attach([&wakes] { wakes.raise(); });
        QElapsedTimer timer;
        timer.start();

        const DriverAnswer answer = channel.evaluate(u"x"_s, waitFor(50ms));
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(answer.status, Status::Unavailable);
        QCOMPARE(answer.reason, u"no-answer"_s);
        QVERIFY2(elapsed >= 50, qPrintable(QString::number(elapsed)));
        QVERIFY2(elapsed < 5000, qPrintable(QString::number(elapsed)));   // 15 s would be the channel's limit
        QVERIFY(!channel.takeCommand().has_value());

        // No time left at all: nothing is queued or woken (Kotlin's withTimeoutOrNull with no time gives up at once).
        QCOMPARE(channel.evaluate(u"x"_s, waitFor(0ns)).reason, u"no-answer"_s);
        QCOMPARE(wakes.count(), 1);
        channel.detach();
    }

    void manyCallersFromManyThreadsEachGetTheirOwnAnswer() {
        constexpr int threads = 16;
        constexpr int calls = 200;
        BrowserChannel channel;
        // The agent answers every command with its own script, so a caller that got somebody else's answer sees another text.
        AgentThread agent(channel, [](const BrowserCommand& command) { return okAnswer(command.id, command.text); });
        std::atomic<int> wrong{0};
        std::atomic<int> right{0};
        {
            std::vector<std::jthread> callers;
            for (int t = 0; t < threads; ++t) {
                callers.emplace_back([&, t] {
                    for (int i = 0; i < calls; ++i) {
                        const QString script = u"t%1-%2"_s.arg(t).arg(i);
                        const DriverAnswer answer = channel.evaluate(script, waitFor());
                        if (answer.status == Status::Ok && answer.value == script) ++right;
                        else ++wrong;
                    }
                });
            }
        }
        QCOMPARE(wrong.load(), 0);
        QCOMPARE(right.load(), threads * calls);
        const std::vector<BrowserCommand> taken = agent.taken();
        QCOMPARE(taken.size(), std::size_t(threads * calls));
        std::set<quint64> ids;
        for (const BrowserCommand& command : taken) ids.insert(command.id);
        QCOMPARE(ids.size(), std::size_t(threads * calls));
    }
};

QTEST_APPLESS_MAIN(BrowserChannelTest)
#include "tst_browser_channel.moc"
