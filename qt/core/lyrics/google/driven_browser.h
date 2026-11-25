#pragma once

#include <QString>

#include <chrono>
#include <functional>
#include <stop_token>

#include "lyrics/google/page_driver.h"
#include "lyrics/google/page_watch.h"
#include "lyrics/google/rendering_browser.h"

// Port of engine/.../lyrics/google/DrivenBrowser.kt.
namespace xgs::lyrics::google {

/// Waits for the given time unless the stop request comes first. false when it ended because of the stop request. A test
/// gives one that moves its clock (Kotlin's virtual time); empty: a wait on a condition variable that wakes at once on a stop
/// request.
using Pause = std::function<bool(std::chrono::nanoseconds, std::stop_token)>;

/// The default Pause: waits on a condition variable for duration (any duration, nanoseconds::max() included) and wakes at
/// once on a stop request. false when it ended because of the stop request.
bool waitUnlessStopped(std::chrono::nanoseconds duration, std::stop_token stop);

struct DrivenBrowserOptions {
    std::chrono::nanoseconds pollInterval{std::chrono::milliseconds(500)};
    std::chrono::nanoseconds settleTime{std::chrono::seconds(4)};
    Clock clock;   // empty: std::chrono::steady_clock
    Pause pause;   // empty: see Pause
};

/// A RenderingBrowser on a PageDriver, a browser that is never shown: no window of its own on screen, and nothing kept between
/// pages (the driver's end decides that).
///
/// The waiting happens here: it looks at the page every pollInterval with a short script (probeScript()) and lets a PageWatch
/// say when to stop; then the card script (cardScript()) reads the lyrics card. The browser does not report HTTP statuses, so
/// the page itself (its address and text) tells a block from a result. The time limit is checked between steps, and every
/// driver call gets the time that is left as its limit (Q3, decision 8). The page is sent back to the blank page after every
/// render, also after a stop or a timeout. A driver that cannot be used gives Unavailable with its reason.
class DrivenBrowser final : public RenderingBrowser {
public:
    /// The limit of the reset after a render. Not the time that is left: after a timeout none is left, and after a stop the
    /// reset still has to happen. Five seconds, the limit the hidden window gives one script's answer (Q3, decision 10): going
    /// to about:blank takes far less, and a browser that cannot do it in that time is not waited on longer.
    static constexpr std::chrono::seconds ResetLimit{5};

    explicit DrivenBrowser(PageDriver& driver, DrivenBrowserOptions options = {});

    RenderResult render(const QString& url, std::chrono::nanoseconds timeout, std::stop_token stop) override;

private:
    RenderResult renderPage(const QString& url, std::chrono::nanoseconds timeout, const std::stop_token& stop);
    std::chrono::nanoseconds now() const;
    bool pause(std::chrono::nanoseconds duration, const std::stop_token& stop) const;

    PageDriver& m_driver;
    DrivenBrowserOptions m_options;
};

}  // namespace xgs::lyrics::google
