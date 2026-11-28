#pragma once

#include <QtGlobal>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

#include "diag/log_sink.h"

// Port of HangDetector.kt and of the decisions in HangWatchdog.kt's tick. The watchdog thread, its clock and what is written
// when a hang is found (the minidump, the log line) belong to qt/app: HangWatchdog here is the loop body without the loop.
namespace xgs::diag {

/// No beat for silentMs; repeat is false for the first report of a hang and true for the later ones.
struct Hung {
    qint64 silentMs = 0;
    bool repeat = false;
    bool operator==(const Hung&) const = default;
};
/// The watched thread beat again after a reported hang; totalMs is the time from its last beat before the hang to the first
/// one after.
struct Recovered {
    qint64 totalMs = 0;
    bool operator==(const Recovered&) const = default;
};
using HangEvent = std::variant<Hung, Recovered>;

/// Decides from two kinds of calls whether the watched thread has stopped answering: onBeat is what a task posted to that
/// thread calls when it runs; onTick is what the watchdog thread calls once per interval. It owns neither a thread nor a
/// clock: every call says what time it is (startMs says it for the creation). The calls may come from different threads.
///
/// A hang is reported when the last beat is more than thresholdMs ago. While it lasts the report is repeated, first repeatMs
/// after the first report, then at intervals that double up to maxRepeatMs; a recovery starts the sequence over. If two ticks
/// are more than maxTickGapMs apart the machine was probably asleep or the watchdog thread starved: the baseline moves to that
/// tick, any hang being reported is dropped without a Recovered, and the tick reports nothing (gapsIgnored counts them). The
/// gap is measured from the end of the previous tick when onTickFinished says when that was.
///
/// Throws std::invalid_argument when a limit is not positive or maxRepeatMs is less than repeatMs.
class HangDetector {
public:
    explicit HangDetector(qint64 startMs, qint64 thresholdMs = 5'000, qint64 repeatMs = 30'000, qint64 maxTickGapMs = 4'000,
                          qint64 maxRepeatMs = 30 * 60'000);

    void onBeat(qint64 nowMs);
    std::optional<HangEvent> onTick(qint64 nowMs);
    /// Says when the work of the last onTick ended; the next tick's gap counts from here.
    void onTickFinished(qint64 nowMs);
    int gapsIgnored() const { return m_gapsIgnored.load(); }

private:
    void report(qint64 nowMs);
    void forgetHang();

    const qint64 m_thresholdMs;
    const qint64 m_repeatMs;
    const qint64 m_maxTickGapMs;
    const qint64 m_maxRepeatMs;
    std::mutex m_mutex;
    qint64 m_lastBeatMs;
    qint64 m_lastTickMs;
    std::optional<qint64> m_hangFromMs;
    qint64 m_lastReportMs = 0;
    qint64 m_repeatDueInMs;
    qint64 m_followingRepeatInMs;
    std::optional<qint64> m_recoveredAtMs;
    std::atomic<int> m_gapsIgnored{0};
};

/// Milliseconds from a clock that only goes forward (not the wall clock).
qint64 monotonicMillis();

struct HangWatchdogOptions {
    qint64 thresholdMs = 5'000;
    qint64 repeatMs = 30'000;
    qint64 maxTickGapMs = 4'000;    // 4 times the default one second between ticks
    qint64 maxRepeatMs = 30 * 60'000;
};

/// The loop body of the watchdog: once per interval the thread that owns it calls tick, which posts a heartbeat to the watched
/// thread through postBeat (only when the previous one has run, so the queue of a blocked thread does not grow), stamps the
/// beat when it runs (not when it is posted) and asks a HangDetector what it makes of the beats so far. onHung and onRecovered
/// are told what it finds (set them before the first tick; they run on the thread that calls tick; what they do takes time that
/// does not count as a gap). tick never throws: a failure (of postBeat, of a callback) is logged as `멈춤 감시 중 오류: <type>`
/// at the first one and then at every 60th in a row, and the next tick goes on. A skipped gap is logged at INFO. The logger name
/// is `xgs.hang`. A heartbeat that runs after the watchdog is gone does nothing.
class HangWatchdog {
public:
    using PostBeat = std::function<void(std::function<void()>)>;

    HangWatchdog(PostBeat postBeat, std::function<qint64()> clockMs, LogSink log, HangWatchdogOptions options = {});

    void setOnHung(std::function<void(const Hung&)> onHung);
    void setOnRecovered(std::function<void(const Recovered&)> onRecovered);

    void tick() noexcept;

    static constexpr const char16_t* LoggerName = u"xgs.hang";

private:
    struct Shared;
    void postHeartbeat();

    std::shared_ptr<Shared> m_shared;
    PostBeat m_postBeat;
    std::function<qint64()> m_clockMs;
    LogSink m_log;
    std::function<void(const Hung&)> m_onHung;
    std::function<void(const Recovered&)> m_onRecovered;
    int m_failedTicks = 0;
};

}  // namespace xgs::diag
