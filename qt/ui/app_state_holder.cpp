#include "app_state_holder.h"

#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <exception>
#include <variant>

#include "filename/filename_formatter.h"
#include "input/input_classifier.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::ui {

namespace {

/// How long the options must stay unchanged before they are written.
constexpr int SaveDelayMs = 400;

const QString LostConnection = u"서버와의 연결이 끊어졌습니다."_s;

static_assert(std::chrono::steady_clock::is_steady, "the default clock must not follow the wall clock");

qint64 monotonicNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// The four options that are remembered; nothing else on the screen is.
UserSettings optionsOf(const UiState& state) {
    return UserSettings{state.outputDir, state.overwrite, state.includeRank, state.searchLyricsOnline};
}

bool isNullOrBlank(const std::optional<QString>& text) {
    return !text || kt::isBlank(*text);
}

ItemRow withFileName(ItemRow row, bool includeRank) {
    if (!row.item.available) return row;
    row.fileName = filename::format(row.item.rank, row.item.artist, row.item.track, includeRank);
    return row;
}

ItemRow withRank(ItemRow row, int rank, bool includeRank) {
    if (!row.item.available) return row;
    row.item.rank = rank;
    return withFileName(std::move(row), includeRank);
}

/// The engine's expectedFileName always carries the rank, so the shown name is built here to honour includeRank.
ItemRow previewRow(const ResolvedItem& item, int singleRank, InputKind kind, bool includeRank) {
    ItemRow row{item, item.expectedFileName,
                item.available ? ItemStatus::ready() : ItemStatus::skipped(item.unavailableReason.value_or(QString()))};
    return kind == InputKind::Video ? withRank(std::move(row), singleRank, includeRank) : withFileName(std::move(row), includeRank);
}

/// A cancelled or aborted job sends no final event for the items that were still waiting or in flight, so those rows go back to
/// ready. Finished, skipped and failed rows keep their result.
void resetTransient(QList<ItemRow>& rows) {
    for (ItemRow& row : rows) {
        switch (row.status.kind) {
        case ItemStatus::Kind::Waiting:
        case ItemStatus::Kind::Downloading:
        case ItemStatus::Kind::Finishing: row.status = ItemStatus::ready(); break;
        default: break;
        }
    }
}

template <class Change>
void updateRow(UiState& state, int rank, Change change) {
    for (ItemRow& row : state.rows) {
        if (row.item.rank == rank && row.item.available) change(row);
    }
}

/// What an exception out of a call of the engine says.
QString exceptionText(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return failureMessage(QString::fromUtf8(e.what()));
    } catch (...) {
        return failureMessage(u"알 수 없는 오류"_s);
    }
}

/// The message of a failed (or, never in practice, cancelled) answer: the engine's own text.
template <class T>
QString messageOf(const Outcome<T>& outcome) {
    return outcome.error;
}

}  // namespace

QString failureMessage(const QString& detail) {
    return u"서버와 통신 중 오류가 발생했습니다: "_s + detail;
}

// ---- QTimerScheduler ----------------------------------------------------------------

QTimerScheduler::QTimerScheduler(QObject* parent) : QObject(parent) {}

QTimerScheduler::~QTimerScheduler() = default;

int QTimerScheduler::setTimeout(std::function<void()> callback, int ms) {
    const int id = m_nextId++;
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id, callback = std::move(callback)] {
        clearTimeout(id);
        callback();
    });
    m_timers.emplace(id, timer);
    timer->start(ms);
    return id;
}

void QTimerScheduler::clearTimeout(int handle) {
    const auto it = m_timers.find(handle);
    if (it == m_timers.end()) return;
    it->second->stop();
    it->second->deleteLater();
    m_timers.erase(it);
}

// ---- AppStateHolder -----------------------------------------------------------------

AppStateHolder::AppStateHolder(AppApi& api, SettingsStore& settings, QObject* parent)
    : QObject(parent),
      m_api(api),
      m_settings(settings),
      m_now(monotonicNow),
      m_ownScheduler(std::make_unique<QTimerScheduler>()),
      m_scheduler(*m_ownScheduler) {
    loadSettings();
}

AppStateHolder::AppStateHolder(AppApi& api, SettingsStore& settings, Clock now, Scheduler& scheduler, QObject* parent)
    : QObject(parent), m_api(api), m_settings(settings), m_now(now ? std::move(now) : Clock(monotonicNow)), m_scheduler(scheduler) {
    loadSettings();
}

