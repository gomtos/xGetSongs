#pragma once

#include <memory>

#include "net/http_client.h"

namespace xgs::net {

// HttpClient on Qt Network. One dedicated thread owns a QNetworkAccessManager (no proxy, no cookies, redirects followed
// except https to http); get() hands the request to that thread and waits for the answer, so the calling thread needs no
// event loop. A QCoreApplication must exist. The destructor ends every call in flight (they return Failed) and waits for
// the thread.
class QtHttpClient final : public HttpClient {
public:
    QtHttpClient();
    ~QtHttpClient() override;
    QtHttpClient(const QtHttpClient&) = delete;
    QtHttpClient& operator=(const QtHttpClient&) = delete;

    HttpResponse get(const HttpRequest& request, std::stop_token stop) override;

    struct Core;

private:
    std::shared_ptr<Core> m_core;
};

}  // namespace xgs::net
