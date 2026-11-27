#include "playlist_entry_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "playlist_entry_check.h"

using namespace Qt::StringLiterals;

namespace xgs::app {

PlaylistEntryDialog::PlaylistEntryDialog(const QString& title, const ui::Playlist& initial, QList<ui::Playlist> current,
                                         int editingIndex, QWidget* parent)
    : QDialog(parent), m_current(std::move(current)), m_editingIndex(editingIndex) {
    setObjectName(u"playlistEntryDialog"_s);
    setWindowTitle(title);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_name = new QLineEdit(initial.name, this);
    m_name->setObjectName(u"name"_s);
    m_id = new QLineEdit(initial.pid, this);
    m_id->setObjectName(u"id"_s);
    m_id->setMinimumWidth(360);
    form->addRow(u"이름"_s, m_name);
    form->addRow(u"재생목록 ID 또는 주소"_s, m_id);
    layout->addLayout(form);

    m_reason = new QLabel(this);
    m_reason->setObjectName(u"reason"_s);
    m_reason->setProperty("tone", u"bad"_s);
    m_reason->setWordWrap(true);
    m_reason->setTextFormat(Qt::PlainText);
    m_reason->hide();
    layout->addWidget(m_reason);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* save = buttons->addButton(u"저장"_s, QDialogButtonBox::AcceptRole);
    save->setObjectName(u"save"_s);
    save->setDefault(true);
    QPushButton* cancel = buttons->addButton(u"취소"_s, QDialogButtonBox::RejectRole);
    cancel->setObjectName(u"cancel"_s);
    connect(buttons, &QDialogButtonBox::accepted, this, &PlaylistEntryDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void PlaylistEntryDialog::save() {
    const ui::EntryCheck check = ui::checkEntry(m_name->text(), m_id->text(), m_current, m_editingIndex);
    if (!check.entry) {
        m_reason->setText(check.reason);
        m_reason->show();
        return;
    }
    m_entry = check.entry;
    accept();
}

}  // namespace xgs::app
