#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "fixture_server.h"
#include "lyrics/google/browser_channel.h"
#include "lyrics/google/google_lyrics_provider.h"
#include "webview/lyrics_browser.h"
#include "webview/runtime.h"
#include "webview_test_support.h"

// A Google lyrics lookup through the whole chain, without Google: GoogleLyricsProvider on a worker thread, the BrowserChannel,
// LyricsBrowser (the agent and the hidden WebView2 window on the GUI thread), and the result pages of a FixtureServer on
// 127.0.0.1 in Google's place (FetcherOptions::searchBase, Q3 decision 12). The place of server's BrowserChainTest, whose HTTP
// routes are gone (Q3 decision 3). The made-up card has three dummy lines; no song here is real.

using namespace Qt::StringLiterals;
using xgs::LyricsQuery;
using xgs::lyrics::google::BrowserChannel;
using xgs::lyrics::google::DrivenBrowserOptions;
using xgs::lyrics::google::FetcherOptions;
using xgs::lyrics::google::GoogleLyricsLog;
using xgs::lyrics::google::GoogleLyricsProvider;
using xgs::lyrics::google::PageOutcome;
using xgs::lyrics::google::decodeScriptResult;
using xgs::webview::HiddenWebViewOptions;
using xgs::webview::LyricsBrowser;
using xgs::webview::runtimeVersion;
using namespace xgs::webview::testing;

namespace {

const QString Lyrics = u"더미 하나\n더미 둘\n더미 셋"_s;

/// The provider's log lines ("info: ..." / "warn: ..."), written from the worker thread.
class ChainLog final : public GoogleLyricsLog {
public:
    void info(const QString& message) override { add(u"info: "_s + message); }
    void warn(const QString& message) override { add(u"warn: "_s + message); }
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

FetcherOptions fetcherOptions(const QString& searchBase) {
    FetcherOptions options;
    options.searchBase = searchBase;
    options.minInterval = std::chrono::nanoseconds::zero();   // no ten-second wait between the songs of a check
    return options;
}

DrivenBrowserOptions drivenOptions() {
    DrivenBrowserOptions options;
    options.settleTime = std::chrono::milliseconds(300);   // the fixture pages are complete at once
    return options;
}

/// One find on a worker thread. The jthread's own stop token is the lookup's: destroying the Lookup (a test that failed early)
/// stops the lookup and joins.
class Lookup {
public:
    Lookup(GoogleLyricsProvider& provider, LyricsQuery query)
        : m_state(std::make_shared<State>()), m_thread([&provider, query, state = m_state](std::stop_token stop) {
              std::optional<QString> lyrics = provider.find(query, stop);
              std::lock_guard lock(state->mutex);
              state->lyrics = std::move(lyrics);
              state->finished = true;
          }) {}

    bool finished() const {
        std::lock_guard lock(m_state->mutex);
        return m_state->finished;
    }
    std::optional<QString> lyrics() const {
        std::lock_guard lock(m_state->mutex);
        return m_state->lyrics;
    }
    void stop() { m_thread.request_stop(); }

private:
    struct State {
        mutable std::mutex mutex;
        bool finished = false;
        std::optional<QString> lyrics;
    };
    std::shared_ptr<State> m_state;
    std::jthread m_thread;
};

LyricsQuery song(const QString& artist, const QString& title) {
    return LyricsQuery{artist, title, std::nullopt, std::nullopt};
}

bool hasLine(const QStringList& lines, const QString& part) {
    for (const QString& line : lines)
        if (line.contains(part)) return true;
    return false;
}

// The child of withoutARuntimeTheLookupIsUnavailableAndShowsNothing: one lookup on a PC without the runtime (see
// runNoRuntimeChild). Prints one line that the parent reads.
int childMain(const QString& userDataFolder) {
    const bool runtime = runtimeVersion().has_value();
    ScreenWatch watch;
    QString line;
    {
        BrowserChannel channel;
        auto browser = std::make_unique<LyricsBrowser>(channel, HiddenWebViewOptions{userDataFolder, CheckBrowserArguments});
        ChainLog log;
        // Port 9 of the machine itself: nothing is sent anywhere, the window is never made.
        GoogleLyricsProvider provider(channel, &log, fetcherOptions(u"http://127.0.0.1:9/search"_s), drivenOptions());
        Lookup lookup(provider, song(u"Card"_s, u"Dummy Song"_s));
        // Thirty seconds: without a runtime the lookup ends at once.
        (void)QTest::qWaitFor([&] { return lookup.finished(); }, 30000);
        line = u"runtime=%1 finished=%2 lyrics=%3 log=%4"_s.arg(runtime ? u"yes"_s : u"none"_s)
                   .arg(lookup.finished() ? 1 : 0)
                   .arg(lookup.lyrics() ? u"some"_s : u"none"_s)
                   .arg(log.lines().join(u" | "_s));
    }
    QTest::qWait(300);
    line += u" visible=%1"_s.arg(watch.seen().size());
    std::fputs(line.toUtf8().constData(), stdout);
    std::fputs("\n", stdout);
    std::fflush(stdout);
    return 0;
}

}  // namespace

class LyricsBrowserChainTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FixtureServer> m_server;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ScreenWatch> m_watch;
    FILETIME m_since{};

