#include <QtTest>

#include "diag/log_directory.h"

using namespace Qt::StringLiterals;
using xgs::diag::logDirectoryCandidates;

// Kotlin's SidecarLoggingTest. Its first test (a folder the shell asked for comes first) has no counterpart: the Tauri shell
// passes no log folder.
class SidecarLoggingTest : public QObject {
    Q_OBJECT

private slots:
    void withoutAFolderFromTheShellTheAppDataFolderIsTriedFirst() {
        const QString temp = u"C:\\Users\\x\\AppData\\Local\\Temp"_s;
        const QString appData = temp + u"\\data"_s;

        QCOMPARE(logDirectoryCandidates(appData, temp), (QStringList{appData + u"\\logs"_s, temp + u"\\xgetsongs-logs"_s}));
    }
};

QTEST_GUILESS_MAIN(SidecarLoggingTest)
#include "tst_sidecar_logging.moc"
