#include <QtTest>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QSet>
#include <QSignalSpy>
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>

#include "remote_playlists.h"

// RemotePlaylists (spec §5.5). The real class refuses everything but https, and a test must never reach a real host, so:
//  - the choice of addresses (parseConfig) and the reading of a list (parseList) are tested as functions;
//  - the transfer (limits, redirects, cancel, failures) is tested against a server on 127.0.0.1 through a subclass that
//    overrides the protected seams (which addresses are allowed, the time limit, the size limit); the unchanged class is
//    also shown to refuse http and send nothing.
// Every name, ID and answer is made up.

using namespace Qt::StringLiterals;
using namespace xgs::ui;

namespace {

QString pid(int n) {
    return u"PLdummy"_s + QString::number(n).rightJustified(27, u'0');
}

/// The bytes of a JSON text written with ' for ".
QByteArray json(const QString& text) {
    QString copy = text;
    copy.replace(u'\'', u'"');
    return copy.toUtf8();
}

QByteArray listBody(const QList<QPair<int, QString>>& entries) {
    QString text = u"{'playlists':["_s;
    bool first = true;
    for (const auto& [sn, id] : entries) {
        if (!first) text += u',';
        first = false;
        text += u"{'sn':%1,'name':'더미 %2','pid':'%3','is_free':true}"_s.arg(sn).arg(sn).arg(id);
    }
    return json(text + u"]}"_s);
}

// ---- a made-up server on 127.0.0.1 ----

struct Route {
    int status = 200;
    QByteArray body;
    QByteArray location;           // 3xx: where to
    enum class Mode { Normal, Hang, Endless, LyingLength } mode = Mode::Normal;
};

class TestServer {
public:
    TestServer() {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) onConnection(socket);
        });
    }
    bool ok() const { return m_server.isListening(); }
    QString base() const { return u"http://127.0.0.1:%1"_s.arg(m_server.serverPort()); }
    QUrl url(const QString& path) const { return QUrl(base() + path); }
    void route(const QString& path, Route route) { m_routes[path] = std::move(route); }
    QStringList requests() const { return m_requests; }
    QList<QByteArray> headerBlocks() const { return m_headerBlocks; }
    int disconnectedEndless() const { return m_endlessDisconnected; }
    int hangingDisconnected() const { return m_hangDisconnected; }

private:
    void onConnection(QTcpSocket* socket) {
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0 || m_answered.contains(socket)) return;
            m_answered.insert(socket);
            respond(socket, QString::fromLatin1(buffer->left(end)), buffer->left(end));
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket] { m_answered.remove(socket); socket->deleteLater(); });
    }

    void respond(QTcpSocket* socket, const QString& head, const QByteArray& rawHead) {
        const QStringList lines = head.split(u"\r\n"_s);
        const QString target = lines.value(0).split(u' ').value(1);
        m_requests.append(target);
        m_headerBlocks.append(rawHead);
        const auto it = m_routes.find(target);
        if (it == m_routes.end()) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const Route& route = it->second;
        switch (route.mode) {
        case Route::Mode::Hang:   // never answers
            QObject::connect(socket, &QTcpSocket::disconnected, socket, [this] { ++m_hangDisconnected; });
            return;
        case Route::Mode::Endless: {
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
            auto* timer = new QTimer(socket);
            QObject::connect(socket, &QTcpSocket::disconnected, socket, [this] { ++m_endlessDisconnected; });
            QObject::connect(timer, &QTimer::timeout, socket, [socket] {
                if (socket->state() == QAbstractSocket::ConnectedState) socket->write(QByteArray(4096, ' '));
            });
            timer->start(2);
            return;
        }
        case Route::Mode::LyingLength:
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 99999999\r\nConnection: close\r\n\r\n");
            socket->write(route.body);
            return;   // then silence
        case Route::Mode::Normal:
            break;
        }
        QByteArray answer = "HTTP/1.1 " + QByteArray::number(route.status) + " X\r\nContent-Type: application/json\r\nContent-Length: " +
                            QByteArray::number(route.body.size()) + "\r\nConnection: close\r\n";
        if (!route.location.isEmpty()) answer += "Location: " + route.location + "\r\n";
        answer += "\r\n" + route.body;
        socket->write(answer);
        socket->disconnectFromHost();
    }

    std::map<QString, Route> m_routes;
    QSet<QTcpSocket*> m_answered;
    QStringList m_requests;
    QList<QByteArray> m_headerBlocks;
    int m_endlessDisconnected = 0;
    int m_hangDisconnected = 0;
    // Last, so that it goes first, with the sockets whose handlers use the members above.
    QTcpServer m_server;
};

