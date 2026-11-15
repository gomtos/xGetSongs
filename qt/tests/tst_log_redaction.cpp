#include <QtTest>

#include <QElapsedTimer>

#include <optional>

#include "log/log_redaction.h"

using namespace Qt::StringLiterals;
using xgs::redaction::MaxTextLength;

namespace {

QString paths(const QString& text) {
    return xgs::redaction::redactPaths(text);
}

QString redact(const QString& text, const QStringList& knownNames) {
    return xgs::redaction::redact(text, knownNames);
}

// Kotlin's assertEquals(expected, actual, message): the failure message, or nullopt when they are equal.
std::optional<QString> checkEquals(const QString& expected, const QString& actual, const QString& message = QString()) {
    if (expected == actual) return std::nullopt;
    return (message.isEmpty() ? QString() : message + u"\n   "_s) + u"expected: \""_s + expected + u"\"\n   actual:   \""_s + actual +
           u"\""_s;
}

}  // namespace

#define ASSERT_EQ_MSG(expected, actual, message)                                              \
    do {                                                                                      \
        const std::optional<QString> failure_ = checkEquals((expected), (actual), (message)); \
        QVERIFY2(!failure_, failure_ ? qPrintable(*failure_) : "");                           \
    } while (false)
#define ASSERT_EQ(expected, actual) ASSERT_EQ_MSG(expected, actual, QString())

class LogRedactionTest : public QObject {
    Q_OBJECT

private slots:
    void aPathWithSpacesInItsFolderNamesIsReplacedAsAWhole() {
        const QString text =
            u"NoSuchFileException: C:\\Users\\Some User\\AppData\\Roaming\\xGetSongs\\work\\job-1\\001 Artist - Title.f251.webm"_s;

        ASSERT_EQ(u"NoSuchFileException: <경로>"_s, paths(text));
    }

    void twoPathsSeparatedByAnArrowAreReplacedOneByOne() {
        const QString text = u"C:\\work\\job 1\\001 A - B.mp3 -> D:\\Music\\My Playlist\\001 A - B.mp3"_s;

        ASSERT_EQ(u"<경로> -> <경로>"_s, paths(text));
    }

    void theSystemReasonAfterTheLastPathStaysReadable() {
        const QString text = u"FileSystemException: C:\\work\\job-1\\001 A - B.mp3 -> D:\\Music\\My Playlist\\001 A - B.mp3: "_s +
                             u"The process cannot access the file because it is being used by another process"_s;

        ASSERT_EQ(
            u"FileSystemException: <경로> -> <경로>: The process cannot access the file because it is being used by another process"_s,
            paths(text));
    }

    void aSingleFilePathFollowedByAReasonKeepsTheReason() {
        ASSERT_EQ(u"<경로>: Access is denied"_s, paths(u"D:\\Music\\a.mp3: Access is denied"_s));
    }

    void aUncPathIsReplacedWithItsHostAndShare() {
        ASSERT_EQ(u"<경로>: Access is denied"_s, paths(u"\\\\NAS\\Music Share\\Playlists\\001 A - B.mp3: Access is denied"_s));
        ASSERT_EQ(u"open <경로>:failed"_s, paths(u"open \\\\server\\share\\dir\\f.mp3:failed"_s));
    }

    void aLongPathPrefixIsPartOfThePath() {
        ASSERT_EQ(u"error: <경로>"_s, paths(u"error: \\\\?\\C:\\very\\long\\path\\file.mp3"_s));
    }

    void aPathWithDoubledBackslashesLikeAPythonErrorIsReplaced() {
        const QString text = u"ERROR: unable to open for writing: [Errno 13] Permission denied: 'C:\\\\Users\\\\Some User\\\\Music\\\\a.mp3'"_s;

        const QString redacted = paths(text);

        QVERIFY2(!redacted.contains(u"Users"_s), qPrintable(redacted));
        QVERIFY2(!redacted.contains(u"Music"_s), qPrintable(redacted));
        QVERIFY2(redacted.startsWith(u"ERROR: unable to open for writing: [Errno 13] Permission denied: '<경로>"_s), qPrintable(redacted));
    }

    void aPathEndsAtTheEndOfTheLine() {
        ASSERT_EQ(u"first <경로>\nnext line stays\n<경로>\r\ndone"_s, paths(u"first C:\\a\\b\nnext line stays\nD:\\c\\d\r\ndone"_s));
    }

    void everyPathInSeveralLinesIsReplaced() {
        ASSERT_EQ(u"<경로>\n<경로>\n<경로>"_s, paths(u"C:\\a\\1.mp3\nD:\\b\\2.mp3\n\\\\h\\s\\3.mp3"_s));
    }

    void textWithoutPathsIsUnchanged() {
        const QStringList texts = {
            u""_s,
            u"연결이 끊어졌습니다 (HTTP 429)"_s,
            u"HTTP 403: Forbidden"_s,
            u"https://www.youtube.com/watch?v=abc&list=PL1"_s,
            u"ERROR: [youtube] abcdefghijk: Video unavailable"_s,
            u"at 10:30 the job (a:b) ended"_s,
            u"C:"_s,
            u"C:\\"_s,
            u"a\\b"_s,
            u"5 / 3 | 2"_s,
            u"-> arrow ->"_s,
        };
        for (const QString& text : texts) ASSERT_EQ_MSG(text, paths(text), text);
    }

