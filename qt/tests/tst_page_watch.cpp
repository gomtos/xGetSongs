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
        // (This line once expected `"hasCard":"true"` to be refused; the Q3 Task 5 differential showed that Kotlin reads it as
        // true: see aProbeIsReadAsKotlinDecodesIt. A string that is no boolean is refused.)
        QVERIFY(!probeFromJson(u"{\"url\":\"u\",\"readyState\":\"complete\",\"hasCard\":\"yes\",\"bodyText\":\"\"}"_s).has_value());
        QVERIFY(!probeFromJson(u""_s).has_value());
    }

    // Added (Q3 Task 5, found by the differential `P pj`): Kotlin's decodeFromString<PageProbe> is not parseToJsonElement. It
    // takes the next key without a comma, skips the values of other keys without checking them, reads true and false in any
    // ASCII case, quoted or not, refuses null for a string, checks every occurrence of a repeated key, and keeps the last.
    void aProbeIsReadAsKotlinDecodesIt() {
        const auto read = [](const QString& members) { return probeFromJson(u"{"_s + members + u"}"_s); };
        const QString base = u"\"url\":\"u\",\"readyState\":\"r\",\"bodyText\":\"b\","_s;

        // the next key without a comma, after any kind of value
        QVERIFY(read(u"\"url\":\"u\" \"readyState\":\"r\" \"bodyText\":\"b\" \"hasCard\":true"_s).has_value());
        QVERIFY(read(base + u"\"hasCard\":false\"x\":1"_s).has_value());
        QVERIFY(read(base + u"\"hasCard\":true,\"x\":1 \"y\":null \"z\":{} \"w\":2"_s).has_value());
        // the value of another key is skipped unchecked inside a container, and any literal will do
        QVERIFY(read(base + u"\"hasCard\":true,\"x\":[1,,2],\"y\":{\"a\":},\"z\":#,\"v\":'y'"_s).has_value());
        QVERIFY(!read(base + u"\"hasCard\":true,\"x\":[}]"_s).has_value());
        QVERIFY(!read(base + u"\"hasCard\":true,\"x\":\"a\"b"_s).has_value());
        // booleans in any ASCII case, quoted (escapes decoded) or not; nothing else
        for (const QString& yes : {u"TRUE"_s, u"True"_s, u"tRuE"_s, u"\"true\""_s, u"\"TRUE\""_s, u"\"\\u0074rue\""_s}) {
            const std::optional<PageProbe> probe = read(base + u"\"hasCard\":"_s + yes);
            QVERIFY2(probe.has_value(), qPrintable(yes));
            QCOMPARE(probe->hasCard, true);
        }
        for (const QString& no : {u"FALSE"_s, u"\"False\""_s}) {
            const std::optional<PageProbe> probe = read(base + u"\"hasCard\":"_s + no);
            QVERIFY2(probe.has_value(), qPrintable(no));
            QCOMPARE(probe->hasCard, false);
        }
        for (const QString& bad : {u"1"_s, u"null"_s, u"tru"_s, u"truex"_s, u"\"true \""_s, u"fal\u017fe"_s, u"\"FAL\u017fE\""_s,
                                   u"\uff54rue"_s, u"[true]"_s, u"'true'"_s}) {
            QVERIFY2(!read(base + u"\"hasCard\":"_s + bad).has_value(), qPrintable(bad));
        }
        // a string property takes a string only; a repeated key is checked each time and the last value counts
        QVERIFY(!read(u"\"url\":null,\"url\":\"a\",\"readyState\":\"r\",\"bodyText\":\"b\",\"hasCard\":true"_s).has_value());
        QVERIFY(!read(base + u"\"hasCard\":\"x\",\"hasCard\":true"_s).has_value());
        QVERIFY(!read(base + u"\"url\":1,\"hasCard\":true"_s).has_value());
        const std::optional<PageProbe> last = read(base + u"\"url\":\"c\",\"hasCard\":true,\"hasCard\":\"FALSE\""_s);
        QVERIFY(last.has_value());
        QCOMPARE(last->url, u"c"_s);
        QCOMPARE(last->hasCard, false);
        // a key written with an escape is that key; nothing but JSON white space around the object
        QVERIFY(probeFromJson(u"{\"\\u0068asCard\":true,"_s + base.chopped(1) + u"}\r\n\t "_s).has_value());
        QVERIFY(!probeFromJson(u"\u00a0{"_s + base + u"\"hasCard\":true}"_s).has_value());
        QVERIFY(!probeFromJson(u"{"_s + base + u"\"hasCard\":true,}"_s).has_value());
    }
};

QTEST_APPLESS_MAIN(PageWatchTest)
#include "tst_page_watch.moc"
