#include <QtTest>

#include "diag/startup_record.h"

using namespace Qt::StringLiterals;
using xgs::diag::startupRecord;
using xgs::diag::StartupInfo;

namespace {

StartupInfo info(std::optional<QString> appVersion = u"1.0.0"_s) {
    StartupInfo i;
    i.appVersion = appVersion;
    i.qtVersion = u"6.11.3"_s;
    i.osName = u"Windows 11"_s;
    i.osVersion = u"10.0"_s;
    i.osArchitecture = u"x86_64"_s;
    i.processors = 8;
    i.headless = false;
    i.workingDir = u"C:\\work"_s;
    i.logDir = u"C:\\logs"_s;
    return i;
}

}  // namespace

// The startup record tests of Kotlin's DiagnosticsTest, with the Java lines replaced by the Qt, OS and processor lines. Not
// ported: theStartupRecordAsksForNothingBeyondTheListedProperties (the record takes plain values; there are no system
// properties to ask for).
class DiagnosticsTest : public QObject {
    Q_OBJECT

private slots:
    void theStartupRecordHasEverythingTheBriefAsksFor() {
        const QString text = startupRecord(info());

        for (const QString& expected : {u"xGetSongs 1.0.0"_s, u"6.11.3"_s, u"Windows 11"_s, u"10.0"_s, u"x86_64"_s}) {
            QVERIFY2(text.contains(expected), qPrintable(expected + u" in "_s + text));
        }
        QVERIFY2(text.contains(u"프로세서 8개"_s), qPrintable(text));
        QVERIFY2(text.contains(u"C:\\work"_s), qPrintable(text));
        QVERIFY2(text.contains(u"C:\\logs"_s), qPrintable(text));
        QVERIFY2(text.contains(u"headless=false"_s), qPrintable(text));
    }

    void theVersionIsLeftOutWhenItIsNotKnown() {
        const QString text = startupRecord(info(std::nullopt));

        QVERIFY2((text + u"\n"_s).contains(u"앱: xGetSongs\n"_s), qPrintable(text));
        QVERIFY2(!text.contains(u"null"_s), qPrintable(text));
    }

    void aMissingPropertyDoesNotBreakTheStartupRecord() {
        StartupInfo i;
        i.processors = 4;
        i.headless = true;
        i.workingDir = u"C:\\w"_s;
        i.logDir = u"C:\\l"_s;

        const QString text = startupRecord(i);

        QVERIFY2(text.contains(u"프로세서 4개"_s), qPrintable(text));
        QVERIFY2(text.contains(u"headless=true"_s), qPrintable(text));
        QVERIFY2(text.contains(u"Qt: ?"_s) && text.contains(u"OS: ? ? ?"_s), qPrintable(text));
    }

    void theHeadlessLineIsLeftOutWhenNobodyAskedAnAwtForIt() {
        StartupInfo i;
        i.processors = 4;
        i.workingDir = u"C:\\w"_s;
        i.logDir = u"C:\\l"_s;

        const QString text = startupRecord(i);

        QVERIFY2(!text.contains(u"headless"_s), qPrintable(text));
        QVERIFY2(text.endsWith(u"로그 폴더: C:\\l"_s), qPrintable(text));
    }

    // ---- added: the lines the Qt version has instead of Java's --------------------------------

    void theLinesAreTheKotlinOnesWithQtOsAndProcessorLinesInsteadOfJavaAndHeap() {
        const QString text = startupRecord(info());

        QCOMPARE(text, u"시작 정보\n  앱: xGetSongs 1.0.0\n  Qt: 6.11.3\n  OS: Windows 11 10.0 x86_64\n  프로세서 8개\n  작업 폴더: C:\\work\n"
                       u"  로그 폴더: C:\\logs\n  headless=false"_s);
        QVERIFY(!text.contains(u"Java"_s));
        QVERIFY(!text.contains(u"힙"_s));
    }
};

QTEST_GUILESS_MAIN(DiagnosticsTest)
#include "tst_startup_record.moc"
