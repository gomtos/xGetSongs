#include <QtTest>

#include <chrono>
#include <optional>
#include <stop_token>
#include <vector>

#include "lyrics/google/browser_channel.h"
#include "lyrics/google/google_lyrics_provider.h"
#include "support/google_fakes.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsProviderTest.kt. Cancellation is a stop request
// and an empty result (Q3, decision 7). Kotlin's TestTimeSource and `pause = {}` become a clock that stands still and a pause
// that only notes what it was asked to wait.

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::LyricsQuery;
using xgs::lyrics::google::BrowserChannel;
using xgs::lyrics::google::DriverAnswer;
using xgs::lyrics::google::DrivenBrowserOptions;
using xgs::lyrics::google::FetcherOptions;
using xgs::lyrics::google::GoogleLyricsProvider;
using xgs::lyrics::google::RenderingBrowser;
using xgs::lyrics::google::RenderResult;
using xgs::lyrics::google::Wait;
using xgs::lyrics::google::testing::FakeBrowser;
using xgs::lyrics::google::testing::loadedCard;
using xgs::lyrics::google::testing::loadedSorry;
using xgs::lyrics::google::testing::loadedWithoutCard;
using Durations = std::vector<std::chrono::nanoseconds>;

namespace {

const LyricsQuery query{u"IU"_s, u"Love poem"_s, std::nullopt, 258};

FetcherOptions standingStill(Durations* pauses = nullptr) {
    FetcherOptions options;
    options.clock = [] { return std::chrono::nanoseconds(0); };
    options.pause = [pauses](std::chrono::nanoseconds duration, std::stop_token stop) {
        if (pauses) pauses->push_back(duration);
        return !stop.stop_requested();
    };
    return options;
}

/// A browser whose render requests the stop of the lookup and answers Stopped (Kotlin's browser threw a CancellationException).
class StoppingBrowser final : public RenderingBrowser {
public:
    explicit StoppingBrowser(std::stop_source& source) : m_source(source) {}
    int calls = 0;
    RenderResult render(const QString&, std::chrono::nanoseconds, std::stop_token) override {
        ++calls;
        m_source.request_stop();
        RenderResult result;
        result.kind = RenderResult::Kind::Stopped;
        return result;
    }

private:
    std::stop_source& m_source;
};

}  // namespace

class GoogleLyricsProviderTest : public QObject {
    Q_OBJECT

private slots:
    void aFoundCardGivesItsLyrics() {
        FakeBrowser browser({loadedCard()});
        GoogleLyricsProvider provider(browser, [] {}, standingStill());

        QCOMPARE(provider.find(query, std::stop_token()), std::optional<QString>(u"더미 하나\n더미 둘\n더미 셋"_s));
        QVERIFY(browser.unexpected.isEmpty());
    }

    void noCardGivesNull() {
        FakeBrowser browser({loadedWithoutCard()});
        GoogleLyricsProvider provider(browser, [] {}, standingStill());

        QCOMPARE(provider.find(query, std::stop_token()), std::optional<QString>());
        QVERIFY(browser.unexpected.isEmpty());
    }

    void aBlockPageGivesNull() {
        FakeBrowser browser({loadedSorry()});
        GoogleLyricsProvider provider(browser, [] {}, standingStill());

        QCOMPARE(provider.find(query, std::stop_token()), std::optional<QString>());
        QVERIFY(browser.unexpected.isEmpty());
    }

    void aBlankArtistOrTitleMakesNoRequest() {
        FakeBrowser browser;
        GoogleLyricsProvider provider(browser, [] {}, standingStill());

        LyricsQuery blankArtist = query;
        blankArtist.artist = u" "_s;
        LyricsQuery blankTitle = query;
        blankTitle.title = u""_s;
        QCOMPARE(provider.find(blankArtist, std::stop_token()), std::optional<QString>());
        QCOMPARE(provider.find(blankTitle, std::stop_token()), std::optional<QString>());
        QVERIFY(browser.urls.isEmpty());
    }

    void aCancellationGetsThrough() {
        // Kotlin: the CancellationException of the browser comes out of find. Here: the stop request ends find with nothing.
        std::stop_source source;
        StoppingBrowser browser(source);
        GoogleLyricsProvider provider(browser, [] {}, standingStill());

        QCOMPARE(provider.find(query, source.get_token()), std::optional<QString>());
        QCOMPARE(browser.calls, 1);
    }

    void closingRunsTheCloseAction() {
        bool closed = false;
        FakeBrowser browser;
        GoogleLyricsProvider(browser, [&closed] { closed = true; }, standingStill()).close();

        QVERIFY(closed);
    }

    void closingTheProviderClosesTheChannel() {
        BrowserChannel channel;

        GoogleLyricsProvider(channel).close();

        const DriverAnswer answer = channel.load(u"https://example.test/"_s, Wait{std::stop_token(), 15s});
        QCOMPARE(answer.status, DriverAnswer::Status::Unavailable);
        QCOMPARE(answer.reason, u"closed"_s);
    }

    void aLookupWithNoAgentIsNullAtOnce() {
        // Kotlin checked that runTest's virtual time had not moved. Here every wait of the lookup and of its browser goes
        // through a pause that notes it, and none may have been asked for.
        BrowserChannel channel;
        Durations fetcherPauses;
        Durations browserPauses;
        DrivenBrowserOptions browserOptions;
        browserOptions.clock = [] { return std::chrono::nanoseconds(0); };
        browserOptions.pause = [&browserPauses](std::chrono::nanoseconds duration, std::stop_token stop) {
            browserPauses.push_back(duration);
            return !stop.stop_requested();
        };
        GoogleLyricsProvider provider(channel, nullptr, standingStill(&fetcherPauses), browserOptions);

        QCOMPARE(provider.find(query, std::stop_token()), std::optional<QString>());

        QVERIFY2(fetcherPauses.empty() && browserPauses.empty(), "nothing was waited for");
    }
};

QTEST_GUILESS_MAIN(GoogleLyricsProviderTest)
#include "tst_google_lyrics_provider.moc"
