#include <QtTest>

#include <QElapsedTimer>
#include <QFile>

#include <vector>

#include "lyrics/google/lyrics_card.h"
#include "lyrics/google/page_scripts.h"
#include "lyrics/google/selectors.h"
#include "support/google_fakes.h"

// parseCard against the answers lyrics_card.js gives for the made-up pages of
// engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/LyricsCardParserTest.kt (written by hand: no WebView2 here; Q3 Task 6
// runs the script on the same pages and checks that it answers exactly this). The slots named after a Kotlin test with
// "FromAScriptResult" added keep that test's expected values; aFoundResultNeverPrintsTheLyrics is the Kotlin test itself.
// Every sentence is a dummy.

using namespace Qt::StringLiterals;
using xgs::lyrics::google::CardMaxLines;
using xgs::lyrics::google::CardMaxUnits;
using xgs::lyrics::google::CardScopes;
using xgs::lyrics::google::GoogleLyricsResult;
using xgs::lyrics::google::Line;
using xgs::lyrics::google::cardScript;
using xgs::lyrics::google::parseCard;
using xgs::lyrics::google::testing::ScriptLine;
using xgs::lyrics::google::testing::cardResult;
using xgs::lyrics::google::testing::scriptAnswer;
using Kind = GoogleLyricsResult::Kind;

class LyricsCardParserTest : public QObject {
    Q_OBJECT

private slots:
    // testPage(testCard(testParagraph("더미 문장 1", "더미 문장 2"), testParagraph("더미 문장 3", "더미 문장 4", "더미 문장 5")))
    void linesOfAParagraphAreJoinedByNewlineAndParagraphsByOneBlankLineFromAScriptResult() {
        const QString answer = u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"더미 문장 1\"},{\"p\":0,\"t\":\"더미 문장 2\"},"
                               u"{\"p\":1,\"t\":\"더미 문장 3\"},{\"p\":1,\"t\":\"더미 문장 4\"},{\"p\":1,\"t\":\"더미 문장 5\"}]}"_s;
        // The fakes of the other tests build the same answer.
        QCOMPARE(cardResult({{u"더미 문장 1"_s, u"더미 문장 2"_s}, {u"더미 문장 3"_s, u"더미 문장 4"_s, u"더미 문장 5"_s}}), answer);

        const GoogleLyricsResult result = parseCard(answer);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 문장 1\n더미 문장 2\n\n더미 문장 3\n더미 문장 4\n더미 문장 5"_s);
        QCOMPARE(result.lineCount, 5);
        QCOMPARE(result.paragraphCount, 2);
    }

