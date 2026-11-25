#include <QtTest>

#include <chrono>
#include <optional>

#include "lyrics/google/page_watch.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/PageWatchTest.kt. Kotlin's TestTimeSource is a clock the
// test moves itself (m_now); Kotlin makes a new test instance, and so a new watch, per test: init() does the same. The
// added test is the Q3 brief's check of probeFromJson.

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::lyrics::google::PageProbe;
using xgs::lyrics::google::PageWatch;
using xgs::lyrics::google::probeFromJson;

namespace {

PageProbe probe(const QString& url = u"https://www.google.com/search?q=x"_s, const QString& state = u"complete"_s,
                bool card = false, const QString& body = QString()) {
    return PageProbe{url, state, card, body};
}

PageProbe withState(const QString& state, bool card = false) {
    return probe(u"https://www.google.com/search?q=x"_s, state, card);
}

PageProbe atUrl(const QString& url) {
    return probe(url);
}

}  // namespace

class PageWatchTest : public QObject {
    Q_OBJECT

    std::chrono::nanoseconds m_now{0};
    std::optional<PageWatch> m_watch;

    PageWatch& watch() { return *m_watch; }

private slots:
    void init() {
        m_now = 0ns;
        m_watch.emplace(4s, [this] { return m_now; });
    }

    void aVisibleCardEndsTheWaitAtOnce() {
        QVERIFY(watch().isDone(withState(u"loading"_s, true)));
    }

    void aBlockPageEndsTheWaitAtOnce() {
        QVERIFY(watch().isDone(probe(u"https://www.google.com/sorry/index?continue=x"_s, u"loading"_s)));
        QVERIFY(watch().isDone(probe(u"https://www.google.com/search?q=x"_s, u"complete"_s, false, u"unusual traffic"_s)));
    }

    void aPageThatIsStillLoadingIsNeverDone() {
        QVERIFY(!watch().isDone(withState(u"loading"_s)));
        m_now += 1min;
        QVERIFY(!watch().isDone(withState(u"loading"_s)));
    }

    void aCompletePageWithoutACardIsDoneOnlyOnceItHasStayedQuiet() {
        QVERIFY(!watch().isDone(probe()));
        m_now += 3s;
        QVERIFY(!watch().isDone(probe()));
        m_now += 1s;
        QVERIFY(watch().isDone(probe()));
    }

    void loadingAgainStartsTheQuietTimeOver() {
        QVERIFY(!watch().isDone(probe()));
        m_now += 3s;
        QVERIFY(!watch().isDone(withState(u"loading"_s)));
        m_now += 3s;
        QVERIFY(!watch().isDone(probe()));
        m_now += 3s;
        QVERIFY(!watch().isDone(probe()));
        m_now += 1s;
        QVERIFY(watch().isDone(probe()));
    }

    void anotherAddressStartsTheQuietTimeOver() {
        QVERIFY(!watch().isDone(atUrl(u"https://www.google.com/search?q=a"_s)));
        m_now += 3s;
        QVERIFY(!watch().isDone(atUrl(u"https://www.google.com/search?q=b"_s)));
        m_now += 3s;
        QVERIFY(!watch().isDone(atUrl(u"https://www.google.com/search?q=b"_s)));
        m_now += 1s;
        QVERIFY(watch().isDone(atUrl(u"https://www.google.com/search?q=b"_s)));
    }

    // Added (Q3 brief): what the probe script answers is read only when it has all four fields with the right types.
    void aProbeOfTheWrongShapeIsNoProbe() {
        const std::optional<PageProbe> good = probeFromJson(
            u"{\"url\":\"https://www.google.com/search?q=x\",\"readyState\":\"complete\",\"hasCard\":true,"
            u"\"bodyText\":\"더미 하나\",\"extra\":[1,{\"a\":null}]}"_s);
        QVERIFY(good.has_value());
        QCOMPARE(good->url, u"https://www.google.com/search?q=x"_s);
        QCOMPARE(good->readyState, u"complete"_s);
        QCOMPARE(good->hasCard, true);
        QCOMPARE(good->bodyText, u"더미 하나"_s);
        const std::optional<PageProbe> noCard =
            probeFromJson(u"{\"url\":\"u\",\"readyState\":\"loading\",\"hasCard\":false,\"bodyText\":\"\"}"_s);
        QVERIFY(noCard.has_value());
        QCOMPARE(noCard->hasCard, false);

        QVERIFY(!probeFromJson(u"null"_s).has_value());
        QVERIFY(!probeFromJson(u"42"_s).has_value());
        QVERIFY(!probeFromJson(u"[]"_s).has_value());
        QVERIFY(!probeFromJson(u"{\"url\":\"u\",\"readyState\":\"complete\",\"hasCard\":true}"_s).has_value());
        QVERIFY(!probeFromJson(u"{\"url\":\"u\",\"readyState\":\"complete\",\"hasCard\":\"true\",\"bodyText\":\"\"}"_s).has_value());
        QVERIFY(!probeFromJson(u""_s).has_value());
    }
};

QTEST_APPLESS_MAIN(PageWatchTest)
#include "tst_page_watch.moc"
