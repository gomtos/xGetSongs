#include <QtTest>

#include <QElapsedTimer>

#include <algorithm>
#include <optional>

#include "lyrics/lyrics_extractor.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;
using xgs::lyrics::extract;
using xgs::lyrics::tidy;

namespace {

// Every lyric and description in here is made up.
const QString block = u"첫 번째 줄\n두 번째 줄\nLa la la"_s;

// [lines] joined with `\n` (Kotlin's text(vararg lines)).
QString text(const QStringList& lines) {
    return lines.join(u'\n');
}

QString show(const std::optional<QString>& value) {
    return value ? u"\""_s + *value + u"\""_s : u"null"_s;
}

// Kotlin's assertEquals(expected, actual, message): the failure message, or nullopt when they are equal (null equals null).
std::optional<QString> checkEquals(const std::optional<QString>& expected, const std::optional<QString>& actual,
                                   const QString& message = QString()) {
    if (expected == actual) return std::nullopt;
    return (message.isEmpty() ? QString() : message + u"\n   "_s) + u"expected: "_s + show(expected) + u"\n   actual:   "_s +
           show(actual);
}

// Lines 0001xxxx...: the 4-digit number of the line, then 996 times "x" (1000 units).
QStringList longNumberedLines(int count) {
    QStringList lines;
    for (int i = 1; i <= count; ++i) lines.append(QString::number(i).rightJustified(4, u'0') + QString(u'x').repeated(996));
    return lines;
}

}  // namespace

#define ASSERT_EQ_MSG(expected, actual, message)                                                    \
    do {                                                                                            \
        const std::optional<QString> failure_ = checkEquals((expected), (actual), (message));       \
        QVERIFY2(!failure_, failure_ ? qPrintable(*failure_) : "");                                 \
    } while (false)
#define ASSERT_EQ(expected, actual) ASSERT_EQ_MSG(expected, actual, QString())
#define ASSERT_NULL_MSG(actual, message) ASSERT_EQ_MSG(std::optional<QString>(), actual, message)
#define ASSERT_NULL(actual) ASSERT_EQ_MSG(std::optional<QString>(), actual, QString())

class LyricsExtractorTest : public QObject {
    Q_OBJECT

private:
    QString withTerminator(const QString& terminator) {
        return text({u"[Lyrics]"_s, u"Line one"_s, u"Line two"_s, u""_s, u"Line three"_s, terminator, u"Tail one"_s, u"Tail two"_s});
    }

private slots:
    // ---- the structure of a real description ----

    void findsTheBlockBetweenTheHeadingAndTheSeparatorThatFollowsIt() {
        const QString description = text({
            u"Example Artist - Example Song (Official Audio)"_s,
            u"Listen on every platform."_s,
            u"======="_s,
            u"[Lyrics]"_s,
            u"======="_s,
            u"첫 번째 줄"_s,
            u"두 번째 줄"_s,
            u""_s,
            u"La la la"_s,
            u"네 번째 줄"_s,
            u"======="_s,
            u"Instagram: https://example.invalid/artist"_s,
            u"Twitter: https://example.invalid/artist"_s,
            u""_s,
            u"© 2024 Example Label. All rights reserved."_s,
            u""_s,
            u"#example #song"_s,
        });

        ASSERT_EQ(u"첫 번째 줄\n두 번째 줄\n\nLa la la\n네 번째 줄"_s, extract(description));
    }

