#include <QtTest>

#include "version.h"

class SmokeTest : public QObject {
    Q_OBJECT
private slots:
    void versionIsSet() {
        QCOMPARE(xgs::version(), QStringLiteral("0.1.0"));
    }
};

QTEST_APPLESS_MAIN(SmokeTest)
#include "tst_smoke.moc"
