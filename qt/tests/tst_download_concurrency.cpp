#include <QtTest>
#include <QThread>

#include "job/download_concurrency.h"

using xgs::job::concurrencyForCores;

class DownloadConcurrencyTest : public QObject {
    Q_OBJECT

private slots:
    void itIsSeventyPercentOfTheCoresRoundedDown() {
        QCOMPARE(concurrencyForCores(3), 2);
        QCOMPARE(concurrencyForCores(4), 2);
        QCOMPARE(concurrencyForCores(8), 5);
        QCOMPARE(concurrencyForCores(10), 7);
        QCOMPARE(concurrencyForCores(12), 8);
        QCOMPARE(concurrencyForCores(16), 11);
        QCOMPARE(concurrencyForCores(32), 22);
    }

    void itIsNeverLessThanOne() {
        QCOMPARE(concurrencyForCores(1), 1);
        QCOMPARE(concurrencyForCores(2), 1);
        QCOMPARE(concurrencyForCores(0), 1);
    }

    void theAutomaticNumberIsForTheCoresOfThisMachine() {
        QCOMPARE(xgs::job::automaticConcurrency(), concurrencyForCores(QThread::idealThreadCount()));
    }
};

QTEST_APPLESS_MAIN(DownloadConcurrencyTest)
#include "tst_download_concurrency.moc"