/// What a test of the transfer talks to: the class with its seams opened for 127.0.0.1 and short limits.
class LoopbackPlaylists : public RemotePlaylists {
public:
    LoopbackPlaylists(QNetworkAccessManager& network, QUrl configUrl, int timeout = 3000, qint64 limit = kRemoteMaxBytes)
        : RemotePlaylists(network, std::move(configUrl)), m_timeout(timeout), m_limit(limit) {}
    QStringList refusedPrefixes;   // addresses whose path starts with one of these are refused (the redirect tests)

protected:
    bool urlAllowed(const QUrl& url) const override {
        if (url.scheme() != u"http"_s || url.host() != u"127.0.0.1"_s) return false;
        for (const QString& prefix : refusedPrefixes) {
            if (url.path().startsWith(prefix)) return false;
        }
        return true;
    }
    int timeoutMs() const override { return m_timeout; }
    qint64 maxBytes() const override { return m_limit; }

private:
    int m_timeout;
    qint64 m_limit;
};

/// Runs a fetch to its end and gives the outcome (a failed test when done never comes within the limit).
std::optional<RemoteOutcome> run(RemotePlaylists& remote, int waitMs = 10000) {
    QSignalSpy spy(&remote, &RemotePlaylists::done);
    remote.fetch();
    if (!spy.wait(waitMs) && spy.isEmpty()) return std::nullopt;
    return qvariant_cast<RemoteOutcome>(spy.first().first());
}

}  // namespace

class RemotePlaylistsTest : public QObject {
    Q_OBJECT

private slots:
    // ---- which addresses are asked for ----

    void onlyHttpsAddressesAreAllowed() {
        QVERIFY(isHttpsUrl(QUrl(u"https://www.example.com/a.json"_s)));
        QVERIFY(isHttpsUrl(QUrl(u"HTTPS://www.example.com/a.json"_s)));
        QVERIFY(isHttpsUrl(QUrl(u"https://example.com:8443/a?b=c"_s)));
        QVERIFY(!isHttpsUrl(QUrl(u"http://www.example.com/a.json"_s)));
        QVERIFY(!isHttpsUrl(QUrl(u"http://127.0.0.1/a.json"_s)));
        QVERIFY(!isHttpsUrl(QUrl(u"ftp://www.example.com/a.json"_s)));
        QVERIFY(!isHttpsUrl(QUrl(u"file:///C:/a.json"_s)));
        QVERIFY(!isHttpsUrl(QUrl(u"//www.example.com/a.json"_s)));
        QVERIFY(!isHttpsUrl(QUrl(u"www.example.com/a.json"_s)));
        QVERIFY(!isHttpsUrl(QUrl(u"https:///a.json"_s)));
        QVERIFY(!isHttpsUrl(QUrl(u"https://user:pw@www.example.com/a.json"_s)));
        QVERIFY(!isHttpsUrl(QUrl()));
    }

    void theDefaultConfigAddressIsHttps() {
        QVERIFY(isHttpsUrl(defaultRemoteConfigUrl()));
        QCOMPARE(defaultRemoteConfigUrl().toString(), u"https://www.gtcsoft.kr/remote_config/cherry_music_rc.json"_s);
    }