    // testPage(testCard(testParagraph("앞 1", "앞 2"), testParagraph("후렴 더미"), testParagraph("뒤 1", "뒤 2")))
    void aParagraphOfOneLineIsAParagraphFromAScriptResult() {
        const GoogleLyricsResult result = parseCard(
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"앞 1\"},{\"p\":0,\"t\":\"앞 2\"},{\"p\":1,\"t\":\"후렴 더미\"},"
            u"{\"p\":2,\"t\":\"뒤 1\"},{\"p\":2,\"t\":\"뒤 2\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"앞 1\n앞 2\n\n후렴 더미\n\n뒤 1\n뒤 2"_s);
        QCOMPARE(result.lineCount, 5);
        QCOMPARE(result.paragraphCount, 3);
    }

    // testPage(testCard(testParagraph("  더미   문장  ", "&nbsp;", "", "더미 둘", "더미 셋"))): the DOM holds &nbsp; as U+00A0.
    void linesAreTrimmedAndEmptyOnesAreDroppedFromAScriptResult() {
        const GoogleLyricsResult result = parseCard(
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"  더미   문장  \"},{\"p\":0,\"t\":\" \"},{\"p\":0,\"t\":\"\"},"
            u"{\"p\":0,\"t\":\"더미 둘\"},{\"p\":0,\"t\":\"더미 셋\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 문장\n더미 둘\n더미 셋"_s);
        QCOMPARE(result.lineCount, 3);
        QCOMPARE(result.paragraphCount, 1);
    }

    // testPage(testCard("""<div jsname="U8S5sf">${testLine("더미 <b>강조</b> 문장")}${testLine("둘")}${testLine("셋")}</div>"""))
    void markupInsideALineKeepsItsTextFromAScriptResult() {
        const GoogleLyricsResult result = parseCard(
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"더미 강조 문장\"},{\"p\":0,\"t\":\"둘\"},{\"p\":0,\"t\":\"셋\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 강조 문장\n둘\n셋"_s);
    }

    // testPage("""<div jsname="WbKHeb">${testParagraph("더미 1", "더미 2", "더미 3")}</div>""")
    void withoutALyricIdCardTheWbkContainerIsTheScopeFromAScriptResult() {
        const GoogleLyricsResult result = parseCard(
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"더미 1\"},{\"p\":0,\"t\":\"더미 2\"},{\"p\":0,\"t\":\"더미 3\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 1\n더미 2\n더미 3"_s);
    }

    // testPage("""<div>${testLine("가")}${testLine("나")}</div><div>${testLine("다")}</div>""")
    void withoutAnyContainerTheLineSpansOfThePageAreReadFromAScriptResult() {
        const GoogleLyricsResult result = parseCard(
            u"{\"scope\":false,\"lines\":[{\"p\":0,\"t\":\"가\"},{\"p\":0,\"t\":\"나\"},{\"p\":1,\"t\":\"다\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"가\n나\n\n다"_s);
        QCOMPARE(result.paragraphCount, 2);
    }

    // testPage(testParagraph("바깥 줄") + testCard(testParagraph("안 1", "안 2", "안 3"))): the script reads inside the scope only.
    void lineSpansOutsideTheCardAreIgnoredFromAScriptResult() {
        const GoogleLyricsResult result = parseCard(
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"안 1\"},{\"p\":0,\"t\":\"안 2\"},{\"p\":0,\"t\":\"안 3\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"안 1\n안 2\n안 3"_s);
    }

    // testPage("")
    void aPageWithoutAnyCardMarkerHasNoCardFromAScriptResult() {
        QCOMPARE(parseCard(u"{\"scope\":false,\"lines\":[]}"_s).kind, Kind::NoCard);
    }

    // testPage("""<div data-lyricid="id-1"><div jsname="WbKHeb"></div></div>""")
    void aCardWithoutLinesIsAFailedExtractionFromAScriptResult() {
        QCOMPARE(parseCard(u"{\"scope\":true,\"lines\":[]}"_s).kind, Kind::ExtractionFailed);
    }

    // testPage(testCard(testParagraph("더미 1", "더미 2")))
    void aCardWithFewerThanThreeLinesIsAFailedExtractionFromAScriptResult() {
        QCOMPARE(parseCard(u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"더미 1\"},{\"p\":0,\"t\":\"더미 2\"}]}"_s).kind,
                 Kind::ExtractionFailed);
    }

    // testPage(testCard(testParagraph("비밀 문장 1", "비밀 문장 2", "비밀 문장 3")))
    void aFoundResultNeverPrintsTheLyrics() {
        const GoogleLyricsResult result = parseCard(cardResult({{u"비밀 문장 1"_s, u"비밀 문장 2"_s, u"비밀 문장 3"_s}}));
        QCOMPARE(result.kind, Kind::Found);

        QVERIFY(!result.toString().contains(u"비밀"_s));
    }

    // Added (Q3 brief): an answer that is not of the script's shape is a failed extraction, not a crash.
    void aCardResultOfTheWrongShapeIsNotACrash() {
        const QStringList answers{
            u""_s,
            u"null"_s,
            u"[]"_s,
            u"{\"scope\":\"yes\"}"_s,
            u"{\"scope\":true,\"lines\":{}}"_s,
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":5}]}"_s,
            u"{\"scope\":true,\"lines\":[{\"t\":\"x\"}]}"_s,
            u"{\"failed\":true}"_s,   // what the script answers when something went wrong inside it
        };
        for (const QString& answer : answers) QVERIFY2(parseCard(answer).kind == Kind::ExtractionFailed, qPrintable(answer));
    }

    // Added (Q3 brief): a card over CardMaxLines lines or CardMaxUnits units of text is a failed extraction, read quickly. One
    // second for what takes milliseconds: generous for a slow machine and the debug build.
    void aHugeCardIsReadWithinALimit() {
        std::vector<ScriptLine> manyLines;
        for (int i = 0; i <= CardMaxLines; ++i) manyLines.push_back(ScriptLine{0, u"더미 %1"_s.arg(i)});
        const QString tooManyLines = scriptAnswer(true, manyLines);
        manyLines.pop_back();
        const QString mostLines = scriptAnswer(true, manyLines);

        const QString tooLongLine = scriptAnswer(true, {ScriptLine{0, QString(CardMaxUnits + 1, u'가')}});
        std::vector<ScriptLine> longLines;
        for (int i = 0; i < 1000; ++i) longLines.push_back(ScriptLine{0, QString(CardMaxUnits / 1000, u'가')});
        const QString mostUnits = scriptAnswer(true, longLines);
        longLines.push_back(ScriptLine{0, u"가"_s});
        const QString tooManyUnits = scriptAnswer(true, longLines);

        QElapsedTimer timer;
        timer.start();
        const GoogleLyricsResult overLines = parseCard(tooManyLines);
        const GoogleLyricsResult atLines = parseCard(mostLines);
        const GoogleLyricsResult overUnitsInOneLine = parseCard(tooLongLine);
        const GoogleLyricsResult atUnits = parseCard(mostUnits);
        const GoogleLyricsResult overUnits = parseCard(tooManyUnits);
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(overLines.kind, Kind::ExtractionFailed);
        QCOMPARE(atLines.kind, Kind::Found);
        QCOMPARE(overUnitsInOneLine.kind, Kind::ExtractionFailed);
        QCOMPARE(atUnits.kind, Kind::Found);
        QCOMPARE(overUnits.kind, Kind::ExtractionFailed);
        QVERIFY2(elapsed < 1000, qPrintable(QString::number(elapsed)));
    }

    // Added (Q3 brief): cardScript() is lyrics_card.js as it is in the repository, and the script's selectors and limits are
    // those of selectors.h and lyrics_card.h.
    void theEmbeddedScriptIsTheFile() {
        QFile file(QStringLiteral(XGS_LYRICS_CARD_JS));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(!text.isEmpty());

        QCOMPARE(cardScript(), text);

        const QString scopes = u"  var CARD_SCOPES = ['"_s + CardScopes[0].toString() + u"', '"_s + CardScopes[1].toString() + u"'];\n"_s;
        QVERIFY2(text.contains(scopes), qPrintable(scopes));
        const QString line = u"  var LINE = '"_s + Line.toString() + u"';\n"_s;
        QVERIFY2(text.contains(line), qPrintable(line));
        const QString maxLines = u"  var MAX_LINES = "_s + QString::number(CardMaxLines) + u";\n"_s;
        QVERIFY2(text.contains(maxLines), qPrintable(maxLines));
        const QString maxUnits = u"  var MAX_UNITS = "_s + QString::number(CardMaxUnits) + u";\n"_s;
        QVERIFY2(text.contains(maxUnits), qPrintable(maxUnits));
    }
};

QTEST_APPLESS_MAIN(LyricsCardParserTest)
#include "tst_lyrics_card.moc"
