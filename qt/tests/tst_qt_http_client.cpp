#include <QtTest>

#include <QSslSocket>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "support/fake_http_server.h"
#include "net/qt_http_client.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::net::HttpRequest;
using xgs::net::HttpResponse;
using xgs::net::HttpStatus;
using xgs::net::QtHttpClient;
using xgs::testing::FakeHttpServer;
using Clock = std::chrono::steady_clock;

namespace {

// Stands in for a server that sleeps: a handler waits on it for at most `limit`. The test (or the destructor, which runs
// before the server's when the gate is declared after the server) opens it so the server's threads end at once. A handler
// captures a Waiter by value, which shares the gate's state: a handler that was dispatched but has not reached wait() when
// the Gate is destroyed still waits on live state (the server holds the handler until its threads have ended).
class Gate {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        bool open = false;
    };

public:
    class Waiter {
    public:
        explicit Waiter(std::shared_ptr<State> state) : m_state(std::move(state)) {}
        void wait(std::chrono::milliseconds limit) const {
            std::unique_lock lock(m_state->mutex);
            m_state->changed.wait_for(lock, limit, [this] { return m_state->open; });
        }

    private:
        std::shared_ptr<State> m_state;
    };

    Gate() = default;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    ~Gate() { open(); }
    void open() {
        {
            std::lock_guard lock(m_state->mutex);
            m_state->open = true;
        }
        m_state->changed.notify_all();
    }
    Waiter waiter() const { return Waiter(m_state); }

private:
    std::shared_ptr<State> m_state = std::make_shared<State>();
};

HttpRequest requestTo(const FakeHttpServer& server, const QString& pathAndQuery) {
    HttpRequest request;
    request.url = server.base() + pathAndQuery;
    return request;
}

// Waits until the server has seen at least `count` requests (at most 5 s).
bool waitForSeen(const FakeHttpServer& server, qsizetype count) {
    const auto deadline = Clock::now() + 5s;
    while (server.seen().size() < count) {
        if (Clock::now() > deadline) return false;
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

quint16 portOf(const FakeHttpServer& server) {
    const QString base = server.base();
    return static_cast<quint16>(base.mid(base.lastIndexOf(u':') + 1).toUShort());
}

class Winsock {
public:
    Winsock() {
        WSADATA data;
        m_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~Winsock() {
        if (m_ok) WSACleanup();
    }
    bool ok() const { return m_ok; }

private:
    bool m_ok = false;
};

// A blocking TCP connection to 127.0.0.1:port; INVALID_SOCKET (with WSAGetLastError set) when it fails.
SOCKET connectToLoopback(quint16 port) {
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) return socket;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof address) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        ::closesocket(socket);
        WSASetLastError(error);
        return INVALID_SOCKET;
    }
    return socket;
}

const char* statusName(HttpStatus status) {
    switch (status) {
    case HttpStatus::Response: return "Response";
    case HttpStatus::Unreachable: return "Unreachable";
    case HttpStatus::Failed: return "Failed";
    case HttpStatus::Cancelled: return "Cancelled";
    }
    return "?";
}

}  // namespace

namespace QTest {
template <>
char* toString(const HttpStatus& status) {
    return qstrdup(statusName(status));
}
}  // namespace QTest

