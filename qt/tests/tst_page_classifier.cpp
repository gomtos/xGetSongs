#include <QtTest>

#include <optional>

#include "lyrics/google/page_classifier.h"
#include "lyrics/google/selectors.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/PageClassifierTest.kt.

using namespace Qt::StringLiterals;
using xgs::lyrics::google::BlockPageMaxText;
using xgs::lyrics::google::blockOf;
using Kind = xgs::lyrics::google::GoogleLyricsResult::Kind;
using Block = std::optional<Kind>;

class PageClassifierTest : public QObject {
    Q_OBJECT

    const QString resultsUrl = u"https://www.google.com/search?q=x&hl=ko"_s;

private slots:
    void theConsentHostIsTheConsentPage() {
        QCOMPARE(blockOf(u"https://consent.google.com/m?continue=x"_s, std::nullopt, u""_s), Block(Kind::Consent));
    }

    void theSorryPathIsTheCaptchaPage() {
        QCOMPARE(blockOf(u"https://www.google.com/sorry/index?continue=x"_s, std::nullopt, u""_s), Block(Kind::Captcha));
    }

    void unusualTrafficTextOnAShortPageIsCaptcha() {
        const QString english = u"Our systems have detected unusual traffic from your computer network."_s;
        const QString korean = u"비정상적인 트래픽이 감지되었습니다."_s;

        QCOMPARE(blockOf(resultsUrl, std::nullopt, english), Block(Kind::Captcha));
        QCOMPARE(blockOf(resultsUrl, std::nullopt, korean), Block(Kind::Captcha));
    }

    void tooManyRequestsTextOnAShortPageIsRateLimited() {
        QCOMPARE(blockOf(resultsUrl, std::nullopt, u"429. That's an error. Too Many Requests"_s), Block(Kind::RateLimited));
    }

    void status429IsRateLimitedWhateverTheText() {
        QCOMPARE(blockOf(resultsUrl, 429, u""_s), Block(Kind::RateLimited));
    }

    void theSorryPathBeatsStatus429() {
        QCOMPARE(blockOf(u"https://www.google.com/sorry/index"_s, 429, u""_s), Block(Kind::Captcha));
    }

    void theWordsOnALongPageAreNotReadForBlockSigns() {
        const QString longPage = u"unusual traffic too many requests "_s + QString(BlockPageMaxText, QChar(u'가'));

        QCOMPARE(blockOf(resultsUrl, std::nullopt, longPage), Block());
    }

    void anOrdinaryResultPageIsNoBlock() {
        QCOMPARE(blockOf(resultsUrl, 200, u"검색 결과 더미"_s), Block());
        QCOMPARE(blockOf(resultsUrl, std::nullopt, u""_s), Block());
    }
};

QTEST_APPLESS_MAIN(PageClassifierTest)
#include "tst_page_classifier.moc"
