#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdio>
#include <memory>

#include "fixture_server.h"
#include "lyrics/google/browser_agent.h"
#include "webview/hidden_web_view.h"
#include "webview/runtime.h"
#include "webview_test_support.h"

// The hidden WebView2 window against the fixed pages of a FixtureServer on 127.0.0.1 (the place of the Tauri shell's G5 check,
// shell/src-tauri/src/browser_check.rs, run against the window rules of browser_window.rs and Q3 decision 10). No Google, no
// lyrics. Every test watches that no window of this process or of its WebView2 processes becomes visible, and ends by waiting
// for the browser processes of its own user-data folder (a QTemporaryDir) to be gone; no other process is looked at or ended.

using namespace Qt::StringLiterals;
using xgs::lyrics::google::PageOutcome;
using xgs::lyrics::google::decodeScriptResult;
using xgs::webview::HiddenWebView;
using xgs::webview::HiddenWebViewOptions;
using xgs::webview::runtimeVersion;
using namespace xgs::webview::testing;

namespace {

// One look at a fixture page: its card marker, the counter of its timer (-1: none), the start of its text and its address.
const QString Look =
    u"JSON.stringify({ card: !!document.querySelector('div[data-lyricid]'), ticks: window.__ticks === undefined ? -1 : "
    u"window.__ticks, text: (document.body ? document.body.innerText : '').substring(0, 300), url: location.href })"_s;

// The child of withoutARuntimeLoadFailsAndShowsNothing: a load on a PC without the runtime (see runNoRuntimeChild). Prints one
// line that the parent reads.
int childMain(const QString& userDataFolder) {
    const bool runtime = runtimeVersion().has_value();
    ScreenWatch watch;
    QString line;
    {
        HiddenWebView view(HiddenWebViewOptions{userDataFolder, CheckBrowserArguments});
        // Twenty seconds: without a runtime the loader answers at once.
        const Waited loaded = waitForPage([&](auto done) { view.load(u"http://127.0.0.1:9/none"_s, std::move(done)); }, 20000);
        line = u"runtime=%1 answered=%2 calls=%3 ok=%4 hasWindow=%5 text=%6"_s.arg(runtime ? u"yes"_s : u"none"_s)
                   .arg(loaded.outcome ? 1 : 0)
                   .arg(loaded.calls)
                   .arg(loaded.outcome && loaded.outcome->ok ? 1 : 0)
                   .arg(view.hasWindow() ? 1 : 0)
                   .arg(loaded.outcome ? loaded.outcome->text : QString());
    }
    QTest::qWait(300);
    line += u" visible=%1"_s.arg(watch.seen().size());
    std::fputs(line.toUtf8().constData(), stdout);
    std::fputs("\n", stdout);
    std::fflush(stdout);
    return 0;
}

}  // namespace

class HiddenWebViewTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FixtureServer> m_server;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ScreenWatch> m_watch;
    FILETIME m_since{};

    HiddenWebViewOptions options() const { return HiddenWebViewOptions{m_dir->path(), CheckBrowserArguments}; }
    QString url(const QString& path) const { return m_server->base() + path; }

    static Waited load(HiddenWebView& view, const QString& address) {
        // Ten seconds: the first load also makes the window (about a second here).
        return waitForPage([&](auto done) { view.load(address, std::move(done)); }, 10000);
    }

    // One look; empty when it failed (why goes into *why when given).
    static QJsonObject look(HiddenWebView& view, QString* why = nullptr) {
        const Waited looked = waitForPage([&](auto done) { view.evaluate(Look, std::move(done)); }, 8000);
        if (!looked.outcome || !looked.outcome->ok) {
            if (why) *why = looked.outcome ? looked.outcome->text : u"no answer in 8 s"_s;
            return {};
        }
        if (why) *why = looked.outcome->text;
        return QJsonDocument::fromJson(decodeScriptResult(looked.outcome->text).value_or(QString()).toUtf8()).object();
    }

    // Looks every 100 ms until the page shows its card; the last look (empty when there was none). Ten seconds: the fixture
    // pages show their card after 1.5 s at most. When it gives up it says what the last look answered.
    static QJsonObject waitForCard(HiddenWebView& view) {
        QJsonObject seen;
        QString why;
        int looks = 0;
        const bool shown = QTest::qWaitFor(
            [&] {
                ++looks;
                seen = look(view, &why);
                if (seen.value(u"card"_s).toBool()) return true;
                QTest::qWait(100);
                return false;
            },
            10000);
        if (!shown)
            qWarning("no card after %d looks; hasWindow %d; last answer: %s", looks, int(view.hasWindow()), qUtf8Printable(why.left(400)));
        return seen;
    }

    // The files under folder whose name starts with prefix (case ignored).
    static QStringList filesNamed(const QString& folder, const QString& prefix) {
        QStringList found;
        QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (it.fileName().startsWith(prefix, Qt::CaseInsensitive)) found.append(path);
        }
        return found;
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
        // The views of the tests are gone (each test owns its own). Twenty seconds: the browser process ends a moment after its
        // last window is gone (about a second here).
        const auto left = browserProcessesLeft(m_dir->path(), m_since, 20000);
        const QStringList visible = m_watch->seen();
        m_watch.reset();
        m_dir.reset();
        m_server.reset();
        QVERIFY2(visible.isEmpty(), qPrintable(u"visible windows: "_s + visible.join(u"; "_s)));
        QVERIFY2(left.isEmpty(), qPrintable(u"browser processes left: "_s + describe(left)));
    }

    void theWindowIsMadeOnTheFirstLoadAndIsNeverShown() {
        HiddenWebView view(options());
        QVERIFY(!view.hasWindow());
        QCOMPARE(view.windowsMade(), 0);
        QVERIFY(!view.isShownOnScreen());

        const Waited loaded = load(view, url(u"/first"_s));
        QVERIFY(loaded.outcome);
        QVERIFY2(loaded.outcome->ok, qPrintable(loaded.outcome->text));
        QCOMPARE(loaded.calls, 1);
        QVERIFY(view.hasWindow());
        QCOMPARE(view.windowsMade(), 1);
        QVERIFY(!view.isShownOnScreen());

        QVERIFY(waitForCard(view).value(u"card"_s).toBool());
        QVERIFY(!view.isShownOnScreen());
        QVERIFY(m_watch->looks() >= 5);
        QVERIFY2(m_watch->seen().isEmpty(), qPrintable(m_watch->seen().join(u"; "_s)));
    }

    void timersRunInTheHiddenWindow() {
        HiddenWebView view(options());
        QVERIFY(load(view, url(u"/first"_s)).outcome.value_or(PageOutcome{}).ok);
        const QJsonObject before = waitForCard(view);
        QVERIFY(before.value(u"card"_s).toBool());
        QTest::qWait(500);
        const QJsonObject after = look(view);

        // The page's timer fires every 50 ms: ten ticks in 500 ms when nothing holds it back (browser_check.rs's bound).
        const int ticks = after.value(u"ticks"_s).toInt(-1) - before.value(u"ticks"_s).toInt(-1);
        qInfo("%d ticks in 500 ms", ticks);
        QVERIFY2(ticks >= 8, qPrintable(QString::number(ticks)));
    }

    void theWindowIsReusedForTheNextPage() {
        HiddenWebView view(options());
        QVERIFY(load(view, url(u"/first"_s)).outcome.value_or(PageOutcome{}).ok);
        QVERIFY(waitForCard(view).value(u"card"_s).toBool());

        QVERIFY(load(view, url(u"/second"_s)).outcome.value_or(PageOutcome{}).ok);
        const QJsonObject seen = waitForCard(view);

        QVERIFY2(seen.value(u"text"_s).toString().contains(u"card of second"_s), qPrintable(seen.value(u"text"_s).toString()));
        QCOMPARE(view.windowsMade(), 1);
    }

    void aNewWindowHasNoCookieOfTheOldOne() {
        HiddenWebView view(options());
        QVERIFY(load(view, url(u"/first"_s)).outcome.value_or(PageOutcome{}).ok);
        QVERIFY(waitForCard(view).value(u"card"_s).toBool());
        // The same window keeps the cookie of /first (an hour long) for the next page: the check below can see one.
        QVERIFY(load(view, url(u"/second"_s)).outcome.value_or(PageOutcome{}).ok);
        const QJsonObject same = waitForCard(view);
        QVERIFY2(same.value(u"text"_s).toString().contains(u"cookie[seen=1]"_s), qPrintable(same.value(u"text"_s).toString()));

        view.close();
        QVERIFY(!view.hasWindow());
        QVERIFY(load(view, url(u"/second"_s)).outcome.value_or(PageOutcome{}).ok);
        const QJsonObject fresh = waitForCard(view);

        QCOMPARE(view.windowsMade(), 2);
        QVERIFY2(fresh.value(u"text"_s).toString().contains(u"cookie[]"_s), qPrintable(fresh.value(u"text"_s).toString()));

        // Nothing of the cookie is written into the user-data folder: an InPrivate window keeps it in memory only. Looked at once
        // the browser processes have ended (they write their files before they go).
        view.close();
        QTest::qWait(200);   // the window is taken down from the event loop (close may run inside a WebView2 handler)
        const auto left = browserProcessesLeft(m_dir->path(), m_since, 20000);
        QVERIFY2(left.isEmpty(), qPrintable(describe(left)));
        QStringList holding;
        QDirIterator it(m_dir->path(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile file(path);
            if (file.size() > 64 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) continue;
            const QByteArray bytes = file.readAll();
            if (bytes.contains("seen") && bytes.contains("127.0.0.1")) holding.append(QDir(m_dir->path()).relativeFilePath(path));
        }
        QVERIFY2(holding.isEmpty(), qPrintable(holding.join(u", "_s)));
    }

    void aDownloadIsRefused() {
        // Only looked at: nothing is written to or removed from the user's Downloads folder.
        const QString downloads = downloadsFolder();
        QVERIFY(!downloads.isEmpty());
        const auto listing = [&] {
            return QDir(downloads).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        };
        const QStringList before = listing();

        HiddenWebView view(options());
        QVERIFY(load(view, url(u"/popup"_s)).outcome.value_or(PageOutcome{}).ok);
        // The page clicks its download link 600 ms after it loaded; the request shows that the download was tried, and the
        // count that it reached the refusal (a download that WebView2 drops on its own would prove nothing).
        QTRY_VERIFY_WITH_TIMEOUT(m_server->countOf(u"/download.bin"_s) > 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(view.downloadsRefused(), 1, 10000);
        QTest::qWait(1500);

        QStringList appeared;
        const QSet<QString> old(before.begin(), before.end());
        for (const QString& name : listing()) {
            if (!old.contains(name)) appeared.append(name);
        }
        QStringList ours;
        for (const QString& name : appeared) {
            if (name.contains(u"xgs-check-download"_s, Qt::CaseInsensitive) || name.endsWith(u".crdownload"_s, Qt::CaseInsensitive))
                ours.append(name);
        }
        if (appeared.size() > ours.size()) qInfo("other new entries in Downloads (not ours): %s", qPrintable(appeared.join(u", "_s)));
        QVERIFY2(ours.isEmpty(), qPrintable(u"in Downloads: "_s + ours.join(u", "_s)));
        const QStringList inFolder = filesNamed(m_dir->path(), u"xgs-check-download"_s) + filesNamed(m_dir->path(), u"Unconfirmed"_s);
        QVERIFY2(inFolder.isEmpty(), qPrintable(inFolder.join(u", "_s)));
        QVERIFY(filesNamed(QDir::tempPath(), u"xgs-check-download"_s).isEmpty());
    }

    void aPopupIsNotOpened() {
        HiddenWebView view(options());
        QVERIFY(load(view, url(u"/popup"_s)).outcome.value_or(PageOutcome{}).ok);
        // window.open runs 300 ms after the page loaded, the download link 600 ms after; once that is requested, the popup had
        // its chance a while ago, and one more second goes by for a slow popup request.
        QTRY_VERIFY_WITH_TIMEOUT(m_server->countOf(u"/download.bin"_s) > 0, 10000);
        QTest::qWait(1000);

        // WebView2 blocks a window.open that no click asked for before NewWindowRequested is raised, so this page cannot get a
        // popup that far: the refusal in HiddenWebView (NewWindowRequested handled with no new window) is not exercised here,
        // only that no popup opens (as in browser_check.rs).
        QCOMPARE(m_server->countOf(u"/popup-child"_s), 0);
        QCOMPARE(view.windowsMade(), 1);
    }

    void aNavigationOutsideTheAllowedAddressesIsRefused() {
        HiddenWebView view(options());
        const QString first = url(u"/first"_s);
        QVERIFY(load(view, first).outcome.value_or(PageOutcome{}).ok);
        QVERIFY(waitForCard(view).value(u"card"_s).toBool());

        // The page itself tries to go elsewhere. file:// from an http page is also refused by the browser itself, and ftp:// is
        // no scheme it loads; the blob: address is one the browser would open (no network involved), so only the window's
        // navigation rule keeps the page where it is.
        const QStringList attempts{
            u"location.href = 'file:///C:/'; 1"_s,
            u"location.href = 'ftp://127.0.0.1/'; 1"_s,
            u"location.href = URL.createObjectURL(new Blob(['<p>blob page</p>'], { type: 'text/html' })); 1"_s,
        };
        for (const QString& attempt : attempts) {
            const Waited tried = waitForPage([&](auto done) { view.evaluate(attempt, std::move(done)); }, 8000);
            QVERIFY2(tried.outcome && tried.outcome->ok, qPrintable(attempt));
            QTest::qWait(1000);
            const QJsonObject seen = look(view);
            QVERIFY2(seen.value(u"url"_s).toString() == first, qPrintable(attempt + u" -> "_s + seen.value(u"url"_s).toString()));
        }

        // A load the rule refuses is answered without going anywhere.
        for (const QString& address : {u"file:///C:/"_s, u"ftp://127.0.0.1/"_s, u"http://127.0.0.2/x"_s}) {
            const Waited refused = load(view, address);
            QVERIFY(refused.outcome);
            QVERIFY(!refused.outcome->ok);
            QCOMPARE(refused.outcome->text, u"url not allowed"_s);
            QCOMPARE(refused.calls, 1);
        }
        QVERIFY(view.hasWindow());
        QCOMPARE(look(view).value(u"url"_s).toString(), first);
    }

    void aHungPageAnswersScriptTimedOutAfterFiveSeconds() {
        HiddenWebView view(options());
        QVERIFY(load(view, url(u"/hang"_s)).outcome.value_or(PageOutcome{}).ok);
        QTest::qWait(1500);   // the page starts its endless loop 300 ms after it loaded

        QElapsedTimer timer;
        timer.start();
        const Waited answered = waitForPage([&](auto done) { view.evaluate(u"1 + 1"_s, std::move(done)); }, 15000);
        const qint64 waited = timer.elapsed();
        qInfo("gave up after %lld ms", waited);

        QVERIFY(answered.outcome);
        QVERIFY(!answered.outcome->ok);
        QCOMPARE(answered.outcome->text, u"script timed out"_s);
        QCOMPARE(answered.calls, 1);
        // The limit is five seconds; eight leaves room for a slow event loop (browser_check.rs's bound).
        QVERIFY2(waited >= 4500 && waited <= 8000, qPrintable(QString::number(waited)));
    }

    void aBrowserProcessThatDiesIsReplaced() {
        HiddenWebView view(options());
        QVERIFY(load(view, url(u"/first"_s)).outcome.value_or(PageOutcome{}).ok);
        QVERIFY(waitForCard(view).value(u"card"_s).toBool());
        const std::optional<quint32> pid = view.browserProcessId();
        QVERIFY(pid);

        // Ends only the browser process of this view, and only after checking through the same handle that its command line
        // holds this test's own user-data folder.
        const HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, *pid);
        QVERIFY(process);
        const QString commandLine = xgs::testsupport::commandLineOf(process);
        const bool ours = commandLine.contains(QDir::toNativeSeparators(m_dir->path()), Qt::CaseInsensitive);
        if (ours) {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, 10000);
        }
        CloseHandle(process);
        QVERIFY2(ours, "the browser process of the view does not hold the test's user-data folder");

        const Waited after = waitForPage([&](auto done) { view.evaluate(u"1 + 1"_s, std::move(done)); }, 10000);
        QVERIFY(after.outcome);
        QVERIFY(!after.outcome->ok);
        qInfo("evaluate after the browser process died: %s", qPrintable(after.outcome->text));
        // ProcessFailed closes the window.
        QTRY_VERIFY_WITH_TIMEOUT(!view.hasWindow(), 10000);

        view.close();
        QVERIFY(load(view, url(u"/second"_s)).outcome.value_or(PageOutcome{}).ok);
        QCOMPARE(view.windowsMade(), 2);
        QVERIFY(waitForCard(view).value(u"text"_s).toString().contains(u"card of second"_s));
    }

    void destroyingTheViewAnswersWhatIsPendingWithClosed() {
        // A load whose window is still being made.
        auto calls = std::make_shared<int>(0);
        auto got = std::make_shared<PageOutcome>();
        auto view = std::make_unique<HiddenWebView>(options());
        view->load(url(u"/first"_s), [calls, got](PageOutcome outcome) {
            ++*calls;
            *got = outcome;
        });
        QCOMPARE(*calls, 0);
        view.reset();
        QCOMPARE(*calls, 1);
        QVERIFY(!got->ok);
        QCOMPARE(got->text, u"closed"_s);

        // A script that waits on a page that never answers.
        view = std::make_unique<HiddenWebView>(options());
        QVERIFY(load(*view, url(u"/hang"_s)).outcome.value_or(PageOutcome{}).ok);
        QTest::qWait(1500);
        auto scriptCalls = std::make_shared<int>(0);
        auto scriptGot = std::make_shared<PageOutcome>();
        view->evaluate(u"1 + 1"_s, [scriptCalls, scriptGot](PageOutcome outcome) {
            ++*scriptCalls;
            *scriptGot = outcome;
        });
        QTest::qWait(200);
        QCOMPARE(*scriptCalls, 0);
        view.reset();
        QCOMPARE(*scriptCalls, 1);
        QVERIFY(!scriptGot->ok);
        QCOMPARE(scriptGot->text, u"closed"_s);

        // Nothing more comes afterwards (the five-second script limit included).
        QTest::qWait(6000);
        QCOMPARE(*calls, 1);
        QCOMPARE(*scriptCalls, 1);
    }

    void withoutARuntimeLoadFailsAndShowsNothing() {
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        // Sixty seconds: the child answers in well under a second without a runtime.
        const ChildRun run = runNoRuntimeChild(empty.path(), m_dir->path(), 60000);
        qInfo("child: %s", qUtf8Printable(run.output.trimmed()));

        QCOMPARE(run.exitCode, 0);
        QVERIFY2(run.output.contains(u"runtime=none answered=1 calls=1 ok=0 hasWindow=0 text="_s), qPrintable(run.output));
        QVERIFY2(run.output.contains(u" visible=0"_s), qPrintable(run.output));
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (const std::optional<QString> folder = childUserDataFolder()) return childMain(*folder);
    HiddenWebViewTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_hidden_web_view.moc"
