#pragma once

#include <QHash>
#include <QString>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>

#include "lyrics/google/driven_browser.h"
#include "lyrics/google/lyrics_result.h"
#include "lyrics/google/page_watch.h"
#include "lyrics/google/rendering_browser.h"
#include "lyrics/google/search_query.h"

// Port of engine/.../lyrics/google/GoogleLyricsFetcher.kt.
namespace xgs::lyrics::google {

struct FetcherOptions {
    std::chrono::nanoseconds minInterval{std::chrono::seconds(10)};
    std::chrono::nanoseconds firstBackoff{std::chrono::minutes(5)};
    std::chrono::nanoseconds maxBackoff{std::chrono::minutes(60)};
    std::chrono::nanoseconds pageTimeout{std::chrono::seconds(20)};
    /// Where the searches go (Q3, decision 12): a check points it at its own fixture server.
    QString searchBase = DefaultSearchBase.toString();
    Clock clock;                       // empty: std::chrono::steady_clock
    Pause pause;                       // how the interval is waited out; empty: waitUnlessStopped
    GoogleLyricsLog* log = nullptr;    // not owned; nothing: no log
};

/// Looks a song up in Google's lyrics card through a RenderingBrowser, politely. The rules:
///  - one trip per song, or two when the first finds no card and the artist has a parenthesised name (requests()); trips run
///    one at a time, at least minInterval after the previous one ended;
///  - Google answering with trouble (Captcha, Consent, RateLimited, Timeout) puts Google off limits for firstBackoff, then
///    twice that for the next trouble in a row, and so on up to maxBackoff (min(maxBackoff, firstBackoff × 2^min(streak, 16)));
///    an ordinary answer ends the streak. While it is off limits the answer is CoolingDown and no trip is made. Nothing tries
///    to get around a block;
///  - what Google said about a song (Found, NoCard, ExtractionFailed) is remembered for as long as the fetcher lives, under
///    cacheKey() of its first search; trouble and an unusable browser are not, they say nothing about the song.
///
/// Whatever goes wrong is a GoogleLyricsResult; an exception from the browser or from reading its page is
/// BrowserUnavailable("exception") (Kotlin: the exception's class name). fetch never throws.
///
/// Threads. fetch may be called from any number of threads at once. A call takes the fetcher's turn for its whole lookup
/// (both trips of a song), so trips never overlap; the turn is a flag that waiting calls watch on a condition variable, never
/// a lock held across a wait. A call that waits for the turn, waits out the interval (with pause) or waits inside the browser
/// returns nothing at once when its own stop token is triggered; a stopped lookup starts no cool-down, is not remembered and
/// does not count as a trip for the interval (Kotlin: the CancellationException passed by before the end of the trip was
/// noted). The remembered answer is looked up before waiting for the turn and again once the turn is taken: another call may
/// have asked about the song meanwhile. The log is called from whichever thread made the trip, while it holds the turn.
///
/// Differences from Kotlin: a stopped call returns nothing where Kotlin throws a CancellationException, and a call whose stop
/// was requested before it began (or before it got the turn, or between its trips) returns nothing even where Kotlin would
/// have answered without suspending (a remembered answer, CoolingDown, a blank song).
class GoogleLyricsFetcher {
public:
    /// The browser is referenced and must outlive the fetcher.
    explicit GoogleLyricsFetcher(RenderingBrowser& browser, FetcherOptions options = {});
    GoogleLyricsFetcher(const GoogleLyricsFetcher&) = delete;
    GoogleLyricsFetcher& operator=(const GoogleLyricsFetcher&) = delete;

    /// What Google's lyrics card says about the song; nothing when stop was requested (or, should something throw outside the
    /// trip, std::bad_alloc say, or the log).
    std::optional<GoogleLyricsResult> fetch(const QString& artist, const QString& title, std::stop_token stop);

private:
    class Turn;

    std::optional<GoogleLyricsResult> lookUp(const QString& artist, const QString& title, const std::stop_token& stop);
    std::optional<GoogleLyricsResult> remembered(const QString& key);
    bool isCoolingDown() const;
    bool waitForInterval(const std::stop_token& stop);
    std::optional<GoogleLyricsResult> tripTo(const GoogleSearchRequest& request, const std::stop_token& stop);
    void record(const GoogleLyricsResult& result);
    void startCoolDown(const QString& what);
    void info(const QString& message) const;
    void warn(const QString& message) const;
    std::chrono::nanoseconds now() const;

    RenderingBrowser& m_browser;
    FetcherOptions m_options;

    // The turn and the remembered answers share one mutex, held only for a moment, never across a wait or a trip.
    std::mutex m_mutex;
    std::condition_variable_any m_turnFreed;
    bool m_busy = false;
    QHash<QString, GoogleLyricsResult> m_remembered;

    // The three below are only touched by the call that holds the turn.
    std::optional<std::chrono::nanoseconds> m_lastTripEnd;
    std::optional<std::chrono::nanoseconds> m_coolDownEnd;
    int m_troubleStreak = 0;
};

}  // namespace xgs::lyrics::google