void AppStateHolder::loadSettings() {
    const UserSettings stored = m_settings.load();
    m_state.outputDir = !isNullOrBlank(stored.outputDir) ? *stored.outputDir : m_settings.defaultOutputDir();
    m_state.overwrite = stored.overwrite;
    m_state.includeRank = stored.includeRank;
    m_state.searchLyricsOnline = stored.searchLyricsOnline;
    m_lastKnown = optionsOf(m_state);
}

AppStateHolder::~AppStateHolder() {
    if (m_saveHandle != 0) m_scheduler.clearTimeout(m_saveHandle);
}

void AppStateHolder::update(const std::function<void(UiState&)>& change) {
    change(m_state);
    emit changed();
}

// ---- simple setters ----

void AppStateHolder::onInput(const QString& text) {
    update([&](UiState& state) { state.input = text; });
}

void AppStateHolder::onOutputDir(const QString& dir) {
    changeOptions([&](UiState& state) { state.outputDir = dir; });
}

void AppStateHolder::onOverwrite(bool value) {
    changeOptions([&](UiState& state) { state.overwrite = value; });
}

void AppStateHolder::onIncludeRank(bool value) {
    changeOptions([&](UiState& state) {
        state.includeRank = value;
        if (state.phase != Phase::Preview) return;
        for (ItemRow& row : state.rows) row = withFileName(row, value);
    });
}

void AppStateHolder::onSearchLyricsOnline(bool value) {
    changeOptions([&](UiState& state) { state.searchLyricsOnline = value; });
}

void AppStateHolder::onAlbumName(const QString& text) {
    update([&](UiState& state) { state.albumName = text; });
}

void AppStateHolder::onSingleRank(int value) {
    update([&](UiState& state) {
        const int rank = std::clamp(value, filename::MinRank, filename::MaxRank);
        state.singleRank = rank;
        if (!state.resolved || state.resolved->kind != InputKind::Video) return;
        for (ItemRow& row : state.rows) row = withRank(row, rank, state.includeRank);
    });
}

void AppStateHolder::dismissError() {
    update([](UiState& state) { state.error.reset(); });
}

// ---- remembered options ----

void AppStateHolder::changeOptions(const std::function<void(UiState&)>& change) {
    const UserSettings old = optionsOf(m_state);
    update(change);
    if (optionsOf(m_state) != old) scheduleSave();
}

void AppStateHolder::scheduleSave() {
    if (m_saveHandle != 0) m_scheduler.clearTimeout(m_saveHandle);
    m_saveHandle = m_scheduler.setTimeout(
        [this] {
            m_saveHandle = 0;
            saveIfChanged();
        },
        SaveDelayMs);
}

void AppStateHolder::flushSettings() {
    if (m_saveHandle != 0) m_scheduler.clearTimeout(m_saveHandle);
    m_saveHandle = 0;
    saveIfChanged();
}

void AppStateHolder::saveIfChanged() {
    const UserSettings current = optionsOf(m_state);
    if (current == m_lastKnown) return;
    m_settings.save(current);
    m_lastKnown = current;
}

void AppStateHolder::reset() {
    m_epoch += 1;
    m_jobStarted = false;
    update([](UiState& state) {
        UiState fresh;
        fresh.outputDir = state.outputDir;
        fresh.overwrite = state.overwrite;
        fresh.includeRank = state.includeRank;
        fresh.searchLyricsOnline = state.searchLyricsOnline;
        fresh.tools = state.tools;
        state = std::move(fresh);
    });
}

// ---- tools ----

void AppStateHolder::refreshTools() {
    QPointer<AppStateHolder> self(this);
    try {
        m_api.tools([self](Outcome<ToolsStatus> answer) {
            if (!self) return;
            self->update([&](UiState& state) {
                if (answer.isOk()) state.tools = *answer.value;
                else state.error = messageOf(answer);
            });
        });
    } catch (...) {
        const QString message = exceptionText(std::current_exception());
        update([&](UiState& state) { state.error = message; });
    }
}

void AppStateHolder::installYtDlp() {
    runToolAction([this](std::function<void(Outcome<QString>)> done) { m_api.installYtDlp(std::move(done)); });
}

void AppStateHolder::updateYtDlp() {
    runToolAction([this](std::function<void(Outcome<QString>)> done) { m_api.updateYtDlp(std::move(done)); });
}

