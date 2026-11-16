#include <QtTest>

#include <optional>

#include "tags/ffmetadata.h"
#include "tags/track_tags.h"

using xgs::tags::TrackTags;
using namespace Qt::StringLiterals;

namespace {

// Kotlin's tags(title, artist, album, albumArtist = artist, trackNumber, comment) with the same defaults, and the lyrics
// of `.copy(lyrics = ...)`. An absent album is spelled `.album = std::nullopt`.
struct Options {
    QString title = u"Dynamite"_s;
    QString artist = u"BTS"_s;
    std::optional<QString> album = u"Best of BTS"_s;
    std::optional<QString> albumArtist;   // default: the artist
    int trackNumber = 7;
    std::optional<QString> comment;
    std::optional<QString> lyrics;
};

TrackTags tags(const Options& o = {}) {
    return TrackTags{o.title, o.artist, o.album, o.albumArtist.value_or(o.artist), o.trackNumber, o.comment, o.lyrics};
}

// text.removeSuffix("\n").split("\n")
QStringList lines(QString text) {
    if (text.endsWith(u'\n')) text.chop(1);
    return text.split(u'\n', Qt::KeepEmptyParts);
}

QString render(const Options& o = {}) {
    return xgs::tags::renderFfmetadata(tags(o));
}

}  // namespace

class FfmetadataTest : public QObject {
    Q_OBJECT

private slots:
    void writesTheHeaderThenEveryTagInOrder() {
        const QString text = render();

        QCOMPARE(lines(text), (QStringList{
                                  u";FFMETADATA1"_s,
                                  u"title=Dynamite"_s,
                                  u"artist=BTS"_s,
                                  u"album_artist=BTS"_s,
                                  u"album=Best of BTS"_s,
                                  u"track=7"_s,
                              }));
        QVERIFY(text.endsWith(u'\n'));
        QVERIFY(!text.contains(u"\r"_s));
    }

    void theLyricsAreRenderedAfterTheCommentWithLineBreaksEscaped() {
        const QString withLyrics = render({.comment = u"note"_s, .lyrics = u"Line one\nLine two=three\n첫 번째 줄"_s});

        QCOMPARE(withLyrics,
                 u";FFMETADATA1\ntitle=Dynamite\nartist=BTS\nalbum_artist=BTS\nalbum=Best of BTS\ntrack=7\ncomment=note\n"
                 u"lyrics=Line one\\\nLine two\\=three\\\n첫 번째 줄\n"_s);
    }

    void blankLyricsAreNotRendered() {
        for (const std::optional<QString>& lyrics :
             {std::optional<QString>(), std::optional<QString>(u""_s), std::optional<QString>(u" \n\t"_s)}) {
            QVERIFY2(!render({.lyrics = lyrics}).contains(u"lyrics"_s),
                     qPrintable(u"lyrics = ["_s + lyrics.value_or(u"null"_s) + u"]"_s));
        }
    }

    void albumArtistComesFromItsOwnField() {
        const QString text = render({.artist = u"Artist"_s, .albumArtist = u"Various"_s});

        QVERIFY(lines(text).contains(u"artist=Artist"_s));
        QVERIFY(lines(text).contains(u"album_artist=Various"_s));
    }

    void omitsTheAlbumWhenItIsNullOrBlank() {
        for (const std::optional<QString>& album :
             {std::optional<QString>(), std::optional<QString>(u""_s), std::optional<QString>(u"   "_s)}) {
            QStringList keys;
            for (const QString& line : lines(render({.album = album}))) keys.append(line.left(line.indexOf(u'=') < 0 ? line.size() : line.indexOf(u'=')));

            QCOMPARE(keys, (QStringList{u";FFMETADATA1"_s, u"title"_s, u"artist"_s, u"album_artist"_s, u"track"_s}));
        }
    }

    void theCommentIsRenderedUnlessItIsNullOrBlank() {
        for (const std::optional<QString>& comment :
             {std::optional<QString>(), std::optional<QString>(u""_s), std::optional<QString>(u"\t "_s)}) {
            bool any = false;
            for (const QString& line : lines(render({.comment = comment}))) any = any || line.startsWith(u"comment"_s);
            QVERIFY2(!any, qPrintable(u"comment = ["_s + comment.value_or(u"null"_s) + u"]"_s));
        }

        const QString text = render({.comment = u"https://www.youtube.com/watch?v=abc"_s});

        QVERIFY2(lines(text).contains(u"comment=https://www.youtube.com/watch?v\\=abc"_s), qPrintable(text));
    }

    void writesTheTrackNumberAsAPlainInteger() {
        QVERIFY(lines(render({.trackNumber = 1})).contains(u"track=1"_s));
        QVERIFY(lines(render({.trackNumber = 999})).contains(u"track=999"_s));
    }

    void escapesEqualsSemicolonHashAndBackslash() {
        const QString text = render({.title = u"a=b;c#d\\e"_s});

        QVERIFY(lines(text).contains(uR"(title=a\=b\;c\#d\\e)"_s));
    }

    void escapesALineBreakByKeepingItAfterABackslash() {
        const QString text = render({.title = u"first\nsecond"_s, .album = std::nullopt});

        QCOMPARE(text, u";FFMETADATA1\ntitle=first\\\nsecond\nartist=BTS\nalbum_artist=BTS\ntrack=7\n"_s);
    }

    void escapesACarriageReturnToo() {
        const QString text = render({.title = u"first\r\nsecond"_s, .album = std::nullopt});

        QVERIFY2(text.startsWith(u";FFMETADATA1\ntitle=first\\\r\\\nsecond\n"_s), qPrintable(text));
    }

