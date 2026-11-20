#include <QtTest>

#include <optional>

#include "lyrics/lyrics_provider.h"
#include "support/fake_lyrics_provider.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/FallbackLyricsProviderTest.kt.

using namespace Qt::StringLiterals;
using xgs::LyricsQuery;
using xgs::lyrics::FallbackLyricsProvider;
using xgs::testing::FakeLyricsProvider;

namespace {

const LyricsQuery query{u"IU"_s, u"Love poem"_s, std::nullopt, std::nullopt};

}  // namespace

namespace QTest {
template <>
char* toString(const std::optional<QString>& value) {
    return value ? QTest::toString(u"\""_s + *value + u"\""_s) : qstrdup("null");
}
}  // namespace QTest

class FallbackLyricsProviderTest : public QObject {
    Q_OBJECT
private slots:
    void theFirstAnswerWinsAndLaterProvidersAreNotAsked() {
        FakeLyricsProvider first([](const LyricsQuery&) { return std::optional<QString>(u"첫째 더미"_s); });
        FakeLyricsProvider second([](const LyricsQuery&) { return std::optional<QString>(u"둘째 더미"_s); });

        QCOMPARE(FallbackLyricsProvider({&first, &second}).find(query, {}), std::optional<QString>(u"첫째 더미"_s));
        QCOMPARE(second.queries(), QList<LyricsQuery>());
    }

    void aProviderWithNothingPassesTheQueryOn() {
        FakeLyricsProvider first([](const LyricsQuery&) { return std::optional<QString>(); });
        FakeLyricsProvider second([](const LyricsQuery&) { return std::optional<QString>(u"둘째 더미"_s); });

        QCOMPARE(FallbackLyricsProvider({&first, &second}).find(query, {}), std::optional<QString>(u"둘째 더미"_s));
        QCOMPARE(first.queries(), QList<LyricsQuery>{query});
        QCOMPARE(second.queries(), QList<LyricsQuery>{query});
    }

    void noAnswerAnywhereIsNull() {
        FakeLyricsProvider a;
        FakeLyricsProvider b;
        QCOMPARE(FallbackLyricsProvider({&a, &b}).find(query, {}), std::optional<QString>());
        QCOMPARE(FallbackLyricsProvider({}).find(query, {}), std::optional<QString>());
    }
};

QTEST_GUILESS_MAIN(FallbackLyricsProviderTest)
#include "tst_fallback_lyrics_provider.moc"
