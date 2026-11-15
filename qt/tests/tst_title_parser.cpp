#include <QtTest>

#include <optional>

#include "title/title_parser.h"

using xgs::Confidence;
using xgs::ParsedTrack;
using namespace Qt::StringLiterals;

namespace {

QString confidenceName(Confidence confidence) {
    switch (confidence) {
        case Confidence::High: return u"HIGH"_s;
        case Confidence::Medium: return u"MEDIUM"_s;
        case Confidence::Low: return u"LOW"_s;
    }
    return u"?"_s;
}

QString describe(const ParsedTrack& track) {
    return u"ParsedTrack(artist=%1, title=%2, confidence=%3)"_s.arg(track.artist, track.title, confidenceName(track.confidence));
}

// Kotlin's assertParsed: the failure message, or nullopt when parse gives exactly ParsedTrack(artist, title, confidence).
std::optional<QString> checkParsed(const QString& raw, const QString& artist, const QString& title,
                                   Confidence confidence = Confidence::High, const std::optional<QString>& channel = std::nullopt,
                                   const std::optional<QString>& metaArtist = std::nullopt,
                                   const std::optional<QString>& metaTrack = std::nullopt) {
    const ParsedTrack expected{artist, title, confidence};
    const ParsedTrack parsed = xgs::parseTitle(raw, channel, metaArtist, metaTrack);
    if (parsed == expected) return std::nullopt;
    return u"title: %1\n   expected: %2\n   actual:   %3"_s.arg(raw, describe(expected), describe(parsed));
}

}  // namespace

#define ASSERT_PARSED(...)                                                          \
    do {                                                                            \
        const std::optional<QString> failure = checkParsed(__VA_ARGS__);            \
        QVERIFY2(!failure, failure ? qPrintable(*failure) : "");                    \
    } while (false)

class TitleParserTest : public QObject {
    Q_OBJECT

private slots:
    // ---- fixtures taken from the reference playlist "Melon Daily Top 100" ----------------

    void quotedTitleWithOfficialMusicVideoSuffix() {
        ASSERT_PARSED(u"소연 (SOYEON) '퇴사할게여 (Narr. 기안84)' Official Music Video"_s, u"소연 (SOYEON)"_s,
                      u"퇴사할게여 (Narr. 기안84)"_s);
    }

    void curlyQuotes() { ASSERT_PARSED(u"RESCENE (리센느) ‘LOVE ATTACK’ Official MV"_s, u"RESCENE (리센느)"_s, u"LOVE ATTACK"_s); }

    void curlyQuotesWithApostropheInside() {
        ASSERT_PARSED(u"ILLIT (아일릿) ‘It’s Me’ Official MV"_s, u"ILLIT (아일릿)"_s, u"It’s Me"_s);
    }

    void dashSeparatorWithTrailingMv() {
        ASSERT_PARSED(u"아이오아이 (I.O.I) - 갑자기 (Suddenly) MV"_s, u"아이오아이 (I.O.I)"_s, u"갑자기 (Suddenly)"_s);
    }

    void dashSeparatorWithQuotedTitle() { ASSERT_PARSED(u"ATEEZ(에이티즈) - 'BAD' Official MV"_s, u"ATEEZ(에이티즈)"_s, u"BAD"_s); }

    void leadingMvTagAndUnderscoreSeparator() {
        ASSERT_PARSED(u"[MV] 태연 (TAEYEON)_ 만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"_s, u"태연 (TAEYEON)"_s,
                      u"만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"_s);
    }

    void spacedUnderscoreSeparator() {
        ASSERT_PARSED(u"[MV] Woody(우디) _ Sadder Than Yesterday(어제보다 슬픈 오늘)"_s, u"Woody(우디)"_s,
                      u"Sadder Than Yesterday(어제보다 슬픈 오늘)"_s);
    }

    void liveClipSuffix() { ASSERT_PARSED(u"WOODZ 'Drowning' Live Clip"_s, u"WOODZ"_s, u"Drowning"_s); }

    void leadingEmojiAndProgramTailAfterPipe() {
        ASSERT_PARSED(u"🎤진영&최유리 - 생각을 멈추다 보면 | 들어봐! 유리의 숲2 EP.01 진영 편"_s, u"진영&최유리"_s,
                      u"생각을 멈추다 보면"_s);
    }

    void trailingBracketBlockAndPipeTail() {
        ASSERT_PARSED(u"성시경 - 너의 모든 순간 [유희열의 스케치북/You Heeyeol’s Sketchbook] | KBS 210528 방송"_s, u"성시경"_s,
                      u"너의 모든 순간"_s);
    }

    void apostropheInsideArtistNameIsNotAQuote() {
        ASSERT_PARSED(u"Girls' Generation-HRS 소녀시대-효리수 'Skibidi' Performance Video"_s,
                      u"Girls' Generation-HRS 소녀시대-효리수"_s, u"Skibidi"_s);
    }

