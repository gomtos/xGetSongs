#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <chrono>
#include <stop_token>
#include <utility>

// A blocking HTTP GET for the engine (the LRCLIB lyrics provider, the yt-dlp download). The production implementation is
// QtHttpClient (qt_http_client.h); tests give the engine a fake.
namespace xgs::net {

struct HttpRequest {
    // Sent as written, except that Qt's QUrl decodes percent-encoded unreserved characters (A-Z a-z 0-9 - . _ ~, which
    // RFC 3986 section 2.3 defines as equivalent to the encoded form), writes percent-encodings with upper-case hex digits,
    // percent-encodes a raw non-ASCII character as UTF-8 and drops a fragment. A URL that is not valid in QUrl's strict
    // mode (a raw space, a lone '%', a raw " < > [ ] { } | ^ ` or backslash) is a Failed answer with nothing sent.
    QString url;
    QList<std::pair<QByteArray, QByteArray>> headers;
    std::chrono::milliseconds totalTimeout{0};          // from sending until the body is in; 0: none
    std::chrono::milliseconds idleTimeout{0};           // no byte for this long; 0: none
    qint64 maxBodyBytes = -1;                           // -1: no limit
};

// Response: the server answered with a status line (any code, 404 and 500 included).
// Unreachable: the connection was refused, the host was not found, the network was not reachable, or a timeout of the
//   request ran out.
// Failed: everything else (a broken URL, a scheme that is not http or https, a TLS failure, a connection closed in the
//   middle of the body, too many redirects, a client destroyed with the call in flight).
// Cancelled: the stop token was triggered before the answer was in.
enum class HttpStatus { Response, Unreachable, Failed, Cancelled };

struct HttpResponse {
    HttpStatus status = HttpStatus::Failed;
    int code = 0;                 // Response only
    QByteArray body;              // Response only; empty when bodyTooLarge
    bool bodyTooLarge = false;    // the body was longer than maxBodyBytes: reading stopped, the connection was dropped
    QString error;                // Unreachable / Failed: text for a person
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    // Blocking, may be called from any thread (several at once), never throws. Redirects are followed except from https
    // to http. No proxy is used.
    virtual HttpResponse get(const HttpRequest& request, std::stop_token stop) = 0;
};

}  // namespace xgs::net
