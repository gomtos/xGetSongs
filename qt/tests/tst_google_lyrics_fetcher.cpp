#include <QtTest>

#include <QElapsedTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

#include "lyrics/google/lyrics_fetcher.h"
#include "support/google_fakes.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsFetcherTest.kt. Kotlin ran on runTest with a
// TestTimeSource the test moved itself; here the test's clock and the test's pause stand in for it (the pause records what it
// is asked to wait and moves the clock by that much). Cancellation is a stop request and an empty result (Q3, decision 7).
// FakeBrowser cannot throw like Kotlin's `error(...)`: where Kotlin's fake would have thrown, the test checks that its
// `unexpected` list is empty.

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::lyrics::google::FetcherOptions;
using xgs::lyrics::google::GoogleLyricsFetcher;
using xgs::lyrics::google::GoogleLyricsLog;
using xgs::lyrics::google::GoogleLyricsResult;
using xgs::lyrics::google::RenderingBrowser;
using xgs::lyrics::google::RenderResult;
using xgs::lyrics::google::waitUnlessStopped;
using xgs::lyrics::google::testing::FakeBrowser;
using xgs::lyrics::google::testing::loadedBrokenCard;
using xgs::lyrics::google::testing::loadedCard;
using xgs::lyrics::google::testing::loadedSorry;
using xgs::lyrics::google::testing::loadedWithoutCard;
using xgs::lyrics::google::testing::TestSearchUrl;
using Kind = GoogleLyricsResult::Kind;
using OptKind = std::optional<Kind>;
using Durations = std::vector<std::chrono::nanoseconds>;

namespace {

OptKind kindOf(const std::optional<GoogleLyricsResult>& result) {
    return result ? OptKind(result->kind) : std::nullopt;
}

RenderResult ofKind(RenderResult::Kind kind, const QString& reason = QString()) {
    RenderResult result;
    result.kind = kind;
    result.reason = reason;
    return result;
}

RenderResult loaded(const QString& url, const QString& bodyText) {
    RenderResult result = ofKind(RenderResult::Kind::Loaded);
    result.url = url;
    result.card = u"{\"scope\":false,\"lines\":[]}"_s;
    result.bodyText = bodyText;
    return result;
}

/// A browser that does what the test says (Kotlin's `object : RenderingBrowser`).
class LambdaBrowser final : public RenderingBrowser {
public:
    using Render = std::function<RenderResult(const QString&, std::stop_token)>;
    explicit LambdaBrowser(Render render) : m_render(std::move(render)) {}
    RenderResult render(const QString& url, std::chrono::nanoseconds, std::stop_token stop) override {
        return m_render(url, stop);
    }

private:
    Render m_render;
};

class ListLog final : public GoogleLyricsLog {
public:
    void info(const QString& message) override {
        std::lock_guard lock(m_mutex);
        m_lines.append(u"INFO "_s + message);
    }
    void warn(const QString& message) override {
        std::lock_guard lock(m_mutex);
        m_lines.append(u"WARN "_s + message);
    }
    QStringList lines() const {
        std::lock_guard lock(m_mutex);
        return m_lines;
    }
    void clear() {
        std::lock_guard lock(m_mutex);
        m_lines.clear();
    }

private:
    mutable std::mutex m_mutex;
    QStringList m_lines;
};

/// Something that happens once, or more often, and that a test waits for with a limit (so that a broken fetcher fails the test
/// instead of hanging it).
class Signal {
public:
    void set() {
        {
            std::lock_guard lock(m_mutex);
            m_set = true;
        }
        m_changed.notify_all();
    }
    bool waitFor(std::chrono::nanoseconds limit) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, limit, [this] { return m_set; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_set = false;
};

// The limit of a wait for something that happens within milliseconds; generous for a loaded machine and the debug build.
constexpr std::chrono::seconds EventLimit{30};

bool anyContains(const QStringList& lines, const QString& part) {
    for (const QString& line : lines)
        if (line.contains(part)) return true;
    return false;
}

}  // namespace

class GoogleLyricsFetcherTest : public QObject {
    Q_OBJECT

