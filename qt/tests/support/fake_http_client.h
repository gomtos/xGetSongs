#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <mutex>
#include <stop_token>

#include "net/http_client.h"

// HttpClient stand-ins for tests.
namespace xgs::testing {

/// Wraps a client and counts its get() calls; every call is passed on. Stands in for what the Kotlin tests count through a
/// java.net.http client's proxy selector or executor: that a request went through the given client.
class CountingHttpClient final : public net::HttpClient {
public:
    explicit CountingHttpClient(net::HttpClient& inner) : m_inner(inner) {}

    net::HttpResponse get(const net::HttpRequest& request, std::stop_token stop) override;
    int calls() const { return m_calls.load(); }

private:
    net::HttpClient& m_inner;
    std::atomic<int> m_calls{0};
};

/// Answers get() from a script (a function of the request and the stop token) and records the URL of every request in order.
/// Without a script every call is a Failed answer. Safe to call from several threads; the script is called without a lock.
class FakeHttpClient final : public net::HttpClient {
public:
    using Script = std::function<net::HttpResponse(const net::HttpRequest&, std::stop_token)>;

    explicit FakeHttpClient(Script script = {});

    net::HttpResponse get(const net::HttpRequest& request, std::stop_token stop) override;
    QStringList urls() const;

    /// A Response with the given code and body.
    static net::HttpResponse response(int code, const QByteArray& body = {});

private:
    Script m_script;
    mutable std::mutex m_mutex;
    QStringList m_urls;
};

}  // namespace xgs::testing
