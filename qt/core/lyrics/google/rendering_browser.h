#pragma once

#include <QString>

#include <chrono>
#include <stop_token>

// Port of engine/.../lyrics/google/RenderingBrowser.kt.
namespace xgs::lyrics::google {

/// What one render came to.
///   - Loaded: the page as it stands: its final address (url), the answer of the card script (card, read by parseCard; Kotlin
///     carried the page's HTML here) and the first part of its visible text (bodyText);
///   - TimedOut: the page did not settle within the time limit;
///   - Unavailable: the browser cannot be used; reason is the name of what went wrong;
///   - Stopped: the stop request ended the render (Kotlin's CancellationException).
struct RenderResult {
    enum class Kind { Loaded, TimedOut, Unavailable, Stopped };
    Kind kind = Kind::Unavailable;
    QString url;
    QString card;
    QString bodyText;
    QString reason;
};

/// A browser that can open an address and hand back the finished page.
class RenderingBrowser {
public:
    virtual ~RenderingBrowser() = default;

    /// Opens url, waits until the page shows a lyrics card or a block page or has settled without one (but at most timeout),
    /// and returns it. Never throws; a stop request gives Stopped, a browser that cannot be used Unavailable.
    virtual RenderResult render(const QString& url, std::chrono::nanoseconds timeout, std::stop_token stop) = 0;
};

}  // namespace xgs::lyrics::google
