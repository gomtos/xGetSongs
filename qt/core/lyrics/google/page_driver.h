#pragma once

#include <QString>

#include <chrono>
#include <optional>
#include <stop_token>

// Port of engine/.../lyrics/google/PageDriver.kt (the interface PageDriver and BrowserUnavailableException).
namespace xgs::lyrics::google {

/// How long one driver call may take, and the stop request that ends it early.
struct Wait {
    std::stop_token stop;
    std::chrono::nanoseconds limit;
};

/// What a driver call came to. Unavailable: the browser cannot be used, `reason` is a short name of why (`no-agent`,
/// `no-answer`, `closed`, or what the browser reported); Kotlin threw BrowserUnavailableException(reason). Stopped: the call
/// ended because of the stop request (Kotlin's CancellationException). `value` is what a script gave (nothing when it gave
/// nothing) and is used by evaluate only.
struct DriverAnswer {
    enum class Status { Ok, Unavailable, Stopped };
    Status status = Status::Unavailable;
    std::optional<QString> value;
    QString reason;
};

/// What the Google lyrics lookup needs from a hidden browser, and nothing more: open an address, run a script in the page, go
/// back to the blank page. Everything that depends on how Google writes its pages (what to look for, when the page is done)
/// is in DrivenBrowser, on this side. No call throws.
class PageDriver {
public:
    virtual ~PageDriver() = default;

    /// Opens url (making the window when there is none) and returns once the load has started.
    virtual DriverAnswer load(const QString& url, const Wait& wait) = 0;

    /// Runs script in the page; Ok with the text it gave, or with no value when it gave nothing.
    virtual DriverAnswer evaluate(const QString& script, const Wait& wait) = 0;

    /// Back to `about:blank`, so that nothing of the last page stays loaded. The window stays.
    virtual DriverAnswer reset(const Wait& wait) = 0;
};

}  // namespace xgs::lyrics::google
