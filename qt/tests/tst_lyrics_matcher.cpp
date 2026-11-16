#include <QtTest>

#include <optional>

#include "lyrics/lyrics_matcher.h"
#include "model/models.h"

using xgs::LyricsQuery;
using xgs::lyrics::isMatch;
using xgs::lyrics::LyricsCandidate;
using namespace Qt::StringLiterals;

namespace {

// Kotlin's query(...) and candidate(...) with their defaults; a test names only the fields it changes (C++ designated
// initializers, so in the order of the fields).
struct Q {
    QString artist = u"IU"_s;
    QString title = u"Love poem"_s;
    std::optional<QString> album = std::nullopt;
    std::optional<int> durationSeconds = 258;
};

struct C {
    std::optional<QString> trackName = u"Love poem"_s;
    std::optional<QString> artistName = u"IU"_s;
    std::optional<QString> albumName = u"Love poem"_s;
    std::optional<double> duration = 258.0;
    std::optional<bool> instrumental = false;
    std::optional<QString> plainLyrics = u"La la la\nLa la\nLa"_s;
};

LyricsQuery query(const Q& q = {}) {
    return LyricsQuery{q.artist, q.title, q.album, q.durationSeconds};
}

LyricsCandidate candidate(const C& c = {}) {
    return LyricsCandidate{c.trackName, c.artistName, c.albumName, c.duration, c.instrumental, c.plainLyrics};
}

QStringList titleVariants(const QString& title) {
    return xgs::lyrics::titleVariants(title);
}

QStringList artistVariants(const QString& artist) {
    return xgs::lyrics::artistVariants(artist);
}

QString normalize(const QString& text) {
    return xgs::lyrics::normalizeForMatching(text);
}

// Kotlin's pick gives the candidate (assertSame compares it by identity); here the index of it, -1 for none (assertNull).
int picked(const LyricsQuery& q, const QList<LyricsCandidate>& candidates) {
    return int(xgs::lyrics::pick(q, candidates));
}

}  // namespace

// Kotlin's assertEquals(expected, actual, message) for lists of strings.
#define COMPARE_LIST_MSG(actual, expected, message)                                                                    \
    do {                                                                                                               \
        const QStringList actual_ = (actual);                                                                          \
        const QStringList expected_ = (expected);                                                                      \
        QVERIFY2(actual_ == expected_, qPrintable(u"%1\n   expected: [%2]\n   actual:   [%3]"_s.arg(                   \
                                           QString::fromUtf8(message), expected_.join(u", "), actual_.join(u", ")))); \
    } while (false)

class LyricsMatcherTest : public QObject {
    Q_OBJECT

private slots:
    // ---- titleVariants ----

    void aPlainTitleHasOnlyItself() {
        QCOMPARE(titleVariants(u"Love poem"_s), QStringList({u"Love poem"_s}));
        QCOMPARE(titleVariants(u"사랑의 시"_s), QStringList({u"사랑의 시"_s}));
    }

