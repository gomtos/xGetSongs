#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "diag/log_directory.h"

using namespace Qt::StringLiterals;
using xgs::diag::chooseLogDirectory;
using xgs::diag::ensureLogDirectory;
using xgs::diag::logDirectoryCandidates;

namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

}  // namespace

// The log folder tests of Kotlin's DiagnosticsTest. The ones about the folder next to the application and about the Java
// application directory are not ported (the Qt app has no such candidate); the rest of that class is in tst_startup_record and
// tst_run_record.
class DiagnosticsTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString m_base;
    int m_counter = 0;

    QString path(const QString& relative) const { return m_base + u'/' + relative; }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void init() {
        m_base = m_dir.filePath(u"t%1"_s.arg(++m_counter));  // every test has a folder of its own
        QVERIFY(QDir().mkpath(m_base));
    }

    void theLogFolderIsCreatedWithItsParents() {
        const QString folder = path(u"a/b/logs"_s);

        QVERIFY(ensureLogDirectory(folder));

        QVERIFY(QFileInfo(folder).isDir());
        QVERIFY2(ensureLogDirectory(folder), "an existing folder is fine");
    }

    void aLogFolderThatCannotBeCreatedIsOnlyReported() {
        const QString file = path(u"a-file"_s);
        writeFile(file, "not a folder");
        QStringList reports;

        const bool created = ensureLogDirectory(file + u"/logs"_s, [&](const QString& line) { reports << line; });

        QVERIFY(!created);
        QCOMPARE(reports.size(), 1);
        QVERIFY2(reports.first().contains(u"a-file"_s), qPrintable(reports.first()));
    }

    void theFirstLogFolderIsUsedWhenItCanBeCreated() {
        const QString preferred = path(u"app/log"_s);
        const QString fallback = path(u"temp/xgetsongs-logs"_s);
        QStringList reports;

        const QString chosen = chooseLogDirectory({preferred, fallback}, [&](const QString& line) { reports << line; });

        QCOMPARE(chosen, preferred);
        QVERIFY(QFileInfo(preferred).isDir());
        QVERIFY2(!QFileInfo::exists(fallback), "the fallback is not touched");
        QVERIFY(reports.isEmpty());
    }

    void theNextLogFolderIsUsedWhenTheFirstCannotBeCreated() {
        const QString blocker = path(u"app"_s);
        writeFile(blocker, "a file where the app folder should be");
        const QString fallback = path(u"temp2/xgetsongs-logs"_s);
        QStringList reports;

        const QString chosen = chooseLogDirectory({blocker + u"/log"_s, fallback}, [&](const QString& line) { reports << line; });

        QCOMPARE(chosen, fallback);
        QVERIFY(QFileInfo(fallback).isDir());
        bool mentioned = false;
        for (const QString& report : reports) mentioned = mentioned || report.contains(u"app"_s);
        QVERIFY2(mentioned, qPrintable(reports.join(u'|')));
    }

    void theLastCandidateIsTriedWhenTheOnesBeforeItFail() {
        const QString blocker = path(u"a-file2"_s);
        writeFile(blocker, "not a folder");
        const QString last = path(u"temp3/xgetsongs-logs"_s);

        const QString chosen = chooseLogDirectory({blocker + u"/one"_s, blocker + u"/two"_s, last}, [](const QString&) {});

        QCOMPARE(chosen, last);
        QVERIFY(QFileInfo(last).isDir());
    }

    void whenNoFolderCanBeCreatedTheLastOneIsStillTheAnswerAndNothingIsThrown() {
        const QString blocker = path(u"a-file3"_s);
        writeFile(blocker, "not a folder");

        const QString chosen = chooseLogDirectory({blocker + u"/one"_s, blocker + u"/two"_s}, [](const QString&) {});

        QCOMPARE(chosen, blocker + u"/two"_s);
    }

    // The Tauri shell passes no log folder: appData\logs, then the temp folder.
    void withoutAKnownApplicationFolderOnlyTheOtherLogFoldersAreCandidates() {
        const QStringList candidates = logDirectoryCandidates(u"C:\\Data\\xGetSongs"_s, u"C:\\Temp"_s);

        QCOMPARE(candidates, (QStringList{u"C:\\Data\\xGetSongs\\logs"_s, u"C:\\Temp\\xgetsongs-logs"_s}));
    }

    // ---- added -------------------------------------------------------------------------------

    void aTrailingSeparatorDoesNotDoubleTheBackslash() {
        QCOMPARE(logDirectoryCandidates(u"C:\\Data\\xGetSongs\\"_s, u"C:\\Temp\\"_s),
                 (QStringList{u"C:\\Data\\xGetSongs\\logs"_s, u"C:\\Temp\\xgetsongs-logs"_s}));
        QCOMPARE(logDirectoryCandidates(u"C:/Data/xGetSongs/"_s, u"C:/Temp/"_s),
                 (QStringList{u"C:/Data/xGetSongs\\logs"_s, u"C:/Temp\\xgetsongs-logs"_s}));
    }

    void noCandidateGivesNoFolderAndThrowsNothing() {
        QVERIFY(chooseLogDirectory({}, [](const QString&) {}).isEmpty());
    }

    void theDefaultReportGoesToStderrAndNeverThrows() {
        const QString blocker = path(u"a-file4"_s);
        writeFile(blocker, "not a folder");

        QVERIFY(!ensureLogDirectory(blocker + u"/logs"_s));  // the line goes to stderr; nothing is thrown
        QCOMPARE(chooseLogDirectory({blocker + u"/x"_s}), blocker + u"/x"_s);
    }
};

QTEST_GUILESS_MAIN(DiagnosticsTest)
#include "tst_log_directory.moc"
