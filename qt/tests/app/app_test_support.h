#pragma once

#include <QtTest>

#include <QApplication>
#include <QGuiApplication>
#include <QNetworkAccessManager>

#include "remote_playlists.h"

// What the widget tests share: a main that makes the QApplication on the offscreen platform (no window ever reaches the screen),
// and a stand-in for the transfer of the remote playlists that never touches the network.
namespace xgs::app::test {

/// Records fetch and cancel; the test answers with answer(). The network access manager it is given is never used.
class FakeRemotePlaylists final : public ui::RemotePlaylists {
public:
    explicit FakeRemotePlaylists(QNetworkAccessManager& network) : ui::RemotePlaylists(network, QUrl()) {}

    void fetch() override {
        fetchCalls += 1;
        running = true;
    }
    void cancel() override {
        cancelCalls += 1;
        running = false;
    }
    bool isRunning() const override { return running; }

    /// Ends the run with outcome, as the real transfer does (done is not emitted after a cancel).
    void answer(const ui::RemoteOutcome& outcome) {
        running = false;
        done(outcome);
    }

    int fetchCalls = 0;
    int cancelCalls = 0;
    bool running = false;
};

/// The offscreen platform reads fonts from a folder of its own (none here): it is pointed at the system's fonts, so the widgets
/// get real text metrics. (The platform also warns "This plugin does not support propagateSizeHints()" for each window it shows;
/// that line is harmless.)
inline void useSystemFonts() {
    if (qEnvironmentVariableIsSet("QT_QPA_FONTDIR")) return;
    const QString windows = qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows"));
    qputenv("QT_QPA_FONTDIR", (windows + QStringLiteral("/Fonts")).toUtf8());
}

}  // namespace xgs::app::test

/// The test main of the widget tests: the offscreen platform is forced before the QApplication is made, and a run on any other
/// platform (a -platform argument) is refused, so nothing is ever shown on the screen.
#define XGS_OFFSCREEN_TEST_MAIN(TestClass)                                                                  \
    int main(int argc, char** argv) {                                                                       \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                                            \
        xgs::app::test::useSystemFonts();                                                                   \
        QApplication app(argc, argv);                                                                       \
        if (QGuiApplication::platformName() != QLatin1StringView("offscreen")) {                            \
            qCritical("the widget tests run on the offscreen platform only");                               \
            return 1;                                                                                       \
        }                                                                                                   \
        TestClass test;                                                                                     \
        QTEST_SET_MAIN_SOURCE_PATH                                                                          \
        return QTest::qExec(&test, argc, argv);                                                             \
    }