    // The test's clock and the record of the pauses. Only the thread holding the fetcher's turn reads or moves them, but the
    // threaded tests read them from the test's thread too, so a mutex guards them.
    mutable std::mutex m_timeMutex;
    std::chrono::nanoseconds m_time{0};
    Durations m_pauses;
    ListLog m_log;

    std::chrono::nanoseconds now() const {
        std::lock_guard lock(m_timeMutex);
        return m_time;
    }

    void advance(std::chrono::nanoseconds duration) {
        std::lock_guard lock(m_timeMutex);
        m_time += duration;
    }

    Durations pauses() const {
        std::lock_guard lock(m_timeMutex);
        return m_pauses;
    }

    FetcherOptions options() {
        FetcherOptions out;
        out.clock = [this] { return now(); };
        out.pause = [this](std::chrono::nanoseconds duration, std::stop_token stop) {
            if (stop.stop_requested()) return false;
            std::lock_guard lock(m_timeMutex);
            m_pauses.push_back(duration);
            m_time += duration;
            return true;
        };
        out.log = &m_log;
        return out;
    }

    std::optional<GoogleLyricsResult> fetch(GoogleLyricsFetcher& fetcher, const QString& artist, const QString& title) {
        return fetcher.fetch(artist, title, std::stop_token());
    }

private slots:
    void init() {
        std::lock_guard lock(m_timeMutex);
        m_time = std::chrono::nanoseconds(0);
        m_pauses.clear();
        m_log.clear();
    }

