#pragma once

#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

#include "app_api.h"
#include "app_state_holder.h"
#include "playlist_store.h"
#include "playlists_dialog.h"
#include "remote_playlists.h"
#include "settings_store.h"
#include "ui_types.h"

class QCheckBox;
class QDialog;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;

// The window of the app: it draws what the AppStateHolder says and tells the holder what the user does, as shell/src/main.ts
// did for shell/index.html (the same elements, texts and enabled rules), with the two additions of the Qt build: the
// `플레이리스트 ▾` button left of the input (design spec §5.2) and the `정보` button at the top right of the tools card (§6).
namespace xgs::app {

/// What the window asks of the desktop; the tests answer without dialogs.
struct MainWindowPrompts {
    /// The yt-dlp install question (index.html's install-dialog): true to download.
    std::function<bool(QWidget* parent)> confirmInstall;
    /// The system's folder chooser starting in initial when that is a folder; nothing when cancelled.
    std::function<std::optional<QString>(QWidget* parent, const QString& initial)> pickFolder;
    /// Shows the folder (the nearest one that exists) in the file manager. Never fails.
    std::function<void(const QString& path)> openFolder;
    /// Runs a dialog of the window (the playlists and about dialogs) until it closes.
    std::function<void(QDialog& dialog)> runDialog;
    /// What the playlists dialog asks.
    PlaylistsDialogPrompts playlists;

    static MainWindowPrompts real();
};

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    /// Everything is referenced and must outlive the window. The window makes the holder (which loads the settings) and asks
    /// for the tools at once.
    MainWindow(ui::AppApi& api, ui::SettingsStore& settings, ui::PlaylistStore& playlists, ui::RemotePlaylists& remote,
               MainWindowPrompts prompts = MainWindowPrompts::real(), QWidget* parent = nullptr);
    ~MainWindow() override;

    ui::AppStateHolder& holder() { return *m_holder; }
    QMenu* playlistMenu() const { return m_playlistMenu; }

    /// A second copy of the app was started: this window comes to the front (as the Tauri app's single-instance callback did).
    void bringToFront();

signals:
    /// The window has closed, after the options were written: the app ends now.
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void render();
    void renderTools(const ui::UiState& state);
    void renderList(const ui::UiState& state);
    void patchRow(int index, const ui::ItemRow& row);
    void renderActions(const ui::UiState& state);
    void showText(QLineEdit* box, const QString& value);
    bool composing(const QObject* box) const;
    void filterRank();
    void fillPlaylistMenu();
    void managePlaylists();
    void pickFolder();
    void confirmInstall();
    void showAbout();
    void submit();

    ui::AppApi& m_api;
    ui::PlaylistStore& m_playlistStore;
    ui::RemotePlaylists& m_remote;
    MainWindowPrompts m_prompts;
    std::unique_ptr<ui::AppStateHolder> m_holder;

    // The text boxes in which an IME composes right now (index.html: compositionstart/compositionend).
    QSet<const QObject*> m_composing;

    QFrame* m_toolsCard = nullptr;
    QWidget* m_toolsContent = nullptr;
    QPushButton* m_about = nullptr;
    QToolButton* m_playlistButton = nullptr;
    QMenu* m_playlistMenu = nullptr;
    QLineEdit* m_input = nullptr;
    QPushButton* m_resolve = nullptr;
    QFrame* m_errorBanner = nullptr;
    QLabel* m_errorText = nullptr;
    QWidget* m_resolved = nullptr;
    QWidget* m_spacer = nullptr;
    QLabel* m_infoTitle = nullptr;
    QLabel* m_truncated = nullptr;
    QPushButton* m_videoOnly = nullptr;
    QWidget* m_options = nullptr;
    QWidget* m_optionsMore = nullptr;
    QLineEdit* m_outputDir = nullptr;
    QWidget* m_destinationRow = nullptr;
    QLabel* m_destination = nullptr;
    QPushButton* m_openFolder = nullptr;
    QLineEdit* m_album = nullptr;
    QCheckBox* m_optOverwrite = nullptr;
    QCheckBox* m_optRank = nullptr;
    QCheckBox* m_optLyrics = nullptr;
    QWidget* m_rankField = nullptr;
    QLineEdit* m_singleRank = nullptr;
    QTreeWidget* m_list = nullptr;
    QWidget* m_actions = nullptr;
    QHBoxLayout* m_actionsLayout = nullptr;

    // What is shown, so that only what changed is drawn again (main.ts: shownTools, shownRows, shownActions, syncedRank).
    struct ShownTools {
        std::optional<ToolsStatus> tools;
        bool busy = false;
        std::optional<QString> message;
        bool running = false;
        bool operator==(const ShownTools&) const = default;
    };
    std::optional<ShownTools> m_shownTools;
    QList<ui::ItemRow> m_shownRows;
    std::optional<QString> m_shownActions;
    std::optional<int> m_syncedRank;
    QList<ui::Playlist> m_playlists;
};

}  // namespace xgs::app