void AppStateHolder::runToolAction(const std::function<void(std::function<void(Outcome<QString>)>)>& action) {
    update([](UiState& state) {
        state.toolBusy = true;
        state.toolMessage.reset();
    });
    QPointer<AppStateHolder> self(this);
    const auto fail = [self](const QString& message) {
        if (!self) return;
        self->update([&](UiState& state) {
            state.toolBusy = false;
            state.toolMessage = message;
        });
    };
    try {
        action([self, fail](Outcome<QString> result) {
            if (!self) return;
            if (!result.isOk()) {
                fail(messageOf(result));
                return;
            }
            const QString message = *result.value;
            try {
                self->m_api.tools([self, fail, message](Outcome<ToolsStatus> tools) {
                    if (!self) return;
                    if (!tools.isOk()) {
                        fail(messageOf(tools));
                        return;
                    }
                    self->update([&](UiState& state) {
                        state.toolBusy = false;
                        state.toolMessage = message;
                        state.tools = *tools.value;
                    });
                });
            } catch (...) {
                fail(exceptionText(std::current_exception()));
            }
        });
    } catch (...) {
        fail(exceptionText(std::current_exception()));
    }
}

// ---- resolving ----

void AppStateHolder::resolve() {
    if (!canResolve(m_state)) return;
    const ClassifyResult result = classifyInput(m_state.input);
    if (!result.ok()) {
        const QString message = rejectMessage(result.reason);
        update([&](UiState& state) { state.error = message; });
        return;
    }
    doResolve(m_state.input);
}

void AppStateHolder::switchToVideoOnly() {
    if (!m_state.resolved || !m_state.resolved->alsoVideoId || m_state.resolved->alsoVideoId->isEmpty()) return;
    const QString url = ParsedInput{ParsedInput::Kind::Video, *m_state.resolved->alsoVideoId, std::nullopt}.canonicalUrl();
    update([&](UiState& state) { state.input = url; });
    doResolve(url);
}

void AppStateHolder::doResolve(const QString& input) {
    const bool hadResult = m_state.resolved.has_value();
    const Phase phaseBefore = m_state.phase;
    update([](UiState& state) {
        state.phase = Phase::Resolving;
        state.error.reset();
    });
    QPointer<AppStateHolder> self(this);
    // A failed lookup must not strip the buttons from a preview that is still on screen. The user may have changed the rank
    // option while the lookup ran (the rows were not rewritten then), so a restored preview shows names that follow the option
    // as it is now.
    const auto fail = [self, hadResult, phaseBefore](const QString& message) {
        if (!self) return;
        const Phase phase = hadResult ? phaseBefore : Phase::Idle;
        self->update([&](UiState& state) {
            state.phase = phase;
            if (phase == Phase::Preview) {
                for (ItemRow& row : state.rows) row = withFileName(row, state.includeRank);
            }
            state.error = message;
        });
    };
    try {
        m_api.resolve(input, [self, fail](Outcome<ResolveResult> answer) {
            if (!self) return;
            if (!answer.isOk()) {
                fail(messageOf(answer));
                return;
            }
            const ResolveResult response = *answer.value;
            self->update([&](UiState& state) {
                state.phase = Phase::Preview;
                state.rows.clear();
                for (const ResolvedItem& item : response.items) {
                    state.rows.append(previewRow(item, state.singleRank, response.kind, state.includeRank));
                }
                state.resolved = response;
                state.summary.reset();
                state.jobStatus.reset();
            });
        });
    } catch (...) {
        fail(exceptionText(std::current_exception()));
    }
}

// ---- downloading ----

JobRequest AppStateHolder::jobRequest(const UiState& state, const std::optional<QList<int>>& ranks) const {
    // What `POST /jobs` of the server did with the lookup it had kept: the available items (of the ranks asked for), a single
    // video with the rank the user chose.
    const ResolveResult& resolved = *state.resolved;
    const bool isVideo = resolved.kind == InputKind::Video;
    JobRequest request;
    for (const ResolvedItem& item : resolved.items) {
        if (!item.available || (ranks && !ranks->contains(item.rank))) continue;
        ResolvedItem chosen = item;
        if (isVideo) chosen.rank = state.singleRank;
        request.items.append(chosen);
    }
    request.isPlaylist = resolved.kind == InputKind::Playlist;
    request.playlistTitle = resolved.playlistTitle;
    request.options.outputDir = state.outputDir;
    request.options.overwrite = state.overwrite;
    request.options.singleRank = state.singleRank;
    request.options.includeRank = state.includeRank;
    request.options.searchLyricsOnline = state.searchLyricsOnline;
    request.options.albumName = !isNullOrBlank(state.albumName) ? state.albumName : std::nullopt;
    return request;
}

void AppStateHolder::startDownload() {
    const UiState before = m_state;
    if (!before.resolved) return;
    if (before.phase != Phase::Preview && before.phase != Phase::Finished) return;
    if (kt::isBlank(before.outputDir)) {
        update([](UiState& state) { state.error = u"출력 폴더를 지정하세요."_s; });
        return;
    }
    update([](UiState& state) {
        state.phase = Phase::Running;
        state.error.reset();
        state.summary.reset();
        state.jobStatus.reset();
        state.elapsedMs.reset();
        for (ItemRow& row : state.rows) {
            if (row.item.available) row.status = ItemStatus::waiting();
        }
    });
    runJob(jobRequest(before, std::nullopt), before);
}

