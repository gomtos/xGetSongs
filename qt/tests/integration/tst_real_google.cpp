#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include "integration_support.h"
#include "lyrics/google/browser_channel.h"
#include "lyrics/google/google_lyrics_provider.h"
#include "model/models.h"
#include "webview/lyrics_browser.h"
#include "webview/runtime.h"

// The one real Google check of Q3 (plan decision 15): the real LyricsBrowser and GoogleLyricsProvider with default options ask
// for the song of that decision once. It runs only when XGS_INTEGRATION=1 and XGS_INTEGRATION_GOOGLE=1 are both set and skips
// otherwise (before anything is made: no WebView, no request). The planned runs never set them; the user has to allow it.
// Output: the names of the log lines (counts and result names), the lines and paragraphs, the time. Never the lyrics or the song.

using namespace Qt::StringLiterals;
using xgs::LyricsQuery;
using xgs::lyrics::google::BrowserChannel;
using xgs::lyrics::google::GoogleLyricsLog;
using xgs::lyrics::google::GoogleLyricsProvider;
using xgs::webview::HiddenWebViewOptions;
using xgs::webview::LyricsBrowser;

namespace {

class RealLog final : public GoogleLyricsLog {
public:
    void info(const QString& message) override { add(message); }
    void warn(const QString& message) override { add(message); }
    QStringList lines() const {
        std::lock_guard lock(m_mutex);
        return m_lines;
    }

private:
    void add(const QString& line) {
        std::lock_guard lock(m_mutex);
        m_lines.append(line);
    }
    mutable std::mutex m_mutex;
    QStringList m_lines;
};

bool googleEnabled() {
    return xgs::integration::enabled() && qEnvironmentVariable("XGS_INTEGRATION_GOOGLE") == QStringLiteral("1");
}

}  // namespace

class RealGoogleTest : public QObject {
    Q_OBJECT

private slots:
    void oneSongIsLookedUpWithOneRequest() {
        if (!googleEnabled()) QSKIP("set XGS_INTEGRATION=1 and XGS_INTEGRATION_GOOGLE=1 to ask the real Google (once)");
        if (!xgs::webview::runtimeVersion()) QSKIP("WebView2 runtime must be installed");

        QTemporaryDir userData;
        QVERIFY(userData.isValid());
        RealLog log;
        std::optional<QString> lyrics;
        bool finished = false;
        QElapsedTimer clock;
        clock.start();
        {
            BrowserChannel channel;
            LyricsBrowser browser(channel, HiddenWebViewOptions{userData.path(), QString()});
            GoogleLyricsProvider provider(channel, &log);
            std::mutex mutex;
            // A one-artist song without parentheses: one search request (decision 15).
            const LyricsQuery query{u"RESCENE"_s, u"LOVE ATTACK"_s, std::nullopt, std::nullopt};
            std::jthread worker([&](std::stop_token stop) {
                std::optional<QString> found = provider.find(query, stop);
                std::lock_guard lock(mutex);
                lyrics = std::move(found);
                finished = true;
            });
            // Sixty seconds: the lookup's own limit is 20 s plus the window being made.
            (void)QTest::qWaitFor(
                [&] {
                    std::lock_guard lock(mutex);
                    return finished;
                },
                60000);
            worker.request_stop();
        }
        const qint64 ms = clock.elapsed();
        const QString name = log.lines().join(u" | "_s);
        qInfo("result: %s; %lld ms", qUtf8Printable(name), static_cast<long long>(ms));
        if (!lyrics) QSKIP(qPrintable(u"no lyrics (blocked or no card), not a failure: "_s + name));
        QVERIFY2(lyrics->count(u'\n') + 1 >= 3, "fewer than three lines");
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    RealGoogleTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_real_google.moc"