    HiddenWebViewOptions options() const { return HiddenWebViewOptions{m_dir->path(), CheckBrowserArguments}; }
    QString searchBase() const { return m_server->base() + u"/search"_s; }

    // The address the page of the view is at ("" when it cannot be read).
    static QString addressOf(LyricsBrowser& browser) {
        const Waited looked =
            waitForPage([&](auto done) { browser.view().evaluate(u"location.href"_s, std::move(done)); }, 8000);
        if (!looked.outcome || !looked.outcome->ok) return QString();
        return decodeScriptResult(looked.outcome->text).value_or(QString());
    }

private slots:
    void initTestCase() {
        if (!runtimeVersion()) QSKIP("WebView2 runtime must be installed");
    }

    void init() {
        m_since = xgs::testsupport::nowAsFileTime();
        m_server = std::make_unique<FixtureServer>();
        QVERIFY(m_server->isListening());
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_watch = std::make_unique<ScreenWatch>();
    }

    void cleanup() {
        // Twenty seconds: the browser process ends a moment after its last window is gone (about a second here).
        const auto left = browserProcessesLeft(m_dir->path(), m_since, 20000);
        const QStringList visible = m_watch->seen();
        m_watch.reset();
        m_dir.reset();
        m_server.reset();
        QVERIFY2(visible.isEmpty(), qPrintable(u"visible windows: "_s + visible.join(u"; "_s)));
        QVERIFY2(left.isEmpty(), qPrintable(u"browser processes left: "_s + describe(left)));
    }

    void aSongWithACardIsFoundThroughTheWholeChain() {
        BrowserChannel channel;
        LyricsBrowser browser(channel, options());
        QVERIFY(channel.hasAgent());
        ChainLog log;
        GoogleLyricsProvider provider(channel, &log, fetcherOptions(searchBase()), drivenOptions());

        Lookup lookup(provider, song(u"Card"_s, u"Dummy Song One"_s));
        // Thirty seconds: the lookup takes about a second (the window is made, the card shows at once).
        QTRY_VERIFY_WITH_TIMEOUT(lookup.finished(), 30000);

        QCOMPARE(lookup.lyrics(), std::optional<QString>(Lyrics));
        QVERIFY2(log.lines().contains(u"info: 구글 가사 카드: 찾음 (줄 3, 문단 1)"_s), qPrintable(log.lines().join(u" | "_s)));
        QCOMPARE(m_server->countOf(u"/search"_s), 1);
        QString search;
        for (const QString& target : m_server->requests())
            if (target.startsWith(u"/search"_s)) search = target;
        const QUrlQuery query(QUrl(m_server->base() + search));
        QCOMPARE(query.queryItemValue(u"q"_s, QUrl::FullyDecoded), u"Card Dummy Song One lyrics"_s);
        QCOMPARE(query.queryItemValue(u"hl"_s), u"en"_s);
        // The page went back to the blank page after the render; the window stays for the next song.
        QVERIFY(browser.view().hasWindow());
        QCOMPARE(addressOf(browser), u"about:blank"_s);
    }

    void aSongWithoutACardIsNoLyrics() {
        BrowserChannel channel;
        LyricsBrowser browser(channel, options());
        ChainLog log;
        GoogleLyricsProvider provider(channel, &log, fetcherOptions(searchBase()), drivenOptions());

        Lookup lookup(provider, song(u"Plain"_s, u"Dummy Song Two"_s));
        QTRY_VERIFY_WITH_TIMEOUT(lookup.finished(), 30000);

        QCOMPARE(lookup.lyrics(), std::nullopt);
        QCOMPARE(log.lines(), QStringList{u"info: 구글 가사 카드: 카드 없음"_s});
        QCOMPARE(m_server->countOf(u"/search"_s), 1);
    }

