#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "lyrics/google/browser_agent.h"
#include "lyrics/google/lyrics_card.h"
#include "lyrics/google/page_scripts.h"
#include "support/google_fakes.h"
#include "webview/hidden_web_view.h"
#include "webview/runtime.h"
#include "webview_test_support.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/LyricsCardParserTest.kt with the real browser: every
// made-up page of the Kotlin test (built by the same testPage, testCard, testParagraph and testLine) is put into a hidden
// WebView2 (setHtmlForTest), lyrics_card.js reads it there, and parseCard reads the script's answer; the Kotlin expected values
// are kept. Each test also checks that the script answered exactly what tst_lyrics_card wrote by hand for that page (the
// answers the Qt-only tests and the fakes rely on). Every sentence is a dummy.

using namespace Qt::StringLiterals;
using xgs::lyrics::google::GoogleLyricsResult;
using xgs::lyrics::google::PageOutcome;
using xgs::lyrics::google::cardScript;
using xgs::lyrics::google::decodeScriptResult;
using xgs::lyrics::google::parseCard;
using xgs::webview::HiddenWebView;
using xgs::webview::HiddenWebViewOptions;
using xgs::webview::runtimeVersion;
using xgs::webview::testing::CheckBrowserArguments;
using xgs::webview::testing::ScreenWatch;
using xgs::webview::testing::waitForPage;
using Kind = GoogleLyricsResult::Kind;
namespace fakes = xgs::lyrics::google::testing;

namespace {

// GoogleTestSupport.kt's builders.
QString testLine(const QString& text) {
    return u"<span jsname=\"YS01Ge\">"_s + text + u"</span>"_s;
}

QString testParagraph(const QStringList& lines) {
    QStringList spans;
    for (const QString& line : lines) spans.append(testLine(line));
    return u"<div jsname=\"U8S5sf\">"_s + spans.join(u"<br>"_s) + u"</div>"_s;
}

QString testCard(const QStringList& paragraphs) {
    return u"<div data-lyricid=\"id-1\"><div jsname=\"WbKHeb\">"_s + paragraphs.join(QString()) + u"</div></div>"_s;
}

QString testPage(const QString& body) {
    return u"<html><body><div>검색 결과 더미</div>"_s + body + u"</body></html>"_s;
}

}  // namespace

class LyricsCardParserTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<HiddenWebView> m_view;
    std::unique_ptr<ScreenWatch> m_watch;
    FILETIME m_since{};

    // The answer of lyrics_card.js for html in the real browser, as the agent decodes it; a failed call fails the test.
    QString scriptAnswerFor(const QString& html) {
        // Ten seconds: a page is set in well under a second; the first one also makes the window (about a second here).
        const auto set = waitForPage([&](auto done) { m_view->setHtmlForTest(html, std::move(done)); }, 10000);
        if (!set.outcome || !set.outcome->ok) {
            qWarning("setHtmlForTest: %s", set.outcome ? qPrintable(set.outcome->text) : "no answer");
            return QString();
        }
        const auto read = waitForPage([&](auto done) { m_view->evaluate(cardScript(), std::move(done)); }, 10000);
        if (!read.outcome || !read.outcome->ok) {
            qWarning("card script: %s", read.outcome ? qPrintable(read.outcome->text) : "no answer");
            return QString();
        }
        return decodeScriptResult(read.outcome->text).value_or(QString());
    }

    GoogleLyricsResult found(const QString& html, const QString& expectedAnswer) {
        const QString answer = scriptAnswerFor(html);
        [&] { QCOMPARE(answer, expectedAnswer); }();
        return parseCard(answer);
    }

