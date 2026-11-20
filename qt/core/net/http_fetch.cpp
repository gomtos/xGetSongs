#include "net/http_fetch.h"

#include <QString>

#include <utility>

namespace xgs::net {

using namespace Qt::StringLiterals;

tools::Fetch httpFetch(HttpClient& client, std::chrono::milliseconds idleTimeout, qint64 maxBytes) {
    return [&client, idleTimeout, maxBytes](const QString& url, std::stop_token stop) -> Outcome<QByteArray> {
        HttpRequest request;
        request.url = url;
        request.headers = {{"User-Agent", AppUserAgent.toUtf8()}};
        request.idleTimeout = idleTimeout;
        request.maxBodyBytes = maxBytes;
        HttpResponse response = client.get(request, std::move(stop));
        switch (response.status) {
        case HttpStatus::Cancelled:
            return Outcome<QByteArray>::cancelled();
        case HttpStatus::Unreachable:
        case HttpStatus::Failed:
            return Outcome<QByteArray>::failed(std::move(response.error));
        case HttpStatus::Response:
            break;
        }
        if (response.code != 200) return Outcome<QByteArray>::failed(u"HTTP "_s + QString::number(response.code));
        if (response.bodyTooLarge) return Outcome<QByteArray>::failed(u"내려받는 파일이 너무 큽니다."_s);
        return Outcome<QByteArray>::ok(std::move(response.body));
    };
}

}  // namespace xgs::net
