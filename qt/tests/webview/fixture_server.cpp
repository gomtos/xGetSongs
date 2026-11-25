#include "fixture_server.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace {

// A page that counts timer ticks (every 50 ms), shows the cookies it got, and shows its card after delayMs (fixture.mjs's
// page()). The card only has the marker the probe looks for; it holds no lyrics line.
QByteArray tickingPage(const QString& name, int delayMs, const QString& extra = QString()) {
    const QString page =
        u"<!doctype html><html><head><meta charset=\"utf-8\"><title>%1</title></head><body><p id=\"status\">loading %1</p>\n"
        u"<script>\n"
        u"document.getElementById(\"status\").textContent += \" cookie[\" + document.cookie + \"]\";\n"
        u"window.__ticks = 0;\n"
        u"setInterval(function () { window.__ticks++; }, 50);\n"
        u"setTimeout(function () { document.body.insertAdjacentHTML(\"beforeend\", '<div data-lyricid=\"1\">card of %1</div>'); }, %2);\n"
        u"%3\n"
        u"</script></body></html>"_s.arg(name)
            .arg(delayMs)
            .arg(extra);
    return page.toUtf8();
}

QByteArray response(int status, const QByteArray& reason, const QList<QByteArray>& headers, const QByteArray& body) {
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    for (const QByteArray& header : headers) out += header + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
    return out + body;
}

QByteArray html(const QByteArray& body, const QList<QByteArray>& extraHeaders = {}) {
    QList<QByteArray> headers{"Content-Type: text/html; charset=utf-8"};
    headers += extraHeaders;
    return response(200, "OK", headers, body);
}

// A result page with a lyrics card of three made-up lines in one paragraph, in the markup the card script reads.
QByteArray cardPage() {
    return u"<!doctype html><html><head><meta charset=\"utf-8\"><title>result</title></head><body><div>검색 결과 더미</div>"
           u"<div data-lyricid=\"id-1\"><div jsname=\"WbKHeb\"><div jsname=\"U8S5sf\">"
           u"<span jsname=\"YS01Ge\">더미 하나</span><br><span jsname=\"YS01Ge\">더미 둘</span><br>"
           u"<span jsname=\"YS01Ge\">더미 셋</span></div></div></div></body></html>"_s.toUtf8();
}

QByteArray plainPage() {
    return u"<!doctype html><html><head><meta charset=\"utf-8\"><title>result</title></head><body><div>검색 결과 더미</div>"
           u"<p>결과 없음 더미</p></body></html>"_s.toUtf8();
}

QByteArray sorryPage() {
    return u"<!doctype html><html><head><meta charset=\"utf-8\"><title>sorry</title></head><body>"
           u"<p>Our systems have detected unusual traffic from your computer network.</p></body></html>"_s.toUtf8();
}

QByteArray hangPage() {
    return u"<!doctype html><html><head><meta charset=\"utf-8\"><title>hang</title></head><body><p>멈춤 더미</p>\n"
           u"<script>setTimeout(function () { for (;;) {} }, 0);</script></body></html>"_s.toUtf8();
}

QByteArray restlessPage() {
    return u"<!doctype html><html><head><meta charset=\"utf-8\"><title>restless</title></head><body><p>바뀜 더미</p>\n"
           u"<script>var n = 0; setInterval(function () { history.replaceState(null, '', '/search?restless=' + (++n)); }, 100);"
           u"</script></body></html>"_s.toUtf8();
}

}  // namespace

FixtureServer::FixtureServer(QObject* parent) : QObject(parent), m_server(new QTcpServer(this)) {
    connect(m_server, &QTcpServer::newConnection, this, &FixtureServer::onConnection);
    m_server->listen(QHostAddress::LocalHost, 0);
}

FixtureServer::~FixtureServer() = default;

bool FixtureServer::isListening() const {
    return m_server->isListening();
}

QString FixtureServer::base() const {
    return u"http://127.0.0.1:"_s + QString::number(m_server->serverPort());
}

QStringList FixtureServer::requests() const {
    return m_requests;
}

int FixtureServer::countOf(const QString& prefix) const {
    int count = 0;
    for (const QString& target : m_requests)
        if (target.startsWith(prefix)) ++count;
    return count;
}

void FixtureServer::onConnection() {
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadable(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void FixtureServer::onReadable(QTcpSocket* socket) {
    if (socket->property("answered").toBool()) {
        socket->readAll();
        return;
    }
    QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
    if (!buffer.contains("\r\n\r\n")) {
        socket->setProperty("buffer", buffer);
        return;
    }
    socket->setProperty("answered", true);
    const QByteArray requestLine = buffer.left(buffer.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    const QString target = parts.size() >= 2 ? QString::fromLatin1(parts.at(1)) : QString();
    m_requests.append(target);
    socket->write(answer(target));
    socket->disconnectFromHost();
    Q_EMIT requested(target);
}

QByteArray FixtureServer::answer(const QString& target) {
    const QUrl url(u"http://127.0.0.1"_s + target);
    const QString path = url.path();
    if (path == u"/first") return html(tickingPage(u"first"_s, 700), {"Set-Cookie: seen=1; Path=/; Max-Age=3600"});
    if (path == u"/second") return html(tickingPage(u"second"_s, 1500));
    if (path == u"/popup") {
        return html(tickingPage(
            u"popup"_s, 500,
            u"setTimeout(function () { window.open(\"/popup-child\", \"_blank\"); }, 300);\n"
            u"setTimeout(function () { var link = document.createElement(\"a\"); link.href = \"/download.bin\"; "
            u"link.download = \"xgs-check-download.bin\"; document.body.append(link); link.click(); }, 600);"_s));
    }
    if (path == u"/popup-child") return html(tickingPage(u"popup-child"_s, 100000));
    if (path == u"/hang") return html(tickingPage(u"hang"_s, 100000, u"setTimeout(function () { for (;;) {} }, 300);"_s));
    if (path == u"/download.bin") {
        return response(200, "OK",
                        {"Content-Type: application/octet-stream",
                         "Content-Disposition: attachment; filename=\"xgs-check-download.bin\""},
                        QByteArray(1024, '\1'));
    }
    if (path == u"/sorry/index") return html(sorryPage());
    if (path == u"/search") {
        const QString q = QUrlQuery(url).queryItemValue(u"q"_s, QUrl::FullyDecoded);
        const QString artist = q.section(u' ', 0, 0);
        if (artist == u"Card") return html(cardPage());
        if (artist == u"Plain") return html(plainPage());
        if (artist == u"Block") return response(302, "Found", {"Location: /sorry/index?continue=x"}, QByteArray());
        if (artist == u"Hang") return html(hangPage());
        if (artist == u"Restless") return html(restlessPage());
    }
    return response(404, "Not Found", {"Content-Type: text/plain"}, "not found");
}
