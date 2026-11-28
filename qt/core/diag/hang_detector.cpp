#include "diag/hang_detector.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace xgs::diag {

HangDetector::HangDetector(qint64 startMs, qint64 thresholdMs, qint64 repeatMs, qint64 maxTickGapMs, qint64 maxRepeatMs)
    : m_thresholdMs(thresholdMs),
      m_repeatMs(repeatMs),
      m_maxTickGapMs(maxTickGapMs),
      m_maxRepeatMs(maxRepeatMs),
      m_lastBeatMs(startMs),
      m_lastTickMs(startMs),
      m_repeatDueInMs(repeatMs),
      m_followingRepeatInMs(repeatMs) {
    if (thresholdMs <= 0) throw std::invalid_argument("thresholdMs must be positive");
    if (repeatMs <= 0) throw std::invalid_argument("repeatMs must be positive");
    if (maxTickGapMs <= 0) throw std::invalid_argument("maxTickGapMs must be positive");
    if (maxRepeatMs < repeatMs) throw std::invalid_argument("maxRepeatMs must not be less than repeatMs");
}

void HangDetector::onBeat(qint64 nowMs) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hangFromMs && !m_recoveredAtMs) m_recoveredAtMs = nowMs;
    m_lastBeatMs = nowMs;
}

std::optional<HangEvent> HangDetector::onTick(qint64 nowMs) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const qint64 gap = nowMs - m_lastTickMs;
    m_lastTickMs = nowMs;
    if (gap > m_maxTickGapMs) {
        ++m_gapsIgnored;
        m_lastBeatMs = nowMs;
        forgetHang();
        return std::nullopt;
    }

    if (m_hangFromMs && m_recoveredAtMs) {
        const qint64 total = *m_recoveredAtMs - *m_hangFromMs;
        forgetHang();
        return HangEvent{Recovered{total}};
    }

    const qint64 silentMs = nowMs - m_lastBeatMs;
    if (silentMs <= m_thresholdMs) return std::nullopt;
    if (!m_hangFromMs) {
        m_hangFromMs = m_lastBeatMs;
        report(nowMs);
        return HangEvent{Hung{silentMs, false}};
    }
    if (nowMs - m_lastReportMs >= m_repeatDueInMs) {
        report(nowMs);
        return HangEvent{Hung{silentMs, true}};
    }
    return std::nullopt;
}

void HangDetector::onTickFinished(qint64 nowMs) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_lastTickMs = nowMs;
}

void HangDetector::report(qint64 nowMs) {
    m_lastReportMs = nowMs;
    m_repeatDueInMs = m_followingRepeatInMs;
    m_followingRepeatInMs = std::min(m_followingRepeatInMs * 2, m_maxRepeatMs);
}

void HangDetector::forgetHang() {
    m_hangFromMs.reset();
    m_recoveredAtMs.reset();
    m_repeatDueInMs = m_repeatMs;
    m_followingRepeatInMs = m_repeatMs;
}

qint64 monotonicMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// What a heartbeat that is still queued on the watched thread needs when the watchdog is gone: the detector, the clock and the
// pending flag live as long as the last heartbeat.
struct HangWatchdog::Shared {
    Shared(std::function<qint64()> clockMs, const HangWatchdogOptions& options)
        : clock(std::move(clockMs)),
          detector(clock(), options.thresholdMs, options.repeatMs, options.maxTickGapMs, options.maxRepeatMs) {}

    std::function<qint64()> clock;
    HangDetector detector;
    std::atomic<bool> heartbeatPending{false};   // true from the posting of a heartbeat until it has run on the watched thread
};

HangWatchdog::HangWatchdog(PostBeat postBeat, std::function<qint64()> clockMs, LogSink log, HangWatchdogOptions options)
    : m_shared(std::make_shared<Shared>(clockMs, options)),
      m_postBeat(std::move(postBeat)),
      m_clockMs(std::move(clockMs)),
      m_log(std::move(log)) {}

void HangWatchdog::setOnHung(std::function<void(const Hung&)> onHung) {
    m_onHung = std::move(onHung);
}

void HangWatchdog::setOnRecovered(std::function<void(const Recovered&)> onRecovered) {
    m_onRecovered = std::move(onRecovered);
}

namespace {

constexpr int ReminderEvery = 60;

}  // namespace

void HangWatchdog::tick() noexcept {
    bool checked = false;
    try {
        const qint64 now = m_clockMs();
        postHeartbeat();
        checked = true;
        const int gapsBefore = m_shared->detector.gapsIgnored();
        const std::optional<HangEvent> event = m_shared->detector.onTick(now);
        m_failedTicks = 0;
        if (m_shared->detector.gapsIgnored() != gapsBefore && m_log) {
            m_log(Level::Info, QString::fromUtf16(LoggerName),
                  QStringLiteral("멈춤 감시: 확인 간격이 크게 벌어져 이번 판정은 건너뜀 (절전이거나 감시 스레드가 지연됨, 누적 ") +
                      QString::number(m_shared->detector.gapsIgnored()) + QStringLiteral("회)"));
        }
        if (event) {
            if (const auto* hung = std::get_if<Hung>(&*event)) {
                if (m_onHung) m_onHung(*hung);
            } else if (m_onRecovered) {
                m_onRecovered(std::get<Recovered>(*event));
            }
        }
    } catch (...) {
        // The first failure is logged, then a reminder every ReminderEvery ticks: a broken heartbeat repeats every second.
        ++m_failedTicks;
        if (m_failedTicks == 1 || m_failedTicks % ReminderEvery == 0) {
            try {
                if (m_log) m_log(Level::Warn, QString::fromUtf16(LoggerName), QStringLiteral("멈춤 감시 중 오류: ") + currentExceptionName());
            } catch (...) {
                // Not even the log works: nothing more to do.
            }
        }
    }
    // The gap to the next tick counts from here, after the work of this one (a dump can take a while).
    if (checked) {
        try {
            m_shared->detector.onTickFinished(m_clockMs());
        } catch (...) {
            // The next tick then measures from this one's start.
        }
    }
}

// Posts a heartbeat unless the last one has not run yet. The beat is stamped when it runs, not when it is posted.
void HangWatchdog::postHeartbeat() {
    bool expected = false;
    if (!m_shared->heartbeatPending.compare_exchange_strong(expected, true)) return;
    const std::shared_ptr<Shared> shared = m_shared;
    try {
        m_postBeat([shared] {
            try {
                shared->detector.onBeat(shared->clock());
            } catch (...) {
                // The clock failed: the pending flag still has to go.
            }
            shared->heartbeatPending = false;
        });
    } catch (...) {
        m_shared->heartbeatPending = false;   // it was never queued: the next tick has to be able to post
        throw;
    }
}

}  // namespace xgs::diag
