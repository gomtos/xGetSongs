#include "lyrics/google/lyrics_fetcher.h"

#include <algorithm>
#include <utility>

#include "lyrics/google/lyrics_card.h"
#include "lyrics/google/page_classifier.h"

using namespace Qt::StringLiterals;

namespace xgs::lyrics::google {

namespace {

/// More doublings than this are beyond any maxBackoff; it only keeps the shift from overflowing (Kotlin's MAX_DOUBLINGS).
constexpr int MaxDoublings = 16;

GoogleLyricsResult ofKind(GoogleLyricsResult::Kind kind) {
    GoogleLyricsResult result;
    result.kind = kind;
    return result;
}

GoogleLyricsResult browserUnavailable(const QString& reason) {
    GoogleLyricsResult result = ofKind(GoogleLyricsResult::Kind::BrowserUnavailable);
    result.reason = reason;
    return result;
}

// a + b, held at the ends of nanoseconds instead of wrapping (Kotlin's Duration and TimeMark saturate).
std::chrono::nanoseconds saturatingAdd(std::chrono::nanoseconds a, std::chrono::nanoseconds b) {
    using ns = std::chrono::nanoseconds;
    if (b > ns::zero() && a > ns::max() - b) return ns::max();
    if (b < ns::zero() && a < ns::min() - b) return ns::min();
    return a + b;
}

// duration × 2^doublings, saturated.
std::chrono::nanoseconds doubled(std::chrono::nanoseconds duration, int doublings) {
    using ns = std::chrono::nanoseconds;
    const ns::rep factor = ns::rep(1) << doublings;
    if (duration.count() > ns::max().count() / factor) return ns::max();
    if (duration.count() < ns::min().count() / factor) return ns::min();
    return duration * factor;
}

}  // namespace

/// The fetcher's turn: one lookup at a time. Taking it waits on a condition variable with the caller's stop token, so a stop
/// request ends the wait at once; the mutex is held only to read and set the flag.
class GoogleLyricsFetcher::Turn {
public:
    Turn(GoogleLyricsFetcher& owner, const std::stop_token& stop) : m_owner(owner) {
        std::unique_lock lock(m_owner.m_mutex);
        m_held = m_owner.m_turnFreed.wait(lock, stop, [this] { return !m_owner.m_busy; });
        if (m_held) m_owner.m_busy = true;
    }

    ~Turn() {
        if (!m_held) return;
        {
            std::lock_guard lock(m_owner.m_mutex);
            m_owner.m_busy = false;
        }
        // every waiter: one woken alone might be returning for its stop request and leave the turn untaken
        m_owner.m_turnFreed.notify_all();
    }

    Turn(const Turn&) = delete;
    Turn& operator=(const Turn&) = delete;

