#include "app_services.h"

#include <QDir>
#include <QLoggingCategory>
#include <QNetworkAccessManager>

#include <utility>

#include "engine_api.h"
#include "job/default_download_service.h"
#include "job/item_downloader.h"
#include "lyrics/google/browser_channel.h"
#include "lyrics/google/google_lyrics_provider.h"
#include "lyrics/lrclib_lyrics_provider.h"
#include "lyrics/lyrics_provider.h"
#include "net/http_fetch.h"
#include "net/qt_http_client.h"
#include "output/local_folder_sink.h"
#include "process/system_process_runner.h"
#include "remote_playlists.h"
#include "tools/tool_locator.h"
#include "tools/tool_manager.h"
#include "webview/lyrics_browser.h"
#include "webview/runtime.h"
#include "ytdlp/ytdlp_resolver.h"

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcApp, "xgs.app")
Q_LOGGING_CATEGORY(lcGoogle, "xgs.lyrics.google")

namespace xgs::app {

namespace {

/// dir and name joined with one backslash; dir loses the separators it ends with (a drive root keeps its own).
QString join(const QString& dir, const QString& name) {
    QString base = QDir::toNativeSeparators(dir);
    while (base.size() > 3 && base.endsWith(u'\\')) base.chop(1);
    if (!base.endsWith(u'\\')) base += u'\\';
    return base + name;
}

char16_t asciiLower(char16_t c) {
    return (c >= u'A' && c <= u'Z') ? char16_t(c - u'A' + u'a') : c;
}

/// The drive of a path as a key: `c:` for `C:\…`, `\\server\share` (lower case) for a UNC path, empty when it has neither.
QString driveKey(const QString& path) {
    const QString native = QDir::toNativeSeparators(path);
    QString key;
    if (native.startsWith(u"\\\\"_s)) {
        int separators = 0;
        for (qsizetype i = 2; i < native.size(); ++i) {
            if (native.at(i) == u'\\' && ++separators == 2) break;
            key += QChar(asciiLower(native.at(i).unicode()));
        }
        return u"\\\\"_s + key;
    }
    if (native.size() >= 2 && native.at(1) == u':') return QString(QChar(asciiLower(native.at(0).unicode()))) + u':';
    return QString();
}

/// The download service of the app: a DefaultDownloadService per job whose work folder is on the drive of the job's output
/// folder (workRootFor). DefaultDownloadService holds nothing a job needs besides the downloader (a job copies the rest), so it
/// may go when start returns.
class SameDriveDownloadService final : public job::DownloadService {
public:
    SameDriveDownloadService(job::ItemDownloader& downloader, QString workDir) : m_downloader(downloader), m_workDir(std::move(workDir)) {}

