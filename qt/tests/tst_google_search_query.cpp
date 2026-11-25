#include <QtTest>

#include <QStringList>

#include <vector>

#include "lyrics/google/search_query.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleSearchQueryTest.kt, with the additions of the Q3
// brief (Task 1). Every name is made up or is the Kotlin test's own.

using namespace Qt::StringLiterals;
using xgs::lyrics::google::GoogleSearchRequest;
using xgs::lyrics::google::cacheKey;
using xgs::lyrics::google::requests;

namespace {

QStringList texts(const QString& artist, const QString& title) {
    QStringList out;
    for (const GoogleSearchRequest& request : requests(artist, title)) out.append(request.text);
    return out;
}

QStringList languages(const std::vector<GoogleSearchRequest>& list) {
    QStringList out;
    for (const GoogleSearchRequest& request : list) out.append(request.language);
    return out;
}

}  // namespace

class GoogleSearchQueryTest : public QObject {
    Q_OBJECT

private slots:
    void anArtistWithAParenthesisedNameGivesTheFullNameFirstAndThenTheNameWithoutIt() {
        const std::vector<GoogleSearchRequest> list = requests(u"RESCENE (리센느)"_s, u"LOVE ATTACK"_s);

        QStringList textsOfList;
        for (const GoogleSearchRequest& request : list) textsOfList.append(request.text);
        QCOMPARE(textsOfList, (QStringList{u"RESCENE (리센느) LOVE ATTACK lyrics"_s, u"RESCENE LOVE ATTACK lyrics"_s}));
        QCOMPARE(languages(list), (QStringList{u"ko"_s, u"ko"_s}));
    }

    void aPlainArtistGivesOneRequestInEnglish() {
        const std::vector<GoogleSearchRequest> list = requests(u"IU"_s, u"Love poem"_s);

        QCOMPARE(list.size(), std::size_t(1));
        QCOMPARE(list.front().text, u"IU Love poem lyrics"_s);
        QCOMPARE(list.front().language, u"en"_s);
    }

    void aKoreanTitleAloneMakesItKorean() {
        const std::vector<GoogleSearchRequest> list = requests(u"IU"_s, u"좋은 날"_s);

        QCOMPARE(list.size(), std::size_t(1));
        QCOMPARE(list.front().language, u"ko"_s);
    }

    void theAddressIsEncodedAsUtf8WithPercentTwentyForSpaces() {
        const std::vector<GoogleSearchRequest> list = requests(u"RESCENE (리센느)"_s, u"LOVE ATTACK"_s);
        QVERIFY(!list.empty());
        const QString url = list.front().url();

        QCOMPARE(url, u"https://www.google.com/search?q=RESCENE%20%28%EB%A6%AC%EC%84%BC%EB%8A%90%29%20LOVE%20ATTACK%20lyrics&hl=ko"_s);
        const std::vector<GoogleSearchRequest> plain = requests(u"IU"_s, u"Love poem"_s);
        QCOMPARE(plain.size(), std::size_t(1));
        QCOMPARE(plain.front().url(), u"https://www.google.com/search?q=IU%20Love%20poem%20lyrics&hl=en"_s);
    }

    void quotesAroundWordsOfTheTitleAreDropped() {
        QCOMPARE(texts(u"IU"_s, u"‘Love’ “poem”"_s), QStringList{u"IU Love poem lyrics"_s});
        QCOMPARE(texts(u"IU"_s, u"\"Love\" poem"_s), QStringList{u"IU Love poem lyrics"_s});
    }

    void anApostropheInsideAWordIsKeptAsAStraightOne() {
        QCOMPARE(texts(u"IU"_s, u"Don’t Stop"_s), QStringList{u"IU Don't Stop lyrics"_s});
        QCOMPARE(texts(u"IU"_s, u"Don't Stop"_s), QStringList{u"IU Don't Stop lyrics"_s});
    }

    void aTrailingCreditIsDropped() {
        QCOMPARE(texts(u"IU"_s, u"Song feat. Someone"_s), QStringList{u"IU Song lyrics"_s});
        QCOMPARE(texts(u"IU"_s, u"Song FT. Someone Else"_s), QStringList{u"IU Song lyrics"_s});
    }

    void aCreditInBracketsIsDropped() {
        QCOMPARE(texts(u"IU"_s, u"Song (feat. Someone)"_s), QStringList{u"IU Song lyrics"_s});
        QCOMPARE(texts(u"IU"_s, u"Song [Prod. By Someone]"_s), QStringList{u"IU Song lyrics"_s});
        QCOMPARE(texts(u"소연"_s, u"퇴사할게여 (Narr. 기안84)"_s), QStringList{u"소연 퇴사할게여 lyrics"_s});
    }

    void aTrailingCreditStopsAtTheNextBracket() {
        QCOMPARE(texts(u"IU"_s, u"Song ft. A & B (Remix)"_s), QStringList{u"IU Song (Remix) lyrics"_s});
    }

