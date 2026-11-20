#include <QtTest>

#include <optional>

#include "paths/paths.h"

using namespace Qt::StringLiterals;

// Every expected value below is what the differential oracle answered on this PC's JVM (kind W, Q2b Task 1 report: the
// observation table): Path.of(text).toString(), Path.resolve, Path.getFileName, kotlin.io.path nameWithoutExtension and
// Path.resolveSibling. ERR is Path.of throwing InvalidPathException.
namespace {

QString units(std::initializer_list<char16_t> list) {
    QString out;
    for (const char16_t unit : list) out.append(QChar(unit));
    return out;
}

const QString none = u"<none>"_s;   // an expected std::nullopt

QString shown(const std::optional<QString>& value) {
    return value ? *value : none;
}

}  // namespace

class PathsTest : public QObject {
    Q_OBJECT

private slots:
    void fromTextGivesKotlinsString_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        int n = 0;
        const auto row = [&n](const QString& text, const QString& expected) {
            QTest::addRow("%d %s", ++n, qPrintable(text.left(40))) << text << expected;
        };
        // drive paths: `/` and doubled or mixed separators become one `\`; a trailing separator goes
        row(u"C:\\Music\\a.m4a"_s, u"C:\\Music\\a.m4a"_s);
        row(u"C:/Music/a.m4a"_s, u"C:\\Music\\a.m4a"_s);
        row(u"C:\\\\Music\\\\\\a.m4a"_s, u"C:\\Music\\a.m4a"_s);
        row(u"C://Music//a"_s, u"C:\\Music\\a"_s);
        row(u"C:\\Music/sub\\\\x"_s, u"C:\\Music\\sub\\x"_s);
        row(u"C:\\Music\\"_s, u"C:\\Music"_s);
        row(u"C:\\Music\\\\"_s, u"C:\\Music"_s);
        row(u"C:/Music/"_s, u"C:\\Music"_s);
        row(u"c:\\music"_s, u"c:\\music"_s);
        row(u"c:/"_s, u"c:\\"_s);
        // roots
        row(u"C:\\"_s, u"C:\\"_s);
        row(u"C:/"_s, u"C:\\"_s);
        row(u"C:\\\\"_s, u"C:\\"_s);
        row(u"C:"_s, u"C:"_s);
        row(u"C:a"_s, u"C:a"_s);
        row(u"C:a\\b\\"_s, u"C:a\\b"_s);
        row(u"C:..\\x"_s, u"C:..\\x"_s);
        row(u"Z:x"_s, u"Z:x"_s);
        row(u"a:b"_s, u"a:b"_s);
        row(u"a:/b"_s, u"a:\\b"_s);
        row(u"\\"_s, u"\\"_s);
        row(u"/"_s, u"\\"_s);
        // relative paths, `.` and `..` (no clean-up)
        row(u"a"_s, u"a"_s);
        row(u"C"_s, u"C"_s);
        row(u"a\\b"_s, u"a\\b"_s);
        row(u"a/b/"_s, u"a\\b"_s);
        row(u"\\a"_s, u"\\a"_s);
        row(u"/a"_s, u"\\a"_s);
        row(u"."_s, u"."_s);
        row(u".."_s, u".."_s);
        row(u"..."_s, u"..."_s);
        row(u"./"_s, u"."_s);
        row(u".\\a"_s, u".\\a"_s);
        row(u"a\\..\\b"_s, u"a\\..\\b"_s);
        row(u"C:\\a\\.\\..\\b"_s, u"C:\\a\\.\\..\\b"_s);
        row(u"C:\\a/."_s, u"C:\\a\\."_s);
        // UNC: the root keeps its separator; two or more separators in front
        row(u"\\\\host\\share"_s, u"\\\\host\\share\\"_s);
        row(u"\\\\host\\share\\"_s, u"\\\\host\\share\\"_s);
        row(u"\\\\host\\share\\dir\\f.txt"_s, u"\\\\host\\share\\dir\\f.txt"_s);
        row(u"//host/share/dir"_s, u"\\\\host\\share\\dir"_s);
        row(u"\\\\host\\\\share"_s, u"\\\\host\\share\\"_s);
        row(u"\\\\\\host\\share"_s, u"\\\\host\\share\\"_s);
        row(u"/\\h\\s"_s, u"\\\\h\\s\\"_s);
        row(u"\\\\h\\s\\\\\\x\\\\"_s, u"\\\\h\\s\\x"_s);
        row(u"\\\\host\\share\\."_s, u"\\\\host\\share\\."_s);
        row(u"\\\\host\\share\\.."_s, u"\\\\host\\share\\.."_s);
        row(u"\\\\.\\pipe\\x"_s, u"\\\\.\\pipe\\x"_s);
        row(u"\\\\.\\pipe"_s, u"\\\\.\\pipe\\"_s);
        row(u"\\\\..\\x"_s, u"\\\\..\\x\\"_s);
        row(u"\\\\h.\\s"_s, u"\\\\h.\\s\\"_s);
        row(u"\\\\호스트\\공유\\a"_s, u"\\\\호스트\\공유\\a"_s);
        // the verbatim prefix `\\?\` is dropped
        row(u"\\\\?\\C:\\x"_s, u"C:\\x"_s);
        row(u"\\\\?\\c:\\x"_s, u"c:\\x"_s);
        row(u"\\\\?\\C:\\"_s, u"C:\\"_s);
        row(u"\\\\?\\C:/x"_s, u"C:\\x"_s);
        row(u"\\\\?\\C:\\x/y\\\\z\\"_s, u"C:\\x\\y\\z"_s);
        row(u"\\\\?\\C:\\a\\.."_s, u"C:\\a\\.."_s);
        row(u"\\\\?\\UNC\\host\\share"_s, u"\\\\host\\share\\"_s);
        row(u"\\\\?\\UNC\\h\\s\\x"_s, u"\\\\h\\s\\x"_s);
        row(u"\\\\?\\UNC\\\\h\\s"_s, u"\\\\h\\s\\"_s);
        // trailing spaces and dots stay
        row(u"C:\\a "_s, u"C:\\a "_s);
        row(u"C:\\a."_s, u"C:\\a."_s);
        row(u"C:\\a. "_s, u"C:\\a. "_s);
        row(u"C:\\a ."_s, u"C:\\a ."_s);
        row(u"C:\\a /"_s, u"C:\\a "_s);
        row(u"C:\\a\\b / /"_s, u"C:\\a\\b \\ "_s);
        row(u"C:\\ "_s, u"C:\\ "_s);
        row(u"C: "_s, u"C: "_s);
        row(u". "_s, u". "_s);
        row(u" \\a"_s, u" \\a"_s);
        row(u"\\\\h\\s "_s, u"\\\\h\\s \\"_s);
        row(u"\\\\?\\C:\\a "_s, u"C:\\a "_s);
        // empty, blank, other white space, U+007F
        row(QString(), QString());
        row(u" "_s, u" "_s);
        row(u"   "_s, u"   "_s);
        row(u"\u3000"_s, u"\u3000"_s);
        row(u"C:\\a\u00A0"_s, u"C:\\a\u00A0"_s);
        row(u"C:\\a\uFEFF"_s, u"C:\\a\uFEFF"_s);
        row(u"\u0085"_s, u"\u0085"_s);
        row(u"C:\\a\u007Fb"_s, u"C:\\a\u007Fb"_s);
        // long
        row(u"C:\\"_s + QString(300, u'a'), u"C:\\"_s + QString(300, u'a'));
        row(u"C:\\"_s + u"abcdefgh\\"_s.repeated(40), u"C:\\"_s + u"abcdefgh\\"_s.repeated(40).chopped(1));
        row(QString(5000, u'x'), QString(5000, u'x'));
        // Korean, emoji, unpaired surrogates
        row(u"C:\\노래\\가수 - 제목.m4a"_s, u"C:\\노래\\가수 - 제목.m4a"_s);
        row(u"C:\\😀\\🎵.m4a"_s, u"C:\\😀\\🎵.m4a"_s);
        row(u"C:\\a"_s + units({0xD83D}) + u"b"_s, u"C:\\a"_s + units({0xD83D}) + u"b"_s);
        row(u"C:\\a"_s + units({0xDE00}), u"C:\\a"_s + units({0xDE00}));
        row(units({0xDBFF}), units({0xDBFF}));
        row(u"C:\\"_s + units({0xD83D}) + u"\\x"_s, u"C:\\"_s + units({0xD83D}) + u"\\x"_s);
    }
    void fromTextGivesKotlinsString() {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(shown(xgs::paths::fromText(text)), expected);
    }

    void fromTextRejectsWhatPathOfRejects_data() {
        QTest::addColumn<QString>("text");
        int n = 0;
        const auto row = [&n](const QString& text) { QTest::addRow("%d %s", ++n, qPrintable(text.left(40))) << text; };
        // a drive is an ASCII letter
        row(u"1:\\x"_s);
        row(u"가:\\x"_s);
        row(u"Ｃ:\\x"_s);
        row(u"ı:\\x"_s);
        // incomplete UNC roots and device paths with a `:`
        row(u"\\\\"_s);
        row(u"//"_s);
        row(u"\\\\host"_s);
        row(u"\\\\host\\"_s);
        row(u"\\\\\\h"_s);
        row(u"\\\\.\\C:\\x"_s);
        row(u"\\\\.\\C:"_s);
        row(u"\\\\.\\"_s);
        // verbatim prefixes that are not followed by a drive and a separator, or by `UNC\` and a whole UNC root
        row(u"\\\\?\\"_s);
        row(u"\\\\?\\C:"_s);
        row(u"\\\\?\\C: "_s);
        row(u"\\\\?\\C:x"_s);
        row(u"\\\\?\\1:\\x"_s);
        row(u"\\\\?\\x\\y"_s);
        row(u"\\\\?\\UNC\\"_s);
        row(u"\\\\?\\UNC\\h"_s);
        row(u"\\\\?\\UNC\\h\\"_s);
        row(u"\\\\?\\unc\\h\\s"_s);
        row(u"\\\\?\\UNC/h/s"_s);
        row(u"\\\\?\\\\C:\\x"_s);
        row(u"\\\\?/C:\\x"_s);
        row(u"//?/C:/x"_s);
        row(u"/\\?\\C:\\x"_s);
        row(u"\\\\?\\ C:\\x"_s);
        row(u"\\\\?x\\y"_s);
        row(u"\\\\?\\C:\\a?b"_s);
        row(u"\\\\?\\C:\\x:y"_s);
        // forbidden characters
        row(u"C:\\a*b"_s);
        row(u"C:\\a?b"_s);
        row(u"C:\\a\"b"_s);
        row(u"C:\\a<b"_s);
        row(u"C:\\a>b"_s);
        row(u"C:\\a|b"_s);
        row(u"C:\\a\\b*"_s);
        row(u"C:\\a\\b<"_s);
        row(u"C:\\a"_s + QChar(u'\0') + u"b"_s);
        row(u"C:\\a\u0001b"_s);
        row(u"C:\\a\u001Fb"_s);
        row(u"C:\\a\tb"_s);
        row(u"\t"_s);
        row(u"\u001F"_s);
        // a `:` that is not a drive's
        row(u"C:\\a:b"_s);
        row(u"C:\\a\\b:c"_s);
        row(u"C:\\:"_s);
        row(u"C::"_s);
        row(u"::"_s);
        row(u":"_s);
        row(u":a"_s);
        row(u" :"_s);
        row(u"ab:c"_s);
        row(u"a:b:c"_s);
        row(u"C:a:b"_s);
        row(u" C:\\a"_s);
        row(u" C:"_s);
        row(u"\uFEFFC:\\x"_s);
        row(u"\\\\host:\\x"_s);
        row(u"\\\\h:x\\s"_s);
        row(u"\\\\h\\s:x"_s);
        row(u"\\\\h\\s\\a:b"_s);
        row(u"\\\\ho*st\\s"_s);
        row(u"\\\\h\\s\\a*"_s);
        // quoted
        row(u"\"C:\\Music\""_s);
        row(u"\"a\""_s);
        row(u"'C:\\a'"_s);
    }
    void fromTextRejectsWhatPathOfRejects() {
        QFETCH(QString, text);
        QCOMPARE(shown(xgs::paths::fromText(text)), none);
    }

    void childResolvesLikeKotlin_data() {
        QTest::addColumn<QString>("dir");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected");
        const auto row = [](const QString& dir, const QString& name, const QString& expected) {
            QTest::newRow(qPrintable(dir + u" + "_s + name)) << dir << name << expected;
        };
        row(u"C:\\a"_s, u"b"_s, u"C:\\a\\b"_s);
        row(u"C:\\"_s, u"b"_s, u"C:\\b"_s);
        row(u"C:"_s, u"b"_s, u"C:b"_s);
        row(u"\\"_s, u"b"_s, u"\\b"_s);
        row(QString(), u"b"_s, u"b"_s);
        row(u"a"_s, u"b"_s, u"a\\b"_s);
        row(u"C:\\a"_s, QString(), u"C:\\a"_s);
        row(u"C:"_s, QString(), u"C:"_s);
        row(u"C:\\"_s, QString(), u"C:\\"_s);
        row(QString(), QString(), QString());
        row(u"C:\\a"_s, u"b\\"_s, u"C:\\a\\b"_s);
        row(u"C:\\a"_s, u"b/c"_s, u"C:\\a\\b\\c"_s);
        row(u"C:\\a"_s, u".."_s, u"C:\\a\\.."_s);
        row(u"C:\\a"_s, u"."_s, u"C:\\a\\."_s);
        row(u"C:\\a"_s, u"..\\..\\b"_s, u"C:\\a\\..\\..\\b"_s);
        row(u"C:\\a"_s, u"b "_s, u"C:\\a\\b "_s);
        row(u"C:\\a\\b"_s, u" "_s, u"C:\\a\\b\\ "_s);
        row(u"\\\\h\\s\\"_s, u"x"_s, u"\\\\h\\s\\x"_s);
        row(u"\\\\h\\s\\"_s, QString(), u"\\\\h\\s\\"_s);
        // an absolute name replaces the directory
        row(u"C:\\a"_s, u"D:\\x"_s, u"D:\\x"_s);
        row(u"C:\\a"_s, u"c:\\x"_s, u"c:\\x"_s);
        row(u"C:\\a"_s, u"C:\\"_s, u"C:\\"_s);
        row(u"C:a"_s, u"D:\\x"_s, u"D:\\x"_s);
        row(QString(), u"C:\\x"_s, u"C:\\x"_s);
        row(u"C:\\a"_s, u"\\\\h\\s"_s, u"\\\\h\\s\\"_s);
        row(u"\\\\h\\s\\x"_s, u"\\\\g\\t\\y"_s, u"\\\\g\\t\\y"_s);
        row(u"C:\\a"_s, u"\\\\?\\C:\\x"_s, u"C:\\x"_s);
        // a name with the root `\` keeps the directory's drive or UNC root
        row(u"C:\\a"_s, u"\\x"_s, u"C:\\x"_s);
        row(u"C:a"_s, u"\\x"_s, u"C:\\x"_s);
        row(u"C:"_s, u"\\x"_s, u"C:\\x"_s);
        row(u"C:\\a"_s, u"/"_s, u"C:\\"_s);
        row(u"C:a"_s, u"\\"_s, u"C:\\"_s);
        row(u"C:\\a"_s, u"\\ "_s, u"C:\\ "_s);
        row(u"\\\\h\\s\\x"_s, u"\\y"_s, u"\\\\h\\s\\y"_s);
        row(u"\\\\h\\s\\"_s, u"\\"_s, u"\\\\h\\s\\"_s);
        row(u"a"_s, u"\\x"_s, u"\\x"_s);
        row(u"\\a"_s, u"\\x"_s, u"\\x"_s);
        row(u"\\"_s, u"\\"_s, u"\\"_s);
        row(QString(), u"\\"_s, u"\\"_s);
        // a drive-relative name continues an absolute directory on the same drive (any case), else replaces it
        row(u"C:\\a"_s, u"C:x"_s, u"C:\\a\\x"_s);
        row(u"C:\\a"_s, u"c:x"_s, u"C:\\a\\x"_s);
        row(u"C:\\"_s, u"c:x"_s, u"C:\\x"_s);
        row(u"C:\\a"_s, u"C:"_s, u"C:\\a\\"_s);
        row(u"C:\\a"_s, u"C: "_s, u"C:\\a\\ "_s);
        row(u"C:\\a"_s, u"D:x"_s, u"D:x"_s);
        row(u"C:a"_s, u"C:x"_s, u"C:x"_s);
        row(u"C:a"_s, u"c:x"_s, u"c:x"_s);
        row(u"C:"_s, u"C:x"_s, u"C:x"_s);
        row(u"C:"_s, u"D:"_s, u"D:"_s);
        row(u"\\a"_s, u"C:x"_s, u"C:x"_s);
        row(u"a"_s, u"C:x"_s, u"C:x"_s);
        row(QString(), u"C:x"_s, u"C:x"_s);
        row(u"\\\\h\\s\\x"_s, u"C:x"_s, u"C:x"_s);
    }
    void childResolvesLikeKotlin() {
        QFETCH(QString, dir);
        QFETCH(QString, name);
        QFETCH(QString, expected);
        QCOMPARE(shown(xgs::paths::child(dir, name)), expected);
    }

    void childRejectsWhatPathOfRejects_data() {
        QTest::addColumn<QString>("dir");
        QTest::addColumn<QString>("name");
        QTest::newRow("forbidden in the name") << u"C:\\a"_s << u"b*c"_s;
        QTest::newRow("bare UNC prefix") << u"C:\\a"_s << u"\\\\"_s;
        QTest::newRow("bare UNC prefix after UNC") << u"\\\\h\\s\\x"_s << u"\\\\"_s;
        QTest::newRow("UNC host only") << u"C:\\a"_s << u"\\\\h"_s;
        QTest::newRow("UNC without a share") << u"C:\\a"_s << u"//b//"_s;
        QTest::newRow("colon") << u"C:\\a"_s << u":"_s;
        QTest::newRow("space before a drive") << u"C:\\a\\b"_s << u" C:\\x"_s;
        QTest::newRow("forbidden in the directory") << u"C:\\a*"_s << u"b"_s;
    }
    void childRejectsWhatPathOfRejects() {
        QFETCH(QString, dir);
        QFETCH(QString, name);
        QCOMPARE(shown(xgs::paths::child(dir, name)), none);
    }

    void fileNameIsTheLastPart_data() {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("expected");
        QTest::newRow("file") << u"C:\\a\\b.m4a"_s << u"b.m4a"_s;
        QTest::newRow("drive root") << u"C:\\"_s << none;
        QTest::newRow("drive") << u"C:"_s << none;
        QTest::newRow("root") << u"\\"_s << none;
        QTest::newRow("UNC root") << u"\\\\h\\s\\"_s << none;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("UNC file") << u"\\\\h\\s\\x"_s << u"x"_s;
        QTest::newRow("relative") << u"a"_s << u"a"_s;
        QTest::newRow("drive-relative") << u"C:a"_s << u"a"_s;
        QTest::newRow("dot dot") << u"C:\\a\\.."_s << u".."_s;
        QTest::newRow("dot") << u"."_s << u"."_s;
        QTest::newRow("trailing space") << u"C:\\a "_s << u"a "_s;
        QTest::newRow("two dots") << u"C:\\x.y"_s << u"x.y"_s;
    }
    void fileNameIsTheLastPart() {
        QFETCH(QString, path);
        QFETCH(QString, expected);
        QCOMPARE(shown(xgs::paths::fileName(path)), expected);
    }

    void nameWithoutExtensionDropsFromTheLastDot_data() {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("expected");
        QTest::newRow("a.b.c") << u"a.b.c"_s << u"a.b"_s;
        QTest::newRow(".m4a") << u".m4a"_s << QString();
        QTest::newRow("abc.") << u"abc."_s << u"abc"_s;
        QTest::newRow("abc") << u"abc"_s << u"abc"_s;
        QTest::newRow("drive root") << u"C:\\"_s << QString();
        QTest::newRow("...") << u"..."_s << u".."_s;
        QTest::newRow("a.b.c.m4a") << u"a.b.c.m4a"_s << u"a.b.c"_s;
        QTest::newRow("dot in the directory") << u"C:\\a.b\\c"_s << u"c"_s;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("dot dot") << u"C:\\a\\.."_s << u"."_s;
        QTest::newRow("trailing dot") << u"C:\\a.b."_s << u"a.b"_s;
        QTest::newRow("UNC root") << u"\\\\h\\s\\"_s << QString();
        QTest::newRow("drive") << u"C:"_s << QString();
        QTest::newRow("drive-relative") << u"C:x.y"_s << u"x"_s;
        QTest::newRow("space before the extension") << u"C:\\a.b .c"_s << u"a.b "_s;
    }
    void nameWithoutExtensionDropsFromTheLastDot() {
        QFETCH(QString, path);
        QFETCH(QString, expected);
        QCOMPARE(xgs::paths::nameWithoutExtension(path), expected);
    }

    void siblingResolvesAgainstTheParent_data() {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected");
        const auto row = [](const QString& path, const QString& name, const QString& expected) {
            QTest::newRow(qPrintable(path + u" ~ "_s + name)) << path << name << expected;
        };
        row(u"C:\\a\\b.m4a"_s, u"b.ffmeta"_s, u"C:\\a\\b.ffmeta"_s);
        row(u"C:\\a"_s, u"x"_s, u"C:\\x"_s);
        row(u"C:\\"_s, u"x"_s, u"x"_s);
        row(u"C:"_s, u"x"_s, u"x"_s);
        row(u"a"_s, u"x"_s, u"x"_s);
        row(u"a\\b"_s, u"x"_s, u"a\\x"_s);
        row(u"C:a"_s, u"x"_s, u"C:x"_s);
        row(u"C:a\\b"_s, u"x"_s, u"C:a\\x"_s);
        row(u"\\a"_s, u"x"_s, u"\\x"_s);
        row(u"\\"_s, u"x"_s, u"x"_s);
        row(u"\\\\h\\s\\x"_s, u"y"_s, u"\\\\h\\s\\y"_s);
        row(u"\\\\h\\s\\"_s, u"y"_s, u"y"_s);
        row(u"C:\\a\\b"_s, QString(), u"C:\\a"_s);
        row(u"C:\\a\\b"_s, u"D:\\x"_s, u"D:\\x"_s);
        row(u"C:\\a\\b"_s, u"\\x"_s, u"C:\\x"_s);
        row(u"C:\\a\\b"_s, u"C:x"_s, u"C:\\a\\x"_s);
        row(QString(), u"x"_s, u"x"_s);
        row(u"C:\\a\\b"_s, u"x\\y"_s, u"C:\\a\\x\\y"_s);
        row(u"C:\\a\\b"_s, u".."_s, u"C:\\a\\.."_s);
        row(u"a"_s, QString(), QString());
        row(u"C:\\a"_s, QString(), u"C:\\"_s);
        row(u"C:\\a\\b"_s, u"x*"_s, none);
    }
    void siblingResolvesAgainstTheParent() {
        QFETCH(QString, path);
        QFETCH(QString, name);
        QFETCH(QString, expected);
        QCOMPARE(shown(xgs::paths::sibling(path, name)), expected);
    }
};

QTEST_GUILESS_MAIN(PathsTest)
#include "tst_paths.moc"
