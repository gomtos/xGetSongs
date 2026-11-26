#pragma once

#include <QtTest>

#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "app_api.h"
#include "app_state_holder.h"
#include "settings_store.h"
#include "ui_types.h"

// The stand-ins the screen logic tests need, ported from shell/tests/support/{fakes.ts,fixtures.ts}: the engine (FakeAppApi),
// the settings (FakeSettingsStore), timers that move only when the test says so (FakeScheduler), and the answers the tests
// build on. A promise of the TypeScript fake is a task here that runs on flush(): an answer comes after the call that asked for
// it has returned, as in the original, and never before the test calls flush().
namespace xgs::ui::test {

// ---- the answers --------------------------------------------------------------------------

inline QString pad(int value, int length) {
    return QString::number(value).rightJustified(length, u'0');
}

/// An available item: artist `A<rank>` and track `T<rank>` unless given.
inline ResolvedItem item(int rank, std::optional<QString> artist = std::nullopt, std::optional<QString> track = std::nullopt) {
    const QString a = artist.value_or(QStringLiteral("A") + QString::number(rank));
    const QString t = track.value_or(QStringLiteral("T") + QString::number(rank));
    ResolvedItem result;
    result.rank = rank;
    result.videoId = QStringLiteral("vid") + pad(rank, 8);
    result.title = a + QStringLiteral(" - ") + t;
    result.artist = a;
    result.track = t;
    result.available = true;
    result.expectedFileName = pad(rank, 3) + u' ' + a + QStringLiteral(" - ") + t + QStringLiteral(".m4a");
    return result;
}

/// An item the engine cannot download (a private video).
inline ResolvedItem unavailableItem(int rank = 2, const QString& reason = QStringLiteral("비공개 영상")) {
    ResolvedItem result;
    result.rank = rank;
    result.videoId = QStringLiteral("vid") + pad(rank, 8);
    result.title = QStringLiteral("[Private video]");
    result.available = false;
    result.unavailableReason = reason;
    return result;
}

/// A playlist of three: two available (the third with a guessed artist) and a private one between them.
inline ResolveResult playlist(std::optional<QString> alsoVideoId = std::nullopt) {
    ResolveResult result;
    result.resolveId = QStringLiteral("resolve-1");
    result.kind = InputKind::Playlist;
    result.playlistTitle = QStringLiteral("Sample");
    ResolvedItem third = item(3);
    third.lowConfidence = true;
    result.items = {item(1), unavailableItem(2), third};
    result.alsoVideoId = std::move(alsoVideoId);
    return result;
}

/// A single video, by `IU` called `Love`.
inline ResolveResult video() {
    ResolveResult result;
    result.resolveId = QStringLiteral("resolve-2");
    result.kind = InputKind::Video;
    result.items = {item(1, QStringLiteral("IU"), QStringLiteral("Love"))};
    return result;
}

inline const QList<LyricsOutcome> LyricsOutcomes = {LyricsOutcome::Description, LyricsOutcome::Online, LyricsOutcome::NotFound,
                                                    LyricsOutcome::SearchOff};

/// A state with a response resolved into it, as the labels tests need it (no rows).
inline UiState resolvedState(InputKind kind, std::optional<QString> playlistTitle = std::nullopt,
                             const QString& outputDir = QStringLiteral("D:\\Music"),
                             std::optional<QString> albumName = std::nullopt) {
    UiState state;
    state.outputDir = outputDir;
    state.albumName = std::move(albumName);
    ResolveResult resolved;
    resolved.resolveId = QStringLiteral("r1");
    resolved.kind = kind;
    resolved.playlistTitle = std::move(playlistTitle);
    state.resolved = resolved;
    return state;
}

inline ToolInfo found(const QString& version) {
    return ToolInfo{true, version, std::nullopt};
}

// ---- the engine ---------------------------------------------------------------------------

/// A failure the engine reports: either a message fit for the user (Kotlin's `ApiError`, the TypeScript `apiError`), which
/// reaches the holder as a Failed outcome, or a raw exception (the TypeScript `new Error(...)`), which the fake throws out of
/// the call itself.
struct Failure {
    QString message;
    bool raw = false;
};

inline Failure apiError(const QString& message) {
    return Failure{message, false};
}

inline Failure rawError(const QString& message) {
    return Failure{message, true};
}

/// Holds back the answer of a call until the test releases it (the TypeScript `Deferred` a gate waits for).
class Gate {
public:
    void release() { m_released = true; }
    bool released() const { return m_released; }

private:
    bool m_released = false;
};

class FakeAppApi final : public AppApi {
public:
    ToolsStatus toolsStatus{found(QStringLiteral("1")), found(QStringLiteral("1")), found(QStringLiteral("24"))};
    ResolveResult resolveResponse = playlist();
    std::optional<Failure> resolveError;
    /// When set, `resolve` answers (or fails) only once it is released, so a test can change things while a lookup is held.
    std::shared_ptr<Gate> resolveGate;
    /// When set, `startJob` says the job started only once it is released, so a test can act while the new job has not.
    std::shared_ptr<Gate> startGate;
    std::optional<Failure> startError;
    std::optional<Failure> toolsError;
    QString installMessage = QStringLiteral("installed");

