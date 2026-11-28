#include "diag/run_record.h"

#include "diag/hang_detector.h"
#include "diag/log_directory.h"

namespace xgs::diag {

namespace {

QString markerPath(const QString& logDir) {
    QString base = logDir;
    while (base.endsWith(u'\\') || base.endsWith(u'/')) base.chop(1);
    return base + u'\\' + MARKER_FILE_NAME;
}

void tell(const LogSink& log, Level level, const QString& text) {
    try {
        if (log) log(level, DIAGNOSTICS_LOGGER_NAME, text);
    } catch (...) {
        // A log that fails must not keep the app from starting.
    }
}

}  // namespace

std::unique_ptr<RunMarkerFile> startMarker(const QString& logDir, const Processes& processes, const LogSink& log) {
    std::unique_ptr<RunMarkerFile> marker;
    try {
        marker = std::make_unique<RunMarkerFile>(markerPath(logDir), processes.ownPid(), processes.ownStart());
    } catch (...) {
        tell(log, Level::Warn, QStringLiteral("이번 실행 기록을 만들지 못함 (") + currentExceptionName() + QStringLiteral(")"));
        return nullptr;
    }
    try {
        const auto note = describePreviousRun(
            judgePreviousRun(marker->read(), [&processes](qint64 pid) { return processes.startOf(pid); }));
        if (note) tell(log, note->warn ? Level::Warn : Level::Info, note->text);
    } catch (...) {
        tell(log, Level::Warn, QStringLiteral("이전 실행 기록을 확인하지 못함 (") + currentExceptionName() + QStringLiteral(")"));
    }
    marker->markRunning();
    return marker;
}

RunRecord::RunRecord(const QString& logDir, const LogSink& log, const Processes& processes, const QString& startupText,
                     std::function<std::chrono::milliseconds()> uptime)
    : m_log(log) {
    ensureLogDirectory(logDir);
    tell(log, Level::Info, startupText);
    m_marker = startMarker(logDir, processes, log);

    if (!uptime) {
        const qint64 startedAt = monotonicMillis();
        uptime = [startedAt] { return std::chrono::milliseconds(monotonicMillis() - startedAt); };
    }
    ExitSources sources;
    sources.uptime = std::move(uptime);
    sources.uncaughtSeen = [this] { return m_uncaughtSeen.load(); };
    sources.onExit = [this](bool userRequested) {
        if (m_marker) m_marker->markExited(userRequested);
    };
    m_exit = std::make_unique<ExitRecord>(std::move(sources), log);
}

void RunRecord::setRunningJobs(std::function<int()> runningJobs) {
    m_exit->setRunningJobs(std::move(runningJobs));
}

int RunRecord::runningJobs() const {
    return m_exit->runningJobs();
}

void RunRecord::markUserExit() {
    tell(m_log, Level::Info, QStringLiteral("사용자가 창 닫기를 요청함"));
    m_exit->markUserExit();
}

void RunRecord::noteUncaught() noexcept {
    m_uncaughtSeen = true;
}

void RunRecord::logExit() noexcept {
    m_exit->logExit();
}

}  // namespace xgs::diag