    void textAfterClosingQuoteIsDiscarded() {
        ASSERT_PARSED(u"볼빨간사춘기 BOL4 '여름아 부탁해' Special Clip (with 적재)"_s, u"볼빨간사춘기 BOL4"_s, u"여름아 부탁해"_s);
    }

    void backtickInsideQuotedTitleIsKept() {
        ASSERT_PARSED(
            u"AKMU - '어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)' M/V"_s,
            u"AKMU"_s, u"어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)"_s);
    }

    void otherReferenceTitles() {
        ASSERT_PARSED(u"BIGBANG - ‘BiiiG’ M/V"_s, u"BIGBANG"_s, u"BiiiG"_s);
        ASSERT_PARSED(u"[MV] 한로로 (HANRORO) - 사랑하게 될 거야 (Landing in Love)"_s, u"한로로 (HANRORO)"_s,
                      u"사랑하게 될 거야 (Landing in Love)"_s);
        ASSERT_PARSED(u"Hearts2Hearts 하츠투하츠 'RUDE!' MV"_s, u"Hearts2Hearts 하츠투하츠"_s, u"RUDE!"_s);
        ASSERT_PARSED(u"IU '이 별로부터(Unknown Planet)' MV"_s, u"IU"_s, u"이 별로부터(Unknown Planet)"_s);
        ASSERT_PARSED(u"aespa 에스파 'LEMONADE' MV"_s, u"aespa 에스파"_s, u"LEMONADE"_s);
        ASSERT_PARSED(u"다비치 (DAVICHI) '타임캡슐' Official Music Video"_s, u"다비치 (DAVICHI)"_s, u"타임캡슐"_s);
        ASSERT_PARSED(u"YENA(최예나) - '캐치 캐치' M/V"_s, u"YENA(최예나)"_s, u"캐치 캐치"_s);
        ASSERT_PARSED(u"도경수 Doh Kyung Soo 'Popcorn' MV"_s, u"도경수 Doh Kyung Soo"_s, u"Popcorn"_s);
    }

    // ---- synthetic edge cases -------------------------------------------------------------

    void leadingParenthesisInArtistNameIsKept() {
        ASSERT_PARSED(u"(G)I-DLE - 'TOMBOY' Official Music Video"_s, u"(G)I-DLE"_s, u"TOMBOY"_s);
    }

    void apostropheBeforeSeparator() { ASSERT_PARSED(u"Girls' Generation - Gee"_s, u"Girls' Generation"_s, u"Gee"_s); }

    void separatorInsideQuotesBelongsToTheTitle() { ASSERT_PARSED(u"IU 'Love - Poem' MV"_s, u"IU"_s, u"Love - Poem"_s); }

    void apostropheInsideQuotedTitle() { ASSERT_PARSED(u"Artist 'Don't Stop' MV"_s, u"Artist"_s, u"Don't Stop"_s); }

    void enDashSeparator() { ASSERT_PARSED(u"Artist – Title"_s, u"Artist"_s, u"Title"_s); }

    void nestedLeadingTags() { ASSERT_PARSED(u"[Official MV] [MV] Artist - Song"_s, u"Artist"_s, u"Song"_s); }

    void leadingEmoji() { ASSERT_PARSED(u"🎶 Artist - Song"_s, u"Artist"_s, u"Song"_s); }

    void noiseMustBeASeparateWord() { ASSERT_PARSED(u"Artist - HAMV"_s, u"Artist"_s, u"HAMV"_s); }

    void noiseInParentheses() { ASSERT_PARSED(u"Artist - Song (Official Video)"_s, u"Artist"_s, u"Song"_s); }

    void parenthesesThatAreNotNoiseAreKept() {
        ASSERT_PARSED(u"Artist - Song (feat. Someone)"_s, u"Artist"_s, u"Song (feat. Someone)"_s);
    }

    // ---- fallbacks ------------------------------------------------------------------------

    void metadataIsUsedWhenTitleHasNoPattern() {
        ASSERT_PARSED(u"Dynamite"_s, u"BTS"_s, u"Dynamite"_s, Confidence::Medium, u"BTS - Topic"_s, u"BTS"_s, u"Dynamite"_s);
    }

    void topicChannelTitleIsNeverSplit() {
        ASSERT_PARSED(u"Song - Remix"_s, u"Artist"_s, u"Song - Remix"_s, Confidence::Medium, u"Artist - Topic"_s, u"Artist"_s,
                      u"Song - Remix"_s);
    }

    void topicChannelWithoutMetadataFallsBackToChannelName() {
        ASSERT_PARSED(u"Dynamite"_s, u"BTS"_s, u"Dynamite"_s, Confidence::Low, u"BTS - Topic"_s);
    }

