#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include "net/http_fetch.h"
#include "net/qt_http_client.h"
#include "paths/paths.h"
#include "support/fake_http_server.h"
#include "support/fake_process_runner.h"
#include "tools/tool_manager.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::Outcome;
using xgs::OutcomeStatus;
using xgs::ToolPaths;
using xgs::net::QtHttpClient;
using xgs::testing::FakeHttpServer;
using xgs::tools::Fetch;
using xgs::tools::ToolManager;
using xgs::tools::ToolPathProvider;
using Clock = std::chrono::steady_clock;

namespace {

// Stands in for a server that sleeps (see tst_qt_http_client.cpp): declared after the server, it is opened first on teardown.
class Gate {
public:
    ~Gate() {
        open();
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [this] { return m_waiting == 0; });
    }
    void open() {
        {
            std::lock_guard lock(m_mutex);
            m_open = true;
        }
        m_changed.notify_all();
    }
    void wait(std::chrono::milliseconds limit) {
        std::unique_lock lock(m_mutex);
        ++m_waiting;
        m_changed.wait_for(lock, limit, [this] { return m_open; });
        --m_waiting;
        m_changed.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_open = false;
    int m_waiting = 0;
};

bool waitForSeen(const FakeHttpServer& server, qsizetype count) {
    const auto deadline = Clock::now() + 5s;
    while (server.seen().size() < count) {
        if (Clock::now() > deadline) return false;
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

// Bytes 0..255 in a pattern that shifts every 256 bytes, so a wrong or shifted body cannot compare equal by accident.
QByteArray patternedBytes(qsizetype size) {
    QByteArray bytes(size, '\0');
    for (qsizetype i = 0; i < size; ++i) bytes[i] = static_cast<char>((i * 7 + i / 256) & 0xFF);
    return bytes;
}

class FixedTools final : public ToolPathProvider {
public:
    explicit FixedTools(QString binDir) : m_binDir(std::move(binDir)) {}
    ToolPaths current() const override {
        const QString ytDlp = *xgs::paths::child(m_binDir, u"yt-dlp.exe"_s);
        return ToolPaths{QFileInfo::exists(ytDlp) ? std::optional<QString>(ytDlp) : std::nullopt, std::nullopt, std::nullopt};
    }

private:
    QString m_binDir;
};

// The manager always asks for GitHub's URL: the test points the same fetch at the fake server instead.
Fetch redirectedTo(const QString& url, Fetch inner) {
    return [url, inner = std::move(inner)](const QString&, std::stop_token stop) { return inner(url, stop); };
}

}  // namespace

class HttpFetchTest : public QObject {
    Q_OBJECT
private slots:
    void aSuccessfulDownloadReturnsTheBytes() {
        FakeHttpServer server;
        const QByteArray body = patternedBytes(3 * 1024 * 1024);
        server.route("/f", [&](FakeHttpServer::Exchange& exchange) { exchange.reply(200, body, "application/octet-stream"); });
        QtHttpClient client;

        const Outcome<QByteArray> result = xgs::net::httpFetch(client)(server.base() + u"/f"_s, {});
        QCOMPARE(result.status, OutcomeStatus::Ok);
        QVERIFY(result.value == body);
    }

    void anyStatusButOkIsAFailureThatNamesTheStatus() {
        FakeHttpServer server;
        for (const int code : {404, 500, 204}) {
            server.route("/s" + QByteArray::number(code), [code](FakeHttpServer::Exchange& exchange) {
                exchange.reply(code, code == 204 ? QByteArray() : QByteArray("body"));
            });
        }
        QtHttpClient client;
        const Fetch fetch = xgs::net::httpFetch(client);

        for (const int code : {404, 500, 204}) {
            const Outcome<QByteArray> result = fetch(server.base() + u"/s"_s + QString::number(code), {});
            QCOMPARE(result.status, OutcomeStatus::Failed);
            QCOMPARE(result.error, u"HTTP "_s + QString::number(code));
        }
    }

    void aRedirectToTheFileIsFollowed() {
        FakeHttpServer server;
        server.route("/latest/download/file", [](FakeHttpServer::Exchange& exchange) {
            exchange.write("HTTP/1.1 302 Found\r\nLocation: /releases/file\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        });
        server.route("/releases/file", [](FakeHttpServer::Exchange& exchange) {
            exchange.reply(200, "payload", "application/octet-stream");
        });
        QtHttpClient client;

        const Outcome<QByteArray> result = xgs::net::httpFetch(client)(server.base() + u"/latest/download/file"_s, {});
        QCOMPARE(result.status, OutcomeStatus::Ok);
        QCOMPARE(*result.value, QByteArray("payload"));
    }

    void aRefusedConnectionIsAFailureWithAText() {
        FakeHttpServer server;
        const QString url = server.base() + u"/f"_s;
        server.stop();
        QtHttpClient client;

        const Outcome<QByteArray> result = xgs::net::httpFetch(client)(url, {});
        QCOMPARE(result.status, OutcomeStatus::Failed);
        QVERIFY(!result.error.isEmpty());
    }

    void aDownloadThatStallsFailsAtTheIdleTimeout() {
        FakeHttpServer server;
        Gate gate;
        server.route("/stall", [&](FakeHttpServer::Exchange& exchange) {
            exchange.sendHeaders(200, false, 100);
            exchange.write("x");
            gate.wait(5s);
        });
        QtHttpClient client;

        const auto start = Clock::now();
        const Outcome<QByteArray> result = xgs::net::httpFetch(client, 1s)(server.base() + u"/stall"_s, {});
        const auto took = Clock::now() - start;
        QCOMPARE(result.status, OutcomeStatus::Failed);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(took < 4s);
    }

    void aBodyLargerThanTheLimitIsAFailure() {
        FakeHttpServer server;
        server.route("/big", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, QByteArray(1001, 'b')); });
        server.route("/fits", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, QByteArray(1000, 'b')); });
        QtHttpClient client;
        const Fetch fetch = xgs::net::httpFetch(client, xgs::net::FetchIdleTimeout, 1000);

        const Outcome<QByteArray> big = fetch(server.base() + u"/big"_s, {});
        QCOMPARE(big.status, OutcomeStatus::Failed);
        QCOMPARE(big.error, u"내려받는 파일이 너무 큽니다."_s);
        QCOMPARE(fetch(server.base() + u"/fits"_s, {}).status, OutcomeStatus::Ok);
    }

    void stoppingTheDownloadIsACancel() {
        FakeHttpServer server;
        // Declared before the client: on an early return the client goes first and ends the call, so this does not block.
        std::future<Outcome<QByteArray>> call;
        Gate gate;
        server.route("/sleep", [&](FakeHttpServer::Exchange& exchange) {
            gate.wait(10s);
            exchange.reply(200, "late");
        });
        QtHttpClient client;
        std::stop_source stop;
        const Fetch fetch = xgs::net::httpFetch(client);
        const QString url = server.base() + u"/sleep"_s;
        call = std::async(std::launch::async, [&] { return fetch(url, stop.get_token()); });
        QVERIFY(waitForSeen(server, 1));

        const auto start = Clock::now();
        stop.request_stop();
        QVERIFY(call.wait_for(3s) == std::future_status::ready);
        QVERIFY(Clock::now() - start < 3s);
        QCOMPARE(call.get().status, OutcomeStatus::Cancelled);
    }

    void theAppUserAgentIsSent() {
        FakeHttpServer server;
        server.route("/f", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "x"); });
        QtHttpClient client;