    void aFoundCardGivesItsLyricsAfterOneRequest() {
        FakeBrowser browser({loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        const std::optional<GoogleLyricsResult> found = fetch(fetcher, u"IU"_s, u"Love poem"_s);

        QCOMPARE(kindOf(found), OptKind(Kind::Found));
        QCOMPARE(found->lyrics, u"더미 하나\n더미 둘\n더미 셋"_s);
        QCOMPARE(browser.urls, QStringList{u"https://www.google.com/search?q=IU%20Love%20poem%20lyrics&hl=en"_s});
        QVERIFY(anyContains(m_log.lines(), u"줄 3, 문단 1"_s));
        QVERIFY2(!anyContains(m_log.lines(), u"더미"_s), "the log must not hold lyrics");
        QVERIFY(browser.unexpected.isEmpty());
    }

    void noCardForAnArtistWithAParenthesisedNameTriesTheNameWithoutItOnce() {
        FakeBrowser browser({loadedWithoutCard(), loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        const std::optional<GoogleLyricsResult> result = fetch(fetcher, u"RESCENE (리센느)"_s, u"LOVE ATTACK"_s);

        QCOMPARE(kindOf(result), OptKind(Kind::Found));
        QCOMPARE(browser.urls.size(), 2);
        QVERIFY(browser.urls[1].contains(u"q=RESCENE%20LOVE%20ATTACK%20lyrics&hl=ko"_s));
        QVERIFY2(pauses() == Durations{10s}, "the second request keeps the minimum interval too");
        QVERIFY(browser.unexpected.isEmpty());
    }

    void noCardTwiceIsNoCardAndIsNotAskedAgain() {
        FakeBrowser browser({loadedWithoutCard(), loadedWithoutCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetch(fetcher, u"RESCENE (리센느)"_s, u"LOVE ATTACK"_s)), OptKind(Kind::NoCard));
        QCOMPARE(browser.urls.size(), 2);
        QCOMPARE(kindOf(fetch(fetcher, u"RESCENE (리센느)"_s, u"LOVE ATTACK"_s)), OptKind(Kind::NoCard));
        QVERIFY2(browser.urls.size() == 2, "a song seen once is not asked again");
        QVERIFY(browser.unexpected.isEmpty());
    }

    void aPlainArtistWithNoCardMakesOnlyOneRequest() {
        FakeBrowser browser({loadedWithoutCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::NoCard));
        QCOMPARE(browser.urls.size(), 1);
        QVERIFY(browser.unexpected.isEmpty());
    }

    void theSameSongWrittenDifferentlyIsAskedOnce() {
        FakeBrowser browser({loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::Found));
        QCOMPARE(kindOf(fetch(fetcher, u" iu "_s, u"LOVE POEM"_s)), OptKind(Kind::Found));
        QCOMPARE(browser.urls.size(), 1);
        QVERIFY(browser.unexpected.isEmpty());
    }

    void twoSongsAreKeptTheMinimumIntervalApart() {
        FakeBrowser browser({loadedCard(), loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        fetch(fetcher, u"IU"_s, u"Love poem"_s);
        QVERIFY2(pauses().empty(), "the first request waits for nothing");
        advance(4s);
        fetch(fetcher, u"IU"_s, u"Palette"_s);

        QVERIFY(pauses() == Durations{6s});
        QVERIFY(browser.unexpected.isEmpty());
    }

    void noPauseWhenTheIntervalHasPassedAlready() {
        FakeBrowser browser({loadedCard(), loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        fetch(fetcher, u"IU"_s, u"Love poem"_s);
        advance(11s);
        fetch(fetcher, u"IU"_s, u"Palette"_s);

        QVERIFY(pauses().empty());
        QVERIFY(browser.unexpected.isEmpty());
    }

    void aBlockPageStartsACoolDownDuringWhichNothingIsRequested() {
        FakeBrowser browser({loadedSorry(), loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::Captcha));
        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Palette"_s)), OptKind(Kind::CoolingDown));
        QVERIFY2(browser.urls.size() == 1, "no request while cooling down");
        advance(5min);
        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Palette"_s)), OptKind(Kind::Found));
        QCOMPARE(browser.urls.size(), 2);
        QVERIFY(browser.unexpected.isEmpty());
    }

    void theCoolDownDoublesWhileGoogleKeepsBlockingAndStartsOverWhenItAnswers() {
        FakeBrowser browser({loadedSorry(), loadedSorry(), loadedCard(), loadedSorry(), loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetch(fetcher, u"A"_s, u"a"_s)), OptKind(Kind::Captcha));  // first block: 5 minutes
        advance(5min);
        QCOMPARE(kindOf(fetch(fetcher, u"B"_s, u"b"_s)), OptKind(Kind::Captcha));  // blocked again: 10 minutes
        advance(9min);
        QCOMPARE(kindOf(fetch(fetcher, u"C"_s, u"c"_s)), OptKind(Kind::CoolingDown));
        advance(1min);
        QCOMPARE(kindOf(fetch(fetcher, u"C"_s, u"c"_s)), OptKind(Kind::Found));    // an ordinary answer ends the streak
        QCOMPARE(kindOf(fetch(fetcher, u"D"_s, u"d"_s)), OptKind(Kind::Captcha));  // blocked again: back to 5 minutes
        advance(5min);
        QCOMPARE(kindOf(fetch(fetcher, u"E"_s, u"e"_s)), OptKind(Kind::Found));
        QVERIFY(browser.unexpected.isEmpty());
    }

    void theCoolDownIsCappedAtSixtyMinutes() {
        FakeBrowser browser({loadedSorry(), loadedSorry(), loadedSorry(), loadedSorry(), loadedSorry(), loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        const int minutes[] = {5, 10, 20, 40};
        for (int index = 0; index < 4; ++index) {
            QCOMPARE(kindOf(fetch(fetcher, u"S%1"_s.arg(index), u"t"_s)), OptKind(Kind::Captcha));
            advance(std::chrono::minutes(minutes[index]));
        }
        QCOMPARE(kindOf(fetch(fetcher, u"S4"_s, u"t"_s)), OptKind(Kind::Captcha));  // 5 * 2^4 = 80, capped
        advance(59min);
        QCOMPARE(kindOf(fetch(fetcher, u"S5"_s, u"t"_s)), OptKind(Kind::CoolingDown));
        advance(1min);
        QCOMPARE(kindOf(fetch(fetcher, u"S5"_s, u"t"_s)), OptKind(Kind::Found));
        QVERIFY(browser.unexpected.isEmpty());
    }

    void aTimeoutAlsoStartsACoolDown() {
        FakeBrowser browser({ofKind(RenderResult::Kind::TimedOut)});
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::Timeout));
        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Palette"_s)), OptKind(Kind::CoolingDown));
        QVERIFY(browser.unexpected.isEmpty());
    }

    void blockPagesAreToldApart() {
        const RenderResult consent = loaded(u"https://consent.google.com/m?continue=x"_s, u""_s);
        const RenderResult tooMany = loaded(TestSearchUrl, u"429 Too Many Requests"_s);

        FakeBrowser consentBrowser({consent});
        GoogleLyricsFetcher consentFetcher(consentBrowser, options());
        QCOMPARE(kindOf(fetch(consentFetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::Consent));
        FakeBrowser tooManyBrowser({tooMany});
        GoogleLyricsFetcher tooManyFetcher(tooManyBrowser, options());
        QCOMPARE(kindOf(fetch(tooManyFetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::RateLimited));
        FakeBrowser sorryBrowser({loadedSorry()});
        GoogleLyricsFetcher sorryFetcher(sorryBrowser, options());
        QCOMPARE(kindOf(fetch(sorryFetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::Captcha));
    }

    void anUnavailableBrowserNeitherCoolsDownNorIsRemembered() {
        FakeBrowser browser({ofKind(RenderResult::Kind::Unavailable, u"NoClassDefFoundError"_s), loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        const std::optional<GoogleLyricsResult> first = fetch(fetcher, u"IU"_s, u"Love poem"_s);
        QCOMPARE(kindOf(first), OptKind(Kind::BrowserUnavailable));
        QCOMPARE(first->reason, u"NoClassDefFoundError"_s);
        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::Found));
        QCOMPARE(browser.urls.size(), 2);
        QVERIFY(browser.unexpected.isEmpty());
    }

    void aFailedExtractionIsRememberedAndDoesNotCoolDown() {
        FakeBrowser browser({loadedBrokenCard(), loadedCard()});
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::ExtractionFailed));
        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::ExtractionFailed));
        QCOMPARE(browser.urls.size(), 1);
        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Palette"_s)), OptKind(Kind::Found));
        bool warned = false;
        for (const QString& line : m_log.lines())
            if (line.startsWith(u"WARN"_s) && line.contains(u"형식이 바뀌었을 수"_s)) warned = true;
        QVERIFY(warned);
        QVERIFY(browser.unexpected.isEmpty());
    }

    void anExceptionFromTheBrowserIsAnUnavailableBrowser() {
        // Kotlin threw IllegalStateException("boom") and expected its class name; C++ gives the reason `exception`.
        LambdaBrowser browser([](const QString&, std::stop_token) -> RenderResult { throw std::runtime_error("boom"); });
        GoogleLyricsFetcher fetcher(browser, options());

        const std::optional<GoogleLyricsResult> result = fetch(fetcher, u"IU"_s, u"Love poem"_s);
        QCOMPARE(kindOf(result), OptKind(Kind::BrowserUnavailable));
        QCOMPARE(result->reason, u"exception"_s);
    }

    void aCancellationIsNotSwallowed() {
        // Kotlin's browser threw a CancellationException and the fetch let it through. Here the stop request comes while the
        // browser works, the browser answers Stopped, and the fetch gives nothing: no cool-down, nothing remembered, and the
        // stopped trip does not count for the interval (Kotlin set the end of the trip only after the browser returned).
        std::stop_source source;
        int calls = 0;
        LambdaBrowser browser([&](const QString&, std::stop_token) {
            ++calls;
            if (calls == 1) {
                source.request_stop();
                return ofKind(RenderResult::Kind::Stopped);
            }
            return loadedCard();
        });
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetcher.fetch(u"IU"_s, u"Love poem"_s, source.get_token())), OptKind());
        QCOMPARE(m_log.lines(), QStringList());

        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::Found));
        QCOMPARE(calls, 2);
        QVERIFY(pauses().empty());
    }

    void aBlankArtistOrTitleMakesNoRequest() {
        FakeBrowser browser;
        GoogleLyricsFetcher fetcher(browser, options());

        QCOMPARE(kindOf(fetch(fetcher, u""_s, u"Love poem"_s)), OptKind(Kind::NoCard));
        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u" "_s)), OptKind(Kind::NoCard));
        QVERIFY(browser.urls.isEmpty());
    }

