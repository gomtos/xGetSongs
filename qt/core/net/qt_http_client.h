#pragma once

#include <functional>
#include <memory>

#include "net/http_client.h"

namespace xgs::net {

class QtHttpClient;

namespace test_hooks {
// For tests only: queues work on the client's network thread, behind everything already handed to that thread. False
// (and the work is not run) once the client is closing. A test uses it to hold the network thread still.
bool postToNetworkThread(QtHttpClient& client, std::function<void()> work);
}  // namespace test_hooks

// HttpClient on Qt Network. One dedicated thread owns a QNetworkAccessManager (no proxy, no cookies, redirects followed
// except https to http); get() hands the request to that thread and waits for the answer, so the calling thread needs no
// event loop. A QCoreApplication must exist. The destructor ends every call in flight (they return Failed) and waits for
// the thread.
//
// Besides the caller's headers, Qt sends Host, Connection: Keep-Alive and Accept-Encoding: gzip, deflate (Qt then
// decompresses the body itself, so maxBodyBytes counts decompressed bytes; a body that Qt's decompression safety check
// stops, QNetworkRequest::decompressedSafetyCheckThreshold with 10 MiB by default, ends as Failed with Qt's text), and this
// client sends Accept-Language: en unless the caller gives one (Qt would build it from the system locale). Kotlin's
// java.net.http client sends neither Accept-Encoding nor Accept-Language. A caller's own Accept-Encoding or
// Accept-Language replaces these.
class QtHttpClient final : public HttpClient {
public:
    QtHttpClient();
    ~QtHttpClient() override;
    QtHttpClient(const QtHttpClient&) = delete;
    QtHttpClient& operator=(const QtHttpClient&) = delete;

    HttpResponse get(const HttpRequest& request, std::stop_token stop) override;

private:
    friend bool test_hooks::postToNetworkThread(QtHttpClient& client, std::function<void()> work);
    struct Core;
    std::shared_ptr<Core> m_core;
};

}  // namespace xgs::net
