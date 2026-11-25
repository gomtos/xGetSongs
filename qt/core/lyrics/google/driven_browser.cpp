#include "lyrics/google/driven_browser.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

#include "lyrics/google/page_scripts.h"

using namespace Qt::StringLiterals;

namespace xgs::lyrics::google {

namespace {

// Right after load() the browser may still show the page that was there before.
const QString kBlank = u"about:blank"_s;

RenderResult ofKind(RenderResult::Kind kind) {
    RenderResult result;
    result.kind = kind;
    return result;
}

RenderResult unavailable(const QString& reason) {
    RenderResult result = ofKind(RenderResult::Kind::Unavailable);
    result.reason = reason;
    return result;
}

}  // namespace

DrivenBrowser::DrivenBrowser(PageDriver& driver, DrivenBrowserOptions options) : m_driver(driver), m_options(std::move(options)) {}

std::chrono::nanoseconds DrivenBrowser::now() const {
    if (m_options.clock) return m_options.clock();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

bool waitUnlessStopped(std::chrono::nanoseconds duration, std::stop_token stop) {
    // In pieces of at most a day: wait_for adds its duration to the clock's now, which overflows for a duration near
    // nanoseconds::max().
    constexpr std::chrono::nanoseconds piece = std::chrono::hours(24);
    std::mutex mutex;
    std::condition_variable_any never;
    std::unique_lock lock(mutex);
    while (duration > std::chrono::nanoseconds::zero()) {
        const std::chrono::nanoseconds step = std::min(duration, piece);
        never.wait_for(lock, stop, step, [] { return false; });
        if (stop.stop_requested()) return false;
        duration -= step;
    }
    return !stop.stop_requested();
}

bool DrivenBrowser::pause(std::chrono::nanoseconds duration, const std::stop_token& stop) const {
    if (m_options.pause) return m_options.pause(duration, stop);
    return waitUnlessStopped(duration, stop);
}

RenderResult DrivenBrowser::render(const QString& url, std::chrono::nanoseconds timeout, std::stop_token stop) {
    // Kotlin caught any Exception as Unavailable (with its class name) and reset in `finally`. A driver must not throw, but
    // whatever does (std::bad_alloc, a driver that breaks its contract) ends here as Unavailable("exception"), and the reset
    // still happens.
    RenderResult result;
    try {
        result = renderPage(url, timeout, stop);
    } catch (...) {
        result = unavailable(u"exception"_s);
    }
    // Stop whatever is still loading and let go of the page, also after a stop or a timeout: with a token that cannot stop and a
    // limit of its own. What it answers changes nothing, and a throw from it does not get out.
    try {
        m_driver.reset(Wait{std::stop_token(), ResetLimit});
    } catch (...) {
    }
    return result;
}

RenderResult DrivenBrowser::renderPage(const QString& url, std::chrono::nanoseconds timeout, const std::stop_token& stop) {
    using Kind = RenderResult::Kind;
    using Status = DriverAnswer::Status;
    // Saturated: a timeout of nanoseconds::max() (no limit) must not wrap around to a deadline in the past.
    const std::chrono::nanoseconds start = now();
    const std::chrono::nanoseconds deadline =
        start > std::chrono::nanoseconds::zero() && timeout > std::chrono::nanoseconds::max() - start
            ? std::chrono::nanoseconds::max()
            : start + timeout;
    auto left = [&] { return deadline - now(); };
    // What ends the render between two steps: a stop request, then the time limit (Kotlin's withTimeoutOrNull cut the call
    // that was running at the deadline, whatever it would have answered).
    auto interruption = [&]() -> std::optional<RenderResult> {
        if (stop.stop_requested()) return ofKind(Kind::Stopped);
        if (left() <= std::chrono::nanoseconds::zero()) return ofKind(Kind::TimedOut);
        return std::nullopt;
    };

    if (auto ended = interruption()) return *ended;
    const DriverAnswer loaded = m_driver.load(url, Wait{stop, left()});
    if (loaded.status == Status::Stopped) return ofKind(Kind::Stopped);
    if (auto ended = interruption()) return *ended;
    if (loaded.status == Status::Unavailable) return unavailable(loaded.reason);

    PageWatch watch(m_options.settleTime, m_options.clock);
    while (true) {
        if (!pause(std::min(m_options.pollInterval, left()), stop)) return ofKind(Kind::Stopped);
        if (auto ended = interruption()) return *ended;

        const DriverAnswer looked = m_driver.evaluate(probeScript(), Wait{stop, left()});
        if (looked.status == Status::Stopped) return ofKind(Kind::Stopped);
        if (auto ended = interruption()) return *ended;
        // A browser that cannot run the script ends the render at once: waiting on it for the rest of the time limit would end
        // as a timeout, which is held against Google.
        if (looked.status == Status::Unavailable) return unavailable(looked.reason);
        // Nothing, or not what the script gives: the page is not ready yet.
        if (!looked.value) continue;
        const std::optional<PageProbe> probe = probeFromJson(*looked.value);
        if (!probe) continue;
        if (probe->url == kBlank || !watch.isDone(*probe)) continue;

        const DriverAnswer read = m_driver.evaluate(cardScript(), Wait{stop, left()});
        if (read.status == Status::Stopped) return ofKind(Kind::Stopped);
        if (auto ended = interruption()) return *ended;
        if (read.status == Status::Unavailable) return unavailable(read.reason);
        // No answer is not an empty card: that would be read as "no lyrics card" and remembered for the song.
        if (!read.value) return unavailable(u"no-html"_s);

        RenderResult result = ofKind(Kind::Loaded);
        result.url = probe->url;
        result.card = *read.value;
        result.bodyText = probe->bodyText;
        return result;
    }
}

}  // namespace xgs::lyrics::google
