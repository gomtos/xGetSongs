#include <QApplication>
#include <QFile>

#include <memory>

#include "about_dialog.h"
#include "app_services.h"
#include "diagnostics.h"
#include "main_window.h"
#include "playlist_store.h"
#include "settings_store.h"
#include "single_instance.h"

// xGetSongs (the Qt build): one copy at a time (shared with the Tauri build), the settings and playlists of %APPDATA%\xGetSongs,
// the real engine, and the window. The app ends when the window closes, whatever else is open (the hidden WebView2 window of the
// Google lookup is no Qt window and never keeps it alive); the engine is taken down after the event loop, in order. The diagnostics
// (the log file, the records, the crash and hang dumps; diagnostics.h) start right after the single-instance check, so a second copy
// writes nothing into the first one's files, and before everything else.
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("xGetSongs"));
    QApplication::setApplicationVersion(xgs::app::AboutDialog::appVersion());
    QApplication::setQuitOnLastWindowClosed(false);

    // Check tools set XGS_CHECK_INSTANCE_ID (with a fake APPDATA and LOCALAPPDATA) so a check never meets the user's running app.
    const QString checkId = qEnvironmentVariable("XGS_CHECK_INSTANCE_ID");
    xgs::app::SingleInstance single(checkId.isEmpty() ? xgs::app::SingleInstance::productId() : checkId);
    if (!single.isFirst()) return 0;

    const xgs::app::AppPaths paths = xgs::app::AppPaths::fromEnvironment();
    const std::unique_ptr<xgs::app::Diagnostics> diagnostics = xgs::app::Diagnostics::start(paths);
    const xgs::diag::LogSink log = diagnostics ? diagnostics->sink() : xgs::diag::LogSink();

    QFile style(QStringLiteral(":/xgs/app.qss"));
    if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));

    xgs::ui::SettingsStore settings(paths.settingsFile());
    xgs::ui::PlaylistStore playlists(paths.playlistsFile());
    auto services = std::make_unique<xgs::app::AppServices>(paths, log);
    if (diagnostics) diagnostics->setRunningJobs([target = services.get()] { return target->runningJobs(); });
    auto window = std::make_unique<xgs::app::MainWindow>(services->api(), settings, playlists, services->remotePlaylists());
    single.onSecondLaunch([target = window.get()] { target->bringToFront(); });
    QObject::connect(window.get(), &xgs::app::MainWindow::closed, &app, [&diagnostics] {
        if (diagnostics) diagnostics->markUserExit();
        QCoreApplication::quit();
    });
    window->show();
    const int code = app.exec();
    // Before the teardown: its waits (up to about 1.5 s for a job) are not a hang. The job count of the exit record is the one
    // the user left behind (the teardown cancels them all).
    if (diagnostics) {
        diagnostics->stopWatching();
        const int jobs = services->runningJobs();
        diagnostics->setRunningJobs([jobs] { return jobs; });
    }

    // A copy started from now on finds no window to tell: it waits until this one has ended and then runs (SingleInstance).
    single.stopListening();
    // The screen first (the holder), then the engine (the Google channel is closed first, then EngineApi stops and waits for what
    // runs, up to about 1.5 s for a job), outside every callback of theirs.
    window.reset();
    services.reset();
    if (diagnostics) diagnostics->logExit();
    return code;
}
