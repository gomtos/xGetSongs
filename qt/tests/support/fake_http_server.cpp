#include "fake_http_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <type_traits>

static_assert(std::is_same_v<SOCKET, std::uintptr_t>, "m_listener holds a SOCKET");

namespace xgs::testing {

// One accepted socket. Only the thread serving it closes it; stop() may shut it down (which leaves the handle valid) to wake
// a blocked recv or send.
struct FakeHttpServer::Exchange::Connection {
    SOCKET socket = INVALID_SOCKET;
    std::mutex mutex;
    bool closed = false;
    std::atomic<bool> done{false};

    bool sendAll(const char* data, qsizetype size) {
        while (size > 0) {
            const int chunk = static_cast<int>(std::min<qsizetype>(size, 1 << 20));
            const int sent = ::send(socket, data, chunk, 0);
            if (sent == SOCKET_ERROR || sent <= 0) return false;
            data += sent;
            size -= sent;
        }
        return true;
    }

    bool isClosed() {
        std::lock_guard lock(mutex);
        return closed;
    }

    void shutdownBoth() {
        std::lock_guard lock(mutex);
        if (!closed) ::shutdown(socket, SD_BOTH);
    }

    // abortive: a zero linger time makes closesocket send a reset
    void close(bool abortive) {
        std::lock_guard lock(mutex);
        if (closed) return;
        if (abortive) {
            linger value{1, 0};
            ::setsockopt(socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&value), sizeof value);
        } else {
            ::shutdown(socket, SD_SEND);
        }
        ::closesocket(socket);
        closed = true;
    }
};

namespace {

QByteArray reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 307: return "Temporary Redirect";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "Status";
    }
}

// The bytes up to and including the blank line after the headers, or empty when the client closed first or sent more than
// 64 KiB of headers.
QByteArray readHead(SOCKET socket) {
    QByteArray head;
    char buffer[4096];
    while (!head.contains("\r\n\r\n")) {
        if (head.size() > 64 * 1024) return {};
        const int got = ::recv(socket, buffer, sizeof buffer, 0);
        if (got <= 0) return {};
        head.append(buffer, got);
    }
    return head.left(head.indexOf("\r\n\r\n") + 4);
}

FakeHttpServer::Seen parse(const QByteArray& head) {
    FakeHttpServer::Seen seen;
    const QList<QByteArray> lines = head.split('\n');
    const QList<QByteArray> requestLine = lines.value(0).chopped(lines.value(0).endsWith('\r') ? 1 : 0).split(' ');
    seen.method = requestLine.value(0);
    const QByteArray target = requestLine.value(1);
    const qsizetype question = target.indexOf('?');
    seen.path = question < 0 ? target : target.left(question);
    seen.rawQuery = question < 0 ? QByteArray() : target.mid(question + 1);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        QByteArray line = lines[i];
        if (line.endsWith('\r')) line.chop(1);
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QByteArray rawName = line.left(colon);
        const QByteArray name = rawName.toLower();
        QByteArray value = line.mid(colon + 1);
        while (value.startsWith(' ') || value.startsWith('\t')) value.remove(0, 1);
        while (value.endsWith(' ') || value.endsWith('\t')) value.chop(1);
        seen.headers.append({rawName, value});
        if (name == "user-agent") seen.userAgent = value;
        else if (name == "accept") seen.accept = value;
    }
    return seen;
}

}  // namespace

FakeHttpServer::Exchange::Exchange(std::shared_ptr<Connection> connection, Seen seen)
    : m_connection(std::move(connection)), m_seen(std::move(seen)) {}

const FakeHttpServer::Seen& FakeHttpServer::Exchange::request() const {
    return m_seen;
}

void FakeHttpServer::Exchange::reply(int status, const QByteArray& body, const QByteArray& contentType) {
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\nContent-Type: " + contentType
                          + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    response += body;
    m_started = true;
    m_connection->sendAll(response.constData(), response.size());
}

void FakeHttpServer::Exchange::sendHeaders(int status, bool chunked, qint64 contentLength) {
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status)
                      + "\r\nContent-Type: application/octet-stream\r\n";
    if (chunked) head += "Transfer-Encoding: chunked\r\n";
    else if (contentLength >= 0) head += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    head += "Connection: close\r\n\r\n";
    m_started = true;
    m_chunked = chunked;
    m_connection->sendAll(head.constData(), head.size());
}

