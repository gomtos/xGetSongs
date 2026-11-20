#pragma once

#include <memory>

#include "net/http_client.h"

namespace xgs::net {

// HttpClient on Qt Network. One dedicated thread owns a QNetworkAccessManager (no proxy, no cookies, redirects followed
// except https to http); get() hands the request to that thread and waits for the answer, so the calling thread needs no
// event loop. A QCoreApplication must exist. The destructor ends every call in flight (they return Failed) and waits for
// the thread.
//
// Besides the caller's headers, Qt sends Host, Connection: Keep-Alive and Accept-Encoding: gzip, deflate (Qt then
// decompresses the body itself, so maxBodyBytes counts decompressed bytes), and this client sends Accept-Language: en
// unless the caller gives one (Qt would build it from the system locale). Kotlin's java.net.http client sends neither
// Accept-Encoding nor Accept-Language. A caller's own Accept-Encoding or Accept-Language replaces these.
class QtHttpClient final : public HttpClient {
public:
    QtHttpClient();
    ~QtHttpClient() override;
    QtHttpClient(const QtHttpClient&) = delete;
    QtHttpClient& operator=(const QtHttpClient&) = delete;

    HttpResponse get(const HttpRequest& request, std::stop_token stop) override;

private:
    struct Core;
    std::shared_ptr<Core> m_core;
};

}  // namespace xgs::net
