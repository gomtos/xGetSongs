#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

// An HTTP/1.1 server for tests on Winsock: it listens on 127.0.0.1 only (never the wildcard address) on a port the system
// chooses, serves each connection on its own thread, answers one request per connection and always says
// "Connection: close".
namespace xgs::testing {

class FakeHttpServer {
public:
    // What the server saw of one request: the request target split at the first '?' (rawQuery is empty when there is none),
    // two header values as sent, and every header line in order (name as sent, value without surrounding blanks).
    struct Seen {
        QByteArray method, path, rawQuery, userAgent, accept;
        QList<std::pair<QByteArray, QByteArray>> headers;
    };

    // One request and its answer. A handler answers with reply(), or with sendHeaders() and write()s; write() before any
    // headers sends raw bytes (a handler may write a whole response of its own). When the handler returns, a chunked body is
    // ended, a handler that sent nothing gets a 500, and the connection is closed.
    class Exchange {
    public:
        const Seen& request() const;
        void reply(int status, const QByteArray& body, const QByteArray& contentType = "application/json; charset=utf-8");
        // Content-Length is sent when contentLength >= 0 and the body is not chunked; with neither, the body ends at the close.
        void sendHeaders(int status, bool chunked, qint64 contentLength = -1);
        bool write(const QByteArray& bytes);   // false: the client went away
        void closeAbruptly();                  // a reset (RST), not an orderly close

    private:
        friend class FakeHttpServer;
        struct Connection;
        Exchange(std::shared_ptr<Connection> connection, Seen seen);
        std::shared_ptr<Connection> m_connection;
        Seen m_seen;
        bool m_started = false;   // anything was sent
        bool m_chunked = false;
    };
    using Handler = std::function<void(Exchange&)>;

    FakeHttpServer();    // listens at once; throws std::runtime_error when it cannot
    ~FakeHttpServer();   // stop()
    FakeHttpServer(const FakeHttpServer&) = delete;
    FakeHttpServer& operator=(const FakeHttpServer&) = delete;

    void route(const QByteArray& path, Handler handler);   // others: 404
    QString base() const;                                   // "http://127.0.0.1:<port>"
    QList<Seen> seen() const;
    void clearSeen();
    // Closes the listening socket (the port then refuses connections), shuts the open connections down and waits for their
    // threads. A handler that is still running is waited for.
    void stop();

private:
    struct Worker {
        std::thread thread;
        std::shared_ptr<Exchange::Connection> connection;
    };
    void acceptLoop();
    void serve(const std::shared_ptr<Exchange::Connection>& connection);

    quint16 m_port = 0;
    std::uintptr_t m_listener;   // a SOCKET
    std::thread m_acceptor;
    mutable std::mutex m_mutex;
    bool m_stopped = false;
    QMap<QByteArray, Handler> m_routes;
    QList<Seen> m_seen;
    std::list<Worker> m_workers;
};

}  // namespace xgs::testing