    void theSeparatorAfterTheHeadingIsOptional() {
        const QString description =
            text({u"Intro"_s, u"======="_s, u"[Lyrics]"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s, u"======="_s, u"Links"_s});

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, extract(description));
    }

    void aHeadingLaterInTheDescriptionIsFound() {
        const QString description =
            text({u"[Credits]"_s, u"Someone"_s, u"Somebody"_s, u""_s, u"[Lyrics]"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s});

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, extract(description));
    }

    // ---- the heading line ----

    void everyHeadingSpellingIsAMarker() {
        const QStringList spellings = {
            u"[Lyrics]"_s, u"Lyrics"_s, u"Lyrics:"_s, u"LYRICS"_s, u"lyric"_s, u"Lyric:"_s, u"■ LYRICS ■"_s, u"♬ Lyrics ♬"_s, u"[ Lyrics ]"_s,
            u"가사"_s, u"[가사]"_s, u"【가사】"_s, u"가사:"_s, u"🎵 가사 🎵"_s,
            u"歌詞"_s, u"【歌詞】"_s, u"歌词"_s, u"[歌词]"_s,
            u"[Lyrics / 가사]"_s, u"[가사 / Lyrics]"_s, u"Lyrics 가사"_s, u"Lyrics (Korean)"_s, u"[Lyrics - Kor]"_s, u"Lyrics (Original)"_s,
            u"  [Lyrics]  "_s, u"\t가사\t"_s,
        };
        for (const QString& spelling : spellings) {
            ASSERT_EQ_MSG(block, extract(spelling + u"\n"_s + block), u"marker: [%1]"_s.arg(spelling));
        }
    }

    void aLineThatIsMoreThanAHeadingIsNotAMarker() {
        const QStringList notMarkers = {
            u"Lyrics by 소연"_s, u"Lyrics: some words"_s, u"Lyricsmith"_s, u"My lyrics are here"_s, u"Korean lyrics"_s, u"Song lyrics"_s,
            u"Lyrics (English)"_s, u"Lyrics (Romanized)"_s, u"[Lyrics] 2024"_s, u"가사집"_s, u"Lyric video"_s, u"Lyrics & Music"_s,
        };
        for (const QString& line : notMarkers) {
            ASSERT_NULL_MSG(extract(line + u"\n"_s + block), u"not a marker: [%1]"_s.arg(line));
        }
    }

    void aHashtagLineIsNeverAMarker() {
        // `#Lyrics #가사` would be the marker `lyrics가사` once the symbols are gone, but it is a line of hashtags.
        const QStringList hashtags = {u"#Lyrics"_s, u"#가사"_s, u"#Lyrics #가사"_s, u"  #lyrics  "_s, u"##가사"_s, u"#Lyrics\t#가사"_s};
        for (const QString& line : hashtags) {
            ASSERT_NULL_MSG(extract(text({line, u"Line one"_s, u"Line two"_s, u"Line three"_s})), u"hashtags only: [%1]"_s.arg(line));
        }
    }

    void aHashtagLineBeforeTheRealMarkerIsSkipped() {
        const QString description = text({u"Example Song"_s, u"#Lyrics #가사"_s, u""_s, u"======="_s, u"[Lyrics]"_s, u"Line one"_s,
                                          u"Line two"_s, u"Line three"_s, u"======="_s, u"Tail"_s});

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, extract(description));
    }

    void aHashFollowedByASpaceCanStillBeAMarker() { ASSERT_EQ(block, extract(u"# 가사\n"_s + block)); }

    void theFirstMarkerWinsAndALaterBlockIsIgnored() {
        const QString description = text({
            u"[Lyrics]"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s,
            u"======="_s,
            u"[Lyrics]"_s, u"Other one"_s, u"Other two"_s, u"Other three"_s,
        });

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, extract(description));
    }

    void aSecondMarkerAfterARomanizedSectionIsIgnoredToo() {
        const QString description = text({
            u"가사"_s, u"첫 줄"_s, u"둘째 줄"_s, u"셋째 줄"_s,
            u"[Romanized]"_s, u"cheot jul"_s, u"dul jjae jul"_s, u"set jjae jul"_s,
            u"[Lyrics]"_s, u"Other one"_s, u"Other two"_s, u"Other three"_s,
        });

        ASSERT_EQ(u"첫 줄\n둘째 줄\n셋째 줄"_s, extract(description));
    }

    void aFirstMarkerWithTooFewLinesDoesNotFallBackToALaterMarker() {
        const QString description =
            text({u"[Lyrics]"_s, u"Only one"_s, u"======="_s, u"[Lyrics]"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s});

        ASSERT_NULL(extract(description));
    }

    // ---- where the block starts ----

    void blankAndSeparatorLinesDirectlyAfterTheMarkerAreSkipped() {
        const QString description = text({u"Lyrics"_s, u""_s, u"======="_s, u""_s, u"-----"_s, u"   "_s, u"=-=-="_s,
                                          u"\u3161\u3161\u3161\u3161"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s});

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, extract(description));
    }

    void aSeparatorInTheMiddleOfTheBlockEndsIt() {
        const QString description =
            text({u"Lyrics"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s, u"======="_s, u"Line four"_s, u"Line five"_s});

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, extract(description));
    }

    // ---- where the block ends ----

    void everySeparatorStyleEndsTheBlock() {
        const QStringList separators = {
            u"======="_s, u"==="_s, u"-----"_s, u"___"_s, u"*****"_s, u"~~~"_s, u"###"_s, u"..."_s, u"+++"_s, u"━━━━"_s, u"───"_s, u"═══"_s,
            u"   =====   "_s,
        };
        for (const QString& separator : separators) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(separator)), u"separator: [%1]"_s.arg(separator));
        }
    }

    void separatorCharactersInAnyMixEndTheBlock() {
        const QStringList separators = {
            u"=-=-="_s, u"-=-"_s, u"=-_*~#.+"_s, u"+-+"_s, u"~-~-~-~"_s, u"*=*"_s, u"._."_s, u"-_-_-"_s, u"━─═"_s, u"═══─"_s, u"=━="_s,
            u"\u3161\u3161\u3161\u3161"_s,  // U+3161 four times: the Hangul letter EU typed as a dash
            u"\u2014\u2014\u2014"_s,        // em dashes
            u"\u25AC\u25AC\u25AC"_s,        // black rectangles
            u"\uFF1D\uFF1D\uFF1D"_s,        // fullwidth equals signs
            u"\u3161=\u2014\u25AC\uFF1D-"_s,  // all the look-alikes in one line
            u"  \u3161\u3161\u3161  "_s,
        };
        for (const QString& separator : separators) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(separator)), u"separator: [%1]"_s.arg(separator));
        }
    }

    void aLineWithAnOrdinaryCharacterAmongSeparatorCharactersIsALyricLine() {
        const QString description = text({u"Lyrics"_s, u"Line one"_s, u"===x==="_s, u"--- ---"_s, u"= = ="_s, u"\u3161\u3161a"_s,
                                          u"\u2014\u2014"_s, u"Line two"_s, u"Line three"_s});

        ASSERT_EQ(u"Line one\n===x===\n--- ---\n= = =\n\u3161\u3161a\n\u2014\u2014\nLine two\nLine three"_s, extract(description));
    }

    void aLineWithALinkEndsTheBlock() {
        const QStringList links = {u"https://example.invalid/a"_s, u"http://example.invalid"_s, u"Follow: www.example.invalid"_s,
                                   u"Visit HTTPS://EXAMPLE.INVALID"_s};
        for (const QString& link : links) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(link)), u"link: [%1]"_s.arg(link));
        }
    }

    void aCopyrightLineEndsTheBlock() {
        const QStringList notices = {
            u"© 2024 Example Label"_s, u"  ©2024"_s, u"ⓒ 2024 Example"_s, u"(c) 2024 Example"_s, u"(C) 2024 Example"_s,
            u"Copyright 2024 Example"_s, u"COPYRIGHT: Example"_s, u"All rights reserved."_s, u"ALL RIGHTS RESERVED"_s,
        };
        for (const QString& notice : notices) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(notice)), u"notice: [%1]"_s.arg(notice));
        }
    }

    void aHashtagLineEndsTheBlock() {
        for (const QString& tag : {u"#example"_s, u"#example #song"_s, u"  #태그"_s, u"##double"_s}) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(tag)), u"hashtag: [%1]"_s.arg(tag));
        }
    }

    void aHashFollowedByASpaceIsNotAHashtag() {
        const QString description = text({u"Lyrics"_s, u"Line one"_s, u"# Line two"_s, u"Line three"_s, u"#"_s});

        ASSERT_EQ(u"Line one\n# Line two\nLine three\n#"_s, extract(description));
    }

    void aBracketedHeadingOfAnotherSectionEndsTheBlock() {
        const QStringList headings = {
            u"[Rom]"_s, u"[Romanized]"_s, u"[Romanization]"_s, u"[English Translation]"_s, u"[Eng]"_s, u"【번역】"_s, u"[Translated]"_s,
            u"[Credits]"_s, u"[Info]"_s, u"[Staff]"_s, u"[Links]"_s, u"[SNS]"_s, u"[Follow us]"_s, u"[번역]"_s, u"[해석]"_s, u"[영문]"_s,
            u"[로마자]"_s, u"[크레딧]"_s, u"[정보]"_s, u"  [ROM]  "_s, u"[Lyrics]"_s, u"【가사】"_s, u"[Lyrics / 가사]"_s,
        };
        for (const QString& heading : headings) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(heading)), u"heading: [%1]"_s.arg(heading));
        }
    }

    void aLineInParenthesesStaysInTheLyricsUnlessItIsExactlyTheHeadingOfAnotherSection() {
        // Echo and background lines look like these; only a handful of words in parentheses name a section.
        const QStringList echoes = {
            u"(Follow me)"_s, u"(Romantic)"_s, u"(English version part)"_s, u"(English Ver.)"_s, u"(Info)"_s, u"(Link my hands)"_s,
            u"(Rom)"_s, u"(Lyrics)"_s, u"(가사)"_s, u"  (Staff)  "_s, u"(Credits to you)"_s, u"(Translated)"_s, u"(Roman)"_s, u"(영어)"_s,
            u"(번역가)"_s, u"(Ooh)"_s,
        };
        const QString description =
            text(QStringList{u"[Lyrics]"_s, u"Line one"_s} + echoes + QStringList{u"Line two"_s, u"Line three"_s, u"======="_s, u"Tail"_s});
        QStringList trimmedEchoes;
        for (const QString& echo : echoes) trimmedEchoes.append(xgs::kt::trimEnd(echo));

        ASSERT_EQ(text(QStringList{u"Line one"_s} + trimmedEchoes + QStringList{u"Line two"_s, u"Line three"_s}), extract(description));
    }

    void aLineInParenthesesThatIsExactlyTheHeadingOfAnotherSectionEndsTheBlock() {
        const QStringList headings = {
            u"(English)"_s, u"(English Translation)"_s, u"(Translation)"_s, u"(Romanized)"_s, u"(Romanization)"_s, u"(번역)"_s, u"(해석)"_s,
            u"(영문)"_s, u"(로마자)"_s, u"(Credit)"_s, u"(Credits)"_s, u"( ENGLISH )"_s, u"  (English)  "_s, u"(english translation)"_s,
            u"(English-Translation)"_s, u"(Roman-ized)"_s,
        };
        for (const QString& heading : headings) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(heading)), u"heading: [%1]"_s.arg(heading));
        }
    }

    void theHeadingsOfAnotherSectionInSquareBracketsAreUnchanged() {
        // [ ] and 【 】 keep the broader rule: a heading that merely starts like one of the words ends the block.
        for (const QString& heading :
             {u"[English Ver.]"_s, u"[Rom]"_s, u"【Romanized】"_s, u"[Credits to you]"_s, u"[Info]"_s, u"[Follow us]"_s}) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(heading)), u"heading: [%1]"_s.arg(heading));
        }
    }

    void aLineInParenthesesStillEndsTheBlockWhenItIsACopyrightNoticeOrBoilerplate() {
        for (const QString& line : {u"(c) 2024 Example"_s, u"(Provided to YouTube by Example Label)"_s, u"(https://example.invalid)"_s}) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(line)), u"line: [%1]"_s.arg(line));
        }
    }

    void songStructureTagsAreLyrics() {
        const QString description = text({
            u"[Lyrics]"_s, u"[Verse 1]"_s, u"Line one"_s, u"[Pre-Chorus]"_s, u"Line two"_s, u"[Chorus]"_s, u"Line three"_s,
            u"[Bridge: Name]"_s, u"[후렴]"_s, u"(Ooh)"_s, u"[Intro]"_s, u"[Interlude]"_s, u"======="_s, u"Tail"_s,
        });

        ASSERT_EQ(u"[Verse 1]\nLine one\n[Pre-Chorus]\nLine two\n[Chorus]\nLine three\n[Bridge: Name]\n[후렴]\n(Ooh)\n[Intro]\n[Interlude]"_s,
                  extract(description));
    }

    void aHeadingWordThatIsNotInBracketsIsLyrics() {
        const QString description = text({u"Lyrics"_s, u"Romance in the air"_s, u"English words"_s, u"Info is what I need"_s,
                                          u"Link my hands"_s, u"======="_s});

        ASSERT_EQ(u"Romance in the air\nEnglish words\nInfo is what I need\nLink my hands"_s, extract(description));
    }

    void aMachineGeneratedLineEndsTheBlock() {
        const QStringList lines = {u"Provided to YouTube by Example Label"_s, u"Released on: 2024-01-01"_s, u"Auto-generated by YouTube."_s,
                                   u"  PROVIDED TO YOUTUBE"_s};
        for (const QString& line : lines) {
            ASSERT_EQ_MSG(u"Line one\nLine two\n\nLine three"_s, extract(withTerminator(line)), u"line: [%1]"_s.arg(line));
        }
    }

    void almostASeparatorIsALyricLine() {
        const QString description = text({u"Lyrics"_s, u"Line one"_s, u"=="_s, u"=-"_s, u".."_s, u"Line two"_s, u"Line three"_s});

        ASSERT_EQ(u"Line one\n==\n=-\n..\nLine two\nLine three"_s, extract(description));
    }

    void aBlockWithoutATerminatorRunsToTheEnd() {
        const QString description =
            text({u"Intro"_s, u"[Lyrics]"_s, u"Line one"_s, u"Line two"_s, u""_s, u"Line three"_s, u"Line four"_s});

        ASSERT_EQ(u"Line one\nLine two\n\nLine three\nLine four"_s, extract(description));
    }

    void aTerminatorRightAfterTheMarkerLeavesNothing() {
        ASSERT_NULL(extract(text({u"[Lyrics]"_s, u"https://example.invalid"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s})));
    }

    // ---- validity ----

    void fewerThanThreeNonBlankLinesGivesNull() {
        ASSERT_NULL(extract(text({u"[Lyrics]"_s, u"Line one"_s, u"Line two"_s, u"======="_s, u"Tail"_s})));
        ASSERT_NULL(extract(text({u"[Lyrics]"_s, u"Line one"_s, u""_s, u""_s, u"Line two"_s, u""_s})));
        ASSERT_NULL(extract(text({u"[Lyrics]"_s, u"Line one"_s})));
        ASSERT_NULL(extract(u"[Lyrics]"_s));
    }

    void exactlyThreeNonBlankLinesIsEnough() {
        ASSERT_EQ(u"a\n\nb\nc"_s, extract(text({u"[Lyrics]"_s, u"a"_s, u""_s, u"b"_s, u"c"_s})));
    }

    void aDescriptionWithoutAMarkerGivesNull() {
        ASSERT_NULL(extract(text({u"Song description"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s, u"https://example.invalid"_s})));
    }

    void nullAndBlankInputGiveNull() {
        ASSERT_NULL(extract(std::nullopt));
        ASSERT_NULL(extract(u""_s));
        ASSERT_NULL(extract(u"   \n\t\n  "_s));
    }

    // ---- normalising and cleaning up ----

    void lineBreaksAreNormalisedToLineFeeds() {
        const QString crlf = u"[Lyrics]\r\nLine one\r\nLine two\r\n\r\nLine three\r\n=======\r\nTail"_s;
        const QString cr = u"[Lyrics]\rLine one\rLine two\r\rLine three\r=======\rTail"_s;

        ASSERT_EQ(u"Line one\nLine two\n\nLine three"_s, extract(crlf));
        ASSERT_EQ(u"Line one\nLine two\n\nLine three"_s, extract(cr));
    }

    void aByteOrderMarkIsGoneWhereverItIs() {
        // One at the very start of the description and one inside a lyric line: both must be absent from the result.
        const QString description = u"\uFEFF[Lyrics]\nLine\uFEFF one\nLine two\n\uFEFFLine three\uFEFF"_s;

        const std::optional<QString> result = extract(description);
        QVERIFY(result.has_value());

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, result);
        QVERIFY(!result->contains(QChar(0xFEFF)));
        ASSERT_EQ(block, extract(u"\uFEFF가사\r\n"_s + block));
    }

    void zeroWidthCharactersAreRemovedFromTheResult() {
        const QList<QChar> invisible = {QChar(0x200B), QChar(0x200C), QChar(0x200D), QChar(0x2060), QChar(0xFEFF)};
        const QString description = text({
            u"Lyrics"_s,
            u"Li\u200Bne\u200C o\u200Dne\u2060"_s,
            u"\u2060Line two\uFEFF"_s,
            u"Lin\u200B\u200C\u200D\u2060\uFEFFe three"_s,
        });

        const std::optional<QString> result = extract(description);
        QVERIFY(result.has_value());

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, result);
        QVERIFY(std::none_of(invisible.begin(), invisible.end(), [&](QChar c) { return result->contains(c); }));
    }

    void aLineOfOnlyZeroWidthCharactersIsBlank() {
        const QString description = text({u"Lyrics"_s, u"\u200B"_s, u"\u200C\u200D"_s, u"Line one"_s, u"\u2060"_s, u"\uFEFF"_s,
                                          u"\u200B"_s, u"\u200B"_s, u"Line two"_s, u"Line three"_s, u"\u200B"_s});

        // Blank at the start and the end is dropped; the run of three blank lines inside shrinks to two.
        ASSERT_EQ(u"Line one\n\n\nLine two\nLine three"_s, extract(description));
    }

    void zeroWidthCharactersDoNotHideAMarkerASeparatorOrAHashtag() {
        ASSERT_EQ(block, extract(u"Ly\u200Bric\u200Cs\u200D\n"_s + block));
        ASSERT_EQ(block, extract(u"\u200B가\u2060사\u200B\n"_s + block));

        const QString tail = text({u"Line one"_s, u"Line two"_s, u"Line three"_s});
        ASSERT_EQ(tail, extract(text({u"Lyrics"_s, tail, u"=\u200B==\u200B"_s, u"Tail"_s})));
        ASSERT_EQ(tail, extract(text({u"Lyrics"_s, tail, u"\u200B#example"_s, u"Tail"_s})));
        ASSERT_NULL(extract(text({u"\u200B#Lyrics"_s, tail})));
    }

    // ---- the Hangul filler (U+3164) ----

    void aMarkerFollowedByTheHangulFillerIsStillAMarker() {
        ASSERT_EQ(block, extract(u"가사\u3164\n"_s + block));
        ASSERT_EQ(block, extract(u"\u3164[가사]\u3164\u3164\n"_s + block));
        ASSERT_EQ(block, extract(u"Lyrics\u3164\n"_s + block));
    }

    void aLineOfOnlyTheHangulFillerIsBlank() {
        const QString description = text({u"Lyrics"_s, u"\u3164"_s, u"Line one"_s, u"\u3164"_s, u"\u3164\u3164"_s, u"\u3164 "_s,
                                          u"Line two"_s, u"Line three"_s, u"\u3164"_s});

        ASSERT_EQ(u"Line one\n\n\nLine two\nLine three"_s, extract(description));
        ASSERT_NULL(extract(text({u"[Lyrics]"_s, u"Line one"_s, u"\u3164"_s, u"Line two"_s, u"\u3164"_s})));
    }

    void theHangulFillerBecomesASpaceAndItsTrailingOnesAreTrimmed() {
        const QString description = text({u"Lyrics"_s, u"Line\u3164one\u3164"_s, u"\u3164Line two"_s, u"Line three\u3164\u3164"_s});

        ASSERT_EQ(u"Line one\n Line two\nLine three"_s, extract(description));
    }

    void runsOfThreeOrMoreBlankLinesBecomeTwo() {
        const QString description = text({u"Lyrics"_s, u"a"_s, u""_s, u""_s, u""_s, u""_s, u"b"_s, u""_s, u""_s, u"c"_s, u""_s, u"d"_s,
                                          u"   "_s, u"\t"_s, u" "_s, u"e"_s});

        ASSERT_EQ(u"a\n\n\nb\n\n\nc\n\nd\n\n\ne"_s, extract(description));
    }

    void leadingAndTrailingBlankLinesOfTheBlockAreDropped() {
        const QString description =
            text({u"Lyrics"_s, u""_s, u""_s, u"Line one"_s, u"Line two"_s, u"Line three"_s, u""_s, u"   "_s, u""_s, u"======="_s});

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, extract(description));
    }

    void trailingWhitespaceIsTrimmedButIndentationStays() {
        const QString description =
            text({u"Lyrics"_s, u"  Line one   "_s, u"Line two\t"_s, u"Line three \u3000"_s, u"   "_s, u"Line four"_s});

        ASSERT_EQ(u"  Line one\nLine two\nLine three\n\nLine four"_s, extract(description));
    }

    void controlCharactersAreRemovedButTabsAndLineBreaksStay() {
        const QString description = u"Lyrics\nLine\x0000 one\x0007\nLine\x007F two\x009F\n\x0001Line three\x000B\nA\tB\n\x0000\n\x0000"_s;

        ASSERT_EQ(u"Line one\nLine two\nLine three\nA\tB"_s, extract(description));
    }

    void aControlCharacterNextToASeparatorDoesNotHideIt() {
        const QString description = text({u"Lyrics"_s, u"Line one"_s, u"Line two"_s, u"Line three"_s, u"=====\x0000"_s, u"Tail"_s});

        ASSERT_EQ(u"Line one\nLine two\nLine three"_s, extract(description));
    }

    void theResultIsCutAtALineBoundaryWhenItWouldPassThirtyThousandCharacters() {
        // 40 lines of 1000 characters: 29 lines plus their line feeds are 29028 characters, the 30th line would make 30029.
        const QStringList longLines = longNumberedLines(40);
        const std::optional<QString> result = extract(text(QStringList{u"Lyrics"_s} + longLines));
        QVERIFY(result.has_value());

        QCOMPARE(result->split(u'\n'), longLines.mid(0, 29));
        QCOMPARE(result->size(), qsizetype(29028));
    }

    void aBlockJustUnderTheLimitIsKeptWhole() {
        // 3000 lines of 9 characters: 3000 * 10 - 1 = 29999 characters; one more line would make 30009.
        QStringList shortLines;
        for (int i = 0; i < 3001; ++i) shortLines.append(u"Line "_s + QString::number(i).rightJustified(4, u'0'));
        const std::optional<QString> kept = extract(text(QStringList{u"Lyrics"_s} + shortLines.mid(0, 3000)));
        QVERIFY(kept.has_value());
        const std::optional<QString> cut = extract(text(QStringList{u"Lyrics"_s} + shortLines));
        QVERIFY(cut.has_value());

        QCOMPARE(kept->size(), qsizetype(29999));
        QCOMPARE(*kept, shortLines.mid(0, 3000).join(u'\n'));
        QCOMPARE(*kept, *cut);
    }

    void aCutDoesNotLeaveABlankLineAtTheEnd() {
        // 29 lines of 1000 characters (29028), a blank line (29029), then a line that does not fit: the cut lands right after the blank line.
        const QStringList longLines = longNumberedLines(29);
        const QString description = text(QStringList{u"Lyrics"_s} + longLines + QStringList{u""_s, QString(u'y').repeated(1000)});

        const std::optional<QString> result = extract(description);
        QVERIFY(result.has_value());

        QCOMPARE(result->split(u'\n'), longLines);
    }

    void koreanEmojiAndOtherNonBmpTextSurvives() {
        const QString lyrics = text({u"한국어 가사 첫 줄 🎵"_s, u"日本語の歌詞 𠮷野家"_s, u"𝄞 music ♪ 🎶 가나다라"_s, u"Ünïcödé ñ"_s});

        ASSERT_EQ(lyrics, extract(u"[가사]\n"_s + lyrics + u"\n=======\nTail"_s));
    }

    void theResultNeverContainsACarriageReturn() {
        const std::optional<QString> result = extract(u"[Lyrics]\r\nLine one\r\nLine two\rLine three\r\n"_s);
        QVERIFY(result.has_value());

        QVERIFY2(!result->contains(u'\r'), qPrintable(*result));
    }

    // ---- tidy: the clean-up and validity rules, for a text that is the lyrics already ----

    void tidyOfNullAndBlankTextGivesNull() {
        ASSERT_NULL(tidy(std::nullopt));
        ASSERT_NULL(tidy(u""_s));
        ASSERT_NULL(tidy(u"   \n\t\n  "_s));
        ASSERT_NULL(tidy(u"\u3164\n\u200B\n\uFEFF"_s));
    }

    void tidyKeepsAPlainTextAsItIs() {
        ASSERT_EQ(block, tidy(block));
        ASSERT_EQ(u"a\n\nb\nc"_s, tidy(u"a\n\nb\nc"_s));
    }

    void tidyUnifiesLineBreaksAndTrimsTheEndsOfTheLinesButNotTheIndentation() {
        const QString value = u"  Line one   \r\nLine two\t\r\n\r\nLine three \u3000\rLine four\r\n"_s;

        ASSERT_EQ(u"  Line one\nLine two\n\nLine three\nLine four"_s, tidy(value));
    }

    void tidyRemovesControlCharactersButKeepsTabsAndLineBreaks() {
        const QString value = u"Line\x0000 one\x0007\nLine\x007F two\x009F\n\x0001Line three\x000B\nA\tB\n\x0000"_s;

        ASSERT_EQ(u"Line one\nLine two\nLine three\nA\tB"_s, tidy(value));
    }

    void tidyDropsBlankLinesAtBothEndsAndShrinksRunsOfThreeOrMoreBlankLinesToTwo() {
        const QString value = text({u""_s, u"  "_s, u"a"_s, u""_s, u""_s, u""_s, u""_s, u"b"_s, u""_s, u""_s, u"c"_s, u""_s, u"d"_s, u"   "_s,
                                    u"\t"_s, u" "_s, u"e"_s, u""_s, u"\t"_s, u""_s});

        ASSERT_EQ(u"a\n\n\nb\n\n\nc\n\nd\n\n\ne"_s, tidy(value));
    }

    void tidyNeedsAtLeastThreeNonBlankLines() {
        ASSERT_NULL(tidy(u"Only one"_s));
        ASSERT_NULL(tidy(u"Line one\n\n\nLine two\n"_s));
        ASSERT_EQ(u"a\n\nb\nc"_s, tidy(u"a\n\nb\nc"_s));
    }

    void tidyCutsAtALineBoundaryWhenTheTextWouldPassThirtyThousandCharacters() {
        const QStringList longLines = longNumberedLines(40);

        const std::optional<QString> result = tidy(longLines.join(u'\n'));
        QVERIFY(result.has_value());

        QCOMPARE(result->split(u'\n'), longLines.mid(0, 29));
        QCOMPARE(result->size(), qsizetype(29028));
    }

    void tidyKeepsABlockJustUnderTheLimitWhole() {
        QStringList lines;
        for (int i = 0; i < 3000; ++i) lines.append(u"Line "_s + QString::number(i).rightJustified(4, u'0'));

        const std::optional<QString> result = tidy(lines.join(u'\n'));
        QVERIFY(result.has_value());

        QCOMPARE(result->size(), qsizetype(29999));
        QCOMPARE(*result, lines.join(u'\n'));
    }

    void tidyCutsAWholeLineAndNeverSplitsASurrogatePair() {
        // 100 notes of two UTF-16 units each: a line of 200 characters; 149 such lines make 29948 characters, 150 would make 30149.
        const QString line = u"🎵"_s.repeated(100);
        QStringList many;
        for (int i = 0; i < 400; ++i) many.append(line);

        const std::optional<QString> result = tidy(many.join(u'\n'));
        QVERIFY(result.has_value());

        QStringList expected;
        for (int i = 0; i < 149; ++i) expected.append(line);
        QCOMPARE(result->split(u'\n'), expected);
        QCOMPARE(result->size(), qsizetype(29948));
        const QString& r = *result;
        int loneSurrogates = 0;
        for (qsizetype i = 0; i < r.size(); ++i) {
            if ((r[i].isHighSurrogate() && !(i + 1 < r.size() && r[i + 1].isLowSurrogate())) ||
                (r[i].isLowSurrogate() && !(i > 0 && r[i - 1].isHighSurrogate()))) {
                ++loneSurrogates;
            }
        }
        QCOMPARE(loneSurrogates, 0);
        // A single line that is longer than the limit cannot be cut inside: it is dropped, and what is left is too short.
        ASSERT_NULL(tidy(u"a\nb\n"_s + u"🎵"_s.repeated(20'000)));
    }

    void tidyKeepsKoreanEmojiAndOtherNonBmpText() {
        const QString lyrics = text({u"한국어 가사 첫 줄 🎵"_s, u"日本語の歌詞 𠮷野家"_s, u"𝄞 music ♪ 🎶 가나다라"_s, u"Ünïcödé ñ"_s});

        ASSERT_EQ(lyrics, tidy(lyrics));
    }

    void tidyRemovesZeroWidthCharactersAndTurnsTheHangulFillerIntoASpace() {
        const QString value = u"Li\u200Bne\u200C one\u200D\n\u2060Line\u3164two\uFEFF\n\u3164\n\u3164\n\u200B\n\u3164\nLine three\u3164"_s;

        const std::optional<QString> result = tidy(value);
        QVERIFY(result.has_value());

        ASSERT_EQ(u"Line one\nLine two\n\n\nLine three"_s, result);
        const QString invisible = u"\u200B\u200C\u200D\u2060\uFEFF\u3164"_s;
        QVERIFY(std::none_of(result->begin(), result->end(), [&](QChar c) { return invisible.contains(c); }));
    }

    void tidyDoesNotLookForAHeadingOrASeparator() {
        // It is for text that is the lyrics already: a heading, separator, link or hashtag line is just a line.
        const QString value = text({u"[Lyrics]"_s, u"Line one"_s, u"======="_s, u"https://example.invalid"_s, u"#example"_s});

        ASSERT_EQ(value, tidy(value));
    }

    void extractGivesTheTidyBlock() {
        const QString noisy = text({u"Lyrics"_s, u""_s, u"  Line one   "_s, u""_s, u""_s, u""_s, u""_s, u"Line two\t"_s, u"Line three"_s,
                                    u""_s, u""_s, u"======="_s, u"Tail"_s});
        const QString blockText = u"  Line one   \n\n\n\n\nLine two\t\nLine three"_s;

        ASSERT_EQ(tidy(blockText), extract(noisy));
        ASSERT_EQ(u"  Line one\n\n\nLine two\nLine three"_s, extract(noisy));
    }

    // ---- added in the port (no Kotlin counterpart): a 1 MB description ----

    void extractOnOneMegabyteDescriptionFinishesQuickly() {
        const QString line = u"가사 한 줄 lyric line\n"_s;
        QString description = u"[Lyrics]\n"_s;
        while (description.size() <= 1'048'576) description += line;

        QElapsedTimer timer;
        timer.start();
        const std::optional<QString> result = extract(description);
        const qint64 elapsed = timer.elapsed();
        qInfo("extract of %lld UTF-16 units took %lld ms", static_cast<long long>(description.size()), static_cast<long long>(elapsed));

#ifdef NDEBUG
        // The time limit is about the optimized build. An unoptimized Debug build with Qt's checks on is many times slower
        // (5.5 s here); there the extraction still runs and its result is still checked, and the time is reported above.
        QVERIFY2(elapsed < 2000, qPrintable(u"took %1 ms"_s.arg(elapsed)));
#endif
        QVERIFY(result.has_value());
        QVERIFY(result->size() <= 30000);
        QVERIFY(!result->endsWith(u'\n'));
    }
};

QTEST_APPLESS_MAIN(LyricsExtractorTest)
#include "tst_lyrics_extractor.moc"
