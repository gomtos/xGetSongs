#include "diag/exit_record.h"

namespace xgs::diag {

QString formatUptime(std::chrono::milliseconds uptime) {
    const qint64 totalSeconds = (uptime.count() < 0 ? 0 : uptime.count()) / 1'000;
    const qint64 hours = totalSeconds / 3'600;
    const qint64 minutes = totalSeconds % 3'600 / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) return QString::number(hours) + QStringLiteral("시간 ") + QString::number(minutes) + QStringLiteral("분 ") + QString::number(seconds) + QStringLiteral("초");
    if (minutes > 0) return QString::number(minutes) + QStringLiteral("분 ") + QString::number(seconds) + QStringLiteral("초");
    return QString::number(seconds) + QStringLiteral("초");
}

ExitDescription describeExit(bool userRequested, int runningJobs, std::chrono::milliseconds uptime, bool uncaughtBefore) {
    const QString jobs = runningJobs < 0 ? QStringLiteral("등록된 작업 수를 알 수 없음")
                                         : QStringLiteral("등록된 작업 ") + QString::number(runningJobs) + QStringLiteral("개");
    const QString details = QStringLiteral("가동 ") + formatUptime(uptime) + QStringLiteral(", ") + jobs;
    if (userRequested) {
        return {false, QStringLiteral("JVM 종료 시작: 사용자가 창을 닫음 (") + details + QStringLiteral(")")};
    }
    const QString note = uncaughtBefore ? QStringLiteral(" | 직전에 처리되지 않은 예외가 있었음") : QString();
    return {true, QStringLiteral("JVM 종료 시작: 창을 닫지 않은 종료 (콘솔 종료, SIGTERM, 로그오프·시스템 종료, 또는 처리되지 않은 예외 뒤의 종료) (") +
                      details + QStringLiteral(")") + note};
}

ExitRecord::ExitRecord(ExitSources sources, LogSink log) : m_sources(std::move(sources)), m_log(std::move(log)) {}

void ExitRecord::markUserExit() noexcept {
    m_userRequested = true;
}

void ExitRecord::setRunningJobs(std::function<int()> runningJobs) {
    const std::lock_guard<std::mutex> lock(m_jobsMutex);
    m_sources.runningJobs = std::move(runningJobs);
}

int ExitRecord::runningJobs() const {
    std::function<int()> source;
    {
        const std::lock_guard<std::mutex> lock(m_jobsMutex);
        source = m_sources.runningJobs;
    }
    try {
        return source ? source() : -1;
    } catch (...) {
        return -1;
    }
}

ExitDescription ExitRecord::describe(int runningJobs, std::chrono::milliseconds uptime, bool crashed) const {
    return describeExit(m_userRequested.load(), runningJobs, uptime, crashed);
}

QString ExitRecord::text(int runningJobs, std::chrono::milliseconds uptime, bool crashed) const {
    return describe(runningJobs, uptime, crashed).text;
}

void ExitRecord::logExit() noexcept {
    try {
        const bool user = m_userRequested.load();
        const int jobs = runningJobs();
        std::chrono::milliseconds up{0};
        try {
            if (m_sources.uptime) up = m_sources.uptime();
        } catch (...) {
            up = std::chrono::milliseconds{0};
        }
        bool uncaught = false;
        try {
            if (m_sources.uncaughtSeen) uncaught = m_sources.uncaughtSeen();
        } catch (...) {
            uncaught = false;
        }
        QString memory;
        try {
            if (m_sources.memoryMb) {
                const auto [used, max] = m_sources.memoryMb();
                memory = QStringLiteral(" | 힙 사용 ") + QString::number(used) + QStringLiteral("MB (최대 ") + QString::number(max) + QStringLiteral("MB)");
            }
        } catch (...) {
            memory.clear();
        }
        const ExitDescription exit = describeExit(user, jobs, up, uncaught);
        if (m_log) m_log(exit.warn ? Level::Warn : Level::Info, QString::fromUtf16(LoggerName), exit.text + memory);
        try {
            if (m_sources.onExit) m_sources.onExit(user);
        } catch (...) {
            // The marker is a convenience: the record above is what matters.
        }
    } catch (...) {
        // Nobody is left to report to: this runs while the app is going down.
    }
}

}  // namespace xgs::diag