    void aBracketedPartIsLeftOutInTheSecondVariant() {
        QCOMPARE(titleVariants(u"Title (feat. X)"_s), QStringList({u"Title (feat. X)"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title [Live]"_s), QStringList({u"Title [Live]"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title {Remix}"_s), QStringList({u"Title {Remix}"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title <Inst.>"_s), QStringList({u"Title <Inst.>"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"사랑의 시 (Love poem)"_s), QStringList({u"사랑의 시 (Love poem)"_s, u"사랑의 시"_s}));
    }

    void spacesAreCollapsedWhereABracketedPartWasTaken() {
        QCOMPARE(titleVariants(u"A (B) C"_s), QStringList({u"A (B) C"_s, u"A C"_s}));
        QCOMPARE(titleVariants(u"A (B)  C [D]"_s), QStringList({u"A (B)  C [D]"_s, u"A C"_s}));
        QCOMPARE(titleVariants(u"Title (Live) (Remix)"_s), QStringList({u"Title (Live) (Remix)"_s, u"Title"_s}));
    }

    void nestedBracketsAreTakenToo() {
        QCOMPARE(titleVariants(u"A (B (C)) D"_s), QStringList({u"A (B (C)) D"_s, u"A D"_s}));
        QCOMPARE(titleVariants(u"A [B (C)] D"_s), QStringList({u"A [B (C)] D"_s, u"A D"_s}));
    }

    void aTitleThatIsOnlyBracketsKeepsOnlyItself() {
        QCOMPARE(titleVariants(u"(Live)"_s), QStringList({u"(Live)"_s}));
        QCOMPARE(titleVariants(u"(A) [B]"_s), QStringList({u"(A) [B]"_s}));
    }

    void aTrailingCreditIsLeftOutInTheThirdVariant() {
        QCOMPARE(titleVariants(u"Title feat. X"_s), QStringList({u"Title feat. X"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title ft. X Y"_s), QStringList({u"Title ft. X Y"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title prod. X"_s), QStringList({u"Title prod. X"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title Narr. X"_s), QStringList({u"Title Narr. X"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title feat X"_s), QStringList({u"Title feat X"_s, u"Title"_s}));
    }

    void aTrailingCreditIsFoundWhateverTheCase() {
        QCOMPARE(titleVariants(u"Title FEAT. X"_s), QStringList({u"Title FEAT. X"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title Prod. X"_s), QStringList({u"Title Prod. X"_s, u"Title"_s}));
        QCOMPARE(titleVariants(u"Title NARR. X"_s), QStringList({u"Title NARR. X"_s, u"Title"_s}));
    }

    void aCreditIsLeftOutAfterTheBracketedPartsWentAndAllThreeKindsComeInOrder() {
        QCOMPARE(titleVariants(u"Title (Live) feat. X"_s),
                 QStringList({u"Title (Live) feat. X"_s, u"Title feat. X"_s, u"Title"_s}));
        COMPARE_LIST_MSG(titleVariants(u"Title (feat. X)"_s), QStringList({u"Title (feat. X)"_s, u"Title"_s}),
                         "equal variants are listed once");
    }

    void aCreditWordInsideAWordOrInFrontOfTheTitleIsNotACredit() {
        QCOMPARE(titleVariants(u"Without You"_s), QStringList({u"Without You"_s}));
        QCOMPARE(titleVariants(u"Featuring Love"_s), QStringList({u"Featuring Love"_s}));
        COMPARE_LIST_MSG(titleVariants(u"With You"_s), QStringList({u"With You"_s}),
                         "nothing in front of it, so nothing is a credit");
        QCOMPARE(titleVariants(u"Prodigal Son"_s), QStringList({u"Prodigal Son"_s}));
    }

    void withIsNotACreditWordSoNoShorterTitleIsOfferedForTitlesThatHaveIt() {
        // "Stay With Me" is a whole title, not the song "Stay" with a credit. A credit written (with X) is a bracketed part.
        QCOMPARE(titleVariants(u"Dance With Me"_s), QStringList({u"Dance With Me"_s}));
        QCOMPARE(titleVariants(u"Stay With Me"_s), QStringList({u"Stay With Me"_s}));
        QCOMPARE(titleVariants(u"Title with X"_s), QStringList({u"Title with X"_s}));
        QCOMPARE(titleVariants(u"Title WITH X"_s), QStringList({u"Title WITH X"_s}));
        QCOMPARE(titleVariants(u"Title (with X)"_s), QStringList({u"Title (with X)"_s, u"Title"_s}));
    }

    void quoteCharactersAreLeftOutInTheLastVariant() {
        QCOMPARE(titleVariants(u"Don't Stop"_s), QStringList({u"Don't Stop"_s, u"Dont Stop"_s}));
        QCOMPARE(titleVariants(u"\"Golden\" 'x'"_s), QStringList({u"\"Golden\" 'x'"_s, u"Golden x"_s}));
        QCOMPARE(titleVariants(u"Don\u2019t \u201CStop\u201D"_s), QStringList({u"Don\u2019t \u201CStop\u201D"_s, u"Dont Stop"_s}));
        QCOMPARE(titleVariants(u"\u2018Golden\u2019 \u201Ex\u201F"_s),
                 QStringList({u"\u2018Golden\u2019 \u201Ex\u201F"_s, u"Golden x"_s}));
    }

    void quotesAreLeftOutOfTheReducedTextAndTheOrderStaysTitleBracketsCreditQuotes() {
        QCOMPARE(titleVariants(u"\"Golden\" (Live) feat. X"_s),
                 QStringList({u"\"Golden\" (Live) feat. X"_s, u"\"Golden\" feat. X"_s, u"\"Golden\""_s, u"Golden"_s}));
    }

    void titleVariantsAreDistinctAndNeverBlank() {
        QCOMPARE(titleVariants(u""_s), QStringList());
        QCOMPARE(titleVariants(u"   "_s), QStringList());
        QCOMPARE(titleVariants(u"  Title  "_s), QStringList({u"Title"_s}));
        COMPARE_LIST_MSG(titleVariants(u"''"_s), QStringList({u"''"_s}), "the quote-free text would be blank");
    }

    // ---- artistVariants ----

    void aPlainArtistHasOnlyItself() {
        QCOMPARE(artistVariants(u"IU"_s), QStringList({u"IU"_s}));
        QCOMPARE(artistVariants(u"아이유"_s), QStringList({u"아이유"_s}));
    }

    void anArtistWithAnAliasInParenthesesHasTheWholeTheOutsideAndTheInside() {
        QCOMPARE(artistVariants(u"소연 (SOYEON)"_s), QStringList({u"소연 (SOYEON)"_s, u"소연"_s, u"SOYEON"_s}));
        QCOMPARE(artistVariants(u"(G)I-DLE"_s), QStringList({u"(G)I-DLE"_s, u"I-DLE"_s, u"G"_s}));
    }

    void equalArtistVariantsAreListedOnce() {
        QCOMPARE(artistVariants(u"LOVE ATTACK (LOVE ATTACK)"_s), QStringList({u"LOVE ATTACK (LOVE ATTACK)"_s, u"LOVE ATTACK"_s}));
    }

    void atMostThreeArtistVariantsAreGiven() {
        QCOMPARE(artistVariants(u"A (B) (C) (D)"_s), QStringList({u"A (B) (C) (D)"_s, u"A"_s, u"B"_s}));
    }

    void blankArtistVariantsAreNotGiven() {
        QCOMPARE(artistVariants(u""_s), QStringList());
        QCOMPARE(artistVariants(u"  "_s), QStringList());
        COMPARE_LIST_MSG(artistVariants(u"A ()"_s), QStringList({u"A ()"_s, u"A"_s}), "the empty part inside the parentheses is dropped");
    }

    // ---- normalize ----

    void normalizeKeepsOnlyLowercaseLettersAndDigits() {
        QCOMPARE(normalize(u"Love Poem!"_s), u"lovepoem"_s);
        QCOMPARE(normalize(u"A-B_C 1.2"_s), u"abc12"_s);
        QCOMPARE(normalize(u"Don't Stop"_s), u"dontstop"_s);
        QCOMPARE(normalize(u"Don\u2019t   Stop"_s), u"dontstop"_s);
        QCOMPARE(normalize(u" -- !? "_s), u""_s);
        QCOMPARE(normalize(u""_s), u""_s);
    }

    void normalizeKeepsHangulAndOtherLettersAndLowercasesThem() {
        QCOMPARE(normalize(u"소연 (SOYEON)"_s), u"소연soyeon"_s);
        QCOMPARE(normalize(u"사랑의 시"_s), u"사랑의시"_s);
        QCOMPARE(normalize(u"ÜNÏ"_s), u"ünï"_s);
        QCOMPARE(normalize(u"日本語 123"_s), u"日本語123"_s);
    }

    void normalizeAppliesCompatibilityNormalisationFirst() {
        // Full-width letters and digits (U+FF29 U+FF35 U+FF11) are the ASCII ones.
        QCOMPARE(normalize(u"\uFF29\uFF35\uFF11"_s), u"iu1"_s);
        QCOMPARE(normalize(u"\uFF29\uFF35"_s), u"iu"_s);
        // Hangul as separate jamo (U+1112 U+1161 U+11AB) is the composed syllable.
        QCOMPARE(normalize(u"\u1112\u1161\u11AB"_s), u"한"_s);
        QCOMPARE(normalize(u"\u1112\u1161\u11AB\u1100\u1173\u11AF"_s), normalize(u"한글"_s));
        // A ligature (U+FB01) is two letters.
        QCOMPARE(normalize(u"\uFB01ne"_s), u"fine"_s);
    }

    void titlesThatDifferOnlyInWidthOrInComposedHangulAreEqual() {
        QVERIFY(isMatch(query({.title = u"\uFF2Cove poem"_s}), candidate({.trackName = u"Love poem"_s})));
        QVERIFY(isMatch(query({.title = u"한글"_s}), candidate({.trackName = u"\u1112\u1161\u11AB\u1100\u1173\u11AF"_s})));
        QVERIFY(isMatch(query({.artist = u"한글"_s}), candidate({.artistName = u"\u1112\u1161\u11AB\u1100\u1173\u11AF"_s})));
    }

    // ---- isMatch ----

    void anExactRecordIsAMatch() {
        QVERIFY(isMatch(query(), candidate()));
    }

    void titlesAreEqualAfterNormalisation() {
        QVERIFY(isMatch(query({.title = u"Love poem"_s}), candidate({.trackName = u"LOVE POEM!"_s})));
        QVERIFY(isMatch(query({.title = u"Don't Stop"_s}), candidate({.trackName = u"Don\u2019t Stop"_s})));
        QVERIFY(isMatch(query({.title = u"A-B"_s}), candidate({.trackName = u"a b"_s})));
    }

    void aVariantOfTheTitleOnEitherSideIsEnoughWhenWhatIsLeftOutIsNoVersionMarker() {
        QVERIFY(isMatch(query({.title = u"Love poem (feat. X)"_s}), candidate({.trackName = u"Love poem"_s})));
        QVERIFY(isMatch(query({.title = u"Love poem"_s}), candidate({.trackName = u"Love poem (Official Audio)"_s})));
        QVERIFY(isMatch(query({.title = u"Love poem (Official)"_s}), candidate({.trackName = u"Love poem (MV)"_s})));
        QVERIFY(isMatch(query({.title = u"Love poem feat. X"_s}), candidate({.trackName = u"Love poem"_s})));
        QVERIFY(isMatch(query({.title = u"Love poem"_s}), candidate({.trackName = u"Love poem feat. X"_s})));
        QVERIFY(isMatch(query({.title = u"LOVE ATTACK"_s}), candidate({.trackName = u"LOVE ATTACK (LOVE ATTACK)"_s})));
    }

    void theQuerysOwnVersionWordsMayBeLeftOutBecauseAVideoOfALiveVersionHasTheSameLyrics() {
        QVERIFY(isMatch(query({.title = u"Love poem (Live)"_s}), candidate({.trackName = u"Love poem"_s})));
    }

    void aVersionMarkerOfTheQueryKeepsItFromMatchingTheBaseSongToo() {
        const QStringList markers = {
            u"Japanese Ver."_s, u"English Version"_s, u"Chinese"_s, u"Inst."_s, u"Instrumental"_s, u"Remix"_s, u"Radio Edit"_s,
            u"Club Mix"_s, u"Remastered 2011"_s, u"Demo"_s, u"Cover"_s, u"ver"_s, u"일본어 버전"_s, u"영어"_s, u"중국어"_s,
            u"한국어"_s, u"반주"_s, u"리믹스"_s, u"인스트"_s, u"밴드 버전"_s, u"커버"_s, u"데모"_s, u"리마스터"_s, u"믹스"_s,
            u"에디트"_s,
        };
        for (const QString& marker : markers) {
            QVERIFY2(!isMatch(query({.title = u"Hello (%1)"_s.arg(marker)}), candidate({.trackName = u"Hello"_s})),
                     qPrintable(u"the video of a version is not the base song: "_s + marker));
        }
        QVERIFY(!isMatch(query({.title = u"Hello (Japanese Ver.) feat. X"_s}), candidate({.trackName = u"Hello"_s})));
        QVERIFY(!isMatch(query({.title = u"Hello feat. X (Instrumental)"_s}), candidate({.trackName = u"Hello"_s})));
    }

    void aLiveOrAcousticTakeInTheQueryStillMatchesTheBaseSong() {
        for (const QString& marker : {u"Live"_s, u"live"_s, u"Acoustic"_s, u"Lives"_s, u"라이브"_s, u"어쿠스틱"_s}) {
            QVERIFY2(isMatch(query({.title = u"Hello (%1)"_s.arg(marker)}), candidate({.trackName = u"Hello"_s})), qPrintable(marker));
        }
        // Live is no marker for the query, but one for the candidate: a record of a live take is its own entry.
        QVERIFY(!isMatch(query({.title = u"Hello"_s}), candidate({.trackName = u"Hello (Live)"_s})));
        // Together with another version word the query is another version again.
        QVERIFY(!isMatch(query({.title = u"Hello (Live Ver.)"_s}), candidate({.trackName = u"Hello"_s})));
        QVERIFY(!isMatch(query({.title = u"Hello (Live) (Remix)"_s}), candidate({.trackName = u"Hello"_s})));
    }

    void aVersionOfTheQueryMatchesTheRecordOfTheSameVersionThroughTheWholeTitle() {
        QVERIFY(isMatch(query({.title = u"Hello (Japanese Ver.)"_s}), candidate({.trackName = u"Hello (Japanese Ver.)"_s})));
        const QList<LyricsCandidate> candidates = {
            candidate({.trackName = u"Hello"_s, .plainLyrics = u"korean"_s}),
            candidate({.trackName = u"Hello (Japanese Ver.)"_s, .plainLyrics = u"japanese"_s}),
        };
        const int index = picked(query({.title = u"Hello (Japanese Ver.)"_s}), candidates);
        // pick(...)?.plainLyrics
        const std::optional<QString> lyrics = index >= 0 ? candidates.at(index).plainLyrics : std::nullopt;
        QCOMPARE(lyrics.value_or(u"<null>"_s), u"japanese"_s);
    }

    void aVersionMarkerOfTheCandidateKeepsItFromMatchingAShorterTitle() {
        const QStringList markers = {
            u"ver"_s, u"Ver."_s, u"version"_s, u"Japanese Ver."_s, u"English Version"_s, u"Chinese"_s, u"Korean ver"_s, u"Inst"_s,
            u"Inst."_s, u"Instrumental"_s, u"Remix"_s, u"Remixes"_s, u"remix by X"_s, u"Live"_s, u"Live at Somewhere"_s,
            u"Acoustic"_s, u"Acoustic Version"_s, u"Cover"_s, u"Edit"_s, u"Radio Edit"_s, u"Mix"_s, u"Club Mix"_s, u"Remaster"_s,
            u"Remastered 2011"_s, u"2011 Remaster"_s, u"Demo"_s, u"demo ver."_s, u"Ver2"_s, u"Remix2"_s, u"Live2023"_s,
            u"반주"_s, u"일본어 버전"_s, u"영어"_s, u"중국어"_s, u"한국어"_s, u"라이브"_s, u"리믹스"_s, u"어쿠스틱"_s, u"한국어Ver"_s,
            u"AR 반주"_s,
        };
        const QList<std::pair<QString, QString>> brackets = {{u"("_s, u")"_s}, {u"["_s, u"]"_s}, {u"{"_s, u"}"_s}, {u"<"_s, u">"_s}};
        for (const QString& marker : markers) {
            for (const auto& [open, close] : brackets) {
                QVERIFY2(!isMatch(query({.title = u"Hello"_s}), candidate({.trackName = u"Hello "_s + open + marker + close})),
                         qPrintable(u"marker: "_s + open + marker + close));
            }
        }
        QVERIFY(!isMatch(query({.title = u"Hello"_s}), candidate({.trackName = u"Hello (Japanese Ver.)"_s})));
    }

    void aVersionMarkerInACreditOfTheCandidateKeepsItFromMatchingAShorterTitleToo() {
        QVERIFY(!isMatch(query({.title = u"Hello"_s}), candidate({.trackName = u"Hello feat. X Remix"_s})));
        QVERIFY(!isMatch(query({.title = u"Hello"_s}), candidate({.trackName = u"Hello (Live) feat. X"_s})));
        QVERIFY2(isMatch(query({.title = u"Hello"_s}), candidate({.trackName = u"Hello feat. X"_s})), "a plain credit is left out");
    }

    void theFullTitleOfTheCandidateStillMatchesWhateverItsVersionMarker() {
        QVERIFY(isMatch(query({.title = u"Hello (Japanese Ver.)"_s}), candidate({.trackName = u"Hello (Japanese Ver.)"_s})));
        QVERIFY(isMatch(query({.title = u"Hello Japanese Ver."_s}), candidate({.trackName = u"Hello (Japanese Ver.)"_s})));
        QVERIFY(isMatch(query({.title = u"Hello (Live)"_s}), candidate({.trackName = u"Hello (Live)"_s})));
    }

    void aMarkerNeedsToBeAWholeWordForEnglishButNotForKorean() {
        // "Verse", "Lively", "Editor" and "Mixture" only start like a marker; the shorter title is still offered.
        for (const QString& word : {u"Verse 2"_s, u"Lively"_s, u"Editor's Note"_s, u"Mixture"_s, u"Democracy"_s, u"Discover"_s, u"Korea"_s}) {
            QVERIFY2(isMatch(query({.title = u"Hello"_s}), candidate({.trackName = u"Hello (%1)"_s.arg(word)})),
                     qPrintable(u"not a marker: "_s + word));
        }
    }

    void aTitleThatMerelyStartsWithTheShorterOneDoesNotMatchBecauseOfAWordLikeWith() {
        QVERIFY(!isMatch(query({.title = u"Stay"_s}), candidate({.trackName = u"Stay With Me"_s})));
        QVERIFY(!isMatch(query({.title = u"Stay With Me"_s}), candidate({.trackName = u"Stay"_s})));
        QVERIFY(!isMatch(query({.title = u"Dance With Me"_s}), candidate({.trackName = u"Dance"_s})));
        QVERIFY(!isMatch(query({.title = u"Dance"_s}), candidate({.trackName = u"Dance With Me"_s})));
        QVERIFY(isMatch(query({.title = u"Dance With Me"_s}), candidate({.trackName = u"Dance With Me"_s})));
        QVERIFY2(isMatch(query({.title = u"Dance (with X)"_s}), candidate({.trackName = u"Dance"_s})), "a credit in brackets is left out");
    }

    void aTitleThatMerelyContainsOrStartsLikeTheOtherIsNotAMatch() {
        QVERIFY(!isMatch(query({.title = u"Love poem"_s}), candidate({.trackName = u"Love poem 2"_s})));
        QVERIFY(!isMatch(query({.title = u"Love"_s}), candidate({.trackName = u"Love poem"_s})));
        QVERIFY(!isMatch(query({.title = u"Love poem"_s}), candidate({.trackName = u"Another song"_s})));
    }

    void aMissingOrBlankCandidateTitleIsNotAMatch() {
        QVERIFY(!isMatch(query(), candidate({.trackName = std::nullopt})));
        QVERIFY(!isMatch(query(), candidate({.trackName = u""_s})));
        QVERIFY2(!isMatch(query({.title = u"!!!"_s}), candidate({.trackName = u"???"_s})), "titles with no letter or digit say nothing");
    }

    void theArtistMayContainTheOtherOrBeContainedInIt() {
        QVERIFY(isMatch(query({.artist = u"IU"_s}), candidate({.artistName = u"IU (아이유)"_s})));
        QVERIFY(isMatch(query({.artist = u"IU"_s}), candidate({.artistName = u"IU & Someone"_s})));
        QVERIFY(isMatch(query({.artist = u"IU & Someone"_s}), candidate({.artistName = u"IU"_s})));
        QVERIFY(isMatch(query({.artist = u"iu"_s}), candidate({.artistName = u"I.U."_s})));
    }

    void theArtistVariantsOfBothSidesAreUsed() {
        QVERIFY(isMatch(query({.artist = u"소연 (SOYEON)"_s}), candidate({.artistName = u"SOYEON"_s})));
        QVERIFY(isMatch(query({.artist = u"소연 (SOYEON)"_s}), candidate({.artistName = u"소연"_s})));
        QVERIFY(isMatch(query({.artist = u"SOYEON"_s}), candidate({.artistName = u"소연 (SOYEON)"_s})));
        QVERIFY(isMatch(query({.artist = u"(G)I-DLE"_s}), candidate({.artistName = u"I-DLE"_s})));
    }

    void anotherArtistIsNotAMatch() {
        QVERIFY(!isMatch(query({.artist = u"IU"_s}), candidate({.artistName = u"Somebody Else"_s})));
        QVERIFY(!isMatch(query({.artist = u"아이유"_s}), candidate({.artistName = u"IU"_s})));
        QVERIFY(!isMatch(query(), candidate({.artistName = std::nullopt})));
        QVERIFY(!isMatch(query(), candidate({.artistName = u""_s})));
    }

    void artistsAreComparedByWholeWordsNotBySubstrings() {
        QVERIFY(!isMatch(query({.artist = u"Rain"_s}), candidate({.artistName = u"Rainbow"_s})));
        QVERIFY(!isMatch(query({.artist = u"Rainbow"_s}), candidate({.artistName = u"Rain"_s})));
        QVERIFY(!isMatch(query({.artist = u"IU"_s}), candidate({.artistName = u"Liu Yifei"_s})));
        QVERIFY(!isMatch(query({.artist = u"Liu Yifei"_s}), candidate({.artistName = u"IU"_s})));
        QVERIFY(!isMatch(query({.artist = u"Jin"_s}), candidate({.artistName = u"Jinx"_s})));
        QVERIFY(!isMatch(query({.artist = u"BTS"_s}), candidate({.artistName = u"Abtsa"_s})));
        QVERIFY(isMatch(query({.artist = u"Rain"_s}), candidate({.artistName = u"Rain"_s})));
        QVERIFY(isMatch(query({.artist = u"Rain"_s}), candidate({.artistName = u"Rain & Friends"_s})));
        QVERIFY(isMatch(query({.artist = u"Friends & Rain"_s}), candidate({.artistName = u"Rain"_s})));
    }

    void aRunOfWordsOfOneSideMustSitTogetherInTheOther() {
        QVERIFY(isMatch(query({.artist = u"Taylor Swift"_s}), candidate({.artistName = u"Taylor Swift feat. Somebody"_s})));
        QVERIFY(isMatch(query({.artist = u"Somebody & Taylor Swift"_s}), candidate({.artistName = u"Taylor Swift"_s})));
        QVERIFY2(!isMatch(query({.artist = u"Taylor Swift"_s}), candidate({.artistName = u"Taylor Somebody Swift"_s})),
                 "the words are not together");
        QVERIFY2(!isMatch(query({.artist = u"Taylor Swift"_s}), candidate({.artistName = u"Swift Taylor"_s})), "the order differs");
    }

    void anAliasAndAnAbbreviationStillMatchThroughTheirVariantsOrTheirLettersAndDigits() {
        QVERIFY(isMatch(query({.artist = u"아이오아이 (I.O.I)"_s}), candidate({.artistName = u"아이오아이"_s})));
        QVERIFY(isMatch(query({.artist = u"아이오아이 (I.O.I)"_s}), candidate({.artistName = u"I.O.I"_s})));
        QVERIFY2(isMatch(query({.artist = u"아이오아이 (I.O.I)"_s}), candidate({.artistName = u"IOI"_s})), "I.O.I and IOI are the same letters");
        QVERIFY(isMatch(query({.artist = u"IOI"_s}), candidate({.artistName = u"I.O.I"_s})));
        QVERIFY(isMatch(query({.artist = u"IU & Someone"_s}), candidate({.artistName = u"IU"_s})));
        QVERIFY(isMatch(query({.artist = u"AKMU"_s}), candidate({.artistName = u"AKMU (악뮤)"_s})));
    }

    void theWidthOfCharactersAndTheirCompositionDoNotMatterForTheArtistEither() {
        QVERIFY(isMatch(query({.artist = u"\uFF29\uFF35"_s}), candidate({.artistName = u"IU"_s})));
        QVERIFY(isMatch(query({.artist = u"IU"_s}), candidate({.artistName = u"\uFF29\uFF35 & Someone"_s})));
    }

    void anArtistOfOneCharacterIsNeverAMatch() {
        QVERIFY(!isMatch(query({.artist = u"A"_s}), candidate({.artistName = u"A"_s})));
        QVERIFY(!isMatch(query({.artist = u"A"_s}), candidate({.artistName = u"ABBA"_s})));
        QVERIFY(!isMatch(query({.artist = u"ABBA"_s}), candidate({.artistName = u"A"_s})));
        QVERIFY2(!isMatch(query({.artist = u"!"_s}), candidate({.artistName = u"?"_s})), "nothing is left of either after normalising");
    }

    void aDifferenceOfEightSecondsIsAcceptedAndNineIsNot() {
        QVERIFY(isMatch(query({.durationSeconds = 258}), candidate({.duration = 266.0})));
        QVERIFY(isMatch(query({.durationSeconds = 258}), candidate({.duration = 250.0})));
        QVERIFY(!isMatch(query({.durationSeconds = 258}), candidate({.duration = 267.0})));
        QVERIFY(!isMatch(query({.durationSeconds = 258}), candidate({.duration = 249.0})));
    }

    void aFractionalCandidateDurationIsComparedAsItIs() {
        QVERIFY(isMatch(query({.durationSeconds = 258}), candidate({.duration = 265.9})));
        QVERIFY(isMatch(query({.durationSeconds = 258}), candidate({.duration = 250.1})));
        QVERIFY(!isMatch(query({.durationSeconds = 258}), candidate({.duration = 266.1})));
        QVERIFY(!isMatch(query({.durationSeconds = 258}), candidate({.duration = 249.9})));
    }

    void anUnknownDurationOnEitherSideIsAccepted() {
        QVERIFY(isMatch(query({.durationSeconds = std::nullopt}), candidate({.duration = 999.0})));
        QVERIFY(isMatch(query({.durationSeconds = 258}), candidate({.duration = std::nullopt})));
        QVERIFY(isMatch(query({.durationSeconds = std::nullopt}), candidate({.duration = std::nullopt})));
    }

    void anInstrumentalRecordIsNotAMatch() {
        QVERIFY(!isMatch(query(), candidate({.instrumental = true})));
        QVERIFY(!isMatch(query(), candidate({.instrumental = true, .plainLyrics = u"La la la\nLa la\nLa"_s})));
        QVERIFY2(isMatch(query(), candidate({.instrumental = std::nullopt})), "an unknown flag is not true");
        QVERIFY(isMatch(query(), candidate({.instrumental = false})));
    }

    void aRecordWithoutLyricsIsNotAMatch() {
        QVERIFY(!isMatch(query(), candidate({.plainLyrics = std::nullopt})));
        QVERIFY(!isMatch(query(), candidate({.plainLyrics = u""_s})));
        QVERIFY(!isMatch(query(), candidate({.plainLyrics = u"  \n\t "_s})));
    }

    // ---- pick ----

    void pickTakesTheAcceptableCandidateWithTheSmallestDurationDifference() {
        const LyricsCandidate far = candidate({.albumName = u"far"_s, .duration = 264.0});
        const LyricsCandidate near = candidate({.albumName = u"near"_s, .duration = 259.0});
        const LyricsCandidate exact = candidate({.albumName = u"exact"_s, .duration = 258.0});
        const LyricsCandidate other = candidate({.trackName = u"Another song"_s, .albumName = u"other"_s, .duration = 258.0});

        QCOMPARE(picked(query(), {far, near, other, exact}), 3);   // exact
        QCOMPARE(picked(query(), {far, other, near}), 2);          // near
        QCOMPARE(picked(query(), {other, far}), 1);                // far
    }

    void pickKeepsTheFirstOfEqualDifferences() {
        const LyricsCandidate below = candidate({.albumName = u"below"_s, .duration = 256.0});
        const LyricsCandidate above = candidate({.albumName = u"above"_s, .duration = 260.0});
        const LyricsCandidate sameAsBelow = candidate({.albumName = u"again"_s, .duration = 256.0});

        QCOMPARE(picked(query(), {below, above, sameAsBelow}), 0);   // below
        QCOMPARE(picked(query(), {above, below}), 0);                // above
    }

    void pickNeverTakesAVersionOfTheSongInsteadOfTheSongItselfWhateverTheDurationDifference() {
        const LyricsCandidate version = candidate({.trackName = u"Hello (Japanese Ver.)"_s, .albumName = u"v"_s, .duration = 258.0,
                                                   .plainLyrics = u"Japanese one\nJapanese two\nJapanese three"_s});
        const LyricsCandidate exact = candidate({.trackName = u"Hello"_s, .albumName = u"e"_s, .duration = 264.0});

        QCOMPARE(picked(query({.title = u"Hello"_s}), {version, exact}), 1);   // exact
        QCOMPARE(picked(query({.title = u"Hello"_s}), {exact, version}), 0);   // exact
    }

    void pickRanksTheTitleMatchFirstWholeThenShortenedOnTheQuerySideThenShortenedOnTheCandidateSide() {
        const LyricsCandidate whole = candidate({.trackName = u"Love poem (feat. X)"_s, .albumName = u"whole"_s, .duration = 264.0});
        const LyricsCandidate queryShortened = candidate({.trackName = u"Love poem"_s, .albumName = u"q"_s, .duration = 262.0});
        const LyricsCandidate candidateShortened = candidate({.trackName = u"Love poem (MV)"_s, .albumName = u"c"_s, .duration = 258.0});
        const LyricsQuery asked = query({.title = u"Love poem (feat. X)"_s});

        QCOMPARE(picked(asked, {candidateShortened, queryShortened, whole}), 2);   // whole
        QCOMPARE(picked(asked, {candidateShortened, queryShortened}), 1);          // queryShortened
        QCOMPARE(picked(asked, {candidateShortened}), 0);                          // candidateShortened
        // The whole title of a plain query beats a candidate that had to be shortened, even when its length is farther off.
        const LyricsQuery plain = query({.title = u"Love poem"_s});
        const LyricsCandidate exact = candidate({.trackName = u"Love poem"_s, .albumName = u"exact"_s, .duration = 265.0});
        const LyricsCandidate official = candidate({.trackName = u"Love poem (Official Audio)"_s, .albumName = u"official"_s, .duration = 258.0});
        QCOMPARE(picked(plain, {official, exact}), 1);   // exact
    }

    void withEqualTitleQualityAnAlbumEqualToTheQueryAlbumWinsEvenWithALargerDurationDifference() {
        const LyricsCandidate otherAlbum = candidate({.albumName = u"Another album"_s, .duration = 258.0});
        const LyricsCandidate sameAlbum = candidate({.albumName = u"LOVE POEM!"_s, .duration = 263.0});
        const LyricsQuery asked = query({.album = u"Love poem"_s});

        QCOMPARE(picked(asked, {otherAlbum, sameAlbum}), 1);   // sameAlbum
        QCOMPARE(picked(asked, {sameAlbum, otherAlbum}), 0);   // sameAlbum
    }

    void theAlbumIsNotConsideredWhenTheQueryHasNone() {
        const LyricsCandidate near = candidate({.albumName = u"Another album"_s, .duration = 258.0});
        const LyricsCandidate far = candidate({.albumName = u"Love poem"_s, .duration = 263.0});

        QCOMPARE(picked(query({.album = std::nullopt}), {far, near}), 1);   // near
        QVERIFY2(picked(query({.album = u"  "_s}), {far, near}) == 1, "a blank album says nothing either");   // near
    }

    void aMissingCandidateAlbumIsNoMatch() {
        const LyricsCandidate noAlbum = candidate({.albumName = std::nullopt, .duration = 258.0});
        const LyricsCandidate sameAlbum = candidate({.albumName = u"Love poem"_s, .duration = 263.0});

        QCOMPARE(picked(query({.album = u"Love poem"_s}), {noAlbum, sameAlbum}), 1);   // sameAlbum
    }

    void theTitleQualityOutranksTheAlbum() {
        const LyricsCandidate exactOtherAlbum = candidate({.trackName = u"Love poem"_s, .albumName = u"Another album"_s, .duration = 258.0});
        const LyricsCandidate shortenedSameAlbum =
            candidate({.trackName = u"Love poem (Official Audio)"_s, .albumName = u"Love poem"_s, .duration = 258.0});

        QCOMPARE(picked(query({.album = u"Love poem"_s}), {shortenedSameAlbum, exactOtherAlbum}), 1);   // exactOtherAlbum
    }

    void withEqualTitleQualityAndAlbumTheSmallestDurationDifferenceWinsAndTiesKeepTheFirst() {
        const LyricsQuery asked = query({.album = u"Love poem"_s});
        const LyricsCandidate far = candidate({.albumName = u"Love poem"_s, .duration = 265.0});
        const LyricsCandidate near = candidate({.albumName = u"Love poem"_s, .duration = 259.0});
        const LyricsCandidate tie = candidate({.albumName = u"Love poem"_s, .duration = 257.0});

        QCOMPARE(picked(asked, {far, near}), 1);                                         // near
        QVERIFY2(picked(asked, {near, tie}) == 0, "equal differences: the first");     // near
        QCOMPARE(picked(asked, {tie, near}), 0);                                         // tie
    }

    void anUnknownDurationDifferenceCountsAsTheLargest() {
        const LyricsCandidate unknown = candidate({.albumName = u"unknown"_s, .duration = std::nullopt});
        const LyricsCandidate far = candidate({.albumName = u"far"_s, .duration = 265.0});

        QCOMPARE(picked(query(), {unknown, far}), 1);   // far
        QCOMPARE(picked(query(), {unknown}), 0);        // unknown
        QVERIFY2(picked(query({.durationSeconds = std::nullopt}), {unknown, far}) == 0, "everything unknown: the first");   // unknown
    }

    void pickSkipsWhatIsNotAcceptable() {
        const LyricsCandidate instrumental = candidate({.albumName = u"inst"_s, .duration = 258.0, .instrumental = true});
        const LyricsCandidate empty = candidate({.albumName = u"empty"_s, .duration = 258.0, .plainLyrics = u" "_s});
        const LyricsCandidate tooLong = candidate({.albumName = u"long"_s, .duration = 300.0});
        const LyricsCandidate good = candidate({.albumName = u"good"_s, .duration = 262.0});

        QCOMPARE(picked(query(), {instrumental, empty, tooLong, good}), 3);   // good
    }

    void pickGivesNullWhenNothingIsAcceptable() {
        QCOMPARE(picked(query(), {}), -1);
        QCOMPARE(picked(query(), {candidate({.trackName = u"Another song"_s}), candidate({.artistName = u"Somebody Else"_s})}), -1);
        QCOMPARE(picked(query(), {candidate({.duration = 300.0}), candidate({.instrumental = true})}), -1);
    }

    // ---- added (C++ only): user decision of 2026-11-15 ----

    // The Greek letters σ and ς are the same letter in matching: normalisation lowercases and then writes ς (U+03C2) as σ
    // (U+03C3). Kotlin tells them apart (a title typed with σ at the end does not match one with ς); this is a deliberate
    // divergence, and the differential kind I has no case with Σ, σ or ς.
    void sigmaFormsAreTheSameLetterInMatching() {
        QCOMPARE(normalize(u"ΟΔΟΣ"_s), normalize(u"οδος"_s));
        QCOMPARE(normalize(u"οδος"_s), normalize(u"οδοσ"_s));
        QCOMPARE(normalize(u"ΟΔΟΣ"_s), u"οδοσ"_s);
        QVERIFY(isMatch(query({.artist = u"Band"_s, .title = u"ΟΔΟΣ-ΑΘΗΝΑΣ"_s}),
                        candidate({.trackName = u"οδοσ-αθηνασ"_s, .artistName = u"Band"_s, .plainLyrics = u"La la la\nLa la\nLa"_s})));
    }
};

QTEST_APPLESS_MAIN(LyricsMatcherTest)
#include "tst_lyrics_matcher.moc"