void AppStateHolder::retryFailed() {
    const UiState before = m_state;
    if (!before.resolved) return;
    const QList<int> ranks = failedRanks(before);
    if (ranks.isEmpty() || before.phase != Phase::Finished) return;
    update([&](UiState& state) {
        state.phase = Phase::Running;
        state.error.reset();
        state.summary.reset();
        state.jobStatus.reset();
        state.elapsedMs.reset();
        for (ItemRow& row : state.rows) {
            if (ranks.contains(row.item.rank)) row.status = ItemStatus::waiting();
        }
    });
    // A single video is always re-run as a whole; its rank is chosen by the user, not by the list.
    const bool isVideo = before.resolved->kind == InputKind::Video;
    runJob(jobRequest(before, isVideo ? std::nullopt : std::optional<QList<int>>(ranks)), before);
}

void AppStateHolder::cancel() {
    if (!m_jobStarted) return;
    try {
        m_api.cancelJob();
    } catch (...) {
        const QString message = exceptionText(std::current_exception());
        update([&](UiState& state) { state.error = message; });
    }
}

void AppStateHolder::runJob(const JobRequest& request, const UiState& before) {
    m_jobStart = m_now();
    // Whether an earlier job had started does not matter: until this one says it runs there is nothing for cancel to cancel.
    m_jobStarted = false;
    const int epoch = m_epoch;
    const int run = ++m_run;
    QPointer<AppStateHolder> self(this);
    const auto current = [self, epoch, run] { return self && epoch == self->m_epoch && run == self->m_run; };
    // `before` is the screen as it was when the user pressed the button. It is restored when the job cannot even be started,
    // so a failed retry does not wipe the results of the previous run.
    const auto restore = [self, current, before](const QString& message) {
        if (!current()) return;
        self->update([&](UiState& state) {
            state.phase = before.phase;
            state.rows = before.rows;
            state.summary = before.summary;
            state.jobStatus = before.jobStatus;
            state.error = message;
        });
    };

    JobCallbacks callbacks;
    callbacks.started = [self, current, restore](std::optional<QString> error) {
        if (error) {
            restore(*error);
            return;
        }
        if (!current()) return;
        self->m_jobStarted = true;
    };
    callbacks.event = [self, current](const JobEvent& event) {
        if (!current()) return;
        self->update([&](UiState& state) { self->apply(state, event); });
    };
    callbacks.ended = [self, current](std::optional<QString> error) {
        if (!current()) return;
        if (!error && self->m_state.phase != Phase::Running) return;
        self->update([&](UiState& state) {
            state.phase = Phase::Finished;
            state.error = error ? *error : LostConnection;
            resetTransient(state.rows);
        });
    };
    try {
        m_api.startJob(request, std::move(callbacks));
    } catch (...) {
        restore(exceptionText(std::current_exception()));
    }
}

void AppStateHolder::apply(UiState& state, const JobEvent& event) const {
    std::visit(
        [&](const auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, ItemStarted>) {
                updateRow(state, e.rank, [&](ItemRow& row) {
                    row.fileName = e.fileName;
                    row.status = ItemStatus::downloading(std::nullopt);
                });
            } else if constexpr (std::is_same_v<E, Progress>) {
                updateRow(state, e.rank, [&](ItemRow& row) {
                    row.status = e.stage == Stage::Downloading ? ItemStatus::downloading(e.percent) : ItemStatus::finishing();
                });
            } else if constexpr (std::is_same_v<E, ItemDone>) {
                updateRow(state, e.rank, [&](ItemRow& row) {
                    row.fileName = e.fileName;
                    row.status = ItemStatus::done(e.lyrics);
                });
            } else if constexpr (std::is_same_v<E, ItemSkipped>) {
                updateRow(state, e.rank, [&](ItemRow& row) { row.status = ItemStatus::skipped(e.reason); });
            } else if constexpr (std::is_same_v<E, ItemFailed>) {
                updateRow(state, e.rank, [&](ItemRow& row) { row.status = ItemStatus::failed(e.message); });
            } else {
                static_assert(std::is_same_v<E, JobDone>);
                state.phase = Phase::Finished;
                state.jobStatus = e.status;
                state.summary = e.summary;
                state.elapsedMs = m_jobStart ? std::optional<qint64>(m_now() - *m_jobStart) : std::nullopt;
                resetTransient(state.rows);
            }
        },
        event);
}

}  // namespace xgs::ui