    // Added (Q3 Task 4): threads and stop requests.

    void stoppingEndsEveryWaitingLookupPromptly() {
        // 64 threads ask for 64 songs; the browser holds the first trip until it is stopped, so one thread is in the browser
        // and the others wait for their turn. One stop request (all threads share its token) must end them all within a
        // second (generous: waking 64 threads takes milliseconds, even in the debug build).
        constexpr int threads = 64;
        std::atomic<bool> block{true};
        std::atomic<int> renders{0};
        Signal entered;
        LambdaBrowser browser([&](const QString&, std::stop_token stop) {
            ++renders;
            if (!block) return loadedCard();
            entered.set();
            waitUnlessStopped(std::chrono::nanoseconds::max(), stop);
            return ofKind(RenderResult::Kind::Stopped);
        });
        GoogleLyricsFetcher fetcher(browser, options());

        std::stop_source source;
        std::latch started(threads);
        std::vector<std::optional<GoogleLyricsResult>> results(threads);
        bool inBrowser = false;
        QElapsedTimer timer;
        {
            std::vector<std::jthread> workers;
            for (int i = 0; i < threads; ++i) {
                workers.emplace_back([&, i] {
                    started.count_down();
                    results[i] = fetcher.fetch(u"Artist %1"_s.arg(i), u"Song %1"_s.arg(i), source.get_token());
                });
            }
            started.wait();
            inBrowser = entered.waitFor(EventLimit);
            timer.start();
            source.request_stop();
            for (std::jthread& worker : workers) worker.join();
        }
        QVERIFY(inBrowser);
        QVERIFY2(timer.elapsed() < 1000, qPrintable(u"%1 ms"_s.arg(timer.elapsed())));
        for (int i = 0; i < threads; ++i)
            QVERIFY2(!results[i].has_value(), qPrintable(results[i] ? results[i]->toString() : QString()));
        QCOMPARE(renders.load(), 1);
        QCOMPARE(m_log.lines(), QStringList());

        // Nothing was held against Google and the stopped trip set no interval: the next lookup goes at once.
        block = false;
        QCOMPARE(kindOf(fetch(fetcher, u"Artist 0"_s, u"Song 0"_s)), OptKind(Kind::Found));
        QCOMPARE(renders.load(), 2);
        QVERIFY(pauses().empty());
    }

