#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpServer;
class QTcpSocket;

// The fixed pages of the webview checks, served on 127.0.0.1 only (Q3 decision 13: a C++ QTcpServer in place of
// shell/tools/browser-check/fixture.mjs). It lives on the thread that made it (the GUI thread of the test) and answers while
// that thread runs its event loop. Every page is made up; there is no Google and no lyrics here.
//
// The pages of fixture.mjs, with the same behaviour:
//   /first         sets a cookie that lasts an hour; counts timer ticks every 50 ms; shows its card after 700 ms
//   /second        the same without the cookie; card after 1500 ms
//   /popup         tries window.open("/popup-child") after 300 ms and clicks a download link to /download.bin after 600 ms
//   /popup-child   what the popup would show (requested only if the popup opened)
//   /hang          an endless loop 300 ms after it loaded
//   /download.bin  1024 bytes as an attachment (xgs-check-download.bin)
// Every page shows "cookie[<document.cookie>]" in its text.
//
// And the result pages of the chain checks (the fetcher's searchBase points at /search), chosen by the first word of `q`
// (the artist):
//   Card      a lyrics card of three made-up lines (더미 하나, 더미 둘, 더미 셋) in one paragraph
//   Plain     a complete page without a card
//   Block     a 302 to /sorry/index?continue=x, which says "unusual traffic" (Google's block page, by its address)
//   Hang      an endless loop as soon as its script has run (so that no look can see it settled first)
//   Restless  a page that changes its address every 100 ms, so it never settles
// anything else: 404.
class FixtureServer final : public QObject {
    Q_OBJECT

public:
    explicit FixtureServer(QObject* parent = nullptr);
    ~FixtureServer() override;

    /// Whether it listens (on 127.0.0.1, a port of the system's choice).
    bool isListening() const;
    /// "http://127.0.0.1:<port>"
    QString base() const;
    /// The request targets seen so far (path and query as sent), in order.
    QStringList requests() const;
    /// How many requests had a path that starts with prefix.
    int countOf(const QString& prefix) const;

signals:
    /// A request came (its target as sent).
    void requested(const QString& target);

private:
    void onConnection();
    void onReadable(QTcpSocket* socket);
    QByteArray answer(const QString& target);

    QTcpServer* m_server;
    QStringList m_requests;
};