        QVERIFY(xgs::net::httpFetch(client)(server.base() + u"/f"_s, {}).isOk());
        const QList<FakeHttpServer::Seen> seen = server.seen();
        QCOMPARE(seen.size(), 1);
        QCOMPARE(QString::fromUtf8(seen[0].userAgent), xgs::net::AppUserAgent.toString());
    }

    void installYtDlpDownloadsThroughTheFetchAndInstalls() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString binDir = *xgs::paths::fromText(temp.path());
        const QByteArray body = patternedBytes(1'000'000);
        FakeHttpServer server;
        server.route("/yt-dlp.exe", [&](FakeHttpServer::Exchange& exchange) {
            exchange.reply(200, body, "application/octet-stream");
        });
        QtHttpClient client;
        xgs::testsupport::FakeProcessRunner runner(
            [](const QStringList&, const xgs::process::LineHandler& out, const xgs::process::LineHandler&) {
                out(u"2026.01.01"_s);
                return 0;
            });
        const FixedTools tools(binDir);
        ToolManager manager(tools, runner, binDir,
                            redirectedTo(server.base() + u"/yt-dlp.exe"_s, xgs::net::httpFetch(client)));

        const Outcome<QString> result = manager.installYtDlp({});
        QVERIFY2(result.isOk(), qPrintable(result.error));
        QFile installed(*xgs::paths::child(binDir, u"yt-dlp.exe"_s));
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QVERIFY(installed.readAll() == body);
        installed.close();
        QVERIFY(!QFileInfo::exists(*xgs::paths::child(binDir, u"yt-dlp.new.exe"_s)));
    }

    void installYtDlpReportsAFailedDownloadWithTheKoreanPrefix() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString binDir = *xgs::paths::fromText(temp.path());
        FakeHttpServer server;   // no route: 404
        QtHttpClient client;
        xgs::testsupport::FakeProcessRunner runner(
            [](const QStringList&, const xgs::process::LineHandler&, const xgs::process::LineHandler&) { return 1; });
        const FixedTools tools(binDir);
        ToolManager manager(tools, runner, binDir,
                            redirectedTo(server.base() + u"/yt-dlp.exe"_s, xgs::net::httpFetch(client)));

        const Outcome<QString> result = manager.installYtDlp({});
        QCOMPARE(result.status, OutcomeStatus::Failed);
        QCOMPARE(result.error, u"yt-dlp 다운로드에 실패했습니다: HTTP 404"_s);
    }
};

QTEST_GUILESS_MAIN(HttpFetchTest)
#include "tst_http_fetch.moc"
