#include "net/qt_http_client.h"

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace xgs::net {

namespace {

// Why the client itself ended a transfer before Qt finished it.
enum class Ending { None, Cancelled, TotalTimeout, IdleTimeout, TooLarge, Closed };

// The meeting point of the calling thread (which waits) and the network thread (which answers).
struct Call {
    std::mutex mutex;
    std::condition_variable answered;
    bool done = false;
    HttpResponse response;

    void answer(HttpResponse value) {
        {
            std::lock_guard lock(mutex);
            response = std::move(value);
            done = true;
        }
        answered.notify_all();
    }

    HttpResponse wait() {
        std::unique_lock lock(mutex);
        answered.wait(lock, [this] { return done; });
        return std::move(response);
    }
};

// One request on the network thread.
struct Transfer {
    std::shared_ptr<Call> call;
    QNetworkReply* reply = nullptr;
    QTimer* idleTimer = nullptr;
    QByteArray body;
    qint64 maxBodyBytes = -1;
    std::chrono::milliseconds totalTimeout{0};
    std::chrono::milliseconds idleTimeout{0};
    Ending ending = Ending::None;
};

HttpResponse failure(HttpStatus status, QString error) {
    HttpResponse response;
    response.status = status;
    response.error = std::move(error);
    return response;
}

// The errors of Qt that mean the server was never reached. An unknown network error counts only before any response:
// Qt reports a network or host that cannot be reached that way (the socket's NetworkError), but after the headers it is
// a broken transfer.
bool isUnreachable(QNetworkReply::NetworkError error, bool responded) {
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
        return true;
    case QNetworkReply::UnknownNetworkError:
        return !responded;
    default:
        return false;
    }
}

}  // namespace

struct QtHttpClient::Core {
    std::mutex mutex;
    QObject* context = nullptr;   // lives on the network thread; null once the client is closing (guarded by mutex)
    QThread thread;
    std::atomic<quint64> nextId{1};

    // Network thread only.
    QNetworkAccessManager* manager = nullptr;
    std::map<quint64, Transfer> transfers;

    // Runs work on the network thread later; false when the client is closing.
    bool post(std::function<void()> work) {
        std::lock_guard lock(mutex);
        if (!context) return false;
        QMetaObject::invokeMethod(context, std::move(work), Qt::QueuedConnection);
        return true;
    }

    void open() {
        manager = new QNetworkAccessManager;
        manager->setProxy(QNetworkProxy::NoProxy);
        manager->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
        manager->setAutoDeleteReplies(false);
    }

    void start(quint64 id, const QUrl& url, const HttpRequest& request, const std::shared_ptr<Call>& call) {
        QNetworkRequest networkRequest(url);
        for (const auto& [name, value] : request.headers) networkRequest.setRawHeader(name, value);
        networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        networkRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        networkRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        networkRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        networkRequest.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);

        Transfer& transfer = transfers[id];
        transfer.call = call;
        transfer.maxBodyBytes = request.maxBodyBytes;
        transfer.totalTimeout = request.totalTimeout;
        transfer.idleTimeout = request.idleTimeout;
        QNetworkReply* reply = manager->get(networkRequest);
        transfer.reply = reply;