    QStringList resolveInputs;
    QList<JobRequest> jobRequests;
    /// The jobs cancelled, by number: `job-1` is the first job started.
    QStringList cancelled;
    int installCalls = 0;
    int stopCalls = 0;

    /// Sends the events to the screen as the running job's next events.
    template <class... Events>
    void send(const Events&... events) {
        Stream& stream = current();
        (stream.queued.push_back(JobEvent(events)), ...);
        later([this, number = m_streams.size()] { drain(number); });
    }

    /// Ends the running job's event stream.
    void closeEvents() { closeEventsOf(int(m_streams.size())); }

    /// Ends the event stream of the jobNumber-th job (1 is the first), which may be one that is no longer the running job.
    void closeEventsOf(int jobNumber) {
        m_streams.at(std::size_t(jobNumber - 1))->closed = true;
        later([this, jobNumber] { drain(std::size_t(jobNumber)); });
    }

    /// Runs every answer that can be given now, and the answers those lead to: the TypeScript `await flush()`.
    void flush() {
        for (;;) {
            auto ready = std::find_if(m_tasks.begin(), m_tasks.end(), [](const Task& task) { return !task.gate || task.gate->released(); });
            if (ready == m_tasks.end()) return;
            std::function<void()> run = std::move(ready->run);
            m_tasks.erase(ready);
            run();
        }
    }

    void tools(std::function<void(Outcome<ToolsStatus>)> done) override {
        if (toolsError) {
            if (toolsError->raw) throw std::runtime_error(toolsError->message.toStdString());
            later([done, message = toolsError->message] { done(Outcome<ToolsStatus>::failed(message)); });
            return;
        }
        later([this, done] { done(Outcome<ToolsStatus>::ok(toolsStatus)); });
    }

    void installYtDlp(std::function<void(Outcome<QString>)> done) override {
        if (toolsError) {
            if (toolsError->raw) throw std::runtime_error(toolsError->message.toStdString());
            later([done, message = toolsError->message] { done(Outcome<QString>::failed(message)); });
            return;
        }
        installCalls += 1;
        later([this, done] { done(Outcome<QString>::ok(installMessage)); });
    }

    void updateYtDlp(std::function<void(Outcome<QString>)> done) override {
        later([done] { done(Outcome<QString>::ok(QStringLiteral("updated"))); });
    }

    /// A raw error is thrown at once (no test holds a lookup that fails with one); anything else is answered on flush(), after
    /// the gate, with whatever resolveError and resolveResponse are then.
    void resolve(const QString& input, std::function<void(Outcome<ResolveResult>)> done) override {
        resolveInputs.append(input);
        if (resolveError && resolveError->raw) throw std::runtime_error(resolveError->message.toStdString());
        later(
            [this, done] {
                if (resolveError) done(Outcome<ResolveResult>::failed(resolveError->message));
                else done(Outcome<ResolveResult>::ok(resolveResponse));
            },
            resolveGate);
    }

    void startJob(JobRequest request, JobCallbacks callbacks) override {
        if (startError) {
            if (startError->raw) throw std::runtime_error(startError->message.toStdString());
            later([callbacks, message = startError->message] { callbacks.started(message); });
            return;
        }
        jobRequests.append(std::move(request));
        auto stream = std::make_unique<Stream>();
        stream->callbacks = std::move(callbacks);
        m_streams.push_back(std::move(stream));
        later(
            [this, number = m_streams.size()] {
                Stream& started = *m_streams.at(number - 1);
                started.callbacks.started(std::nullopt);
                started.watching = true;
                drain(number);
            },
            startGate);
    }

    void cancelJob() override { cancelled.append(QStringLiteral("job-") + QString::number(jobRequests.size())); }

    void stop() override { stopCalls += 1; }

private:
    struct Task {
        std::function<void()> run;
        std::shared_ptr<Gate> gate;
    };

