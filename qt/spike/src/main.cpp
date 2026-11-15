#include "hidden_webview.h"
#include "job_process.h"
#include "report.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QSslSocket>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include <windows.h>

namespace {

// Milliseconds since this process was created (GetProcessTimes creation time).
qint64 msSinceProcessStart()
{
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimeAsFileTime(&now);
    auto toU64 = [](const FILETIME &f) {
        return (static_cast<quint64>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return static_cast<qint64>((toU64(now) - toU64(created)) / 10000); // 100 ns ticks
}

// Measures: first Show of the window, then the first Paint of it (or a child), then the
// first 0 ms timer after that (event loop idle). Then writes "start" and quits.
class StartProbe : public QObject
{
public:
    explicit StartProbe(QWidget *window) : m_window(window) {}

    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (m_done || !obj->isWidgetType())
            return false;
        auto *w = static_cast<QWidget *>(obj);
        if (w->window() != m_window)
            return false;
        if (ev->type() == QEvent::Show && w == m_window)
            m_sawShow = true;
        else if (ev->type() == QEvent::Paint && m_sawShow && m_shownMs < 0) {
            m_shownMs = msSinceProcessStart();
            QTimer::singleShot(0, this, [this] {
                const qint64 idleMs = msSinceProcessStart();
                m_done = true;
                Report::put("start", QJsonObject{
                    {"processStartToShownMs", static_cast<int>(m_shownMs)},
                    {"processStartToIdleMs", static_cast<int>(idleMs)},
                });
                QApplication::quit();
            });
        }
        return false;
    }

private:
    QWidget *m_window;
    bool m_sawShow = false;
    bool m_done = false;
    qint64 m_shownMs = -1;
};

// --webview-check <url>: opens <url> in a HiddenWebView, then every 200 ms evaluates the
// card selector; writes "webview" and quits once it is not null, on failure, or after 10 s.
// The user data folder is %LOCALAPPDATA%\xgs-qt-spike\webview.
std::unique_ptr<HiddenWebView> startWebviewCheck(QApplication &app, const QUrl &url)
{
    auto finished = std::make_shared<bool>(false);
    auto finish = [finished](const QJsonObject &result) {
        if (*finished)
            return;
        *finished = true;
        Report::put("webview", result);
        QApplication::quit();
    };

    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (local.isEmpty()) {
        QTimer::singleShot(0, &app, [finish] {
            finish(QJsonObject{{"ok", false}, {"error", "LOCALAPPDATA is not set"}});
        });
        return nullptr;
    }
    const QString folder = QDir(local).filePath("xgs-qt-spike/webview");
    QDir().mkpath(folder);

    auto view = std::make_unique<HiddenWebView>(QDir::toNativeSeparators(folder));
    HiddenWebView *v = view.get();
    auto sinceOpen = std::make_shared<QElapsedTimer>();
    auto *poll = new QTimer(v);
    poll->setInterval(200);

    // Guard for the environment/controller creation itself (before the 10 s page limit).
    QTimer::singleShot(30000, v, [finish, sinceOpen] {
        if (!sinceOpen->isValid())
            finish(QJsonObject{{"ok", false}, {"error", "timeout before ready"}});
    });

    QObject::connect(v, &HiddenWebView::failed, v, [finish](const QString &error) {
        finish(QJsonObject{{"ok", false}, {"error", error}});
    });
    QObject::connect(v, &HiddenWebView::ready, v, [v, url, poll, sinceOpen, finish] {
        sinceOpen->start();
        v->open(url);
        poll->start();
        QTimer::singleShot(10000, v, [finish] {
            finish(QJsonObject{{"ok", false}, {"error", "timeout"}});
        });
    });
    QObject::connect(v, &HiddenWebView::navigated, v, [finish](bool success) {
        if (!success)
            finish(QJsonObject{{"ok", false}, {"error", "navigation failed"}});
    });
    QObject::connect(poll, &QTimer::timeout, v, [v] {
        v->evaluate("({ text: document.querySelector('div[data-lyricid]')?.textContent ?? null,"
                    " ticks: window.__ticks ?? null })");
    });
    QObject::connect(v, &HiddenWebView::evaluated, v, [finish, sinceOpen, poll](const QString &json) {
        const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
        if (!o.value("text").isString())
            return; // not there yet (or the page is still loading)
        poll->stop();
        finish(QJsonObject{
            {"ok", true},
            {"text", o.value("text").toString()},
            {"ticks", o.value("ticks").toInt()},
            {"waitedMs", sinceOpen->elapsed()},
        });
    });
    return view;
}

// --tls-check: one GET to lrclib.net over Qt's TLS backend (Schannel), 15 s limit;
// writes "tls" and quits.
void startTlsCheck(QApplication &app)
{
    auto *nam = new QNetworkAccessManager(&app);
    QNetworkRequest request(QUrl("https://lrclib.net/api/search?track_name=test&artist_name=test"));
    request.setHeader(QNetworkRequest::UserAgentHeader, "xgs-qt-spike");
    request.setTransferTimeout(15000);
    QNetworkReply *reply = nam->get(request);
    QObject::connect(reply, &QNetworkReply::finished, &app, [reply] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonObject result{
            {"ok", reply->error() == QNetworkReply::NoError && status == 200},
            {"status", status},
            {"backend", QSslSocket::activeBackend()},
        };
        if (reply->error() != QNetworkReply::NoError)
            result.insert("error", reply->errorString());
        Report::put("tls", result);
        reply->deleteLater();
        QApplication::quit();
    });
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Command line contract: xgs-qt-spike.exe [--report <file>] [one switch]
    //   switches: --exit-after-show | --spawn-tree <seconds> | --spawn-cmd <command line>
    //   --cancel-after <ms> goes with --spawn-tree / --spawn-cmd
    //   --webview-check <url> | --tls-check (these two show no window)
    // Later tasks add switches by extending this list (and the dispatch below).
    const QStringList args = app.arguments().mid(1);
    QString mode; // empty = plain window
    int spawnTreeSec = -1;  // --spawn-tree <seconds>
    QString spawnCmd;       // --spawn-cmd <command line>
    int cancelAfterMs = -1; // --cancel-after <ms> (with --spawn-tree / --spawn-cmd)
    QUrl webviewUrl;        // --webview-check <url>
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == "--report" && i + 1 < args.size())
            Report::setPath(args[++i]);
        else if (a == "--exit-after-show")
            mode = a;
        else if (a == "--spawn-tree" && i + 1 < args.size()) {
            mode = a;
            spawnTreeSec = args[++i].toInt();
        } else if (a == "--spawn-cmd" && i + 1 < args.size()) {
            mode = a;
            spawnCmd = args[++i];
        } else if (a == "--cancel-after" && i + 1 < args.size())
            cancelAfterMs = args[++i].toInt();
        else if (a == "--webview-check" && i + 1 < args.size()) {
            mode = a;
            webviewUrl = QUrl(args[++i]);
        } else if (a == "--tls-check")
            mode = a;
    }

    // Checks without a window: nothing of the spike appears on screen.
    if (mode == "--webview-check") {
        std::unique_ptr<HiddenWebView> view = startWebviewCheck(app, webviewUrl);
        const int rc = app.exec();
        view.reset(); // closes the controller while QApplication still exists
        return rc;
    }
    if (mode == "--tls-check") {
        startTlsCheck(app);
        return app.exec();
    }

    QMainWindow window;
    window.setWindowTitle("xgs-qt-spike");

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(new QLineEdit);
    layout->addWidget(new QPlainTextEdit);
    window.setCentralWidget(central);
    window.resize(640, 480);

    if (mode == "--exit-after-show")
        app.installEventFilter(new StartProbe(&window));

    window.show();

    // --spawn-tree / --spawn-cmd: run a child process tree in a Job Object, optionally cancel
    // it after --cancel-after ms, and quit once the child has finished.
    JobProcess job;
    QElapsedTimer sinceSpawn;
    bool childRunning = false;
    bool cancelled = false;
    if (mode == "--spawn-tree" || mode == "--spawn-cmd") {
        QObject::connect(&job, &JobProcess::finished, &app, [&](int exitCode) {
            childRunning = false;
            Report::put("finished", QJsonObject{
                {"exitCode", exitCode},
                {"afterMs", static_cast<qint64>(sinceSpawn.isValid() ? sinceSpawn.elapsed() : -1)},
                {"cancelled", cancelled},
            });
            QApplication::quit();
        });

        QString program;
        QStringList childArgs;
        if (mode == "--spawn-tree") {
            // cmd.exe -> (start /b) PING.EXE, and cmd.exe -> cmd.exe -> PING.EXE: two branches,
            // three levels. Nothing here ends early: ping -n N+1 runs about N seconds.
            const QString ping = QString("ping -n %1 127.0.0.1").arg(spawnTreeSec + 1);
            program = "cmd.exe";
            childArgs = {"/d", "/c", QString("start /b %1 & cmd.exe /d /c %1").arg(ping)};
        } else {
            childArgs = QProcess::splitCommand(spawnCmd);
            if (!childArgs.isEmpty())
                program = childArgs.takeFirst();
        }

        if (program.isEmpty() || !job.start(program, childArgs)) {
            Report::put("spawned", QJsonObject{{"error", "start failed"}});
            return 2;
        }
        sinceSpawn.start();
        childRunning = true;
        Report::put("spawned", QJsonObject{{"pid", job.processId()}});

        if (cancelAfterMs >= 0) {
            QTimer::singleShot(cancelAfterMs, Qt::PreciseTimer, &job, [&] {
                if (!childRunning)
                    return; // already finished on its own: "finished" without "cancelled"
                cancelled = true;
                job.cancel();
                Report::put("cancelled", QJsonObject{
                    {"afterMs", sinceSpawn.elapsed()},
                    {"atEpochMs", QDateTime::currentMSecsSinceEpoch()}, // same clock as the check script
                });
            });
        }
    }

    return app.exec();
}