        if (request.totalTimeout.count() > 0) {
            auto* timer = new QTimer(reply);
            timer->setSingleShot(true);
            timer->setTimerType(Qt::PreciseTimer);
            QObject::connect(timer, &QTimer::timeout, context, [this, id] { end(id, Ending::TotalTimeout); });
            timer->start(request.totalTimeout);
        }
        if (request.idleTimeout.count() > 0) {
            transfer.idleTimer = new QTimer(reply);
            transfer.idleTimer->setSingleShot(true);
            transfer.idleTimer->setTimerType(Qt::PreciseTimer);
            QObject::connect(transfer.idleTimer, &QTimer::timeout, context, [this, id] { end(id, Ending::IdleTimeout); });
            transfer.idleTimer->start(request.idleTimeout);
        }
        QObject::connect(reply, &QNetworkReply::metaDataChanged, context, [this, id] { touch(id); });
        QObject::connect(reply, &QNetworkReply::redirected, context, [this, id] { touch(id); });
        QObject::connect(reply, &QNetworkReply::readyRead, context, [this, id] { read(id); });
        QObject::connect(reply, &QNetworkReply::finished, context, [this, id] { finish(id); });
    }

    // Something arrived: the idle timeout starts again.
    void touch(quint64 id) {
        const auto it = transfers.find(id);
        if (it != transfers.end() && it->second.idleTimer) it->second.idleTimer->start(it->second.idleTimeout);
    }

    void read(quint64 id) {
        const auto it = transfers.find(id);
        if (it == transfers.end() || it->second.ending != Ending::None) return;
        Transfer& transfer = it->second;
        if (transfer.maxBodyBytes >= 0 && transfer.body.size() + transfer.reply->bytesAvailable() > transfer.maxBodyBytes) {
            end(id, Ending::TooLarge);
            return;
        }
        transfer.body += transfer.reply->readAll();
        touch(id);
    }

    // Ends a transfer before Qt does: aborts the reply and answers the call.
    void end(quint64 id, Ending ending) {
        const auto it = transfers.find(id);
        if (it == transfers.end() || it->second.ending != Ending::None) return;
        it->second.ending = ending;
        QNetworkReply* reply = it->second.reply;
        reply->abort();   // may emit finished at once
        if (transfers.contains(id)) finish(id);
    }

    void finish(quint64 id) {
        const auto it = transfers.find(id);
        if (it == transfers.end()) return;
        Transfer transfer = std::move(it->second);
        transfers.erase(it);
        QNetworkReply* reply = transfer.reply;
        reply->disconnect(context);
        reply->deleteLater();   // its timers with it
        transfer.call->answer(answerOf(transfer));
    }

    static HttpResponse answerOf(Transfer& transfer) {
        QNetworkReply* reply = transfer.reply;
        const QVariant code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        switch (transfer.ending) {
        case Ending::Cancelled:
            return failure(HttpStatus::Cancelled, {});
        case Ending::TotalTimeout:
            return failure(HttpStatus::Unreachable,
                           u"The request timed out: no complete answer within %1 ms."_s.arg(transfer.totalTimeout.count()));
        case Ending::IdleTimeout:
            return failure(HttpStatus::Unreachable,
                           u"The request timed out: nothing arrived for %1 ms."_s.arg(transfer.idleTimeout.count()));
        case Ending::Closed:
            return failure(HttpStatus::Failed, u"The HTTP client was closed during the request."_s);
        case Ending::TooLarge: {
            HttpResponse response;
            response.status = HttpStatus::Response;
            response.code = code.value<int>();   // the attribute holds an int
            response.bodyTooLarge = true;
            return response;
        }
        case Ending::None:
            break;
        }

        const QNetworkReply::NetworkError error = reply->error();
        // Qt reports a 4xx or 5xx answer as an error from its content, protocol or server ranges (201 and up); it is
        // still an answer.
        if (error == QNetworkReply::NoError || (code.isValid() && error >= QNetworkReply::ContentAccessDenied)) {
            HttpResponse response;
            response.status = HttpStatus::Response;
            response.code = code.value<int>();   // the attribute holds an int
            const QByteArray rest = reply->readAll();
            if (transfer.maxBodyBytes >= 0 && transfer.body.size() + rest.size() > transfer.maxBodyBytes) {
                response.bodyTooLarge = true;
            } else {
                response.body = std::move(transfer.body);
                response.body += rest;
            }
            return response;
        }
        QString text = reply->errorString();
        if (text.isEmpty()) text = u"The request failed (network error %1)."_s.arg(static_cast<int>(error));
        return failure(isUnreachable(error, code.isValid()) ? HttpStatus::Unreachable : HttpStatus::Failed, text);
    }

    // The client is being destroyed: every transfer ends as Failed, and the manager goes.
    void close() {
        std::vector<quint64> ids;
        for (const auto& entry : transfers) ids.push_back(entry.first);
        for (const quint64 id : ids) end(id, Ending::Closed);
        delete manager;
        manager = nullptr;
    }
};

QtHttpClient::QtHttpClient() : m_core(std::make_shared<Core>()) {
    m_core->thread.setObjectName(u"xgs-http"_s);
    auto* context = new QObject;
    context->moveToThread(&m_core->thread);
    m_core->context = context;
    m_core->thread.start();
    Core* core = m_core.get();
    QMetaObject::invokeMethod(context, [core] { core->open(); }, Qt::BlockingQueuedConnection);
}

QtHttpClient::~QtHttpClient() {
    QObject* context = nullptr;
    {
        std::lock_guard lock(m_core->mutex);
        context = std::exchange(m_core->context, nullptr);
    }
    Core* core = m_core.get();
    // Queued after every start already posted, so those transfers exist and end here.
    QMetaObject::invokeMethod(context, [core] { core->close(); }, Qt::BlockingQueuedConnection);
    m_core->thread.quit();
    m_core->thread.wait();
    delete context;
}

HttpResponse QtHttpClient::get(const HttpRequest& request, std::stop_token stop) {
    if (stop.stop_requested()) return failure(HttpStatus::Cancelled, {});
    const QUrl url(request.url, QUrl::StrictMode);
    if (request.url.isEmpty() || !url.isValid())
        return failure(HttpStatus::Failed, u"Not a valid URL: \"%1\"."_s.arg(request.url));
    if (url.scheme() != u"http" && url.scheme() != u"https")
        return failure(HttpStatus::Failed, u"Only http and https URLs are supported: \"%1\"."_s.arg(request.url));
    if (url.host().isEmpty()) return failure(HttpStatus::Failed, u"The URL has no host: \"%1\"."_s.arg(request.url));

    // The core, not this object, is what the callbacks below hold: a stop requested after the client is gone finds it
    // closed.
    const std::shared_ptr<Core> core = m_core;
    const quint64 id = core->nextId++;
    auto call = std::make_shared<Call>();
    Core* raw = core.get();
    if (!core->post([raw, id, url, request, call] { raw->start(id, url, request, call); }))
        return failure(HttpStatus::Failed, u"The HTTP client was closed during the request."_s);
    std::stop_callback onStop(stop, [core, id] {
        Core* target = core.get();
        core->post([target, id] { target->end(id, Ending::Cancelled); });
    });
    return call->wait();
}

}  // namespace xgs::net
