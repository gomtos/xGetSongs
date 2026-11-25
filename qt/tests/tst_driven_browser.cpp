#include <QtTest>

#include <QElapsedTimer>

#include <chrono>
#include <latch>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include "lyrics/google/driven_browser.h"
#include "support/google_fakes.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/DrivenBrowserTest.kt. Kotlin ran on runTest's virtual time;
// here the test's clock and the test's pause stand in for it (the pause moves the clock by what it is asked to wait). The card
// script's answer takes the place of the page's HTML (Q3, decision 5): the three names with "Html" in them are about the card
// read. Cancellation is a stop request and a Stopped result (Q3, decision 7).

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::lyrics::google::DriverAnswer;
using xgs::lyrics::google::DrivenBrowser;
using xgs::lyrics::google::DrivenBrowserOptions;
using xgs::lyrics::google::RenderResult;
using xgs::lyrics::google::testing::FakeDriver;
using xgs::lyrics::google::testing::cardResult;
using Kind = RenderResult::Kind;
using Probes = std::vector<std::optional<QString>>;

namespace {

const QString url = u"https://www.google.com/search?q=x&hl=en"_s;

QString probe(const QString& probeUrl = url, const QString& readyState = u"complete"_s, bool hasCard = false,
              const QString& bodyText = QString()) {
    return u"{\"url\":\"%1\",\"readyState\":\"%2\",\"hasCard\":%3,\"bodyText\":\"%4\"}"_s.arg(
        probeUrl, readyState, hasCard ? u"true"_s : u"false"_s, bodyText);
}

QString probeWithCard(const QString& bodyText = QString()) {
    return probe(url, u"complete"_s, true, bodyText);
}

QString probeLoading() {
    return probe(url, u"loading"_s);
}

int countOf(const QStringList& calls, const QString& call) {
    return int(calls.count(call));
}

}  // namespace

class DrivenBrowserTest : public QObject {
    Q_OBJECT

    std::chrono::nanoseconds m_time{0};

    DrivenBrowserOptions options() {
        DrivenBrowserOptions out;
        out.pollInterval = 500ms;
        out.settleTime = 4s;
        out.clock = [this] { return m_time; };
        out.pause = [this](std::chrono::nanoseconds duration, std::stop_token stop) {
            if (stop.stop_requested()) return false;
            m_time += duration;
            return true;
        };
        return out;
    }

    RenderResult render(FakeDriver& driver, std::chrono::nanoseconds timeout = 20s, std::stop_token stop = {}) {
        DrivenBrowser browser(driver, options());
        return browser.render(url, timeout, stop);
    }

private slots:
    void aPageThatShowsACardIsReturnedWithItsHtml() {
        const QString card = cardResult({{u"더미 하나"_s, u"더미 둘"_s, u"더미 셋"_s}});
        FakeDriver driver(Probes{probeWithCard(u"text"_s)}, card);

        const RenderResult result = render(driver);

        QCOMPARE(result.kind, Kind::Loaded);
        QCOMPARE(result.url, url);
        QCOMPARE(result.card, card);
        QCOMPARE(result.bodyText, u"text"_s);
    }

    void aCompletePageThatStaysTheSameForTheSettleTimeIsReturned() {
        FakeDriver driver(Probes{probeLoading(), probe(), probe(), probe()});

        const RenderResult result = render(driver);

        QCOMPARE(result.kind, Kind::Loaded);
        // Complete from the second look on: four seconds of looks every half second must pass before it counts as settled.
        QVERIFY2(countOf(driver.calls, u"probe"_s) >= 10, qPrintable(driver.calls.join(u", ")));
    }

    void aProbeOfTheBlankPageIsIgnored() {
        FakeDriver driver(Probes{probe(u"about:blank"_s, u"complete"_s, true), probeWithCard(u"real"_s)});

        const RenderResult result = render(driver);

        QCOMPARE(result.kind, Kind::Loaded);
        QCOMPARE(result.bodyText, u"real"_s);
    }

    void aProbeThatIsNullOrNotJsonKeepsWaiting() {
        FakeDriver driver(Probes{std::nullopt, u"this is not json"_s, probeWithCard()});

        QCOMPARE(render(driver).kind, Kind::Loaded);
    }

    void aPageThatNeverSettlesTimesOut() {
        FakeDriver driver(Probes{probeLoading()});

        QCOMPARE(render(driver, 10s).kind, Kind::TimedOut);
    }

    void aDriverThatIsUnavailableGivesUnavailableWithItsReason() {
        FakeDriver noAgent(Probes{probe()});
        noAgent.loadFailure = FakeDriver::unavailable(u"no-agent"_s);
        // Kotlin's second driver threw IllegalStateException("boom") and got the class's name as the reason; a C++ driver does
        // not throw, so it says the reason itself.
        FakeDriver broken(Probes{probe()});
        broken.loadFailure = FakeDriver::unavailable(u"IllegalStateException"_s);

        const RenderResult noAgentResult = render(noAgent);
        const RenderResult brokenResult = render(broken);
        QCOMPARE(noAgentResult.kind, Kind::Unavailable);
        QCOMPARE(noAgentResult.reason, u"no-agent"_s);
        QCOMPARE(brokenResult.kind, Kind::Unavailable);
        QCOMPARE(brokenResult.reason, u"IllegalStateException"_s);
    }

