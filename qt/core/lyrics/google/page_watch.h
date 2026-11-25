#pragma once

#include <QString>

#include <chrono>
#include <functional>
#include <optional>

// Port of engine/.../lyrics/google/PageWatch.kt (the classes PageProbe and PageWatch).
namespace xgs::lyrics::google {

/// A monotonic clock; a test gives one that it moves itself. Empty: std::chrono::steady_clock.
using Clock = std::function<std::chrono::nanoseconds()>;

/// What one look at the page in the browser saw. bodyText is the first part of the visible text.
struct PageProbe {
    QString url;
    QString readyState;
    bool hasCard = false;
    QString bodyText;
};

/// The probe script's answer read as Kotlin's `Json { ignoreUnknownKeys = true }.decodeFromString<PageProbe>(text)` does
/// (DrivenBrowser.probe); nothing where that throws. The root must be an object holding `url`, `readyState` and `bodyText` as
/// strings and `hasCard` as the literal `true` or `false`; other keys are ignored. The text is read by xgs::json::parse
/// (kotlinx's parseToJsonElement grammar); where decodeFromString's own grammar differs (lenient literals, repeated keys,
/// `null` for a property) is for the Q3 Task 5 differential to look at. The script always answers JSON.stringify of an object
/// with these four fields, so only a page that is not ready yet gives anything else.
std::optional<PageProbe> probeFromJson(const QString& text);

/// Decides when to stop waiting for a page. Done when the page shows a lyrics card or a block page (it will not change), or
/// when it is complete and has stayed so, at the same address, for settleTime: Google loads its result page in two steps, so
/// a page that is complete for a moment is not yet the final one. Loading again or a new address starts that quiet time over.
class PageWatch {
public:
    PageWatch(std::chrono::nanoseconds settleTime, Clock clock);

    bool isDone(const PageProbe& probe);

private:
    std::chrono::nanoseconds now() const;

    std::chrono::nanoseconds m_settleTime;
    Clock m_clock;
    std::optional<QString> m_watchedUrl;
    std::optional<std::chrono::nanoseconds> m_completeSince;
};

}  // namespace xgs::lyrics::google