bool FakeHttpServer::Exchange::write(const QByteArray& bytes) {
    if (m_connection->isClosed()) return false;
    m_started = true;
    if (!m_chunked) return m_connection->sendAll(bytes.constData(), bytes.size());
    if (bytes.isEmpty()) return true;   // an empty chunk would end the body
    const QByteArray frame = QByteArray::number(bytes.size(), 16) + "\r\n" + bytes + "\r\n";
    return m_connection->sendAll(frame.constData(), frame.size());
}

void FakeHttpServer::Exchange::closeAbruptly() {
    m_connection->close(true);
}

FakeHttpServer::FakeHttpServer() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        throw std::runtime_error("socket failed");
    }
    // Nobody else may take the port while this server holds it.
    BOOL exclusive = TRUE;
    ::setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    int length = sizeof address;
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof address) == SOCKET_ERROR
        || ::listen(listener, SOMAXCONN) == SOCKET_ERROR
        || ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) {
        ::closesocket(listener);
        WSACleanup();
        throw std::runtime_error("cannot listen on 127.0.0.1");
    }
    m_listener = listener;
    m_port = ntohs(address.sin_port);
    m_acceptor = std::thread([this] { acceptLoop(); });
}

FakeHttpServer::~FakeHttpServer() {
    stop();
    WSACleanup();
}

void FakeHttpServer::route(const QByteArray& path, Handler handler) {
    std::lock_guard lock(m_mutex);
    m_routes.insert(path, std::move(handler));
}

QString FakeHttpServer::base() const {
    return QStringLiteral("http://127.0.0.1:") + QString::number(m_port);
}

QList<FakeHttpServer::Seen> FakeHttpServer::seen() const {
    std::lock_guard lock(m_mutex);
    return m_seen;
}

void FakeHttpServer::clearSeen() {
    std::lock_guard lock(m_mutex);
    m_seen.clear();
}

void FakeHttpServer::stop() {
    {
        std::lock_guard lock(m_mutex);
        if (m_stopped) return;
        m_stopped = true;
        ::closesocket(static_cast<SOCKET>(m_listener));   // wakes accept()
    }
    m_acceptor.join();
    std::list<Worker> workers;
    {
        std::lock_guard lock(m_mutex);
        workers.swap(m_workers);
    }
    for (Worker& worker : workers) worker.connection->shutdownBoth();
    for (Worker& worker : workers) worker.thread.join();
}

void FakeHttpServer::acceptLoop() {
    for (;;) {
        const SOCKET socket = ::accept(static_cast<SOCKET>(m_listener), nullptr, nullptr);
        if (socket == INVALID_SOCKET) return;   // stop() closed the listener
        auto connection = std::make_shared<Exchange::Connection>();
        connection->socket = socket;
        std::lock_guard lock(m_mutex);
        if (m_stopped) {
            connection->close(true);
            return;
        }
        // reap the connections that are done
        for (auto it = m_workers.begin(); it != m_workers.end();) {
            if (it->connection->done) {
                it->thread.join();
                it = m_workers.erase(it);
            } else {
                ++it;
            }
        }
        m_workers.push_back(Worker{std::thread([this, connection] { serve(connection); }), connection});
    }
}

void FakeHttpServer::serve(const std::shared_ptr<Exchange::Connection>& connection) {
    const QByteArray head = readHead(connection->socket);
    if (!head.isEmpty()) {
        Exchange exchange(connection, parse(head));
        Handler handler;
        {
            std::lock_guard lock(m_mutex);
            m_seen.append(exchange.request());
            handler = m_routes.value(exchange.request().path);
        }
        if (handler) handler(exchange);
        else exchange.reply(404, "not found", "text/plain");
        if (!connection->isClosed()) {
            if (!exchange.m_started) exchange.reply(500, "the handler sent nothing", "text/plain");
            else if (exchange.m_chunked) connection->sendAll("0\r\n\r\n", 5);
        }
    }
    connection->close(false);
    connection->done = true;
}

}  // namespace xgs::testing