    std::unique_ptr<job::JobHandle> start(job::DownloadRequest request, std::stop_source stop) override {
        QString root = m_workDir;
        if (const auto* sink = dynamic_cast<const output::LocalFolderSink*>(request.sink)) root = workRootFor(sink->directory(), m_workDir);
        job::DefaultDownloadService service(m_downloader, root);
        return service.start(std::move(request), std::move(stop));
    }

private:
    job::ItemDownloader& m_downloader;
    QString m_workDir;
};

/// The lines of the Google lookup (counts and result names, never a song or lyrics) for the app's log. The log file is Q5's;
/// until then they go to Qt's message handler.
class GoogleLog final : public lyrics::google::GoogleLyricsLog {
public:
    void info(const QString& message) override { qCInfo(lcGoogle).noquote() << message; }
    void warn(const QString& message) override { qCWarning(lcGoogle).noquote() << message; }
};

}  // namespace

AppPaths AppPaths::fromEnvironment() {
    const QString profile = qEnvironmentVariable("USERPROFILE");
    return fromValues(qEnvironmentVariable("APPDATA"), qEnvironmentVariable("LOCALAPPDATA"),
                      profile.isEmpty() ? std::nullopt : std::optional<QString>(profile));
}

AppPaths AppPaths::fromValues(const QString& appDataVariable, const QString& localAppDataVariable,
                              const std::optional<QString>& userProfileVariable) {
    AppPaths paths;
    if (!appDataVariable.isEmpty()) {
        paths.appData = join(appDataVariable, u"xGetSongs"_s);
    } else {
        const QString home = userProfileVariable && !userProfileVariable->isEmpty() ? *userProfileVariable : QDir::tempPath();
        paths.appData = join(home, u".xgetsongs"_s);
    }
    paths.localAppData = localAppDataVariable.isEmpty() ? paths.appData : join(localAppDataVariable, u"xGetSongs"_s);
    return paths;
}

QString AppPaths::settingsFile() const {
    return join(appData, u"settings.json"_s);
}

QString AppPaths::playlistsFile() const {
    return join(appData, u"playlists.json"_s);
}

QString AppPaths::binDir() const {
    return join(appData, u"bin"_s);
}

QString AppPaths::workDir() const {
    return join(appData, u"work"_s);
}

QString AppPaths::webViewDataFolder() const {
    return join(localAppData, u"qt-webview2"_s);
}

QString workRootFor(const QString& destination, const QString& workDir) {
    const QString drive = driveKey(destination);
    return !drive.isEmpty() && drive == driveKey(workDir) ? workDir : destination;
}

/// The parts in the order they are made; ~AppServices takes them apart in the reverse order, one by one.
struct AppServices::Parts {
    std::unique_ptr<net::QtHttpClient> http;
    std::unique_ptr<process::SystemProcessRunner> runner;
    std::unique_ptr<tools::ToolLocator> locator;
    std::unique_ptr<tools::ToolManager> tools;
    std::unique_ptr<ytdlp::YtDlpResolver> resolver;
    std::unique_ptr<GoogleLog> googleLog;
    std::unique_ptr<lyrics::google::BrowserChannel> channel;
    std::unique_ptr<webview::LyricsBrowser> browser;
    std::unique_ptr<lyrics::LrclibLyricsProvider> lrclib;
    std::unique_ptr<lyrics::google::GoogleLyricsProvider> google;
    std::unique_ptr<lyrics::FallbackLyricsProvider> lyrics;
    std::unique_ptr<job::ItemDownloader> downloader;
    std::unique_ptr<SameDriveDownloadService> downloads;
    std::unique_ptr<ui::EngineApi> api;
    std::unique_ptr<QNetworkAccessManager> network;
    std::unique_ptr<ui::RemotePlaylists> remote;
};

AppServices::AppServices(const AppPaths& paths) : m_parts(std::make_unique<Parts>()) {
    Parts& p = *m_parts;
    // One HTTP client for LRCLIB and the yt-dlp download (the Q2c hand-over).
    p.http = std::make_unique<net::QtHttpClient>();
    p.runner = std::make_unique<process::SystemProcessRunner>();
    p.locator = std::make_unique<tools::ToolLocator>(paths.binDir());
    p.tools = std::make_unique<tools::ToolManager>(*p.locator, *p.runner, paths.binDir(), net::httpFetch(*p.http));
    p.resolver = std::make_unique<ytdlp::YtDlpResolver>(*p.runner, *p.locator);

    // The lyrics: the description (inside the downloader), then LRCLIB, then Google's lyrics card read by the hidden WebView2 window,
    // as the Kotlin server wired them (LocalServer.createServices). One of each for the whole app, so the polite intervals and the
    // cool-downs hold across the songs of a playlist and across jobs.
    p.lrclib = std::make_unique<lyrics::LrclibLyricsProvider>(*p.http);
    std::vector<lyrics::LyricsProvider*> providers{p.lrclib.get()};
    if (const std::optional<QString> runtime = webview::runtimeVersion()) {
        p.googleLog = std::make_unique<GoogleLog>();
        p.channel = std::make_unique<lyrics::google::BrowserChannel>();
        p.browser = std::make_unique<webview::LyricsBrowser>(*p.channel, webview::HiddenWebViewOptions{paths.webViewDataFolder()});
        p.google = std::make_unique<lyrics::google::GoogleLyricsProvider>(*p.channel, p.googleLog.get());
        providers.push_back(p.google.get());
    } else {
        qCInfo(lcApp) << "WebView2 런타임이 없어 구글 가사 검색을 쓰지 않음";
    }
    p.lyrics = std::make_unique<lyrics::FallbackLyricsProvider>(std::move(providers));
    p.downloader = std::make_unique<job::ItemDownloader>(*p.runner, *p.locator, *p.resolver, *p.lyrics);
    p.downloads = std::make_unique<SameDriveDownloadService>(*p.downloader, paths.workDir());
    p.api = std::make_unique<ui::EngineApi>(*p.runner, *p.tools, *p.resolver, *p.downloads, paths.workDir());

    p.network = std::make_unique<QNetworkAccessManager>();
    p.remote = std::make_unique<ui::RemotePlaylists>(*p.network);
}

AppServices::~AppServices() {
    Parts& p = *m_parts;
    p.remote.reset();
    p.network.reset();
    // Stops and waits for everything the bridge started: each job handle cancels and joins its job, and no answer is delivered.
    p.api.reset();
    p.downloads.reset();
    p.downloader.reset();
    p.lyrics.reset();
    // Nothing looks lyrics up any more. Closing the provider closes the channel (Kotlin: on the end of the server's scope); then
    // the hidden browser goes (its window and browser processes with it), then the channel.
    if (p.google) p.google->close();
    p.google.reset();
    p.lrclib.reset();
    p.browser.reset();
    p.channel.reset();
    p.googleLog.reset();
    p.resolver.reset();
    p.tools.reset();
    p.locator.reset();
    p.runner.reset();
    p.http.reset();
}

ui::AppApi& AppServices::api() {
    return *m_parts->api;
}

ui::RemotePlaylists& AppServices::remotePlaylists() {
    return *m_parts->remote;
}

bool AppServices::googleLyrics() const {
    return m_parts->google != nullptr;
}

}  // namespace xgs::app
