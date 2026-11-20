#include <QtTest>

#include "process/command_line.h"

using namespace Qt::StringLiterals;
using xgs::process::windowsCommandLine;

class CommandLineTest : public QObject {
    Q_OBJECT

private slots:
    void quotesOnlyWhatNeedsQuoting() {
        QCOMPARE(windowsCommandLine({u"a"_s}), u"a"_s);
        QCOMPARE(windowsCommandLine({u"a b"_s}), u"\"a b\""_s);
        QCOMPARE(windowsCommandLine({QString()}), u"\"\""_s);
        QCOMPARE(windowsCommandLine({u"C:\\Program Files\\yt-dlp.exe"_s, u"-o"_s, u""_s, u"a\tb"_s}),
                 u"\"C:\\Program Files\\yt-dlp.exe\" -o \"\" \"a\tb\""_s);
        // only cmd.exe gives these a meaning, and it is never involved
        QCOMPARE(windowsCommandLine({u"%PATH%&^!()|<>한글😀"_s}), u"%PATH%&^!()|<>한글😀"_s);
        // backslashes not before a quote are literal, also at the end of an argument that needs no quotes
        QCOMPARE(windowsCommandLine({u"C:\\dir\\"_s, u"a\\\\b"_s}), u"C:\\dir\\ a\\\\b"_s);
        QCOMPARE(windowsCommandLine({}), QString());
    }

    void doublesBackslashesBeforeAQuoteAndAtTheEndOfAQuotedArgument() {
        // a"b -> "a\"b"
        QCOMPARE(windowsCommandLine({u"a\"b"_s}), u"\"a\\\"b\""_s);
        // a\"b (one backslash, then a quote) -> "a\\\"b" (2·1 + 1 backslashes)
        QCOMPARE(windowsCommandLine({u"a\\\"b"_s}), u"\"a\\\\\\\"b\""_s);
        // a\\"b -> "a\\\\\"b" (2·2 + 1)
        QCOMPARE(windowsCommandLine({u"a\\\\\"b"_s}), u"\"a\\\\\\\\\\\"b\""_s);
        // a b\ -> "a b\\" (the backslash before the closing quote doubled)
        QCOMPARE(windowsCommandLine({u"a b\\"_s}), u"\"a b\\\\\""_s);
        // a b\\\ -> "a b\\\\\\"
        QCOMPARE(windowsCommandLine({u"a b\\\\\\"_s}), u"\"a b\\\\\\\\\\\\\""_s);
        // backslashes inside a quoted argument but not before a quote stay single
        QCOMPARE(windowsCommandLine({u"C:\\My Music\\a"_s}), u"\"C:\\My Music\\a\""_s);
        // a lone quote, and a quote at the end
        QCOMPARE(windowsCommandLine({u"\""_s, u"x\\\""_s}), u"\"\\\"\" \"x\\\\\\\"\""_s);
    }
};

QTEST_APPLESS_MAIN(CommandLineTest)
#include "tst_command_line.moc"
