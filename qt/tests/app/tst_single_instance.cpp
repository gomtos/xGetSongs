#include <QtTest>

#include <QThread>
#include <QUuid>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

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

    void aSendAcrossThreadsBringsTheCallbackOnTheFirstInstancesThread() {
        // The second copy runs on another thread (as another process would): its SendMessageTimeoutW waits until this thread
        // takes the message, and the callback runs here.
        const QString id = testId();
        SingleInstance first(id);
        QThread* calledOn = nullptr;
        int told = 0;
        first.onSecondLaunch([&] {
            told += 1;
            calledOn = QThread::currentThread();
        });
        std::atomic<int> secondWasFirst = -1;
        std::jthread second([&] {
            SingleInstance instance(id);
            secondWasFirst = instance.isFirst() ? 1 : 0;
        });
        // Generous: the second copy waits for this thread's event loop (up to its own 10 s).
        QTRY_COMPARE_WITH_TIMEOUT(secondWasFirst.load(), 0, 10000);
        QTRY_COMPARE(told, 1);
        QCOMPARE(calledOn, QThread::currentThread());
        second.join();
    }

    void aLaunchWhileTheFirstShutsDownBecomesFirst() {
        // The first copy no longer pumps messages (it is taking its engine down) and is then destroyed: the second copy, on
        // another thread, gets no answer, waits on the mutex, and becomes the first once it is released.
        const QString id = testId();
        auto first = std::make_unique<SingleInstance>(id);
        QVERIFY(first->isFirst());
        std::atomic<int> secondWasFirst = -1;
        std::atomic<bool> windowFound = false;
        std::atomic<bool> release = false;
        std::jthread second([&] {
            SingleInstance instance(id);
            secondWasFirst = instance.isFirst() ? 1 : 0;
            windowFound = FindWindowW(wide(id + u"-sic"_s).c_str(), wide(id + u"-siw"_s).c_str()) != nullptr;
            while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // no message pumping meanwhile
        QCOMPARE(secondWasFirst.load(), -1);
        first.reset();
        QTRY_COMPARE_WITH_TIMEOUT(secondWasFirst.load(), 1, 10000);
        QVERIFY(windowFound.load());
        release = true;
        second.join();
    }

    void aLaunchAfterTheFirstStoppedListeningWaitsAndBecomesFirst() {
        // The app's own shutdown: stopListening right after the event loop, the destructor a little later.
        const QString id = testId();
        auto first = std::make_unique<SingleInstance>(id);
        first->stopListening();
        QVERIFY(FindWindowW(wide(id + u"-sic"_s).c_str(), wide(id + u"-siw"_s).c_str()) == nullptr);
        std::atomic<int> secondWasFirst = -1;
        std::jthread second([&] {
            SingleInstance instance(id);
            secondWasFirst = instance.isFirst() ? 1 : 0;
        });
        QTest::qWait(300);
        QCOMPARE(secondWasFirst.load(), -1);
        first.reset();
        QTRY_COMPARE_WITH_TIMEOUT(secondWasFirst.load(), 1, 10000);
        second.join();
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
