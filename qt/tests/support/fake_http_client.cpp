#include "fake_http_client.h"

#include <utility>

namespace xgs::testing {

net::HttpResponse CountingHttpClient::get(const net::HttpRequest& request, std::stop_token stop) {
    ++m_calls;
    return m_inner.get(request, std::move(stop));
}

FakeHttpClient::FakeHttpClient(Script script) : m_script(std::move(script)) {}

net::HttpResponse FakeHttpClient::get(const net::HttpRequest& request, std::stop_token stop) {
    {
        std::lock_guard lock(m_mutex);
        m_urls.append(request.url);
    }
    if (m_script) return m_script(request, std::move(stop));
    net::HttpResponse failed;
    failed.status = net::HttpStatus::Failed;
    failed.error = QStringLiteral("FakeHttpClient has no script");
    return failed;
}

QStringList FakeHttpClient::urls() const {
    std::lock_guard lock(m_mutex);
    return m_urls;
}

net::HttpResponse FakeHttpClient::response(int code, const QByteArray& body) {
    net::HttpResponse answer;
    answer.status = net::HttpStatus::Response;
    answer.code = code;
    answer.body = body;
    return answer;
}

}  // namespace xgs::testing