    void aLookAtThePageThatFailsEndsTheRenderAsUnavailableAtOnce() {
        // The browser could not run the script (its window is gone, the page does not answer): waiting on it for the rest of the time
        // limit would end as a timeout, which is held against Google.
        FakeDriver driver(Probes{probe()});
        driver.probeFailure = FakeDriver::unavailable(u"script timed out"_s);

        const RenderResult result = render(driver);

        QCOMPARE(result.kind, Kind::Unavailable);
        QCOMPARE(result.reason, u"script timed out"_s);
        QVERIFY2(countOf(driver.calls, u"probe"_s) == 1, qPrintable(u"no second look: "_s + driver.calls.join(u", ")));
    }

    void aReadOfThePagesHtmlThatFailsIsNotAnEmptyPage() {
        // An empty page would be read as "no lyrics card" and remembered for the song, although the page had shown its card.
        FakeDriver driver(Probes{probeWithCard()});
        driver.htmlFailure = FakeDriver::unavailable(u"script timed out"_s);

        const RenderResult result = render(driver);

        QCOMPARE(result.kind, Kind::Unavailable);
        QCOMPARE(result.reason, u"script timed out"_s);
    }

    void aPageThatGivesNoHtmlIsUnavailableNotAnEmptyPage() {
        FakeDriver driver(Probes{probeWithCard()}, std::nullopt);

        const RenderResult result = render(driver);

        QCOMPARE(result.kind, Kind::Unavailable);
        QCOMPARE(result.reason, u"no-html"_s);
    }

    void theBrowserIsResetAfterEveryRenderEvenWhenItWasCancelled() {
        FakeDriver finished(Probes{probeWithCard()});
        render(finished);
        QCOMPARE(finished.calls.last(), u"reset"_s);

        // Kotlin cancelled the render while it waited between two looks. Here a stop request reaches it there, on another thread,
        // with the real pause and clock: a poll interval of an hour shows that the pause wakes on the stop request and does
        // not sleep it out (the bound on the time taken is generous so that a slow machine does not fail it).
        FakeDriver endless(Probes{probeLoading()});
        std::latch loaded(1);
        endless.onLoad = [&loaded] { loaded.count_down(); };
        DrivenBrowserOptions realTime;
        realTime.pollInterval = 1h;
        DrivenBrowser browser(endless, realTime);
        std::stop_source stopSource;
        RenderResult result;
        QElapsedTimer timer;
        timer.start();
        {
            std::jthread thread([&] { result = browser.render(url, 2h, stopSource.get_token()); });
            loaded.wait();
            stopSource.request_stop();
        }
        QVERIFY2(timer.elapsed() < 30'000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(result.kind, Kind::Stopped);
        QCOMPARE(endless.calls.last(), u"reset"_s);
        QCOMPARE(endless.resetStopPossible, std::optional<bool>(false));
    }

    void aCancellationGetsThrough() {
        FakeDriver driver(Probes{probe()});
        driver.loadFailure = FakeDriver::stopped();

        QCOMPARE(render(driver).kind, Kind::Stopped);
    }

    // Added (Q3 brief, decision 8): every driver call gets the time that is left as its limit; reset gets a short limit of its
    // own and a stop token that cannot stop, also after a stop request.
    void everyDriverCallGetsTheTimeThatIsLeft() {
        FakeDriver driver(Probes{probeLoading(), probeWithCard()});

        QCOMPARE(render(driver, 20s).kind, Kind::Loaded);

        QCOMPARE(driver.calls, (QStringList{u"load "_s + url, u"probe"_s, u"probe"_s, u"html"_s, u"reset"_s}));
        const std::vector<std::chrono::nanoseconds> expected{20s, 19500ms, 19s, 19s, DrivenBrowser::ResetLimit};
        QVERIFY(driver.limits == expected);
        QCOMPARE(driver.resetStopPossible, std::optional<bool>(false));

        std::stop_source stopSource;
        stopSource.request_stop();
        FakeDriver stoppedDriver(Probes{probe()});
        QCOMPARE(render(stoppedDriver, 20s, stopSource.get_token()).kind, Kind::Stopped);
        QCOMPARE(stoppedDriver.calls, QStringList{u"reset"_s});
        QVERIFY(stoppedDriver.limits == std::vector<std::chrono::nanoseconds>{DrivenBrowser::ResetLimit});
        QCOMPARE(stoppedDriver.resetStopPossible, std::optional<bool>(false));
    }
};

QTEST_APPLESS_MAIN(DrivenBrowserTest)
#include "tst_driven_browser.moc"