    void vevoSuffixIsRemovedFromChannelName() {
        ASSERT_PARSED(u"7 rings (Official Video)"_s, u"ArianaGrande"_s, u"7 rings"_s, Confidence::Low, u"ArianaGrandeVevo"_s);
    }

    void unknownArtistWhenNothingIsAvailable() {
        ASSERT_PARSED(u"Some Song"_s, xgs::unknownArtist(), u"Some Song"_s, Confidence::Low);
    }

    // ---- ASCII symbols at the start of an artist name are not junk -------------------------

    void ampersandStaysInArtistName() { ASSERT_PARSED(u"&TEAM 'Go in Blind' MV"_s, u"&TEAM"_s, u"Go in Blind"_s); }

    void asteriskStaysInArtistName() { ASSERT_PARSED(u"*NSYNC - Bye Bye Bye"_s, u"*NSYNC"_s, u"Bye Bye Bye"_s); }

    void dollarSignsStayInArtistName() { ASSERT_PARSED(u"$uicideboy$ - Song"_s, u"$uicideboy$"_s, u"Song"_s); }

    void exclamationMarksAloneAreAnArtistName() { ASSERT_PARSED(u"!!! - Song"_s, u"!!!"_s, u"Song"_s); }

    void emojiWithVariationSelectorIsStillStripped() {
        ASSERT_PARSED(u"❤️ Artist - Song"_s, u"Artist"_s, u"Song"_s);
    }

    // Characterization, not a wish: the Kotlin app's stripLeadingJunk drops every surrogate unit at the start, so a letter
    // outside the BMP (here U+1D49C, mathematical script A) is dropped like an emoji. This reproduces the Kotlin behaviour
    // (the expected values are the Kotlin code's answers, kind T of the differential tool); it is listed in the stage report
    // as a suspected Kotlin bug. Do not "fix" it here without changing the Kotlin side first.
    void leadingNonBmpLettersAreDroppedLikeEmoji() {
        ASSERT_PARSED(u"\U0001D49Crtist - Song"_s, u"rtist"_s, u"Song"_s);
    }

    // ---- any leading [..] tag is dropped, not only known ones ------------------------------

    void anyLeadingBracketTagIsDropped_mama() {
        ASSERT_PARSED(u"[#2024MAMA] G-DRAGON - HOME SWEET HOME (feat. Taeyang, Daesung) | Mnet 241123"_s, u"G-DRAGON"_s,
                      u"HOME SWEET HOME (feat. Taeyang, Daesung)"_s);
    }

    void anyLeadingBracketTagIsDropped_live() {
        ASSERT_PARSED(u"[LIVE] 이창섭 - 천상연 (선녀외전 OST) 라이브 (Full. ver)"_s, u"이창섭"_s,
                      u"천상연 (선녀외전 OST) 라이브 (Full. ver)"_s);
    }

    void anyLeadingBracketTagIsDropped_liveWithShowName() {
        ASSERT_PARSED(u"[LIVE] Car, the garden - 그대 작은 나의 세상이 되어 | 2026 단독공연 'BLUE HEART'"_s,
                      u"Car, the garden"_s, u"그대 작은 나의 세상이 되어"_s);
    }

    void anyLeadingBracketTagIsDropped_programName() {
        ASSERT_PARSED(u"[DJ티비씨] 폴킴(Paul Kim) - 모든 날, 모든 순간 ♬ #비긴어게인3 #DJ티비씨"_s, u"폴킴(Paul Kim)"_s,
                      u"모든 날, 모든 순간 ♬ #비긴어게인3 #DJ티비씨"_s);
    }

    void anyLeadingBracketTagIsDropped_genre() {
        ASSERT_PARSED(u"[Ballad] 임현정 - 사랑은 봄비처럼... 이별은 겨울비처럼..."_s, u"임현정"_s,
                      u"사랑은 봄비처럼... 이별은 겨울비처럼..."_s);
    }

    void anyLeadingBracketTagIsDropped_preRelease() {
        ASSERT_PARSED(u"[선공개] PLAVE - 이 밤을 빌려 말해요 MV (영화 '오늘 밤, 세계에서 이 사랑이 사라진다 해도')"_s, u"PLAVE"_s,
                      u"이 밤을 빌려 말해요 MV (영화 '오늘 밤, 세계에서 이 사랑이 사라진다 해도')"_s);
    }

    void aTitleThatIsOnlyABracketBlockIsKept() {
        const ParsedTrack parsed = xgs::parseTitle(u"[MV]"_s, u"Some Channel"_s);
        QCOMPARE(parsed.artist, u"Some Channel"_s);
        QCOMPARE(parsed.title, u"[MV]"_s);
        QCOMPARE(parsed.confidence, Confidence::Low);
    }
};

QTEST_APPLESS_MAIN(TitleParserTest)
#include "tst_title_parser.moc"