    void aBlockPageStartsTheCoolDown() {
        BrowserChannel channel;
        LyricsBrowser browser(channel, options());
        ChainLog log;
        GoogleLyricsProvider provider(channel, &log, fetcherOptions(searchBase()), drivenOptions());

        {
            Lookup lookup(provider, song(u"Block"_s, u"Dummy Song Three"_s));
            QTRY_VERIFY_WITH_TIMEOUT(lookup.finished(), 30000);
            QCOMPARE(lookup.lyrics(), std::nullopt);
        }
        QCOMPARE(log.lines(), QStringList{u"warn: 구글 가사 카드: CAPTCHA로 막힘, 5분 동안 구글 검색을 쉼"_s});
        QCOMPARE(m_server->countOf(u"/search"_s), 1);
        QCOMPARE(m_server->countOf(u"/sorry/"_s), 1);

        // The next lookup asks nobody.
        Lookup next(provider, song(u"Card"_s, u"Dummy Song Four"_s));
        QTRY_VERIFY_WITH_TIMEOUT(next.finished(), 10000);
        QCOMPARE(next.lyrics(), std::nullopt);
        QTest::qWait(500);
        QCOMPARE(m_server->countOf(u"/search"_s), 1);
        QCOMPARE(log.lines().size(), 1);
    }

    void aHungPageIsReplacedAndIsNotHeldAgainstGoogle() {
        BrowserChannel channel;
        LyricsBrowser browser(channel, options());
        ChainLog log;
        GoogleLyricsProvider provider(channel, &log, fetcherOptions(searchBase()), drivenOptions());

        {
            Lookup lookup(provider, song(u"Hang"_s, u"Dummy Song Five"_s));
            // Thirty seconds: the page stops answering at once and the script limit is five seconds.
            QTRY_VERIFY_WITH_TIMEOUT(lookup.finished(), 30000);
            QCOMPARE(lookup.lyrics(), std::nullopt);
        }
        QCOMPARE(log.lines(), QStringList{u"warn: 구글 가사 카드: 웹뷰를 쓸 수 없음 (script timed out)"_s});
        QCOMPARE(browser.view().windowsMade(), 1);
        QVERIFY(!browser.view().hasWindow());

        // No cool-down: the next song is looked up at once, in a new window.
        Lookup next(provider, song(u"Card"_s, u"Dummy Song Six"_s));
        QTRY_VERIFY_WITH_TIMEOUT(next.finished(), 30000);
        QCOMPARE(next.lyrics(), std::optional<QString>(Lyrics));
        QCOMPARE(browser.view().windowsMade(), 2);
        QCOMPARE(m_server->countOf(u"/search"_s), 2);
        QCOMPARE(log.lines().size(), 2);
        QCOMPARE(log.lines().at(1), u"info: 구글 가사 카드: 찾음 (줄 3, 문단 1)"_s);
    }

    void closingTheBrowserDuringALookupEndsItAtOnce() {
        BrowserChannel channel;
        auto browser = std::make_unique<LyricsBrowser>(channel, options());
        ChainLog log;
        GoogleLyricsProvider provider(channel, &log, fetcherOptions(searchBase()), drivenOptions());

        Lookup lookup(provider, song(u"Hang"_s, u"Dummy Song Seven"_s));
        // Thirty seconds: the window is made first (about a second here).
        QTRY_VERIFY2_WITH_TIMEOUT(m_server->countOf(u"/search"_s) == 1,
                                  qPrintable(u"requests: "_s + m_server->requests().join(u", "_s) + u"; finished: "_s
                                             + QString::number(lookup.finished()) + u"; log: "_s + log.lines().join(u" | "_s)
                                             + u"; windows made in: "_s + creationTimes(browser->view().creationTimesMs())),
                                  30000);
        // The page stops answering as soon as it has loaded; by now the lookup waits for a script that will not answer.
        QTest::qWait(1500);
        QVERIFY(!lookup.finished());

        QElapsedTimer timer;
        timer.start();
        browser.reset();
        QTRY_VERIFY_WITH_TIMEOUT(lookup.finished(), 1000);
        qInfo("the lookup ended %lld ms after the browser was destroyed", timer.elapsed());

        QCOMPARE(lookup.lyrics(), std::nullopt);
        QVERIFY(!channel.hasAgent());
        QCOMPARE(log.lines(), QStringList{u"warn: 구글 가사 카드: 웹뷰를 쓸 수 없음 (closed)"_s});

        // Later lookups find no browser and end at once.
        Lookup later(provider, song(u"Card"_s, u"Dummy Song Eight"_s));
        QTRY_VERIFY_WITH_TIMEOUT(later.finished(), 1000);
        QCOMPARE(later.lyrics(), std::nullopt);
        QCOMPARE(log.lines().value(1), u"warn: 구글 가사 카드: 웹뷰를 쓸 수 없음 (no-agent)"_s);

        // No browser process of this folder is left (the hung page's included).
        const auto left = browserProcessesLeft(m_dir->path(), m_since, 20000);
        QVERIFY2(left.isEmpty(), qPrintable(describe(left)));
    }

