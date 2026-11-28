#pragma once

#include <QString>

#include <memory>
#include <optional>

#include "diag/log_sink.h"

class QNetworkAccessManager;

namespace xgs::ui {
class AppApi;
class RemotePlaylists;
}  // namespace xgs::ui

// The real engine of the app, put together and taken apart in order.
namespace xgs::app {

/// Where the app keeps its files, the folders the Tauri app used (sidecar-host/src/config.rs): the app data folder is
/// `%APPDATA%\xGetSongs`, else `.xgetsongs` in the user's folder (else in the temp folder); the folders that do not belong in the
/// roaming profile go to `%LOCALAPPDATA%\xGetSongs`, else into the app data folder. Read once at the start, from the environment,
/// so that a check can point APPDATA and LOCALAPPDATA at a temp folder. All paths have backslashes.
struct AppPaths {
    QString appData;
    QString localAppData;

    /// From the environment variables APPDATA, LOCALAPPDATA and USERPROFILE (an empty one counts as missing).
    static AppPaths fromEnvironment();
    /// The same from given values (empty or nothing: missing).
    static AppPaths fromValues(const QString& appDataVariable, const QString& localAppDataVariable,
                               const std::optional<QString>& userProfileVariable);

    /// The settings of the screen (the Compose and Tauri apps' file).
    QString settingsFile() const;
    /// The user's playlists (design spec §5.3).
    QString playlistsFile() const;
    /// The yt-dlp the app installed (the Kotlin server's `binDirOf(appDataDir)`), found before PATH.
    QString binDir() const;
    /// Where the downloads are made before they are moved to the output folder when that is on the same drive (the Kotlin
    /// server's `work`; see workRootFor).
    QString workDir() const;
    /// The WebView2 user-data folder of the hidden Google window: its own, not the Tauri app's (Q3 report).
    QString webViewDataFolder() const;
};

/// The work folder of a job that saves into destination: workDir when it is on the same drive (or the same `\\server\share`)
/// as destination, so the finished files are moved, not copied; else destination itself (the job makes its `job-…` folder in
/// it and removes it at the end).
QString workRootFor(const QString& destination, const QString& workDir);

/// Removes what an earlier run left in workDir (the Kotlin server's `workDir.toFile().deleteRecursively()` in createServices):
/// everything inside it, never following a link or junction out of it (the link itself goes, its target stays). What cannot be
/// removed stays and is one WARN line (`xgs.app`) with the count, never a failure; nothing is said when the folder is empty or
/// missing. Call it only after the single-instance check (a running first copy may be using the folder).
void clearStaleWork(const QString& workDir, const diag::LogSink& log);

/// Makes the real parts on the GUI thread and takes them apart in the order the engine and the Google lookup need (the hand-over
/// lists of the Q2d and Q3 reports): the EngineApi (which stops and waits for everything that runs), the download service, the
/// downloader, the lyrics providers (LRCLIB, then Google; the description comes first inside the downloader), the hidden browser
/// (on the GUI thread, never from inside one of its own callbacks), the channel, the runner and the tools, the HTTP client.
/// Making it starts no lookup, runs no tool and shows nothing: the hidden WebView2 window is made only when a song is looked up on
/// Google. Without the WebView2 runtime there is no Google lookup (one log line), and everything else works.
class AppServices final {
public:
    /// log gets the job lines of diag::JobLog and the line of clearStaleWork (empty: nothing is logged). The work folder of the
    /// last run is cleared first (clearStaleWork).
    explicit AppServices(const AppPaths& paths, diag::LogSink log = {});
    /// Must run on the GUI thread, outside any callback of the parts (the app does it after its event loop has ended). Closes the Google
    /// channel before the jobs are cancelled, so a lookup in the hidden browser ends at once; a cancelled job can still take about
    /// 1.5 s (its work folder is removed with retries, see DefaultDownloadService).
    ~AppServices();
    AppServices(const AppServices&) = delete;
    AppServices& operator=(const AppServices&) = delete;

    ui::AppApi& api();
    /// The transfer of the remote playlists (the 초기화 of the playlists dialog), on the GUI thread's network access manager.
    ui::RemotePlaylists& remotePlaylists();
    bool googleLyrics() const;
    /// The jobs the bridge holds (running ones and finished ones whose end has not been delivered yet), for the exit record.
    int runningJobs() const;

private:
    struct Parts;
    std::unique_ptr<Parts> m_parts;
};

}  // namespace xgs::app