class QtHttpClientTest : public QObject {
    Q_OBJECT
private slots:
    void theFakeServerAnswersARawSocketRequest() {
        FakeHttpServer server;
        server.route("/x", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "hello", "text/plain"); });
        Winsock winsock;
        QVERIFY(winsock.ok());
        const SOCKET socket = connectToLoopback(portOf(server));
        QVERIFY(socket != INVALID_SOCKET);
        const QByteArray request = "GET /x?a=%20b HTTP/1.1\r\nHost: 127.0.0.1\r\nUser-Agent: raw\r\n\r\n";
        QCOMPARE(::send(socket, request.constData(), static_cast<int>(request.size()), 0), static_cast<int>(request.size()));
        QByteArray response;
        char buffer[4096];
        for (int got; (got = ::recv(socket, buffer, sizeof buffer, 0)) > 0;) response.append(buffer, got);
        ::closesocket(socket);

        QVERIFY2(response.startsWith("HTTP/1.1 200 "), response.constData());
        QVERIFY2(response.endsWith("\r\n\r\nhello"), response.constData());
        QVERIFY(response.contains("\r\nConnection: close\r\n"));
        const QList<FakeHttpServer::Seen> seen = server.seen();
        QCOMPARE(seen.size(), 1);
        QCOMPARE(seen[0].method, "GET");
        QCOMPARE(seen[0].path, "/x");
        QCOMPARE(seen[0].rawQuery, "a=%20b");
        QCOMPARE(seen[0].userAgent, "raw");
    }

    void aStoppedFakeServerRefusesConnections() {
        FakeHttpServer server;
        const quint16 port = portOf(server);
        server.stop();
        Winsock winsock;
        QVERIFY(winsock.ok());
        const SOCKET socket = connectToLoopback(port);
        const int error = WSAGetLastError();
        if (socket != INVALID_SOCKET) ::closesocket(socket);
        QVERIFY(socket == INVALID_SOCKET);
        QCOMPARE(error, WSAECONNREFUSED);
    }

    void aGetReturnsTheStatusAndTheBody() {
        FakeHttpServer server;
        const QByteArray lyrics = u"가사 첫 줄\n둘째 줄 — ♪"_s.toUtf8();
        server.route("/ok", [&](FakeHttpServer::Exchange& exchange) { exchange.reply(200, lyrics); });
        server.route("/missing", [](FakeHttpServer::Exchange& exchange) { exchange.reply(404, R"({"code":404})"); });
        QtHttpClient client;

        const HttpResponse ok = client.get(requestTo(server, u"/ok"_s), {});
        QCOMPARE(ok.status, HttpStatus::Response);
        QCOMPARE(ok.code, 200);
        QCOMPARE(ok.body, lyrics);
        QVERIFY(!ok.bodyTooLarge);

        const HttpResponse missing = client.get(requestTo(server, u"/missing"_s), {});
        QCOMPARE(missing.status, HttpStatus::Response);
        QCOMPARE(missing.code, 404);
        QCOMPARE(missing.body, R"({"code":404})");
    }

    void theHeadersOfTheRequestAreSent() {
        FakeHttpServer server;
        server.route("/h", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "{}"); });
        QtHttpClient client;
        HttpRequest request = requestTo(server, u"/h"_s);
        request.headers = {{"User-Agent", "xGetSongs-test/1.0 (+https://example.invalid/x; a=b)"},
                           {"Accept", "application/json"}};

        const HttpResponse response = client.get(request, {});
        QCOMPARE(response.status, HttpStatus::Response);
        const QList<FakeHttpServer::Seen> seen = server.seen();
        QCOMPARE(seen.size(), 1);
        QCOMPARE(seen[0].method, "GET");
        QCOMPARE(seen[0].userAgent, "xGetSongs-test/1.0 (+https://example.invalid/x; a=b)");
        QCOMPARE(seen[0].accept, "application/json");
    }

    void theQueryStringReachesTheServerByteForByte() {
        FakeHttpServer server;
        server.route("/q", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "{}"); });
        QtHttpClient client;
        const auto rawQueryOf = [&](const QString& pathAndQuery) {
            server.clearSeen();
            const HttpResponse response = client.get(requestTo(server, pathAndQuery), {});
            const QList<FakeHttpServer::Seen> seen = server.seen();
            if (response.status != HttpStatus::Response || seen.size() != 1 || seen[0].path != "/q")
                return QByteArray("<not one request to /q>");
            return seen[0].rawQuery;
        };

        // Every byte arrives as written, except the one rewrite QUrl always makes: a percent-encoded unreserved character
        // (RFC 3986 2.3: A-Z a-z 0-9 - . _ ~) arrives decoded, so %7E arrives as ~ and %41 as A.
        QCOMPARE(rawQueryOf(u"/q?q=%7E%2A%27%28%29*~.-_%20%2B+%ED%95%9C&x="_s), "q=~%2A%27%28%29*~.-_%20%2B+%ED%95%9C&x=");
        QCOMPARE(rawQueryOf(u"/q?q=%2A%27%28%29*~.-_%20%2B+%ED%95%9C&x="_s), "q=%2A%27%28%29*~.-_%20%2B+%ED%95%9C&x=");
        QCOMPARE(rawQueryOf(u"/q?q=%41%2D%2e%5F%7e&x="_s), "q=A-._~&x=");
        // Two more rewrites a caller could trip over: lower-case hex digits arrive upper-case, and a raw non-ASCII
        // character arrives as its UTF-8 bytes percent-encoded.
        QCOMPARE(rawQueryOf(u"/q?q=%ed%95%9c%2f%3f"_s), "q=%ED%95%9C%2F%3F");
        QCOMPARE(rawQueryOf(u"/q?q=한"_s), "q=%ED%95%9C");

        // The path is not sent as written either: Qt removes dot segments (also percent-encoded ones, since QUrl decodes
        // %2E first) and sends an empty path as "/".
        server.route("/", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "{}"); });
        server.route("/a/b", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "{}"); });
        const auto targetOf = [&](const QString& pathAndQuery) {
            server.clearSeen();
            const HttpResponse response = client.get(requestTo(server, pathAndQuery), {});
            const QList<FakeHttpServer::Seen> seen = server.seen();
            if (response.status != HttpStatus::Response || seen.size() != 1) return QByteArray("<not one request>");
            return seen[0].path + '?' + seen[0].rawQuery;
        };
        QCOMPARE(targetOf(u"/a/../q?x=1"_s), "/q?x=1");
        QCOMPARE(targetOf(u"/a/%2E%2E/q?x=1"_s), "/q?x=1");
        QCOMPARE(targetOf(u"/a/./b"_s), "/a/b?");
        QCOMPARE(targetOf(QString()), "/?");
        QCOMPARE(targetOf(u"?x=1"_s), "/?x=1");
    }

    void onlyTheseHeadersAreSentBesidesTheCallersOwn() {
        FakeHttpServer server;
        server.route("/h", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "{}"); });
        QtHttpClient client;
        HttpRequest request = requestTo(server, u"/h"_s);
        request.headers = {{"User-Agent", "xGetSongs-test/1.0"}};

        QCOMPARE(client.get(request, {}).status, HttpStatus::Response);
        QCOMPARE(server.seen().size(), 1);
        // Accept-Language is the client's fixed value, not one built from the system locale.
        QList<std::pair<QByteArray, QByteArray>> expected = {{"Host", "127.0.0.1:" + QByteArray::number(portOf(server))},
                                                             {"User-Agent", "xGetSongs-test/1.0"},
                                                             {"Accept-Language", "en"},
                                                             {"Connection", "Keep-Alive"},
                                                             {"Accept-Encoding", "gzip, deflate"}};
        QList<std::pair<QByteArray, QByteArray>> sent = server.seen()[0].headers;
        std::sort(expected.begin(), expected.end());
        std::sort(sent.begin(), sent.end());
        QCOMPARE(sent, expected);

        // A caller's own Accept-Language and Accept-Encoding replace the client's and Qt's.
        server.clearSeen();
        request.headers = {{"Accept-Language", "ko"}, {"Accept-Encoding", "identity"}};
        QCOMPARE(client.get(request, {}).status, HttpStatus::Response);
        QByteArrayList languages;
        QByteArrayList encodings;
        for (const auto& [name, value] : server.seen().value(0).headers) {
            if (name.compare("Accept-Language", Qt::CaseInsensitive) == 0) languages.append(value);
            if (name.compare("Accept-Encoding", Qt::CaseInsensitive) == 0) encodings.append(value);
        }
        QCOMPARE(languages, QByteArrayList{"ko"});
        QCOMPARE(encodings, QByteArrayList{"identity"});
    }

    void aRedirectIsFollowed() {
        FakeHttpServer server;
        server.route("/from", [](FakeHttpServer::Exchange& exchange) {
            exchange.write("HTTP/1.1 302 Found\r\nLocation: /to\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        });
        server.route("/to", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "arrived"); });
        QtHttpClient client;

        const HttpResponse response = client.get(requestTo(server, u"/from"_s), {});
        QCOMPARE(response.status, HttpStatus::Response);
        QCOMPARE(response.code, 200);
        QCOMPARE(response.body, "arrived");
        const QList<FakeHttpServer::Seen> seen = server.seen();
        QCOMPARE(seen.size(), 2);
        QCOMPARE(seen[0].path, "/from");
        QCOMPARE(seen[1].path, "/to");
    }

    void aBodyOfExactlyTheLimitIsReadAndOneByteMoreIsTooLarge() {
        FakeHttpServer server;
        server.route("/exact", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, QByteArray(1000, 'a')); });
        server.route("/over", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, QByteArray(1001, 'a')); });
        QtHttpClient client;

        HttpRequest exact = requestTo(server, u"/exact"_s);
        exact.maxBodyBytes = 1000;
        const HttpResponse read = client.get(exact, {});
        QCOMPARE(read.status, HttpStatus::Response);
        QVERIFY(!read.bodyTooLarge);
        QCOMPARE(read.body, QByteArray(1000, 'a'));

        HttpRequest over = requestTo(server, u"/over"_s);
        over.maxBodyBytes = 1000;
        const HttpResponse tooLarge = client.get(over, {});
        QCOMPARE(tooLarge.status, HttpStatus::Response);
        QCOMPARE(tooLarge.code, 200);
        QVERIFY(tooLarge.bodyTooLarge);
        QVERIFY(tooLarge.body.isEmpty());
    }

    void aBodyThatNeverEndsIsDroppedAtTheLimit() {
        constexpr qint64 total = 32LL * 1024 * 1024;
        struct Written {
            bool refused;
            qint64 bytes;
            Clock::duration took;
        };
        std::promise<Written> written;
        FakeHttpServer server;
        server.route("/endless", [&](FakeHttpServer::Exchange& exchange) {
            const auto start = Clock::now();
            exchange.sendHeaders(200, true);
            const QByteArray chunk(64 * 1024, 'z');
            qint64 bytes = 0;
            bool refused = false;
            while (bytes < total) {
                if (!exchange.write(chunk)) {
                    refused = true;
                    break;
                }
                bytes += chunk.size();
            }
            written.set_value({refused, bytes, Clock::now() - start});
        });
        QtHttpClient client;
        HttpRequest request = requestTo(server, u"/endless"_s);
        request.maxBodyBytes = 1024 * 1024;

        const HttpResponse response = client.get(request, {});
        QCOMPARE(response.status, HttpStatus::Response);
        QVERIFY(response.bodyTooLarge);
        QVERIFY(response.body.isEmpty());

        std::future<Written> result = written.get_future();
        QVERIFY(result.wait_for(15s) == std::future_status::ready);
        const Written serverSide = result.get();
        QVERIFY(serverSide.refused);
        QVERIFY2(serverSide.bytes < total, QByteArray::number(serverSide.bytes).constData());
        QVERIFY(serverSide.took < 15s);
    }

    void aSlowAnswerIsUnreachableAtTheTotalTimeout() {
        FakeHttpServer server;
        Gate gate;
        server.route("/slow", [waiter = gate.waiter()](FakeHttpServer::Exchange& exchange) {
            waiter.wait(5s);
            exchange.reply(200, "late");
        });
        QtHttpClient client;
        HttpRequest request = requestTo(server, u"/slow"_s);
        request.totalTimeout = 1s;

        const auto start = Clock::now();
        const HttpResponse response = client.get(request, {});
        const auto took = Clock::now() - start;
        QCOMPARE(response.status, HttpStatus::Unreachable);
        QVERIFY(took < 4s);
        QVERIFY(!response.error.isEmpty());
    }

    void aBodyThatStallsIsUnreachableAtTheTotalTimeout() {
        FakeHttpServer server;
        Gate gate;
        server.route("/stall", [waiter = gate.waiter()](FakeHttpServer::Exchange& exchange) {
            exchange.sendHeaders(200, false, 100);
            exchange.write("x");
            waiter.wait(5s);
        });
        QtHttpClient client;
        HttpRequest request = requestTo(server, u"/stall"_s);
        request.totalTimeout = 1s;

        const auto start = Clock::now();
        const HttpResponse response = client.get(request, {});
        const auto took = Clock::now() - start;
        QCOMPARE(response.status, HttpStatus::Unreachable);
        QVERIFY(took < 4s);
        QVERIFY(!response.error.isEmpty());
    }

    void aBodyThatStallsIsUnreachableAtTheIdleTimeout() {
        FakeHttpServer server;
        Gate gate;
        server.route("/stall", [waiter = gate.waiter()](FakeHttpServer::Exchange& exchange) {
            exchange.sendHeaders(200, false, 100);
            exchange.write("x");
            waiter.wait(5s);
        });
        QtHttpClient client;
        HttpRequest request = requestTo(server, u"/stall"_s);
        request.idleTimeout = 1s;

        const auto start = Clock::now();
        const HttpResponse response = client.get(request, {});
        const auto took = Clock::now() - start;
        QCOMPARE(response.status, HttpStatus::Unreachable);
        QVERIFY(took < 4s);
        QVERIFY(!response.error.isEmpty());
    }

    void aRefusedConnectionIsUnreachable() {
        FakeHttpServer server;
        const HttpRequest request = requestTo(server, u"/api"_s);
        server.stop();
        QtHttpClient client;

        const auto start = Clock::now();
        const HttpResponse response = client.get(request, {});
        const auto took = Clock::now() - start;
        QCOMPARE(response.status, HttpStatus::Unreachable);
        QVERIFY(took < 10s);
        QVERIFY(!response.error.isEmpty());
    }

    void aConnectionClosedInTheMiddleOfTheBodyIsAFailureNotATimeout() {
        FakeHttpServer server;
        server.route("/cut", [](FakeHttpServer::Exchange& exchange) {
            exchange.sendHeaders(200, false, 100);
            exchange.write(QByteArray(10, 'c'));
            std::this_thread::sleep_for(300ms);   // let the client read the headers and the ten bytes first
            exchange.closeAbruptly();
        });
        QtHttpClient client;
        HttpRequest request = requestTo(server, u"/cut"_s);
        request.totalTimeout = 20s;
        request.idleTimeout = 20s;

        const HttpResponse response = client.get(request, {});
        QCOMPARE(response.status, HttpStatus::Failed);
        QVERIFY(!response.error.isEmpty());
    }

    // A gzip body that does not decompress, after a 200: Qt reports it as an error of its content or protocol range (201 and
    // up), the range it also uses for a 4xx or 5xx answer. With a 2xx code that is a transfer that went wrong (Failed), not
    // an answer with a partial body. (aConnectionClosedInTheMiddleOfTheBody… covers a connection error after a 200; this one
    // covers the 201-and-up range, which the client tells apart from an answer only by the code.)
    void anErrorAfterA2xxStatusIsAFailureNotAResponse() {
        FakeHttpServer server;
        server.route("/gzip", [](FakeHttpServer::Exchange& exchange) {
            exchange.write("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 15\r\nConnection: close\r\n\r\n"
                           "not gzip at all");
        });
        QtHttpClient client;
        HttpRequest request = requestTo(server, u"/gzip"_s);
        request.totalTimeout = 10s;

        const HttpResponse response = client.get(request, {});
        QVERIFY2(response.status == HttpStatus::Failed,
                 qPrintable(QString::fromLatin1(statusName(response.status)) + u" code "_s + QString::number(response.code)));
        QVERIFY(!response.error.isEmpty());
    }

    void aBrokenOrUnsupportedUrlIsAFailureWithoutARequest() {
        FakeHttpServer server;
        QtHttpClient client;
        for (const QString& url : {u"http://[not a url"_s, QString(), u"ftp://127.0.0.1/api"_s}) {
            HttpRequest request;
            request.url = url;
            const HttpResponse response = client.get(request, {});
            QVERIFY2(response.status == HttpStatus::Failed, qPrintable(url));
            QVERIFY2(!response.error.isEmpty(), qPrintable(url));
        }
        QVERIFY(server.seen().isEmpty());
    }

    void stoppingWhileTheServerSleepsReturnsPromptly() {
        FakeHttpServer server;
        // Declared before the client: on an early return the client goes first and ends the call, so this does not block.
        std::future<HttpResponse> call;
        Gate gate;
        server.route("/sleep", [waiter = gate.waiter()](FakeHttpServer::Exchange& exchange) {
            waiter.wait(10s);
            exchange.reply(200, "late");
        });
        QtHttpClient client;
        std::stop_source stop;
        call = std::async(std::launch::async, [&] { return client.get(requestTo(server, u"/sleep"_s), stop.get_token()); });
        QVERIFY(waitForSeen(server, 1));

        const auto start = Clock::now();
        stop.request_stop();
        QVERIFY(call.wait_for(3s) == std::future_status::ready);
        QVERIFY(Clock::now() - start < 3s);
        QCOMPARE(call.get().status, HttpStatus::Cancelled);
    }

    void aStopRequestedBeforeTheCallSendsNothing() {
        FakeHttpServer server;
        server.route("/ok", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "{}"); });
        QtHttpClient client;
        std::stop_source stop;
        stop.request_stop();

        const HttpResponse response = client.get(requestTo(server, u"/ok"_s), stop.get_token());
        QCOMPARE(response.status, HttpStatus::Cancelled);
        std::this_thread::sleep_for(200ms);   // a request sent anyway would have arrived by now
        QVERIFY(server.seen().isEmpty());
    }

    void cancellingManyCallsAtOnceReturnsThemAllPromptly() {
        FakeHttpServer server;
        Gate gate;
        server.route("/sleep", [waiter = gate.waiter()](FakeHttpServer::Exchange& exchange) {
            waiter.wait(10s);
            exchange.reply(200, "late");
        });
        server.route("/ok", [](FakeHttpServer::Exchange& exchange) { exchange.reply(200, "fine"); });
        std::stop_source stop;
        constexpr int count = 16;
        std::vector<HttpResponse> responses(count);
        std::vector<Clock::time_point> ended(count);
        QtHttpClient client;
        std::vector<std::jthread> threads;
        // Declared after the client and the threads: on an early return it ends the calls and joins every thread before the
        // client goes, so no thread can call a destroyed client (a jthread's own stop does not reach `stop`).
        struct StopAndJoin {
            std::stop_source& stop;
            std::vector<std::jthread>& threads;
            ~StopAndJoin() {
                stop.request_stop();
                for (std::jthread& thread : threads) {
                    if (thread.joinable()) thread.join();
                }
            }
        } stopAndJoin{stop, threads};
        for (int i = 0; i < count; ++i) {
            threads.emplace_back([&, i] {
                responses[i] = client.get(requestTo(server, u"/sleep"_s), stop.get_token());
                ended[i] = Clock::now();
            });
        }
        QVERIFY(waitForSeen(server, 1));
        std::this_thread::sleep_for(300ms);   // let the other calls reach the client's thread too

        const auto start = Clock::now();
        stop.request_stop();
        for (std::jthread& thread : threads) thread.join();
        for (int i = 0; i < count; ++i) {
            QCOMPARE(responses[i].status, HttpStatus::Cancelled);
            QVERIFY(ended[i] - start < 3s);
        }

        const HttpResponse after = client.get(requestTo(server, u"/ok"_s), {});
        QCOMPARE(after.status, HttpStatus::Response);
        QCOMPARE(after.code, 200);
        QCOMPARE(after.body, "fine");
    }

    void callsFromManyThreadsAllGetTheirOwnAnswer() {
        constexpr int threadCount = 8;
        constexpr int perThread = 25;
        FakeHttpServer server;
        for (int t = 0; t < threadCount; ++t) {
            for (int r = 0; r < perThread; ++r) {
                const QByteArray body = "body " + QByteArray::number(t) + '/' + QByteArray::number(r);
                server.route("/r/" + QByteArray::number(t) + '/' + QByteArray::number(r),
                             [body](FakeHttpServer::Exchange& exchange) { exchange.reply(200, body); });
            }
        }
        QtHttpClient client;
        std::atomic<int> wrong{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < threadCount; ++t) {
            threads.emplace_back([&, t] {
                for (int r = 0; r < perThread; ++r) {
                    const QString path = u"/r/%1/%2"_s.arg(t).arg(r);
                    const HttpResponse response = client.get(requestTo(server, path), {});
                    const QByteArray expected = "body " + QByteArray::number(t) + '/' + QByteArray::number(r);
                    if (response.status != HttpStatus::Response || response.code != 200 || response.body != expected) ++wrong;
                }
            });
        }
        for (std::thread& thread : threads) thread.join();
        QCOMPARE(wrong.load(), 0);
        QCOMPARE(server.seen().size(), threadCount * perThread);
    }

    void destroyingTheClientWithACallInFlightEndsTheCall() {
        FakeHttpServer server;
        // Declared before the client: on an early return the client goes first and ends the call, so this does not block.
        std::future<HttpResponse> call;
        Gate gate;
        server.route("/sleep", [waiter = gate.waiter()](FakeHttpServer::Exchange& exchange) {
            waiter.wait(10s);
            exchange.reply(200, "late");
        });
        auto client = std::make_unique<QtHttpClient>();
        call = std::async(std::launch::async, [&, raw = client.get()] { return raw->get(requestTo(server, u"/sleep"_s), {}); });
        QVERIFY(waitForSeen(server, 1));

        const auto start = Clock::now();
        client.reset();
        QVERIFY(call.wait_for(5s) == std::future_status::ready);
        QVERIFY(Clock::now() - start < 5s);
        const HttpStatus status = call.get().status;
        QVERIFY2(status == HttpStatus::Failed || status == HttpStatus::Cancelled, statusName(status));
    }

    void theSchannelTlsBackendIsLinkedIn() {
        QVERIFY(QSslSocket::supportsSsl());
        QCOMPARE(QSslSocket::activeBackend(), u"schannel"_s);
    }
};

QTEST_GUILESS_MAIN(QtHttpClientTest)
#include "tst_qt_http_client.moc"