    /// What the engine sends for one job: kept until the screen has been told the job started, ended by closeEvents.
    struct Stream {
        JobCallbacks callbacks;
        std::deque<JobEvent> queued;
        bool watching = false;
        bool closed = false;
        bool ended = false;
    };

    Stream& current() { return *m_streams.back(); }

    void later(std::function<void()> run, std::shared_ptr<Gate> gate = nullptr) { m_tasks.push_back(Task{std::move(run), std::move(gate)}); }

    /// Delivers the queued events of the number-th job in order, and its end once it is closed and empty.
    void drain(std::size_t number) {
        Stream& stream = *m_streams.at(number - 1);
        if (!stream.watching || stream.ended) return;
        while (!stream.queued.empty()) {
            const JobEvent event = stream.queued.front();
            stream.queued.pop_front();
            stream.callbacks.event(event);
        }
        if (stream.closed) {
            stream.ended = true;
            stream.callbacks.ended(std::nullopt);
        }
    }

    std::deque<Task> m_tasks;
    std::vector<std::unique_ptr<Stream>> m_streams;
};

// ---- the settings and the timers -----------------------------------------------------------

/// Hands out `stored` on `load` and records every `save`, in order. Never touches a file.
class FakeSettingsStore final : public SettingsStore {
public:
    explicit FakeSettingsStore(UserSettings initial = UserSettings())
        : SettingsStore(QString(), QStringLiteral("C:/Music/xGetSongs")), stored(std::move(initial)) {}

    UserSettings load() override {
        loadCalls += 1;
        return stored;
    }

    bool save(const UserSettings& settings) override {
        saved.append(settings);
        return true;
    }

    UserSettings stored;
    QList<UserSettings> saved;
    int loadCalls = 0;
};

/// Timers on a clock that moves only by advance: callbacks run when the time they wait for has passed.
class FakeScheduler final : public Scheduler {
public:
    int setTimeout(std::function<void()> callback, int ms) override {
        const int id = m_nextId++;
        m_tasks.emplace(id, Pending{m_time + ms, std::move(callback)});
        return id;
    }

    void clearTimeout(int handle) override { m_tasks.erase(handle); }

    /// Moves the clock forward by ms, running (in order) every callback that falls due on the way, the one at the end included.
    void advance(qint64 ms) {
        const qint64 target = m_time + ms;
        for (;;) {
            auto next = m_tasks.end();
            for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
                if (it->second.at <= target && (next == m_tasks.end() || it->second.at < next->second.at)) next = it;
            }
            if (next == m_tasks.end()) break;
            Pending task = std::move(next->second);
            m_tasks.erase(next);
            m_time = task.at;
            task.callback();
        }
        m_time = target;
    }

    int pending() const { return int(m_tasks.size()); }

private:
    struct Pending {
        qint64 at;
        std::function<void()> callback;
    };
    qint64 m_time = 0;
    int m_nextId = 1;
    std::map<int, Pending> m_tasks;
};

}  // namespace xgs::ui::test

// ---- what QCOMPARE prints ------------------------------------------------------------------

namespace xgs::ui {

inline char* toString(const ItemStatus& status) {
    static const char* const names[] = {"ready", "waiting", "downloading", "finishing", "done", "skipped", "failed"};
    QString text = QString::fromLatin1(names[int(status.kind)]);
    if (status.percent) text += u' ' + QString::number(*status.percent);
    if (status.lyrics) text += QStringLiteral(" lyrics=") + QString::number(int(*status.lyrics));
    if (!status.text.isEmpty()) text += QStringLiteral(": ") + status.text;
    return QTest::toString(text);
}

inline char* toString(Phase phase) {
    static const char* const names[] = {"IDLE", "RESOLVING", "PREVIEW", "RUNNING", "FINISHED"};
    return qstrdup(names[int(phase)]);
}

inline char* toString(const UserSettings& settings) {
    return QTest::toString(QStringLiteral("{outputDir=%1, overwrite=%2, includeRank=%3, searchLyricsOnline=%4}")
                               .arg(settings.outputDir.value_or(QStringLiteral("<null>")))
                               .arg(settings.overwrite)
                               .arg(settings.includeRank)
                               .arg(settings.searchLyricsOnline));
}

}  // namespace xgs::ui

// Found by argument-dependent lookup through QString, which is in the global namespace.
inline char* toString(const std::optional<QString>& text) {
    return text ? QTest::toString(*text) : qstrdup("<null>");
}