    void stoppingALookupLeavesTheBlankPage() {
        BrowserChannel channel;
        LyricsBrowser browser(channel, options());
        ChainLog log;
        GoogleLyricsProvider provider(channel, &log, fetcherOptions(searchBase()), drivenOptions());

        Lookup lookup(provider, song(u"Restless"_s, u"Dummy Song Nine"_s));
        // Thirty seconds: the window is made first (about a second here).
        QTRY_VERIFY2_WITH_TIMEOUT(m_server->countOf(u"/search"_s) == 1,
                                  qPrintable(u"requests: "_s + m_server->requests().join(u", "_s) + u"; finished: "_s
                                             + QString::number(lookup.finished()) + u"; log: "_s + log.lines().join(u" | "_s)
                                             + u"; windows made in: "_s + creationTimes(browser.view().creationTimesMs())),
                                  30000);
        // The page changes its address every 100 ms, so it never settles; the lookup keeps looking at it.
        QTest::qWait(1000);
        QVERIFY(!lookup.finished());
        QVERIFY(addressOf(browser).contains(u"restless="_s));

        lookup.stop();
        // Six seconds: the stop ends the wait at once; the reset that follows has five seconds and takes a moment.
        QTRY_VERIFY_WITH_TIMEOUT(lookup.finished(), 6000);

        QCOMPARE(lookup.lyrics(), std::nullopt);
        QCOMPARE(addressOf(browser), u"about:blank"_s);
        QVERIFY(browser.view().hasWindow());
        QVERIFY2(log.lines().isEmpty(), qPrintable(log.lines().join(u" | "_s)));   // a stopped trip says nothing
    }

    void anIdleWindowIsClosedAndTheNextLookupMakesANewOne() {
        BrowserChannel channel;
        // Three seconds, not one: the agent counts the idle time from when a command came (BrowserAgent, as Rust), and its timer
        // may look at once after the answer; the first load makes the window, which took about a second here, so with one second
        // the window could be closed between the load and the first look (the likely cause of one failure in 20 runs).
        LyricsBrowser browser(channel, options(), nullptr, std::chrono::seconds(3));
        ChainLog log;
        GoogleLyricsProvider provider(channel, &log, fetcherOptions(searchBase()), drivenOptions());

        {
            Lookup lookup(provider, song(u"Card"_s, u"Dummy Song Ten"_s));
            QTRY_VERIFY_WITH_TIMEOUT(lookup.finished(), 30000);
            QCOMPARE(lookup.lyrics(), std::optional<QString>(Lyrics));
        }
        QVERIFY(browser.view().hasWindow());
        // Ten seconds: the window goes three seconds after the last command, looked at four times a second.
        QTRY_VERIFY_WITH_TIMEOUT(!browser.view().hasWindow(), 10000);
        QCOMPARE(browser.view().windowsMade(), 1);

        Lookup next(provider, song(u"Card"_s, u"Dummy Song Eleven"_s));
        QTRY_VERIFY_WITH_TIMEOUT(next.finished(), 30000);
        QCOMPARE(next.lyrics(), std::optional<QString>(Lyrics));
        QCOMPARE(browser.view().windowsMade(), 2);
    }

    void withoutARuntimeTheLookupIsUnavailableAndShowsNothing() {
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        // Sixty seconds: the child's lookup ends in well under a second without a runtime.
        const ChildRun run = runNoRuntimeChild(empty.path(), m_dir->path(), 60000);
        qInfo("child: %s", qUtf8Printable(run.output.trimmed()));

        QCOMPARE(run.exitCode, 0);
        QVERIFY2(run.output.contains(u"runtime=none finished=1 lyrics=none log=warn: 구글 가사 카드: 웹뷰를 쓸 수 없음 ("_s),
                 qPrintable(run.output));
        QVERIFY2(!run.output.contains(u"쉼"_s), qPrintable(run.output));
        QVERIFY2(run.output.contains(u" visible=0"_s), qPrintable(run.output));
        QCOMPARE(m_server->requests(), QStringList());
    }
};

int main(int argc, char** argv) {
    const std::optional<QString> folder = childUserDataFolder();
    if (folder) quietChildFailures();
    QGuiApplication app(argc, argv);
    if (folder) return childMain(*folder);
    LyricsBrowserChainTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_lyrics_browser_chain.moc"
