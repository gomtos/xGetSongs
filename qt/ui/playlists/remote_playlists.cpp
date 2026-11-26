#include "remote_playlists.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <cmath>
#include <limits>

#include "playlist_entry_check.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::ui {

namespace {

Q_LOGGING_CATEGORY(lcPlaylists, "xgs.playlists")

constexpr int kMaxNameUnits = 100;
constexpr int kMaxRedirects = 5;

/// One address of the config: a missing, null or blank value is an empty list; a string that is not an allowed address (or any
/// other kind of value) is invalid.
UrlChoice choose(const QJsonValue& value, const std::function<bool(const QUrl&)>& allowed) {
    if (value.isUndefined() || value.isNull()) return UrlChoice{};
    if (!value.isString()) return UrlChoice{UrlChoice::State::Invalid, QUrl()};
    const QString text = kt::trim(value.toString());
    if (text.isEmpty()) return UrlChoice{};
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || !allowed(url)) return UrlChoice{UrlChoice::State::Invalid, QUrl()};
    return UrlChoice{UrlChoice::State::Use, url};
}

}  // namespace

QUrl defaultRemoteConfigUrl() {
    return QUrl(u"https://www.gtcsoft.kr/remote_config/cherry_music_rc.json"_s);
}

bool isHttpsUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == u"https"_s && !url.host().isEmpty() && url.userInfo().isEmpty();
}

ConfigParse parseConfig(const QByteArray& body, const std::function<bool(const QUrl&)>& allowed) {
    ConfigParse result;
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) return result;
    const QJsonValue values = document.object().value(u"values"_s);
    if (!values.isObject()) return result;
    result.ok = true;
    result.ko = choose(values.toObject().value(u"music_video_playlists_url_ko"_s), allowed);
    result.en = choose(values.toObject().value(u"music_video_playlists_url_en"_s), allowed);
    return result;
}

ListParse parseList(const QByteArray& body) {
    ListParse result;
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) return result;
    const QJsonValue playlists = document.object().value(u"playlists"_s);
    if (!playlists.isArray()) return result;
    result.ok = true;
    for (const QJsonValue& value : playlists.toArray()) {
        const QJsonObject entry = value.toObject();
        const QJsonValue sn = entry.value(u"sn"_s);
        const QJsonValue name = entry.value(u"name"_s);
        const QJsonValue pid = entry.value(u"pid"_s);
        bool usable = value.isObject() && sn.isDouble() && name.isString() && pid.isString();
        double number = 0;
        if (usable) {
            number = sn.toDouble();
            usable = std::isfinite(number) && number == std::floor(number) && number >= std::numeric_limits<int>::min() &&
                     number <= std::numeric_limits<int>::max();
        }
        QString cleaned;
        std::optional<QString> id;
        if (usable) {
            cleaned = cleanName(name.toString(), kMaxNameUnits);
            id = playlistIdOf(pid.toString());
            usable = !cleaned.isEmpty() && id.has_value();
        }
        if (!usable) {
            ++result.dropped;
            continue;
        }
        result.entries.append(RemoteEntry{int(number), cleaned, *id});
    }
    return result;
}

// ---- the transfer ----

/// One GET in flight. Shared by the signal handlers and m_transfers; `finished` is set once, by whoever ends it first.
struct RemotePlaylists::Transfer {
    QNetworkReply* reply = nullptr;
    QTimer* timer = nullptr;
    QByteArray body;
    bool finished = false;
    std::function<void(std::optional<QByteArray>)> callback;
};

/// One fetch(): what has been learned so far.
struct RemotePlaylists::Run {
    RemoteOutcome outcome;
    int pending = 0;   // lists still on their way
};

RemotePlaylists::RemotePlaylists(QNetworkAccessManager& network, QUrl configUrl)
    : m_network(network), m_configUrl(std::move(configUrl)) {}

RemotePlaylists::~RemotePlaylists() {
    cancel();
}

bool RemotePlaylists::urlAllowed(const QUrl& url) const {
    return isHttpsUrl(url);
}

void RemotePlaylists::fetch() {
    if (m_running) return;
    m_running = true;
    const auto run = std::make_shared<Run>();
    m_run = run;
    get(m_configUrl, [this, run](std::optional<QByteArray> body) { afterConfig(run, std::move(body)); });
}

void RemotePlaylists::cancel() {
    ++m_generation;   // answers that were only queued (a refused address) are dropped by this
    m_running = false;
    m_run.reset();
    std::vector<std::shared_ptr<Transfer>> transfers;
    transfers.swap(m_transfers);
    for (const std::shared_ptr<Transfer>& transfer : transfers) {
        transfer->finished = true;   // before abort(): the handlers it triggers must do nothing
        transfer->timer->stop();
        if (!transfer->reply->isFinished()) transfer->reply->abort();
        transfer->reply->deleteLater();
    }
}