    void aKnownNameIsReplacedIgnoringCase() {
        const QString redacted = redact(u"Cannot write 001 a - b.MP3 now"_s, {u"001 A - B.mp3"_s});

        ASSERT_EQ(u"Cannot write <파일명> now"_s, redacted);
    }

    void theNameWithoutItsExtensionIsReplacedToo() {
        ASSERT_EQ(u"<파일명> and <파일명>"_s, redact(u"001 A - B.mp3 and 001 a - b"_s, {u"001 A - B.mp3"_s}));
    }

    void aKnownNameInsideAPathGoesWithThePath() {
        const QString redacted = redact(u"NoSuchFileException: C:\\Users\\me\\Music\\001 A - B.mp3"_s, {u"001 A - B.mp3"_s});

        ASSERT_EQ(u"NoSuchFileException: <경로>"_s, redacted);
    }

    void severalKnownNamesAndRepeatedOccurrencesAreAllReplaced() {
        const QString redacted = redact(u"x 001 A - B.mp3, 002 C - D.mp3, 001 A - B.mp3"_s, {u"001 A - B.mp3"_s, u"002 C - D.mp3"_s});

        ASSERT_EQ(u"x <파일명>, <파일명>, <파일명>"_s, redacted);
    }

    void blankAndVeryShortNamesAreIgnored() {
        ASSERT_EQ(u"a b c"_s, redact(u"a b c"_s, {u""_s, u"  "_s, u"a"_s, u"ab"_s}));
    }

    void noKnownNamesJustRedactsThePaths() {
        ASSERT_EQ(u"<경로>: reason"_s, redact(u"C:\\a\\b.mp3: reason"_s, {}));
    }

    // ---- two paths in a row, forward slashes, long prefixes, URIs ----

    void twoPathsSeparatedByACommaAreBothRedactedCompletely() {
        ASSERT_EQ(u"<경로>, <경로>"_s, paths(u"C:\\a\\b.mp3, D:\\Music\\My Playlist\\x.mp3"_s));
        ASSERT_EQ(u"<경로>,<경로>"_s, paths(u"C:\\a\\b.mp3,D:\\Music\\My Playlist\\x.mp3"_s));
        ASSERT_EQ(u"<경로>; <경로>: Access is denied"_s, paths(u"C:\\a\\b.mp3; D:\\x\\y z.mp3: Access is denied"_s));
    }

    void aPathThatStartsRightAfterTheEndOfAnotherIsStillAPath() {
        ASSERT_EQ(u"<경로><경로>"_s, paths(u"C:\\a\\fooD:\\Music\\x.mp3"_s));
    }

    void forwardSlashesRightAfterTheDriveAreAccepted() {
        ASSERT_EQ(u"<경로>"_s, paths(u"C:/Users/me/Song Title.mp3"_s));
        ASSERT_EQ(u"failed: <경로>: Access is denied"_s, paths(u"failed: c:/Users/me/Song Title.mp3: Access is denied"_s));
    }

    void mixedSeparatorsAreOnePath() {
        ASSERT_EQ(u"<경로>"_s, paths(u"D:\\Music/My Playlist/001 A - B.mp3"_s));
        ASSERT_EQ(u"<경로>"_s, paths(u"D:/Music\\My Playlist\\001 A - B.mp3"_s));
    }

    void theVerbatimPrefixesAreHandled() {
        ASSERT_EQ(u"error: <경로>"_s, paths(u"error: \\\\?\\C:\\x\\y z\\a.mp3"_s));
        ASSERT_EQ(u"error: <경로>: Access is denied"_s, paths(u"error: \\\\?\\UNC\\server\\share\\dir\\a.mp3: Access is denied"_s));
        ASSERT_EQ(u"<경로>"_s, paths(u"\\\\?\\unc\\SERVER\\Music Share\\a.mp3"_s));
        ASSERT_EQ(u"<경로>"_s, paths(u"\\\\.\\C:\\x\\a.mp3"_s));
    }

    void aFileUriIsRedactedAsAWhole() {
        ASSERT_EQ(u"cannot read <경로>"_s, paths(u"cannot read file:///C:/Users/me/Music/My%20Song.mp3"_s));
        ASSERT_EQ(u"cannot read <경로>"_s, paths(u"cannot read FILE:///c:/Users/me/a.mp3"_s));
        ASSERT_EQ(u"<경로>: reason"_s, paths(u"file:///D:/Music/My Playlist/a.mp3: reason"_s));
    }

    void aHttpUrlIsNotAPath() {
        const QStringList texts = {
            u"https://www.youtube.com/watch?v=abc&list=PL1"_s,
            u"see http://example.com/a/b and https://example.com:8080/x"_s,
            u"ftp://host/dir"_s,
        };
        for (const QString& text : texts) ASSERT_EQ_MSG(text, paths(text), text);
    }

