#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

#include "playlist_store.h"
#include "remote_playlists.h"

class QLabel;
class QMessageBox;
class QListWidget;
class QPushButton;

// The 목록 관리… dialog of design spec §5.2: the list on the left, 추가, 수정, 삭제, 위로, 아래로 and 초기화 on the right, 닫기 below.
// Every change is written to the playlist file at once (there is no save button). 초기화 merges the remote lists (§5.4, §5.5):
// while they come every button but 닫기 is off and `받는 중…` shows; closing the dialog cancels the transfer.
namespace xgs::app {

/// The questions the dialog asks; the tests answer them without a window.
struct PlaylistsDialogPrompts {
    /// The add/edit dialog: the entry it accepted, nothing when cancelled. editingIndex is -1 when adding.
    std::function<std::optional<ui::Playlist>(QWidget* parent, const QString& title, const ui::Playlist& initial,
                                              const QList<ui::Playlist>& current, int editingIndex)>
        editEntry;
    /// Whether the entry of that name may be deleted.
    std::function<bool(QWidget* parent, const QString& name)> confirmDelete;

    /// The real dialogs (PlaylistEntryDialog and a question box).
    static PlaylistsDialogPrompts real();
};

/// The question before an entry is deleted: `삭제` / `취소`, the name shown as plain text. The 삭제 button is the child `yes`.
std::unique_ptr<QMessageBox> deleteQuestion(QWidget* parent, const QString& name);

class PlaylistsDialog final : public QDialog {
    Q_OBJECT
public:
    /// store and remote are referenced and must outlive the dialog; list is what the store holds now.
    PlaylistsDialog(ui::PlaylistStore& store, ui::RemotePlaylists& remote, QList<ui::Playlist> list,
                    PlaylistsDialogPrompts prompts = PlaylistsDialogPrompts::real(), QWidget* parent = nullptr);
    ~PlaylistsDialog() override;

    QList<ui::Playlist> playlists() const { return m_list; }

    /// Every way of closing ends here: a transfer that runs is cancelled.
    void done(int result) override;

signals:
    /// After each change of the list (it has been written, or the write failed and the status says so).
    void changed(const QList<xgs::ui::Playlist>& list);

private:
    void add();
    void edit();
    void remove();
    void move(int by);
    void reset();
    void merged(const ui::RemoteOutcome& outcome);
    void commit(const QList<ui::Playlist>& list, int current);
    void showList(int current);
    void updateButtons();
    int selectedRow() const;

    ui::PlaylistStore& m_store;
    ui::RemotePlaylists& m_remote;
    QList<ui::Playlist> m_list;
    PlaylistsDialogPrompts m_prompts;
    bool m_fetching = false;
    QListWidget* m_view;
    QLabel* m_status;
    QPushButton* m_add;
    QPushButton* m_edit;
    QPushButton* m_remove;
    QPushButton* m_up;
    QPushButton* m_down;
    QPushButton* m_reset;
    QPushButton* m_close;
};

}  // namespace xgs::app
