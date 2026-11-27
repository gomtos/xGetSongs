#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "playlist_merge.h"

class QNetworkAccessManager;

namespace xgs::ui {

/// What fetch() found (spec §5.5). A language whose URL is empty or missing is an empty list, not a failure.
struct RemoteOutcome {
    bool configFailed = false;
    bool koFailed = false;
    bool enFailed = false;
    QList<RemoteEntry> ko;
    QList<RemoteEntry> en;
    int dropped = 0;   // entries thrown away as untrustworthy, both languages (only this count is logged)
};

inline constexpr int kRemoteTimeoutMs = 15000;           // per request
inline constexpr qint64 kRemoteMaxBytes = 1024 * 1024;   // per response

/// The address of the remote settings file.
QUrl defaultRemoteConfigUrl();

/// An address the real code may request: https, with a host and no user name or password.
bool isHttpsUrl(const QUrl& url);

/// The two list addresses in the remote settings (values.music_video_playlists_url_ko / _en); the other keys, the `//` comment
/// keys included, are not read.
struct UrlChoice {
    enum class State { Empty, Use, Invalid };   // Empty: key missing, null or blank (an empty list); Invalid: not a usable address
    State state = State::Empty;
    QUrl url;
};
struct ConfigParse {
    bool ok = false;   // the bytes are a JSON object with a `values` object
    UrlChoice ko;
    UrlChoice en;
};
ConfigParse parseConfig(const QByteArray& body, const std::function<bool(const QUrl&)>& allowed = isHttpsUrl);

/// One list: `{ "playlists": [ { "sn", "name", "pid", ... } ] }`. Each entry is cleaned (name: no control characters, trimmed,
/// at most 100 units; pid: the playlist ID the input field would take) and thrown away, counted in `dropped`, when it has no
/// whole-number `sn`, no name, or no valid pid. ok is false when the bytes are not a JSON object with a `playlists` array.
struct ListParse {
    bool ok = false;
    QList<RemoteEntry> entries;
    int dropped = 0;
};
ListParse parseList(const QByteArray& body);

/// Reads the two remote lists without blocking: one GET of the settings file, then one of each list (both at once). HTTPS only
/// (a redirect too, and only from https to https), 15 s and 1 MB per request, nothing but Qt's own headers. cancel() aborts at
/// once and done is not emitted for that run. Use it on the thread of the network access manager.
class RemotePlaylists : public QObject {
    Q_OBJECT

public:
    explicit RemotePlaylists(QNetworkAccessManager& network, QUrl configUrl = defaultRemoteConfigUrl());
    ~RemotePlaylists() override;

    /// Starts a run; ignored while one is running. fetch, cancel and isRunning are virtual so that the widget tests can stand in
    /// for the transfer without any network (qt/tests/app).
    virtual void fetch();
    virtual void cancel();
    virtual bool isRunning() const { return m_running; }

signals:
    void done(xgs::ui::RemoteOutcome outcome);

protected:
    // Test seams. The real code asks for https and the limits above; a test that talks to 127.0.0.1 overrides these in a
    // subclass. There is deliberately no public way to allow http.
    virtual bool urlAllowed(const QUrl& url) const;
    virtual int timeoutMs() const { return kRemoteTimeoutMs; }
    virtual qint64 maxBytes() const { return kRemoteMaxBytes; }

private:
    struct Transfer;
    struct Run;
    void get(const QUrl& url, std::function<void(std::optional<QByteArray>)> callback);
    void finishTransfer(const std::shared_ptr<Transfer>& transfer, bool ok);
    void afterConfig(const std::shared_ptr<Run>& run, std::optional<QByteArray> body);
    void afterList(const std::shared_ptr<Run>& run, bool korean, std::optional<QByteArray> body);
    void finishRun(const std::shared_ptr<Run>& run);

    QNetworkAccessManager& m_network;
    QUrl m_configUrl;
    bool m_running = false;
    quint64 m_generation = 0;
    std::vector<std::shared_ptr<Transfer>> m_transfers;
    std::shared_ptr<Run> m_run;
};

}  // namespace xgs::ui

Q_DECLARE_METATYPE(xgs::ui::RemoteOutcome)