    void relativePathsAreNotRecognisedOnPurpose() {
        // Without a drive or a UNC host there is nothing to tell a path from a sentence: these stay as they are.
        const QStringList texts = {u"Music\\a.mp3"_s, u".\\a\\b.mp3"_s, u"..\\x\\y.mp3"_s, u"a/b/c.mp3"_s, u"/usr/lib/x"_s,
                                   u"\\just\\absolute\\on\\this\\drive.mp3"_s};
        for (const QString& text : texts) ASSERT_EQ_MSG(text, paths(text), text);
    }

    // ---- hostile input ----

    void aPathOfFiveThousandPartsIsRedactedWithoutAnError() {
        const QString text = u"C:"_s + u"\\a"_s.repeated(5'000);

        const QString redacted = paths(text);

        ASSERT_EQ(u"<경로>"_s, redacted);
    }

    void aHugeHostileInputIsCutAndRedactedQuickly() {
        const QStringList shapes = {
            u"C:"_s + u"\\a"_s.repeated(100'000),
            u"C:\\"_s.repeated(66'000),
            u"\\\\"_s.repeated(100'000),
            u"a:\\ "_s.repeated(50'000),
            u"C:\\a -> "_s.repeated(25'000),
            u"x"_s.repeated(200'000),
            u"file:///"_s.repeated(25'000),
            u"\\\\?\\UNC\\"_s.repeated(25'000),
        };
        QElapsedTimer timer;
        timer.start();

        for (const QString& text : shapes) {
            const QString redacted = paths(text);
            // The input is cut; the output can be a little longer (a one-character path becomes a placeholder and its space stays).
            QVERIFY2(redacted.size() <= 2 * MaxTextLength, qPrintable(u"cut to the limit: %1"_s.arg(redacted.size())));
        }

        QVERIFY2(timer.elapsed() / 1000 < 10, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
    }

    void aTextOverTheLimitIsCutAndEndsWithAnEllipsis() {
        const QString text = u"x"_s.repeated(10'000);

        const QString redacted = paths(text);

        ASSERT_EQ(u"x"_s.repeated(MaxTextLength) + u"…"_s, redacted);
        ASSERT_EQ_MSG(u"x"_s.repeated(MaxTextLength), paths(u"x"_s.repeated(MaxTextLength)), u"exactly the limit is not cut"_s);
    }

    void theCutNeverSplitsASurrogatePair() {
        const QString text = u"x"_s.repeated(MaxTextLength - 1) + u"\U0001F3B5"_s + u"tail"_s;   // the pair starts at the last kept index

        const QString redacted = paths(text);

        ASSERT_EQ(u"x"_s.repeated(MaxTextLength - 1) + u"…"_s, redacted);
    }

    void whatIsCutOffIsNotKeptEvenWhenItWasAPath() {
        const QString text = u"x"_s.repeated(MaxTextLength) + u" C:\\Users\\secret\\a.mp3"_s;

        QVERIFY(!paths(text).contains(u"secret"_s));
    }

    // ---- known names with regular expression characters ----

    void aKnownNameWithRegexCharactersIsReplacedLiterally() {
        const QString name = u"A (feat. B) [x]$1.mp3"_s;

        const QString redacted = redact(u"could not write A (feat. B) [x]$1.mp3 and a (FEAT. b) [X]$1"_s, {name});

        ASSERT_EQ(u"could not write <파일명> and <파일명>"_s, redacted);
    }

    void aNameThatLooksLikeAPatternDoesNotMatchOtherText() {
        const QString redacted = redact(u"Axxx and A.*"_s, {u"A.*"_s});

        ASSERT_EQ(u"Axxx and <파일명>"_s, redacted);
    }

    void aReplacementPlaceholderIsNotExpandedAsAReference() {
        ASSERT_EQ(u"<경로>"_s, paths(u"C:\\a\\b$1\\c.mp3"_s));
        ASSERT_EQ(u"<파일명>"_s, redact(u"$0$1"_s, {u"$0$1"_s}));
    }

    // ---- added in the port (no Kotlin counterpart): a path of 5000 parts with spaces ----

    void redactPathsWithFiveThousandPartsFinishesQuickly() {
        const QString text = u"C:"_s + u"\\part with space"_s.repeated(5'000);

        QElapsedTimer timer;
        timer.start();
        const QString redacted = paths(text);
        const qint64 elapsed = timer.elapsed();
        qInfo("redactPaths of %lld UTF-16 units took %lld ms", static_cast<long long>(text.size()), static_cast<long long>(elapsed));

        QVERIFY2(elapsed < 2000, qPrintable(u"took %1 ms"_s.arg(elapsed)));
        // Kotlin's answer to the same input (Oracle.java, kind R: "OK 003cacbdb85c003e"): the cut text is one path.
        ASSERT_EQ(u"<경로>"_s, redacted);
    }
};

QTEST_APPLESS_MAIN(LogRedactionTest)
#include "tst_log_redaction.moc"