    void aStopDuringTheIntervalWaitMakesNoTrip() {
        // The pause really waits here (the test's clock does not move), so the second lookup is inside the interval wait when
        // it is stopped. A pause that ignored the stop would wait ten seconds and then make the trip.
        FakeBrowser browser({loadedCard(), loadedCard()});
        FetcherOptions opts = options();
        Signal waiting;
        opts.pause = [&](std::chrono::nanoseconds duration, std::stop_token stop) {
            waiting.set();
            return waitUnlessStopped(duration, stop);
        };
        GoogleLyricsFetcher fetcher(browser, opts);
        QCOMPARE(kindOf(fetch(fetcher, u"IU"_s, u"Love poem"_s)), OptKind(Kind::Found));

        std::optional<GoogleLyricsResult> second = GoogleLyricsResult{};
        bool paused = false;
        QElapsedTimer timer;
        {
            std::jthread worker([&](std::stop_token stop) { second = fetcher.fetch(u"IU"_s, u"Palette"_s, stop); });
            paused = waiting.waitFor(EventLimit);
            timer.start();
            worker.request_stop();
        }
        QVERIFY2(paused, "the second lookup waits out the interval");
        // Generous: the stop wakes the wait at once; ignoring it would take the whole ten seconds.
        QVERIFY2(timer.elapsed() < 5000, qPrintable(u"%1 ms"_s.arg(timer.elapsed())));
        QCOMPARE(kindOf(second), OptKind());
        QCOMPARE(browser.urls.size(), 1);
        QVERIFY(browser.unexpected.isEmpty());
    }