void RemotePlaylists::get(const QUrl& url, std::function<void(std::optional<QByteArray>)> callback) {
    if (!urlAllowed(url)) {
        // Answered later, not from inside fetch(), so that done never comes before fetch() has returned.
        const quint64 generation = m_generation;
        QMetaObject::invokeMethod(
            this, [this, generation, callback = std::move(callback)] { if (generation == m_generation) callback(std::nullopt); },
            Qt::QueuedConnection);
        return;
    }

    QNetworkRequest request(url);   // nothing added: Qt's own headers only
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(kMaxRedirects);
    request.setTransferTimeout(timeoutMs());   // a stalled transfer; the timer below bounds the whole request

    const auto transfer = std::make_shared<Transfer>();
    transfer->callback = std::move(callback);
    transfer->reply = m_network.get(request);
    transfer->timer = new QTimer(transfer->reply);
    transfer->timer->setSingleShot(true);
    m_transfers.push_back(transfer);

    QNetworkReply* reply = transfer->reply;
    const qint64 limit = maxBytes();
    // The handlers hang on the reply, so they go when it does.
    connect(transfer->timer, &QTimer::timeout, reply, [this, transfer] { if (!transfer->finished) finishTransfer(transfer, false); });
    connect(reply, &QNetworkReply::redirected, reply, [this, transfer](const QUrl& target) {
        if (transfer->finished) return;
        if (urlAllowed(target)) transfer->reply->redirectAllowed();
        else finishTransfer(transfer, false);
    });
    connect(reply, &QNetworkReply::metaDataChanged, reply, [this, transfer, limit] {
        if (transfer->finished) return;
        const QVariant length = transfer->reply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid() && length.toLongLong() > limit) finishTransfer(transfer, false);
    });
    connect(reply, &QNetworkReply::readyRead, reply, [this, transfer, limit] {
        if (transfer->finished) return;
        transfer->body.append(transfer->reply->readAll());
        if (transfer->body.size() > limit) finishTransfer(transfer, false);
    });
    connect(reply, &QNetworkReply::finished, reply, [this, transfer, limit] {
        if (transfer->finished) return;
        transfer->body.append(transfer->reply->readAll());
        const int status = transfer->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool good = transfer->reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 &&
                          transfer->body.size() <= limit;
        finishTransfer(transfer, good);
    });
    transfer->timer->start(timeoutMs());
}

void RemotePlaylists::finishTransfer(const std::shared_ptr<Transfer>& transfer, bool ok) {
    if (transfer->finished) return;
    transfer->finished = true;
    transfer->timer->stop();
    if (!transfer->reply->isFinished()) transfer->reply->abort();   // a limit was passed: the connection is dropped
    transfer->reply->deleteLater();
    std::erase(m_transfers, transfer);
    const auto callback = std::move(transfer->callback);
    callback(ok ? std::optional<QByteArray>(transfer->body) : std::nullopt);
}

void RemotePlaylists::afterConfig(const std::shared_ptr<Run>& run, std::optional<QByteArray> body) {
    if (!body) {
        run->outcome.configFailed = true;
        finishRun(run);
        return;
    }
    const ConfigParse config = parseConfig(*body, [this](const QUrl& url) { return urlAllowed(url); });
    if (!config.ok) {
        run->outcome.configFailed = true;
        finishRun(run);
        return;
    }
    run->outcome.koFailed = config.ko.state == UrlChoice::State::Invalid;
    run->outcome.enFailed = config.en.state == UrlChoice::State::Invalid;
    run->pending = (config.ko.state == UrlChoice::State::Use ? 1 : 0) + (config.en.state == UrlChoice::State::Use ? 1 : 0);
    if (run->pending == 0) {
        finishRun(run);
        return;
    }
    if (config.ko.state == UrlChoice::State::Use) {
        get(config.ko.url, [this, run](std::optional<QByteArray> answer) { afterList(run, true, std::move(answer)); });
    }
    if (config.en.state == UrlChoice::State::Use) {
        get(config.en.url, [this, run](std::optional<QByteArray> answer) { afterList(run, false, std::move(answer)); });
    }
}

void RemotePlaylists::afterList(const std::shared_ptr<Run>& run, bool korean, std::optional<QByteArray> body) {
    const ListParse parsed = body ? parseList(*body) : ListParse{};
    bool& failed = korean ? run->outcome.koFailed : run->outcome.enFailed;
    if (!parsed.ok) {
        failed = true;
    } else {
        (korean ? run->outcome.ko : run->outcome.en) = parsed.entries;
        run->outcome.dropped += parsed.dropped;
    }
    if (--run->pending == 0) finishRun(run);
}

void RemotePlaylists::finishRun(const std::shared_ptr<Run>& run) {
    m_running = false;
    m_run.reset();
    // Only the number: the names and IDs of what was thrown away are not written anywhere.
    if (run->outcome.dropped > 0) qCInfo(lcPlaylists, "remote playlists: dropped %d untrustworthy entries", run->outcome.dropped);
    emit done(run->outcome);
}

}  // namespace xgs::ui