    explicit operator bool() const { return m_held; }

private:
    GoogleLyricsFetcher& m_owner;
    bool m_held = false;
};

GoogleLyricsFetcher::GoogleLyricsFetcher(RenderingBrowser& browser, FetcherOptions options)
    : m_browser(browser), m_options(std::move(options)) {}

std::optional<GoogleLyricsResult> GoogleLyricsFetcher::fetch(const QString& artist, const QString& title, std::stop_token stop) {
    try {
        return lookUp(artist, title, stop);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<GoogleLyricsResult> GoogleLyricsFetcher::lookUp(const QString& artist, const QString& title,
                                                              const std::stop_token& stop) {
    using Kind = GoogleLyricsResult::Kind;
    if (stop.stop_requested()) return std::nullopt;
    const std::vector<GoogleSearchRequest> searches = requests(artist, title);
    if (searches.empty()) return ofKind(Kind::NoCard);
    const QString key = cacheKey(searches.front());
    if (std::optional<GoogleLyricsResult> known = remembered(key)) return known;

    const Turn turn(*this, stop);
    if (!turn || stop.stop_requested()) return std::nullopt;
    // Another call may have asked about this song while this one waited for the turn.
    if (std::optional<GoogleLyricsResult> known = remembered(key)) return known;

    GoogleLyricsResult result = ofKind(Kind::NoCard);
    for (const GoogleSearchRequest& search : searches) {
        if (isCoolingDown()) {
            result = ofKind(Kind::CoolingDown);
            break;
        }
        if (!waitForInterval(stop)) return std::nullopt;
        std::optional<GoogleLyricsResult> trip = tripTo(search, stop);
        if (!trip) return std::nullopt;
        result = std::move(*trip);
        if (result.kind != Kind::NoCard) break;
    }
    if (result.kind == Kind::Found || result.kind == Kind::NoCard || result.kind == Kind::ExtractionFailed) {
        std::lock_guard lock(m_mutex);
        m_remembered.insert(key, result);
    }
    return result;
}

std::optional<GoogleLyricsResult> GoogleLyricsFetcher::remembered(const QString& key) {
    std::lock_guard lock(m_mutex);
    const auto found = m_remembered.constFind(key);
    if (found == m_remembered.constEnd()) return std::nullopt;
    return *found;
}

std::chrono::nanoseconds GoogleLyricsFetcher::now() const {
    if (m_options.clock) return m_options.clock();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

bool GoogleLyricsFetcher::isCoolingDown() const {
    return m_coolDownEnd && now() < *m_coolDownEnd;
}

// Waits until minInterval has passed since the last trip ended. false when the stop request came first (or had come already).
bool GoogleLyricsFetcher::waitForInterval(const std::stop_token& stop) {
    if (m_lastTripEnd) {
        const std::chrono::nanoseconds wait = m_options.minInterval - (now() - *m_lastTripEnd);
        if (wait > std::chrono::nanoseconds::zero()) {
            return m_options.pause ? m_options.pause(wait, stop) : waitUnlessStopped(wait, stop);
        }
    }
    return !stop.stop_requested();
}

/// One trip: the browser, then the classification of what it brought back. Nothing when the stop request ended the render: then
/// nothing is noted, neither the end of the trip nor the result.
std::optional<GoogleLyricsResult> GoogleLyricsFetcher::tripTo(const GoogleSearchRequest& request, const std::stop_token& stop) {
    using Kind = GoogleLyricsResult::Kind;
    GoogleLyricsResult result;
    try {
        const RenderResult page = m_browser.render(request.url(m_options.searchBase), m_options.pageTimeout, stop);
        switch (page.kind) {
        case RenderResult::Kind::Stopped: return std::nullopt;
        case RenderResult::Kind::TimedOut: result = ofKind(Kind::Timeout); break;
        case RenderResult::Kind::Unavailable: result = browserUnavailable(page.reason); break;
        case RenderResult::Kind::Loaded:
            if (const std::optional<Kind> block = blockOf(page.url, std::nullopt, page.bodyText)) result = ofKind(*block);
            else result = parseCard(page.card);
            break;
        }
    } catch (...) {
        // Kotlin: BrowserUnavailable(e.javaClass.simpleName).
        result = browserUnavailable(u"exception"_s);
    }
    m_lastTripEnd = now();
    record(result);
    return result;
}

/// Logs result (counts and names only) and keeps the cool-down books.
void GoogleLyricsFetcher::record(const GoogleLyricsResult& result) {
    using Kind = GoogleLyricsResult::Kind;
    switch (result.kind) {
    case Kind::Found:
        m_troubleStreak = 0;
        info(u"구글 가사 카드: 찾음 (줄 %1, 문단 %2)"_s.arg(result.lineCount).arg(result.paragraphCount));
        break;
    case Kind::NoCard:
        m_troubleStreak = 0;
        info(u"구글 가사 카드: 카드 없음"_s);
        break;
    case Kind::ExtractionFailed:
        m_troubleStreak = 0;
        warn(u"구글 가사 카드: 카드는 있으나 줄을 읽지 못함 (구글 페이지 형식이 바뀌었을 수 있음)"_s);
        break;
    case Kind::Captcha: startCoolDown(u"CAPTCHA로 막힘"_s); break;
    case Kind::Consent: startCoolDown(u"동의창이 뜸"_s); break;
    case Kind::RateLimited: startCoolDown(u"요청이 너무 많다는 응답(429)"_s); break;
    case Kind::Timeout: startCoolDown(u"시간 초과"_s); break;
    case Kind::BrowserUnavailable: warn(u"구글 가사 카드: 웹뷰를 쓸 수 없음 ("_s + result.reason + u')'); break;
    case Kind::CoolingDown: break;
    }
}

void GoogleLyricsFetcher::startCoolDown(const QString& what) {
    const std::chrono::nanoseconds length =
        std::min(m_options.maxBackoff, doubled(m_options.firstBackoff, std::min(m_troubleStreak, MaxDoublings)));
    // Kotlin counts on without end; past MaxDoublings the count changes nothing, so it stops there (and cannot overflow).
    if (m_troubleStreak < MaxDoublings) ++m_troubleStreak;
    m_coolDownEnd = saturatingAdd(now(), length);
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(length).count();
    warn(u"구글 가사 카드: "_s + what + u", "_s + QString::number(minutes) + u"분 동안 구글 검색을 쉼"_s);
}

void GoogleLyricsFetcher::info(const QString& message) const {
    if (m_options.log) m_options.log->info(message);
}

void GoogleLyricsFetcher::warn(const QString& message) const {
    if (m_options.log) m_options.log->warn(message);
}

}  // namespace xgs::lyrics::google
