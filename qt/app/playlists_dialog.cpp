#include "playlists_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "playlist_entry_dialog.h"
#include "playlist_merge.h"

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcPlaylists, "xgs.playlists")

namespace xgs::app {

namespace {

QPushButton* makeButton(const QString& name, const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAutoDefault(false);
    return button;
}

QString saveFailedText() {
    return u"목록을 저장하지 못했습니다."_s;
}

}  // namespace

PlaylistsDialogPrompts PlaylistsDialogPrompts::real() {
    PlaylistsDialogPrompts prompts;
    prompts.editEntry = [](QWidget* parent, const QString& title, const ui::Playlist& initial, const QList<ui::Playlist>& current,
                           int editingIndex) -> std::optional<ui::Playlist> {
        PlaylistEntryDialog dialog(title, initial, current, editingIndex, parent);
        if (dialog.exec() != QDialog::Accepted) return std::nullopt;
        return dialog.entry();
    };
    prompts.confirmDelete = [](QWidget* parent, const QString& name) {
        QMessageBox box(QMessageBox::Question, u"삭제"_s, u"‘"_s + name + u"’을(를) 목록에서 지울까요?"_s, QMessageBox::NoButton, parent);
        QPushButton* yes = box.addButton(u"삭제"_s, QMessageBox::AcceptRole);
        QPushButton* no = box.addButton(u"취소"_s, QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.exec();
        return box.clickedButton() == yes;
    };
    return prompts;
}

PlaylistsDialog::PlaylistsDialog(ui::PlaylistStore& store, ui::RemotePlaylists& remote, QList<ui::Playlist> list,
                                 PlaylistsDialogPrompts prompts, QWidget* parent)
    : QDialog(parent), m_store(store), m_remote(remote), m_list(std::move(list)), m_prompts(std::move(prompts)) {
    setObjectName(u"playlistsDialog"_s);
    setWindowTitle(u"목록 관리"_s);
    auto* layout = new QVBoxLayout(this);
    auto* body = new QHBoxLayout();
    m_view = new QListWidget(this);
    m_view->setObjectName(u"list"_s);
    m_view->setMinimumSize(360, 280);
    body->addWidget(m_view, 1);

    auto* side = new QVBoxLayout();
    m_add = makeButton(u"add"_s, u"추가"_s, this);
    m_edit = makeButton(u"edit"_s, u"수정"_s, this);
    m_remove = makeButton(u"remove"_s, u"삭제"_s, this);
    m_up = makeButton(u"up"_s, u"위로"_s, this);
    m_down = makeButton(u"down"_s, u"아래로"_s, this);
    m_reset = makeButton(u"reset"_s, u"초기화"_s, this);
    m_reset->setToolTip(u"원격 목록을 받아 지금 목록에 없는 것만 끝에 더합니다."_s);
    for (QPushButton* button : {m_add, m_edit, m_remove}) side->addWidget(button);
    side->addSpacing(12);
    side->addWidget(m_up);
    side->addWidget(m_down);
    side->addSpacing(12);
    side->addWidget(m_reset);
    side->addStretch(1);
    body->addLayout(side);
    layout->addLayout(body, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(u"status"_s);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    m_close = makeButton(u"close"_s, u"닫기"_s, this);
    bottom->addWidget(m_close);
    layout->addLayout(bottom);

    connect(m_add, &QPushButton::clicked, this, &PlaylistsDialog::add);
    connect(m_edit, &QPushButton::clicked, this, &PlaylistsDialog::edit);
    connect(m_remove, &QPushButton::clicked, this, &PlaylistsDialog::remove);
    connect(m_up, &QPushButton::clicked, this, [this] { move(-1); });
    connect(m_down, &QPushButton::clicked, this, [this] { move(1); });
    connect(m_reset, &QPushButton::clicked, this, &PlaylistsDialog::reset);
    connect(m_close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_view, &QListWidget::currentRowChanged, this, &PlaylistsDialog::updateButtons);
    connect(m_view, &QListWidget::itemDoubleClicked, this, [this] {
        if (!m_fetching) edit();
    });
    connect(&m_remote, &ui::RemotePlaylists::done, this, &PlaylistsDialog::merged);

    showList(m_list.isEmpty() ? -1 : 0);
}

PlaylistsDialog::~PlaylistsDialog() {
    if (m_fetching) m_remote.cancel();
}

void PlaylistsDialog::done(int result) {
    if (m_fetching) {
        m_fetching = false;
        m_remote.cancel();
        m_status->clear();
        updateButtons();
    }
    QDialog::done(result);
}

int PlaylistsDialog::selectedRow() const {
    const int row = m_view->currentRow();
    return row >= 0 && row < m_list.size() ? row : -1;
}

void PlaylistsDialog::showList(int current) {
    m_view->clear();
    for (const ui::Playlist& entry : m_list) {
        auto* item = new QListWidgetItem(entry.name, m_view);
        item->setToolTip(entry.pid);
    }
    m_view->setCurrentRow(current);
    updateButtons();
}

void PlaylistsDialog::updateButtons() {
    const int row = selectedRow();
    const bool idle = !m_fetching;
    m_view->setEnabled(idle);
    m_add->setEnabled(idle);
    m_edit->setEnabled(idle && row >= 0);
    m_remove->setEnabled(idle && row >= 0);
    m_up->setEnabled(idle && row > 0);
    m_down->setEnabled(idle && row >= 0 && row < m_list.size() - 1);
    m_reset->setEnabled(idle);
}

void PlaylistsDialog::commit(const QList<ui::Playlist>& list, int current) {
    m_list = list;
    const bool saved = m_store.save(m_list);
    m_status->setText(saved ? QString() : saveFailedText());
    showList(current);
    Q_EMIT changed(m_list);
}

void PlaylistsDialog::add() {
    const std::optional<ui::Playlist> entry = m_prompts.editEntry(this, u"플레이리스트 추가"_s, ui::Playlist{}, m_list, -1);
    if (!entry) return;
    QList<ui::Playlist> list = m_list;
    list.append(*entry);
    commit(list, int(list.size()) - 1);
}

void PlaylistsDialog::edit() {
    const int row = selectedRow();
    if (row < 0) return;
    const std::optional<ui::Playlist> entry = m_prompts.editEntry(this, u"플레이리스트 수정"_s, m_list.at(row), m_list, row);
    if (!entry) return;
    QList<ui::Playlist> list = m_list;
    list[row] = *entry;
    commit(list, row);
}

void PlaylistsDialog::remove() {
    const int row = selectedRow();
    if (row < 0) return;
    if (!m_prompts.confirmDelete(this, m_list.at(row).name)) return;
    QList<ui::Playlist> list = m_list;
    list.removeAt(row);
    commit(list, list.isEmpty() ? -1 : std::min(row, int(list.size()) - 1));
}

void PlaylistsDialog::move(int by) {
    const int row = selectedRow();
    const int target = row + by;
    if (row < 0 || target < 0 || target >= m_list.size()) return;
    QList<ui::Playlist> list = m_list;
    list.move(row, target);
    commit(list, target);
}

void PlaylistsDialog::reset() {
    if (m_fetching) return;
    m_fetching = true;
    m_status->setText(u"받는 중…"_s);
    updateButtons();
    m_close->setFocus();
    m_remote.fetch();
}

void PlaylistsDialog::merged(const ui::RemoteOutcome& outcome) {
    if (!m_fetching) return;
    m_fetching = false;
    if (outcome.dropped > 0) qCInfo(lcPlaylists) << "원격 목록에서 버린 항목" << outcome.dropped << "개";
    if (outcome.configFailed) {
        m_status->setText(u"원격 설정을 받지 못했습니다. 목록은 그대로입니다."_s);
        updateButtons();
        return;
    }
    const ui::MergeResult result = ui::mergeRemote(m_list, outcome.ko, outcome.en);
    QStringList lines{u"%1개를 더했습니다 (이미 있는 것 %2개)"_s.arg(result.added).arg(result.alreadyThere)};
    if (outcome.koFailed) lines.append(u"한국어 목록을 받지 못했습니다"_s);
    if (outcome.enFailed) lines.append(u"영어 목록을 받지 못했습니다"_s);
    if (result.added > 0) {
        m_list = result.list;
        if (!m_store.save(m_list)) lines.append(saveFailedText());
        showList(selectedRow() >= 0 ? selectedRow() : 0);
        Q_EMIT changed(m_list);
    } else {
        updateButtons();
    }
    m_status->setText(lines.join(u'\n'));
}

}  // namespace xgs::app