private slots:
    void initTestCase() {
        if (!runtimeVersion()) QSKIP("WebView2 runtime must be installed");
        m_since = xgs::testsupport::nowAsFileTime();
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_watch = std::make_unique<ScreenWatch>();
        m_view = std::make_unique<HiddenWebView>(HiddenWebViewOptions{m_dir->path(), CheckBrowserArguments});
    }

    void cleanupTestCase() {
        m_view.reset();
        if (m_watch) QVERIFY2(m_watch->seen().isEmpty(), qPrintable(m_watch->seen().join(u"; "_s)));
        if (m_dir) {
            // Twenty seconds: the browser process ends a moment after its last window is gone (about a second here).
            const auto left = xgs::webview::testing::browserProcessesLeft(m_dir->path(), m_since, 20000);
            QVERIFY2(left.isEmpty(), qPrintable(xgs::webview::testing::describe(left)));
        }
    }

    void linesOfAParagraphAreJoinedByNewlineAndParagraphsByOneBlankLine() {
        const GoogleLyricsResult result = found(
            testPage(testCard({testParagraph({u"더미 문장 1"_s, u"더미 문장 2"_s}),
                               testParagraph({u"더미 문장 3"_s, u"더미 문장 4"_s, u"더미 문장 5"_s})})),
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"더미 문장 1\"},{\"p\":0,\"t\":\"더미 문장 2\"},"
            u"{\"p\":1,\"t\":\"더미 문장 3\"},{\"p\":1,\"t\":\"더미 문장 4\"},{\"p\":1,\"t\":\"더미 문장 5\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 문장 1\n더미 문장 2\n\n더미 문장 3\n더미 문장 4\n더미 문장 5"_s);
        QCOMPARE(result.lineCount, 5);
        QCOMPARE(result.paragraphCount, 2);
    }

    void aParagraphOfOneLineIsAParagraph() {
        const GoogleLyricsResult result = found(
            testPage(testCard({testParagraph({u"앞 1"_s, u"앞 2"_s}), testParagraph({u"후렴 더미"_s}),
                               testParagraph({u"뒤 1"_s, u"뒤 2"_s})})),
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"앞 1\"},{\"p\":0,\"t\":\"앞 2\"},{\"p\":1,\"t\":\"후렴 더미\"},"
            u"{\"p\":2,\"t\":\"뒤 1\"},{\"p\":2,\"t\":\"뒤 2\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"앞 1\n앞 2\n\n후렴 더미\n\n뒤 1\n뒤 2"_s);
        QCOMPARE(result.lineCount, 5);
        QCOMPARE(result.paragraphCount, 3);
    }

    void linesAreTrimmedAndEmptyOnesAreDropped() {
        // The DOM holds &nbsp; as U+00A0.
        const GoogleLyricsResult result = found(
            testPage(testCard({testParagraph({u"  더미   문장  "_s, u"&nbsp;"_s, u""_s, u"더미 둘"_s, u"더미 셋"_s})})),
            u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"  더미   문장  \"},{\"p\":0,\"t\":\""_s + QChar(0x00A0)
                + u"\"},{\"p\":0,\"t\":\"\"},{\"p\":0,\"t\":\"더미 둘\"},{\"p\":0,\"t\":\"더미 셋\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 문장\n더미 둘\n더미 셋"_s);
        QCOMPARE(result.lineCount, 3);
        QCOMPARE(result.paragraphCount, 1);
    }

    void markupInsideALineKeepsItsText() {
        const QString html = testPage(testCard(
            {u"<div jsname=\"U8S5sf\">"_s + testLine(u"더미 <b>강조</b> 문장"_s) + testLine(u"둘"_s) + testLine(u"셋"_s) + u"</div>"_s}));

        const GoogleLyricsResult result =
            found(html, u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"더미 강조 문장\"},{\"p\":0,\"t\":\"둘\"},{\"p\":0,\"t\":\"셋\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 강조 문장\n둘\n셋"_s);
    }

    void withoutALyricIdCardTheWbkContainerIsTheScope() {
        const QString html =
            testPage(u"<div jsname=\"WbKHeb\">"_s + testParagraph({u"더미 1"_s, u"더미 2"_s, u"더미 3"_s}) + u"</div>"_s);

        const GoogleLyricsResult result =
            found(html, u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"더미 1\"},{\"p\":0,\"t\":\"더미 2\"},{\"p\":0,\"t\":\"더미 3\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 1\n더미 2\n더미 3"_s);
    }

    void withoutAnyContainerTheLineSpansOfThePageAreRead() {
        const QString html =
            testPage(u"<div>"_s + testLine(u"가"_s) + testLine(u"나"_s) + u"</div><div>"_s + testLine(u"다"_s) + u"</div>"_s);

        const GoogleLyricsResult result =
            found(html, u"{\"scope\":false,\"lines\":[{\"p\":0,\"t\":\"가\"},{\"p\":0,\"t\":\"나\"},{\"p\":1,\"t\":\"다\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"가\n나\n\n다"_s);
        QCOMPARE(result.paragraphCount, 2);
    }

    void lineSpansOutsideTheCardAreIgnored() {
        const QString html = testPage(testParagraph({u"바깥 줄"_s}) + testCard({testParagraph({u"안 1"_s, u"안 2"_s, u"안 3"_s})}));

        const GoogleLyricsResult result =
            found(html, u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"안 1\"},{\"p\":0,\"t\":\"안 2\"},{\"p\":0,\"t\":\"안 3\"}]}"_s);

        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"안 1\n안 2\n안 3"_s);
    }

    void aPageWithoutAnyCardMarkerHasNoCard() {
        QCOMPARE(found(testPage(QString()), u"{\"scope\":false,\"lines\":[]}"_s).kind, Kind::NoCard);
    }

    void aCardWithoutLinesIsAFailedExtraction() {
        const QString html = testPage(u"<div data-lyricid=\"id-1\"><div jsname=\"WbKHeb\"></div></div>"_s);

        QCOMPARE(found(html, u"{\"scope\":true,\"lines\":[]}"_s).kind, Kind::ExtractionFailed);
    }

    void aCardWithFewerThanThreeLinesIsAFailedExtraction() {
        QCOMPARE(found(testPage(testCard({testParagraph({u"더미 1"_s, u"더미 2"_s})})),
                       u"{\"scope\":true,\"lines\":[{\"p\":0,\"t\":\"더미 1\"},{\"p\":0,\"t\":\"더미 2\"}]}"_s)
                     .kind,
                 Kind::ExtractionFailed);
    }

    void aFoundResultNeverPrintsTheLyrics() {
        const GoogleLyricsResult result = found(testPage(testCard({testParagraph({u"비밀 문장 1"_s, u"비밀 문장 2"_s, u"비밀 문장 3"_s})})),
                                                fakes::cardResult({{u"비밀 문장 1"_s, u"비밀 문장 2"_s, u"비밀 문장 3"_s}}));
        QCOMPARE(result.kind, Kind::Found);

        QVERIFY(!result.toString().contains(u"비밀"_s));
    }

    // Added (Q3 brief): the card answers of google_fakes.h are what the script really answers for the pages Kotlin's builders
    // made for them (GoogleTestSupport.kt: loadedCard, loadedWithoutCard, loadedBrokenCard, loadedSorry), and the model of
    // the differential tool (scriptAnswer over Oracle.java's gfPage: HTML-escaped line text in testPage(testCard(...))) holds
    // for text with markup characters, white space, invisible characters and a pair of surrogates.
    void theScriptGivesWhatTheFakesAssume() {
        QCOMPARE(scriptAnswerFor(testPage(testCard({testParagraph({u"더미 하나"_s, u"더미 둘"_s, u"더미 셋"_s})}))),
                 fakes::loadedCard().card);
        QCOMPARE(scriptAnswerFor(testPage(QString())), fakes::loadedWithoutCard().card);
        QCOMPARE(scriptAnswerFor(testPage(u"<div data-lyricid=\"id-1\"></div>"_s)), fakes::loadedBrokenCard().card);
        QCOMPARE(scriptAnswerFor(u"<html><body></body></html>"_s), fakes::loadedSorry().card);

        // Oracle.java's htmlEscape, and its page of two paragraphs.
        auto escape = [](QString text) {
            return text.replace(u'&', u"&amp;"_s).replace(u'<', u"&lt;"_s).replace(u'>', u"&gt;"_s).replace(u'"', u"&quot;"_s);
        };
        const QString nbsp(QChar(0x00A0)), ideographicSpace(QChar(0x3000)), enSpace(QChar(0x2002));
        const QString zeroWidthSpace(QChar(0x200B)), softHyphen(QChar(0x00AD)), tab(QChar(0x09)), lineFeed(QChar(0x0A));
        const QStringList first{u"a & b <c> \"d\""_s, u"  탭"_s + tab + u"사이  "_s, nbsp + u"앞뒤"_s + nbsp,
                                lineFeed + u"앞 줄바꿈"_s, u"가"_s + lineFeed + u"나"_s, ideographicSpace + u"넓은 공백"_s + enSpace};
        const QStringList second{u"보이지"_s + zeroWidthSpace + u"않음"_s + softHyphen, QString::fromUcs4(U"\U0001F600") + u" 웃음"_s,
                                 u"\\ 역슬래시 /"_s};
        QStringList escapedFirst, escapedSecond;
        for (const QString& line : first) escapedFirst.append(escape(line));
        for (const QString& line : second) escapedSecond.append(escape(line));
        const QString answer = scriptAnswerFor(testPage(testCard({testParagraph(escapedFirst), testParagraph(escapedSecond)})));
        QCOMPARE(answer, fakes::cardResult({first, second}));
    }

    // Added (Q3 brief): the text of a line is the text of its text nodes in document order, nested markup included; what
    // <script> and <style> hold and comments add nothing, and a <br> inside a line is a line break in the answer (parseCard
    // makes it one space, as Jsoup's Element.text() did).
    void aLineWithScriptAndNestedMarkupGivesOnlyItsText() {
        const QString html = testPage(testCard({u"<div jsname=\"U8S5sf\">"_s
                                                + testLine(u"더미 <i>기울임 <b>굵게</b></i> 끝<script>var hidden = '숨김';</script>"
                                                           u"<style>.x { color: red; }</style><!-- 주석 -->"_s)
                                                + u"<br>"_s + testLine(u"위<br>아래"_s) + u"<br>"_s + testLine(u"<b><i>셋째</i></b>"_s)
                                                + u"</div>"_s}));

        const QString answer = scriptAnswerFor(html);

        QCOMPARE(answer, fakes::cardResult({{u"더미 기울임 굵게 끝"_s, u"위\n아래"_s, u"셋째"_s}}));
        const GoogleLyricsResult result = parseCard(answer);
        QCOMPARE(result.kind, Kind::Found);
        QCOMPARE(result.lyrics, u"더미 기울임 굵게 끝\n위 아래\n셋째"_s);
        QCOMPARE(result.lineCount, 3);
        QCOMPARE(result.paragraphCount, 1);
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    LyricsCardParserTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_lyrics_card_parser.moc"