    void theConfigGivesTheTwoListAddressesAndIgnoresTheRest() {
        const ConfigParse parsed = parseConfig(json(
            u"{'values':{'//comment':'x','music_video_playlists_url_ko':'https://example.com/ko.json',"
            u"'//other':{'a':[1,2]},'music_video_playlists_url_en':'https://example.com/en.json','ads':true,'version':3}}"_s));
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.ko.state, UrlChoice::State::Use);
        QCOMPARE(parsed.ko.url, QUrl(u"https://example.com/ko.json"_s));
        QCOMPARE(parsed.en.state, UrlChoice::State::Use);
        QCOMPARE(parsed.en.url, QUrl(u"https://example.com/en.json"_s));
    }

    void aBlankMissingOrNullAddressIsAnEmptyListNotAFailure() {
        const ConfigParse parsed = parseConfig(json(u"{'values':{'music_video_playlists_url_ko':'  ','music_video_playlists_url_en':null}}"_s));
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.ko.state, UrlChoice::State::Empty);
        QCOMPARE(parsed.en.state, UrlChoice::State::Empty);
        const ConfigParse missing = parseConfig(json(u"{'values':{}}"_s));
        QVERIFY(missing.ok);
        QCOMPARE(missing.ko.state, UrlChoice::State::Empty);
        QCOMPARE(missing.en.state, UrlChoice::State::Empty);
    }

    void anAddressThatIsNotHttpsIsInvalid() {
        const ConfigParse parsed = parseConfig(json(
            u"{'values':{'music_video_playlists_url_ko':'http://example.com/ko.json','music_video_playlists_url_en':'https://example.com/en.json'}}"_s));
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.ko.state, UrlChoice::State::Invalid);
        QCOMPARE(parsed.en.state, UrlChoice::State::Use);
        for (const char* odd : {"{'values':{'music_video_playlists_url_ko':5}}", "{'values':{'music_video_playlists_url_ko':'file:///C:/x'}}",
                                "{'values':{'music_video_playlists_url_ko':'not a url'}}", "{'values':{'music_video_playlists_url_ko':['https://a.b/c']}}"}) {
            QCOMPARE(parseConfig(json(QString::fromLatin1(odd))).ko.state, UrlChoice::State::Invalid);
        }
    }

    void aConfigThatIsNotTheExpectedJsonIsNotOk() {
        for (const char* body : {"", "not json", "[]", "5", "null", "{}", "{'values':[]}", "{'values':'x'}"}) {
            QVERIFY2(!parseConfig(json(QString::fromLatin1(body))).ok, body);
        }
    }

    void theAllowedPredicateDecidesWhichAddressesAreUsed() {
        const QByteArray body = json(u"{'values':{'music_video_playlists_url_ko':'http://127.0.0.1:1/ko'}}"_s);
        QCOMPARE(parseConfig(body).ko.state, UrlChoice::State::Invalid);
        QCOMPARE(parseConfig(body, [](const QUrl&) { return true; }).ko.state, UrlChoice::State::Use);
    }

    // ---- reading a list ----

    void aListIsReadWithItsSnAndCleanedValues() {
        const ListParse parsed = parseList(listBody({{2, pid(2)}, {1, pid(1)}}));
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.dropped, 0);
        QCOMPARE(parsed.entries, (QList<RemoteEntry>{{2, u"더미 2"_s, pid(2)}, {1, u"더미 1"_s, pid(1)}}));   // sorting is the merge's job
    }

    void badEntriesAreDroppedAndCounted() {
        // 1 good; then: no name, blank name, name of control characters only, no pid, bad pid, a video id, a mix list, a
        // host that is not YouTube, no sn, sn as text, sn with a fraction, an entry that is not an object.
        const ListParse parsed = parseList(json(
            u"{'playlists':["
            u"{'sn':1,'name':'좋은 것','pid':'"_s + pid(1) + u"'},"
            u"{'sn':2,'pid':'"_s + pid(2) + u"'},"
            u"{'sn':3,'name':'   ','pid':'"_s + pid(3) + u"'},"
            u"{'sn':4,'name':'\\u0001\\u0002','pid':'"_s + pid(4) + u"'},"
            u"{'sn':5,'name':'x'},"
            u"{'sn':6,'name':'x','pid':'PLshort'},"
            u"{'sn':7,'name':'x','pid':'dQw4w9WgXcQ'},"
            u"{'sn':8,'name':'x','pid':'RDCLAK5uy_abcdefgh'},"
            u"{'sn':9,'name':'x','pid':'https://evil.example.com/playlist?list="_s + pid(9) + u"'},"
            u"{'name':'x','pid':'"_s + pid(10) + u"'},"
            u"{'sn':'11','name':'x','pid':'"_s + pid(11) + u"'},"
            u"{'sn':12.5,'name':'x','pid':'"_s + pid(12) + u"'},"
            u"{'sn':13,'name':5,'pid':'"_s + pid(13) + u"'},"
            u"'text',null,[]"
            u"]}"_s));
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.entries, (QList<RemoteEntry>{{1, u"좋은 것"_s, pid(1)}}));
        QCOMPARE(parsed.dropped, 15);
    }

    void aLongNameIsCutAtHundredUnitsAndControlCharactersAreRemoved() {
        const QString longName = QString(150, u'가');
        const ListParse parsed = parseList(json(
            u"{'playlists':[{'sn':1,'name':'"_s + longName + u"','pid':'"_s + pid(1) + u"'},"
            u"{'sn':2,'name':'  a\\u0000b\\nc\\u001fd  ','pid':'"_s + pid(2) + u"'}]}"_s));
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.entries.size(), 2);
        QCOMPARE(parsed.entries[0].name, QString(100, u'가'));
        QCOMPARE(parsed.entries[1].name, u"abcd"_s);
        QCOMPARE(parsed.dropped, 0);
    }

    void aPidGivenAsAPlaylistAddressIsStoredAsTheId() {
        const ListParse parsed = parseList(json(
            u"{'playlists':[{'sn':1,'name':'x','pid':'https://www.youtube.com/playlist?list="_s + pid(1) + u"'}]}"_s));
        QCOMPARE(parsed.entries.size(), 1);
        QCOMPARE(parsed.entries[0].pid, pid(1));
    }

    void aListThatIsNotTheExpectedJsonIsNotOk() {
        for (const char* body : {"", "garbage", "[]", "{}", "{'playlists':5}", "{'playlists':{}}", "null"}) {
            const ListParse parsed = parseList(json(QString::fromLatin1(body)));
            QVERIFY2(!parsed.ok, body);
            QVERIFY(parsed.entries.isEmpty());
        }
    }

    void anEmptyListIsOkAndEmpty() {
        const ListParse parsed = parseList(json(u"{'playlists':[]}"_s));
        QVERIFY(parsed.ok);
        QVERIFY(parsed.entries.isEmpty());
    }

    // ---- the transfer, against 127.0.0.1 ----

    void bothListsAreReadAfterTheConfig() {
        TestServer server;
        QVERIFY(server.ok());
        server.route(u"/config"_s, {200, json(u"{'values':{'//c':'x','music_video_playlists_url_ko':'"_s + server.base() +
                                              u"/ko','music_video_playlists_url_en':'"_s + server.base() + u"/en'}}"_s)});
        server.route(u"/ko"_s, {200, listBody({{2, pid(2)}, {1, pid(1)}})});
        server.route(u"/en"_s, {200, listBody({{1, pid(3)}})});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(!outcome->configFailed && !outcome->koFailed && !outcome->enFailed);
        QCOMPARE(outcome->ko.size(), 2);
        QCOMPARE(outcome->en.size(), 1);
        QCOMPARE(outcome->en[0].pid, pid(3));
        QCOMPARE(outcome->dropped, 0);
        QCOMPARE(server.requests().size(), 3);
        QCOMPARE(server.requests().first(), u"/config"_s);
        QVERIFY(!remote.isRunning());
    }

    void theRequestsCarryNothingThatIdentifiesTheUserOrThePc() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{}}"_s)});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));
        QVERIFY(run(remote).has_value());

        QCOMPARE(server.headerBlocks().size(), 1);
        const QString head = QString::fromLatin1(server.headerBlocks().first());
        for (const QString& name : {u"authorization"_s, u"cookie"_s, u"referer"_s, u"origin"_s, u"x-"_s}) {
            QVERIFY2(!head.contains(u"\r\n"_s + name, Qt::CaseInsensitive), qPrintable(name));
        }
        // Only Qt's default User-Agent, which names neither the user nor the computer.
        const QStringList secrets = {qEnvironmentVariable("USERNAME"), qEnvironmentVariable("COMPUTERNAME"), QSysInfo::machineHostName()};
        for (const QString& secret : secrets) {
            if (secret.size() >= 3) QVERIFY2(!head.contains(secret, Qt::CaseInsensitive), "the request names the user or the PC");
        }
    }

    void theUnchangedClassRefusesHttpAndSendsNothing() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{}}"_s)});
        QNetworkAccessManager network;
        RemotePlaylists remote(network, server.url(u"/config"_s));   // http://127.0.0.1: the real rule says no

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
        QVERIFY(server.requests().isEmpty());
    }

    void aConfigAddressInsideTheConfigThatIsNotAllowedFailsOnlyThatLanguage() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{'music_video_playlists_url_ko':'"_s + server.base() +
                                              u"/ko','music_video_playlists_url_en':'https://example.invalid/en'}}"_s)});
        server.route(u"/ko"_s, {200, listBody({{1, pid(1)}})});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(!outcome->configFailed);
        QVERIFY(!outcome->koFailed);
        QVERIFY(outcome->enFailed);   // https://example.invalid is not allowed by the loopback seam, so it is never requested
        QCOMPARE(outcome->ko.size(), 1);
        QCOMPARE(server.requests(), (QStringList{u"/config"_s, u"/ko"_s}));
    }

    void oneListFailingStillGivesTheOther() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{'music_video_playlists_url_ko':'"_s + server.base() +
                                              u"/ko','music_video_playlists_url_en':'"_s + server.base() + u"/en'}}"_s)});
        server.route(u"/ko"_s, {200, listBody({{1, pid(1)}})});
        server.route(u"/en"_s, {500, QByteArray("oops")});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(!outcome->configFailed && !outcome->koFailed);
        QVERIFY(outcome->enFailed);
        QCOMPARE(outcome->ko.size(), 1);
        QVERIFY(outcome->en.isEmpty());
    }

    void aListThatIsNotJsonFailsItsLanguage() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{'music_video_playlists_url_ko':'"_s + server.base() +
                                              u"/ko','music_video_playlists_url_en':'"_s + server.base() + u"/en'}}"_s)});
        server.route(u"/ko"_s, {200, QByteArray("<html>not json</html>")});
        server.route(u"/en"_s, {200, listBody({{1, pid(1)}})});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->koFailed);
        QVERIFY(!outcome->enFailed);
    }

    void theConfigFailingIsReportedAndNoListIsAsked() {
        for (const Route& route : {Route{404, {}, {}, Route::Mode::Normal}, Route{500, QByteArray("x"), {}, Route::Mode::Normal},
                                   Route{200, QByteArray("not json"), {}, Route::Mode::Normal}, Route{200, json(u"{'values':[]}"_s), {}, Route::Mode::Normal}}) {
            TestServer server;
            server.route(u"/config"_s, route);
            QNetworkAccessManager network;
            LoopbackPlaylists remote(network, server.url(u"/config"_s));

            const std::optional<RemoteOutcome> outcome = run(remote);

            QVERIFY(outcome.has_value());
            QVERIFY(outcome->configFailed);
            QVERIFY(outcome->ko.isEmpty() && outcome->en.isEmpty());
            QCOMPARE(server.requests().size(), 1);
        }
    }

    void aConnectionThatCannotBeMadeIsAConfigFailure() {
        quint16 port = 0;
        {
            QTcpServer taken;
            taken.listen(QHostAddress::LocalHost, 0);
            port = taken.serverPort();
        }   // closed again: nothing listens there
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, QUrl(u"http://127.0.0.1:%1/config"_s.arg(port)));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
    }

    void anEmptyListAddressIsAnEmptyListNotAFailure() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{'music_video_playlists_url_ko':'','music_video_playlists_url_en':'"_s +
                                              server.base() + u"/en'}}"_s)});
        server.route(u"/en"_s, {200, listBody({{1, pid(1)}})});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(!outcome->configFailed && !outcome->koFailed && !outcome->enFailed);
        QVERIFY(outcome->ko.isEmpty());
        QCOMPARE(outcome->en.size(), 1);
        QCOMPARE(server.requests(), (QStringList{u"/config"_s, u"/en"_s}));
    }

    void badEntriesAreCountedAcrossBothLanguages() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{'music_video_playlists_url_ko':'"_s + server.base() +
                                              u"/ko','music_video_playlists_url_en':'"_s + server.base() + u"/en'}}"_s)});
        server.route(u"/ko"_s, {200, json(u"{'playlists':[{'sn':1,'name':'a','pid':'"_s + pid(1) + u"'},{'sn':2,'name':'','pid':'"_s + pid(2) + u"'}]}"_s)});
        server.route(u"/en"_s, {200, json(u"{'playlists':[{'sn':1,'name':'b','pid':'bad'},{'sn':2,'name':'c','pid':'worse'}]}"_s)});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QCOMPARE(outcome->ko.size(), 1);
        QVERIFY(outcome->en.isEmpty());
        QCOMPARE(outcome->dropped, 3);
    }

    // ---- redirects ----

    void aRedirectToAnAllowedAddressIsFollowed() {
        TestServer server;
        server.route(u"/old"_s, Route{302, {}, "/config", Route::Mode::Normal});
        server.route(u"/config"_s, {200, json(u"{'values':{}}"_s)});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/old"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(!outcome->configFailed);
        QCOMPARE(server.requests(), (QStringList{u"/old"_s, u"/config"_s}));
    }

    void aRedirectToAnAddressThatIsNotAllowedIsRefusedAndNotFollowed() {
        TestServer server;
        server.route(u"/old"_s, Route{302, {}, "/blocked/config", Route::Mode::Normal});
        server.route(u"/blocked/config"_s, {200, json(u"{'values':{}}"_s)});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/old"_s));
        remote.refusedPrefixes = {u"/blocked"_s};

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
        QCOMPARE(server.requests(), QStringList{u"/old"_s});   // the target was never requested
    }

    void aRedirectToAnotherSchemeIsRefused() {
        TestServer server;
        server.route(u"/old"_s, Route{302, {}, "ftp://127.0.0.1/config", Route::Mode::Normal});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/old"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
    }

    void aRedirectLoopEndsAsAFailure() {
        TestServer server;
        server.route(u"/a"_s, Route{302, {}, "/b", Route::Mode::Normal});
        server.route(u"/b"_s, Route{302, {}, "/a", Route::Mode::Normal});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/a"_s));

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
        QVERIFY(server.requests().size() <= 10);
    }

    // ---- Review Focus 4: the limits ----

    void aRequestThatNeverAnswersEndsAtTheTimeLimit() {
        TestServer server;
        server.route(u"/config"_s, Route{200, {}, {}, Route::Mode::Hang});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 300);

        QElapsedTimer clock;
        clock.start();
        const std::optional<RemoteOutcome> outcome = run(remote, 8000);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
        QVERIFY2(clock.elapsed() >= 250, "not before its limit");
        QVERIFY2(clock.elapsed() < 5000, qPrintable(u"took %1 ms"_s.arg(clock.elapsed())));
        QTRY_COMPARE_WITH_TIMEOUT(server.hangingDisconnected(), 1, 3000);   // and the connection was dropped
    }
    void aBodyThatKeepsComingEndsAtTheTimeLimitToo() {
        // Bytes keep coming (endless), but the whole request may take only timeoutMs.
        TestServer server;
        server.route(u"/config"_s, Route{200, {}, {}, Route::Mode::Endless});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 400, 100 * 1024 * 1024);   // size limit out of the way

        QElapsedTimer clock;
        clock.start();
        const std::optional<RemoteOutcome> outcome = run(remote, 8000);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
        QVERIFY2(clock.elapsed() < 5000, qPrintable(u"took %1 ms"_s.arg(clock.elapsed())));
    }

    void aBodyOverTheSizeLimitIsRefusedAndTheTransferIsStopped() {
        TestServer server;
        server.route(u"/config"_s, Route{200, QByteArray(5000, ' ') + json(u"{'values':{}}"_s), {}, Route::Mode::Normal});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 3000, 1000);

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
    }

    void aBodyJustWithinTheSizeLimitIsAccepted() {
        TestServer server;
        const QByteArray body = json(u"{'values':{}}"_s);
        server.route(u"/config"_s, Route{200, body + QByteArray(1000 - body.size(), ' '), {}, Route::Mode::Normal});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 3000, 1000);

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(!outcome->configFailed);
    }

    void aBodyOneByteOverTheSizeLimitIsRefused() {
        TestServer server;
        const QByteArray body = json(u"{'values':{}}"_s);
        server.route(u"/config"_s, Route{200, body + QByteArray(1001 - body.size(), ' '), {}, Route::Mode::Normal});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 3000, 1000);

        const std::optional<RemoteOutcome> outcome = run(remote);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
    }

    void aLengthThatIsAnnouncedOverTheLimitIsRefusedWithoutWaitingForTheBody() {
        TestServer server;
        server.route(u"/config"_s, Route{200, QByteArray("{"), {}, Route::Mode::LyingLength});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 8000, 1000);

        QElapsedTimer clock;
        clock.start();
        const std::optional<RemoteOutcome> outcome = run(remote, 6000);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
        QVERIFY2(clock.elapsed() < 4000, qPrintable(u"took %1 ms"_s.arg(clock.elapsed())));   // well before the 8 s time limit
    }

    void aBodyWithNoLengthThatNeverEndsIsCutAtTheSizeLimit() {
        TestServer server;
        server.route(u"/config"_s, Route{200, {}, {}, Route::Mode::Endless});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 8000, 20000);

        QElapsedTimer clock;
        clock.start();
        const std::optional<RemoteOutcome> outcome = run(remote, 6000);

        QVERIFY(outcome.has_value());
        QVERIFY(outcome->configFailed);
        QVERIFY2(clock.elapsed() < 4000, qPrintable(u"took %1 ms"_s.arg(clock.elapsed())));
        QTRY_COMPARE_WITH_TIMEOUT(server.disconnectedEndless(), 1, 3000);   // the transfer was stopped, not left running
    }

    void theLimitsOfTheRealClassAreFifteenSecondsAndOneMegabyte() {
        QCOMPARE(kRemoteTimeoutMs, 15000);
        QCOMPARE(kRemoteMaxBytes, qint64(1024 * 1024));
    }

    // ---- cancel ----

    void cancelStopsTheTransferAndNothingIsEmittedAfterwards() {
        TestServer server;
        server.route(u"/config"_s, Route{200, {}, {}, Route::Mode::Hang});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 300);
        QSignalSpy spy(&remote, &RemotePlaylists::done);

        remote.fetch();
        QVERIFY(remote.isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() == 1, 3000);
        remote.cancel();
        QVERIFY(!remote.isRunning());

        QTest::qWait(800);   // longer than the 300 ms time limit that would have ended it
        QVERIFY(spy.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(server.hangingDisconnected(), 1, 3000);   // the connection was closed
    }

    void cancelBeforeAnythingIsSentEmitsNothing() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{}}"_s)});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));
        QSignalSpy spy(&remote, &RemotePlaylists::done);

        remote.fetch();
        remote.cancel();

        QTest::qWait(300);
        QVERIFY(spy.isEmpty());
    }

    void cancelWhileTheListsAreComingEmitsNothing() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{'music_video_playlists_url_ko':'"_s + server.base() +
                                              u"/ko','music_video_playlists_url_en':'"_s + server.base() + u"/en'}}"_s)});
        server.route(u"/ko"_s, Route{200, {}, {}, Route::Mode::Hang});
        server.route(u"/en"_s, {200, listBody({{1, pid(1)}})});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s), 5000);
        QSignalSpy spy(&remote, &RemotePlaylists::done);

        remote.fetch();
        QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() == 3, 3000);
        QTest::qWait(100);   // /en has answered by now; /ko has not
        remote.cancel();

        QTest::qWait(300);
        QVERIFY(spy.isEmpty());
    }

    void aFetchAfterACancelWorksAndGivesOneAnswer() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{}}"_s)});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));
        QSignalSpy spy(&remote, &RemotePlaylists::done);

        remote.fetch();
        remote.cancel();
        remote.fetch();

        QVERIFY(spy.wait(5000) || !spy.isEmpty());
        QTest::qWait(200);
        QCOMPARE(spy.size(), 1);   // the cancelled run's answer did not come as well
    }

    void cancelWhenNothingRunsDoesNothing() {
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, QUrl(u"http://127.0.0.1:1/x"_s));
        remote.cancel();
        remote.cancel();
        QVERIFY(!remote.isRunning());
    }

    void aSecondFetchWhileOneRunsIsIgnored() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{}}"_s)});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));
        QSignalSpy spy(&remote, &RemotePlaylists::done);

        remote.fetch();
        remote.fetch();

        QVERIFY(spy.wait(5000) || !spy.isEmpty());
        QTest::qWait(200);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(server.requests().size(), 1);
    }

    void aRunCanBeFollowedByAnother() {
        TestServer server;
        server.route(u"/config"_s, {200, json(u"{'values':{}}"_s)});
        QNetworkAccessManager network;
        LoopbackPlaylists remote(network, server.url(u"/config"_s));
        QVERIFY(run(remote).has_value());
        QVERIFY(run(remote).has_value());
        QCOMPARE(server.requests().size(), 2);
    }

    void destroyingTheObjectWhileItRunsIsSafe() {
        TestServer server;
        server.route(u"/config"_s, Route{200, {}, {}, Route::Mode::Hang});
        QNetworkAccessManager network;
        {
            LoopbackPlaylists remote(network, server.url(u"/config"_s), 5000);
            remote.fetch();
            QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() == 1, 3000);
        }
        QTest::qWait(200);   // the replies are gone with it; nothing may call back into the destroyed object
        QTRY_COMPARE_WITH_TIMEOUT(server.hangingDisconnected(), 1, 3000);
    }
};

QTEST_GUILESS_MAIN(RemotePlaylistsTest)
#include "tst_remote_playlists.moc"