    void aBracketThatNamesAnotherVersionStays() {
        QCOMPARE(texts(u"IU"_s, u"Hello (Japanese Ver.)"_s), QStringList{u"IU Hello (Japanese Ver.) lyrics"_s});
    }

    void whitespaceIsCollapsed() {
        QCOMPARE(texts(u"  IU  "_s, u"Love   poem "_s), QStringList{u"IU Love poem lyrics"_s});
    }

    void anArtistThatIsPartlyInParenthesesLosesOnlyThatPartInTheSecondRequest() {
        QCOMPARE(texts(u"(G)I-DLE"_s, u"Tomboy"_s), (QStringList{u"(G)I-DLE Tomboy lyrics"_s, u"I-DLE Tomboy lyrics"_s}));
    }

    void aBlankArtistOrTitleGivesNoRequest() {
        QVERIFY(requests(u""_s, u"Love poem"_s).empty());
        QVERIFY(requests(u"IU"_s, u"  "_s).empty());
    }

    void theCacheKeyIgnoresCaseAndSpacing() {
        const std::vector<GoogleSearchRequest> oneList = requests(u"IU"_s, u"Love poem"_s);
        const std::vector<GoogleSearchRequest> otherList = requests(u" iu "_s, u"LOVE  POEM"_s);
        const std::vector<GoogleSearchRequest> anotherList = requests(u"IU"_s, u"Palette"_s);
        QVERIFY(!oneList.empty());
        QVERIFY(!otherList.empty());
        QVERIFY(!anotherList.empty());
        const QString one = cacheKey(oneList.front());
        const QString other = cacheKey(otherList.front());
        const QString another = cacheKey(anotherList.front());

        QCOMPARE(one, other);
        QVERIFY(one != another);
    }

    // Added (Q3 brief): characters that mean something in an address stay inside the value of q.
    void oddCharactersStayInsideTheQueryOfTheAddress() {
        const std::vector<GoogleSearchRequest> list = requests(u"A&B #1"_s, u"100% +? \U0001F600"_s);
        QCOMPARE(list.size(), std::size_t(1));
        const QString url = list.front().url();
        const QString prefix = u"https://www.google.com/search?q="_s;
        const QString suffix = u"&hl=en"_s;

        QVERIFY2(url.startsWith(prefix), qPrintable(url));
        QVERIFY2(url.endsWith(suffix), qPrintable(url));
        QVERIFY(url.size() > prefix.size() + suffix.size());
        const QString value = url.mid(prefix.size(), url.size() - prefix.size() - suffix.size());
        for (const QChar c : {u'&', u'#', u'+', u'?', u' '}) QVERIFY2(!value.contains(c), qPrintable(value));
    }

    // Added (Q3 brief, decision 12): a check can point the address at its own fixture server.
    void aSearchBaseOfTheCheckIsUsedAsGiven() {
        const std::vector<GoogleSearchRequest> list = requests(u"IU"_s, u"Love poem"_s);
        QCOMPARE(list.size(), std::size_t(1));

        const QString url = list.front().url(u"http://127.0.0.1:8123/search");

        QVERIFY2(url.startsWith(u"http://127.0.0.1:8123/search?q="_s), qPrintable(url));
        QCOMPARE(url, u"http://127.0.0.1:8123/search?q=IU%20Love%20poem%20lyrics&hl=en"_s);
    }

    // Added (Q3 brief, decision 9): σ and ς are one letter for the cache, as for the lyrics matcher.
    void theCacheKeyTreatsBothSigmasAsOneLetter() {
        const QString upper = cacheKey(GoogleSearchRequest{u"ΟΔΟΣ x"_s, u"en"_s});
        const QString lower = cacheKey(GoogleSearchRequest{u"οδοσ x"_s, u"en"_s});
        const QString finalForm = cacheKey(GoogleSearchRequest{u"οδος x"_s, u"en"_s});

        QCOMPARE(upper, lower);
        QCOMPARE(finalForm, lower);
        QCOMPARE(lower, u"οδοσ x"_s);
    }

    // Added (Task 1 review): `\b` after a credit word needs a non-word character next, so `featuring` is no credit word, in
    // brackets or as a tail. In Kotlin, CREDIT_BRACKET and TRAILING_CREDIT both have `(?:feat|ft|prod|narr)\b`; after `feat` comes
    // `u`, an ASCII letter and so a word character, so neither expression matches and the title is kept as written.
    void aCreditWordFollowedByAWordCharacterIsNoCredit() {
        QCOMPARE(texts(u"IU"_s, u"Song (featuring Someone)"_s), QStringList{u"IU Song (featuring Someone) lyrics"_s});
        QCOMPARE(texts(u"IU"_s, u"Song featuring Someone"_s), QStringList{u"IU Song featuring Someone lyrics"_s});
        QCOMPARE(texts(u"IU"_s, u"Song [Production Someone]"_s), QStringList{u"IU Song [Production Someone] lyrics"_s});
    }
};

QTEST_APPLESS_MAIN(GoogleSearchQueryTest)
#include "tst_google_search_query.moc"