    void lookupsFromManyThreadsNeverOverlapAndKeepTheInterval() {
        // Eight threads ask for eight songs at once. The browser notes when each trip starts and ends on the test's clock and
        // whether two were ever inside it together; it yields a few times inside to give an overlap the chance to happen.
        constexpr int threads = 8;
        std::atomic<int> inside{0};
        std::atomic<bool> overlapped{false};
        std::mutex tripsMutex;
        std::vector<std::pair<std::chrono::nanoseconds, std::chrono::nanoseconds>> trips;
        LambdaBrowser browser([&](const QString&, std::stop_token) {
            if (++inside > 1) overlapped = true;
            const std::chrono::nanoseconds start = now();
            for (int i = 0; i < 100; ++i) std::this_thread::yield();
            const std::chrono::nanoseconds end = now();
            {
                std::lock_guard lock(tripsMutex);
                trips.emplace_back(start, end);
            }
            --inside;
            return loadedCard();
        });
        GoogleLyricsFetcher fetcher(browser, options());

        std::vector<std::optional<GoogleLyricsResult>> results(threads);
        {
            std::latch go(1);
            std::vector<std::jthread> workers;
            for (int i = 0; i < threads; ++i) {
                workers.emplace_back([&, i] {
                    go.wait();
                    results[i] = fetcher.fetch(u"Artist %1"_s.arg(i), u"Song %1"_s.arg(i), std::stop_token());
                });
            }
            go.count_down();
        }
        for (const auto& result : results) QCOMPARE(kindOf(result), OptKind(Kind::Found));
        QVERIFY(!overlapped);
        QCOMPARE(int(trips.size()), threads);
        for (std::size_t i = 1; i < trips.size(); ++i) {
            const auto gap = trips[i].first - trips[i - 1].second;
            QVERIFY2(gap >= 10s, qPrintable(u"trip %1 began %2 ns after the one before ended"_s.arg(i).arg(gap.count())));
        }
    }

    void theSecondOfTwoWaitersForOneSongUsesTheRememberedAnswer() {
        // A asks for a song and the browser holds it; B asks for the same song meanwhile and waits for its turn. When A's
        // answer comes, B must take it from what was remembered (the look after the turn) and not make a second trip, which
        // the fake would answer as unexpected.
        FakeBrowser answers({loadedCard()});
        std::mutex gateMutex;
        std::condition_variable gateChanged;
        bool open = false;
        Signal entered;
        LambdaBrowser browser([&](const QString& url, std::stop_token stop) {
            entered.set();
            std::unique_lock lock(gateMutex);
            gateChanged.wait(lock, [&] { return open; });
            lock.unlock();
            return answers.render(url, 20s, stop);
        });
        GoogleLyricsFetcher fetcher(browser, options());

        std::optional<GoogleLyricsResult> first;
        std::optional<GoogleLyricsResult> second;
        bool inBrowser = false;
        {
            std::jthread a([&] { first = fetcher.fetch(u"IU"_s, u"Love poem"_s, std::stop_token()); });
            inBrowser = entered.waitFor(EventLimit);
            std::jthread b([&] { second = fetcher.fetch(u"IU"_s, u"Love poem"_s, std::stop_token()); });
            // Gives B time to make its first look and start waiting for the turn. Nothing outside the fetcher shows that B
            // waits; should B be slower than this, it finds the answer in its first look instead and the test still passes,
            // it just does not exercise the second look that time.
            std::this_thread::sleep_for(200ms);
            {
                std::lock_guard lock(gateMutex);
                open = true;
            }
            gateChanged.notify_all();
        }
        QVERIFY(inBrowser);
        QCOMPARE(kindOf(first), OptKind(Kind::Found));
        QCOMPARE(kindOf(second), OptKind(Kind::Found));
        QCOMPARE(answers.urls.size(), 1);
        QVERIFY(answers.unexpected.isEmpty());
    }
};

QTEST_GUILESS_MAIN(GoogleLyricsFetcherTest)
#include "tst_google_lyrics_fetcher.moc"