    void aValueEndingInALineBreakStaysOnItsOwnTag() {
        const QString text = render({.title = u"first\n"_s, .album = std::nullopt});

        QCOMPARE(text, u";FFMETADATA1\ntitle=first\\\n\nartist=BTS\nalbum_artist=BTS\ntrack=7\n"_s);
    }

    void passesKoreanTextAndQuotesThroughUnchanged() {
        const QString text = render({.title = u"\"Golden\" 'x' 골든 (feat. 지민)"_s,
                                     .artist = u"방탄소년단"_s,
                                     .album = u"한국 노래 모음"_s});

        QCOMPARE(lines(text), (QStringList{
                                  u";FFMETADATA1"_s,
                                  u"title=\"Golden\" 'x' 골든 (feat. 지민)"_s,
                                  u"artist=방탄소년단"_s,
                                  u"album_artist=방탄소년단"_s,
                                  u"album=한국 노래 모음"_s,
                                  u"track=7"_s,
                              }));
    }

    // ffmpeg treats a line that ends in a backslash as continuing on the next line, even when that backslash is itself
    // escaped, so the next tag would be swallowed into the value. A trailing backslash becomes a full-width one.

    void aTrailingBackslashBecomesAFullWidthBackslash() {
        const QString text = render({.title = u"foo\\"_s});

        QCOMPARE(lines(text), (QStringList{u";FFMETADATA1"_s, u"title=foo＼"_s, u"artist=BTS"_s, u"album_artist=BTS"_s,
                                           u"album=Best of BTS"_s, u"track=7"_s}));
    }

    void everyTrailingBackslashIsReplacedButInnerOnesAreJustEscaped() {
        const QString text = render({.title = u"a\\b\\\\"_s, .artist = u"x\\y\\"_s, .album = u"plain"_s});

        QVERIFY2(lines(text).contains(u"title=a\\\\b＼＼"_s), qPrintable(text));
        QVERIFY2(lines(text).contains(u"artist=x\\\\y＼"_s), qPrintable(text));
        QVERIFY2(lines(text).contains(u"album=plain"_s), qPrintable(text));
    }

    void aValueThatIsOnlyABackslashBecomesOnlyAFullWidthBackslash() {
        const QString text = render({.title = u"\\"_s, .artist = u"\\\\"_s});

        QVERIFY2(lines(text).contains(u"title=＼"_s), qPrintable(text));
        QVERIFY2(lines(text).contains(u"artist=＼＼"_s), qPrintable(text));
        QVERIFY2(lines(text).contains(u"album_artist=＼＼"_s), qPrintable(text));
    }

    void noLineEndsInABackslashWhateverTheValues() {
        const QStringList nasty = {
            u"\\"_s, u"a\\"_s, u"a\\\\"_s, u"a\\\n"_s, u"\\\n\\"_s, u"a;\\"_s, u"a=\\"_s, u"end\r\n\\"_s, u"\n\\\n"_s,
        };
        for (const QString& value : nasty) {
            const QString text = render({.title = value, .artist = value, .album = value});

            // Walk the text the way ffmpeg splits it: a backslash escapes the next character, so only an unescaped
            // line feed ends a line. None of those may directly follow a backslash.
            int lineEnds = 0;
            qsizetype i = 0;
            while (i < text.size()) {
                if (text.at(i) == u'\\') {
                    i++;
                } else if (text.at(i) == u'\n') {
                    lineEnds++;
                    QVERIFY2(text.at(i - 1) != u'\\',
                             qPrintable(u"a line ends in a backslash for %1 chars: %2"_s.arg(value.size()).arg(text)));
                }
                i++;
            }
            QVERIFY2(lineEnds == 6, qPrintable(u"header and five tags must stay separate lines: "_s + text));
        }
    }

    void aNulCannotEndALineOrInjectATag() {
        const QString text = render({.title = u"abc\u0000album=Injected"_s, .album = u"Real"_s});

        QVERIFY2(lines(text).contains(u"title=abcalbum\\=Injected"_s), qPrintable(text));
        int albumLines = 0;
        for (const QString& line : lines(text)) albumLines += line.startsWith(u"album="_s) ? 1 : 0;
        QCOMPARE(albumLines, 1);
        QVERIFY2(lines(text).contains(u"album=Real"_s), qPrintable(text));
        QVERIFY(!text.contains(QChar(u'\0')));
    }

    void otherControlCharactersAreDroppedButTabAndLineBreaksStay() {
        const QString text = render({.title = u"a\u0001b\u0007c\u001Bd\u001Fe\tf\ng\rh"_s, .album = std::nullopt});

        QVERIFY2(text.startsWith(u";FFMETADATA1\ntitle=abcde\tf\\\ng\\\rh\nartist=BTS\n"_s), qPrintable(text));
    }

    void spacesAndOtherPrintableTextAreKept() {
        const QString text = render({.title = u" leading and trailing  "_s, .album = u"~!@$%^&*()_+{}|:\"<>?[]'`/,."_s});

        QVERIFY2(lines(text).contains(u"title= leading and trailing  "_s), qPrintable(text));
        QVERIFY2(lines(text).contains(u"album=~!@$%^&*()_+{}|:\"<>?[]'`/,."_s), qPrintable(text));
    }
};

QTEST_APPLESS_MAIN(FfmetadataTest)
#include "tst_ffmetadata.moc"
