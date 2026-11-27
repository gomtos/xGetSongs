#include <QtTest>

#include <QUuid>

#include <windows.h>

#include <optional>

#include "app_test_support.h"
#include "single_instance.h"

// The single instance of the app with a made-up id per test (never the product's): the first instance holds the names that
// tauri-plugin-single-instance uses on Windows (mutex `<id>-sim`, window class `<id>-sic`, window name `<id>-siw`), so the Qt
// build and the Tauri build see each other; a second one tells the first and is not first. The message window is never shown.

using namespace Qt::StringLiterals;
using xgs::app::SingleInstance;

namespace {

QString testId() {
    return u"xgs-test-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

std::wstring wide(const QString& text) {
    return text.toStdWString();
}

}  // namespace

class SingleInstanceTest : public QObject {
    Q_OBJECT

private slots:
    void theProductIdIsTheIdentifierOfTheTauriApp() {
        // shell/src-tauri/tauri.conf.json "identifier"; the plugin's "semver" feature, which would add the version, is off there.
        QCOMPARE(SingleInstance::productId(), u"com.xgetsongs.shell"_s);
    }

    void theFirstInstanceIsFirstAndHoldsTheNamesOfTheTauriPlugin() {
        const QString id = testId();
        SingleInstance first(id);
        QVERIFY(first.isFirst());
        const HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, wide(id + u"-sim"_s).c_str());
        QVERIFY(mutex != nullptr);
        CloseHandle(mutex);
        const HWND window = FindWindowW(wide(id + u"-sic"_s).c_str(), wide(id + u"-siw"_s).c_str());
        QVERIFY(window != nullptr);
        QVERIFY(!IsWindowVisible(window));
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        QCOMPARE(pid, GetCurrentProcessId());
    }

    void aSecondInstanceTellsTheFirst() {
        const QString id = testId();
        SingleInstance first(id);
        int told = 0;
        first.onSecondLaunch([&told] { told += 1; });
        {
            SingleInstance second(id);
            QVERIFY(!second.isFirst());
        }
        QTRY_COMPARE(told, 1);
        SingleInstance third(id);
        QVERIFY(!third.isFirst());
        QTRY_COMPARE(told, 2);
    }

    void theMessageIsTheOneTheTauriPluginSends() {
        // What a Tauri second instance sends (WM_COPYDATA, dwData 1542, "cwd|args" and a NUL) is taken as a second launch too.
        const QString id = testId();
        SingleInstance first(id);
        int told = 0;
        first.onSecondLaunch([&told] { told += 1; });
        const HWND window = FindWindowW(wide(id + u"-sic"_s).c_str(), wide(id + u"-siw"_s).c_str());
        QVERIFY(window != nullptr);
        const QByteArray data = QByteArrayLiteral("C:\\dummy|C:\\dummy\\xGetSongs.exe") + '\0';
        COPYDATASTRUCT copy{1542, DWORD(data.size()), const_cast<char*>(data.constData())};
        DWORD_PTR result = 0;
        QVERIFY(SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy), SMTO_NORMAL, 10000, &result) != 0);
        QCOMPARE(result, DWORD_PTR(1));
        QTRY_COMPARE(told, 1);
        // Another WM_COPYDATA is not ours.
        COPYDATASTRUCT other{7, DWORD(data.size()), const_cast<char*>(data.constData())};
        SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&other), SMTO_NORMAL, 10000, &result);
        QTest::qWait(50);
        QCOMPARE(told, 1);
    }

    void anInstanceAfterTheFirstEndedIsFirst() {
        const QString id = testId();
        {
            SingleInstance first(id);
            QVERIFY(first.isFirst());
        }
        SingleInstance next(id);
        QVERIFY(next.isFirst());
        QVERIFY(FindWindowW(wide(id + u"-sic"_s).c_str(), wide(id + u"-siw"_s).c_str()) != nullptr);
    }

    void anotherIdIsAnotherApp() {
        SingleInstance first(testId());
        SingleInstance other(testId());
        QVERIFY(first.isFirst());
        QVERIFY(other.isFirst());
    }
};

XGS_OFFSCREEN_TEST_MAIN(SingleInstanceTest)
#include "tst_single_instance.moc"
