#pragma once

#include <QDir>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

#include "paths/paths.h"

// What the two real-tool test classes share.
namespace xgs::integration {

/// The Kotlin classes are tagged "integration" and run only through Gradle's integrationTest task; here they run only when the
/// environment variable XGS_INTEGRATION is 1.
inline bool enabled() { return qEnvironmentVariable("XGS_INTEGRATION") == QStringLiteral("1"); }

inline const char* disabledText() { return "set XGS_INTEGRATION=1 to run the real-tool integration tests"; }

/// Kotlin's appData.resolve("bin"): %APPDATA%\xGetSongs\bin, or <user home>\.xgetsongs\bin when APPDATA is not set. The tests
/// only read there (ToolLocator looks for yt-dlp.exe and ffmpeg.exe in it and the tests run what it finds).
inline QString appBinDir() {
    const std::optional<QString> appData =
        qEnvironmentVariableIsSet("APPDATA")
            ? paths::child(paths::fromText(qEnvironmentVariable("APPDATA")).value_or(QString()), QStringLiteral("xGetSongs"))
            : paths::child(paths::fromText(QDir::toNativeSeparators(QDir::homePath())).value_or(QString()),
                           QStringLiteral(".xgetsongs"));
    return paths::child(appData.value_or(QString()), QStringLiteral("bin")).value_or(QString());
}

/// A QTemporaryDir path as a Windows path string (what paths::fromText gives).
inline QString windowsPath(const QString& qtPath) {
    return paths::fromText(QDir::toNativeSeparators(qtPath)).value_or(QString());
}

/// Kotlin's withTimeout: token() is stopped when the time is up (a run then ends as Cancelled, which the test reports).
class Deadline {
public:
    explicit Deadline(std::chrono::milliseconds limit)
        : m_watch([this, limit](std::stop_token own) {
              std::unique_lock lock(m_mutex);
              if (!m_cv.wait_for(lock, own, limit, [] { return false; })) {
                  if (!own.stop_requested()) m_source.request_stop();
              }
          }) {}
    ~Deadline() { m_watch.request_stop(); }
    Deadline(const Deadline&) = delete;
    Deadline& operator=(const Deadline&) = delete;

    std::stop_token token() const { return m_source.get_token(); }
    bool expired() const { return m_source.stop_requested(); }

private:
    std::stop_source m_source;
    std::mutex m_mutex;
    std::condition_variable_any m_cv;
    std::jthread m_watch;   // last: starts after the members it uses and stops before they go
};

}  // namespace xgs::integration

/// Runs a void helper that may fail the current test (QCOMPARE/QVERIFY inside it) and leaves the test function when it did.
#define XGS_CHECKED(call)                         \
    do {                                          \
        call;                                     \
        if (QTest::currentTestFailed()) return;   \
    } while (false)
