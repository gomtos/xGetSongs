#pragma once

#include <QObject>
#include <QString>

class QTimer;

#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "app_api.h"
#include "settings_store.h"
#include "ui_types.h"

// All the screen logic, ported from shell/src/state/holder.ts (itself the Compose app's AppStateHolder.kt). The widgets only render
// state() and call these functions. Lives on one thread (the GUI thread): every function is called there, and the engine answers
// there. The 400 ms wait before the options are saved and the clock behind the elapsed time can be given in, so that the tests run
// in no time at all.
namespace xgs::ui {

/// What the holder needs of the timers (the TypeScript `Scheduler`): one-shot callbacks that can be called off.
class Scheduler {
public:
    virtual ~Scheduler() = default;
    /// Runs callback once after ms on the holder's thread; returns a handle for clearTimeout (never 0).
    virtual int setTimeout(std::function<void()> callback, int ms) = 0;
    /// Calls off the callback of handle if it has not run yet.
    virtual void clearTimeout(int handle) = 0;
};

/// The Scheduler of the app: single-shot QTimers on the thread that made it.
class QTimerScheduler final : public QObject, public Scheduler {
    Q_OBJECT
public:
    explicit QTimerScheduler(QObject* parent = nullptr);
    ~QTimerScheduler() override;
    int setTimeout(std::function<void()> callback, int ms) override;
    void clearTimeout(int handle) override;

private:
    int m_nextId = 1;
    std::map<int, QTimer*> m_timers;
};

class AppStateHolder : public QObject {
    Q_OBJECT
public:
    /// Milliseconds from any fixed point; the time a job took is measured with it.
    using Clock = std::function<qint64()>;

    /// The app's holder: a monotonic clock (std::chrono::steady_clock: a change of the wall clock must not skew "소요") and
    /// QTimers. api and settings are referenced and must outlive the holder. The settings are loaded here, once.
    AppStateHolder(AppApi& api, SettingsStore& settings, QObject* parent = nullptr);
    /// The same with the clock and the timers given in; an empty clock is the monotonic one. scheduler must outlive the holder.
    AppStateHolder(AppApi& api, SettingsStore& settings, Clock now, Scheduler& scheduler, QObject* parent = nullptr);
    ~AppStateHolder() override;

    const UiState& state() const { return m_state; }

    // ---- simple setters ----
    void onInput(const QString& text);
    void onOutputDir(const QString& dir);
    void onOverwrite(bool value);
    /// Turning the rank in the file name on or off rewrites the names shown in the preview. Rows of a running or finished job are
    /// left alone: they show the names the engine really used, and the rows of a job that ended before it started them
    /// (cancelled or aborted) keep the preview names they had.
    void onIncludeRank(bool value);
    /// Whether a song whose description has no lyrics is looked up on the internet. It only matters when a job starts.
    void onSearchLyricsOnline(bool value);
    /// The album name for the tags of the next job, which is also the name of the folder it saves into; blank keeps the default.
    /// Not remembered.
    void onAlbumName(const QString& text);
    /// Changing the rank of a single video rewrites its file name in the preview.
    void onSingleRank(int value);
    void dismissError();

    // ---- remembered options ----
    /// Saves the options as they are now, at once, and drops a save that is still waiting. Call it when the window closes,
    /// because a change made in the last moments would otherwise be lost. Writes nothing when the options are the ones already
    /// stored.
    void flushSettings();
    /// The same as flushSettings.
    void flush() { flushSettings(); }

    void reset();

    // ---- tools ----
    void refreshTools();
    void installYtDlp();
    void updateYtDlp();

    // ---- resolving ----
    void resolve();
    /// For a watch URL that also carried a playlist: look up just that video instead.
    void switchToVideoOnly();

    // ---- downloading ----
    void startDownload();
    /// Runs a new job for just the items that failed last time.
    void retryFailed();
    void cancel();

signals:
    /// After every change of state().
    void changed();

private:
    void loadSettings();
    void update(const std::function<void(UiState&)>& change);
    void changeOptions(const std::function<void(UiState&)>& change);
    void scheduleSave();
    void saveIfChanged();
    void runToolAction(const std::function<void(std::function<void(Outcome<QString>)>)>& action);
    void doResolve(const QString& input);
    JobRequest jobRequest(const UiState& state, const std::optional<QList<int>>& ranks) const;
    void runJob(const JobRequest& request, const UiState& before);
    void apply(UiState& state, const JobEvent& event) const;

    AppApi& m_api;
    SettingsStore& m_settings;
    Clock m_now;
    std::unique_ptr<QTimerScheduler> m_ownScheduler;
    Scheduler& m_scheduler;

    UiState m_state;
    /// Whether the job started last has said that it runs (the TypeScript `jobId`): until it has there is nothing for cancel to
    /// cancel.
    bool m_jobStarted = false;
    /// When the job that runs now was started; the time shown at its end is measured from it.
    std::optional<qint64> m_jobStart;
    /// Bumped by reset: whatever a job that ran before it reports afterwards is ignored.
    int m_epoch = 0;
    /// Counts the jobs started: what an earlier job reports after a later job has begun is ignored too.
    int m_run = 0;
    int m_saveHandle = 0;
    /// The options as they are known to be stored: what the screen started with (what the settings hold, with a blank folder
    /// replaced by the default), then whatever was saved last. Nothing is written while the screen still shows exactly these, so
    /// a file that could not be read is not replaced by defaults and the default folder does not end up in the file unless the
    /// user changes something.
    UserSettings m_lastKnown;
};

/// What the user sees for a failure that is not a message of the engine (an exception out of a call of AppApi): holder.ts's
/// failureMessage for anything that is not the server's message.
QString failureMessage(const QString& detail);

}  // namespace xgs::ui
