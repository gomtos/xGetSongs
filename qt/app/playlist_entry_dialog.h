#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include <optional>

#include "playlist_store.h"

class QLabel;
class QLineEdit;

// The 추가 / 수정 dialog of design spec §5.2: a name and a playlist ID or address. 저장 checks them with ui::checkEntry; what it
// refuses stays open with the reason under the fields.
namespace xgs::app {

class PlaylistEntryDialog final : public QDialog {
    Q_OBJECT
public:
    /// initial fills the fields (empty when adding); current and editingIndex are what checkEntry needs (-1 when adding).
    PlaylistEntryDialog(const QString& title, const ui::Playlist& initial, QList<ui::Playlist> current, int editingIndex,
                        QWidget* parent = nullptr);

    /// The entry, once 저장 accepted it (the ID taken out of an address).
    std::optional<ui::Playlist> entry() const { return m_entry; }

private:
    void save();

    QList<ui::Playlist> m_current;
    int m_editingIndex;
    QLineEdit* m_name;
    QLineEdit* m_id;
    QLabel* m_reason;
    std::optional<ui::Playlist> m_entry;
};

}  // namespace xgs::app
