#include "main_window.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

#include <cmath>

#include "about_dialog.h"
#include "labels.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::app {

namespace {

constexpr int kNameColumn = 1;
constexpr int kStatusColumn = 2;

QLabel* makeLabel(const QString& text, QWidget* parent, const QString& name = QString()) {
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    if (!name.isEmpty()) label->setObjectName(name);
    return label;
}

QPushButton* makeButton(const QString& text, QWidget* parent, const QString& name, bool primary = false) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAutoDefault(false);
    if (primary) button->setProperty("primary", true);
    return button;
}

QLineEdit* makeLineEdit(QWidget* parent, const QString& name) {
    auto* box = new QLineEdit(parent);
    box->setObjectName(name);
    // The standard context menu (cut, copy, paste, ...) and paste stay as Qt makes them.
    box->setContextMenuPolicy(Qt::DefaultContextMenu);
    return box;
}

/// A caption above a field (index.html's `.field-label`).
QWidget* field(const QString& caption, QWidget* input, QWidget* parent, QLabel** captionOut = nullptr) {
    auto* box = new QWidget(parent);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    QLabel* label = makeLabel(caption, box);
    label->setProperty("role", u"caption"_s);
    label->setBuddy(input);
    layout->addWidget(label);
    layout->addWidget(input);
    if (captionOut) *captionOut = label;
    return box;
}

void setTone(QLabel* label, const QString& tone) {
    if (label->property("tone").toString() == tone) return;
    label->setProperty("tone", tone);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

void setTextIfChanged(QLabel* label, const QString& text) {
    if (label->text() != text) label->setText(text);
}

/// sidecar-host/src/folder.rs is_path_the_platform_takes: no `<>"|?*`, a colon only as the second character, no control character.
bool isPathThePlatformTakes(const QString& path) {
    for (qsizetype i = 0; i < path.size(); ++i) {
        const char16_t c = path.at(i).unicode();
        if (c == u'<' || c == u'>' || c == u'"' || c == u'|' || c == u'?' || c == u'*') return false;
        if (c == u':' && i != 1) return false;
        if (c < 0x20 || (c >= 0x7F && c <= 0x9F)) return false;
    }
    return true;
}

/// folder.rs existing_folder_for: the folder itself when it exists, else its nearest existing ancestor; nothing for a blank path
/// or one Windows cannot take.
std::optional<QString> existingFolderFor(const QString& path) {
    if (kt::isBlank(path) || !isPathThePlatformTakes(path)) return std::nullopt;
    QFileInfo current(QDir::current(), path);
    QString candidate = QDir::toNativeSeparators(current.absoluteFilePath());
    for (;;) {
        if (QFileInfo(candidate).isDir()) return candidate;
        const QString parent = QDir::toNativeSeparators(QFileInfo(candidate).path());
        if (parent == candidate || parent.isEmpty() || parent == u"."_s) return std::nullopt;
        candidate = parent;
    }
}

}  // namespace

MainWindowPrompts MainWindowPrompts::real() {
    MainWindowPrompts prompts;
    prompts.confirmInstall = [](QWidget* parent) {
        // index.html's install-dialog.
        QMessageBox box(QMessageBox::NoIcon, u"yt-dlp 설치"_s,
                        u"GitHub(github.com/yt-dlp/yt-dlp)에서 yt-dlp.exe를 내려받아 이 앱 전용 폴더에 저장합니다. 계속할까요?"_s,
                        QMessageBox::NoButton, parent);
        box.addButton(u"취소"_s, QMessageBox::RejectRole);
        QPushButton* install = box.addButton(u"내려받기"_s, QMessageBox::AcceptRole);
        box.setDefaultButton(install);
        box.exec();
        return box.clickedButton() == install;
    };
    prompts.pickFolder = [](QWidget* parent, const QString& initial) -> std::optional<QString> {
        // lib.rs pick_folder: the chooser starts in initial only when that is an existing folder (folder::existing_directory).
        QString start;
        if (!kt::isBlank(initial)) {
            const QFileInfo info(QDir::current(), initial);
            if (info.isDir()) start = info.absoluteFilePath();
        }
        const QString chosen = QFileDialog::getExistingDirectory(parent, u"출력 폴더 선택"_s, start);
        if (chosen.isEmpty()) return std::nullopt;
        return QDir::toNativeSeparators(chosen);
    };
    prompts.openFolder = [](const QString& path) {
        if (const std::optional<QString> folder = existingFolderFor(path)) QDesktopServices::openUrl(QUrl::fromLocalFile(*folder));
    };
    prompts.runDialog = [](QDialog& dialog) { dialog.exec(); };
    prompts.playlists = PlaylistsDialogPrompts::real();
    return prompts;
}

MainWindow::MainWindow(ui::AppApi& api, ui::SettingsStore& settings, ui::PlaylistStore& playlists, ui::RemotePlaylists& remote,
                       MainWindowPrompts prompts, QWidget* parent)
    : QMainWindow(parent), m_api(api), m_playlistStore(playlists), m_remote(remote), m_prompts(std::move(prompts)) {
    setWindowTitle(u"xGetSongs"_s);
    m_holder = std::make_unique<ui::AppStateHolder>(m_api, settings);
    buildUi();
    resize(1000, 760);
    connect(m_holder.get(), &ui::AppStateHolder::changed, this, &MainWindow::render);
    render();
    m_holder->refreshTools();
}

MainWindow::~MainWindow() {
    // The holder goes before the widgets it would draw into; nothing of it may call back any more.
    disconnect(m_holder.get(), nullptr, this, nullptr);
    m_holder.reset();
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    central->setObjectName(u"app"_s);
    auto* page = new QVBoxLayout(central);
    page->setContentsMargins(16, 16, 16, 16);
    page->setSpacing(12);

    // ---- tools card: the content follows the tools; the about button stays at the top right ----
    m_toolsCard = new QFrame(central);
    m_toolsCard->setObjectName(u"tools"_s);
    m_toolsCard->setAccessibleName(u"외부 도구"_s);
    auto* card = new QHBoxLayout(m_toolsCard);
    card->setContentsMargins(12, 10, 12, 10);
    m_toolsContent = new QWidget(m_toolsCard);
    auto* content = new QVBoxLayout(m_toolsContent);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(6);
    content->addWidget(makeLabel(u"도구 확인 중…"_s, m_toolsContent, u"toolsChecking"_s));
    auto* toolLine = new QHBoxLayout();
    toolLine->setSpacing(16);
    for (const QString& name : {u"toolYtDlp"_s, u"toolFfmpeg"_s, u"toolJs"_s}) toolLine->addWidget(makeLabel(QString(), m_toolsContent, name));
    toolLine->addStretch(1);
    content->addLayout(toolLine);
    auto* toolRow = new QHBoxLayout();
    toolRow->setSpacing(12);
    QPushButton* install = makeButton(u"yt-dlp 설치"_s, m_toolsContent, u"install"_s, true);
    QPushButton* update = makeButton(ui::updateYtDlpLabel(), m_toolsContent, u"update"_s);
    QLabel* help = makeLabel(ui::formatFailureHelp(), m_toolsContent, u"formatHelp"_s);
    help->setWordWrap(true);
    help->setProperty("small", true);
    toolRow->addWidget(install, 0, Qt::AlignTop);
    toolRow->addWidget(update, 0, Qt::AlignTop);
    toolRow->addWidget(help, 1);
    content->addLayout(toolRow);
    for (const auto& [name, text] : {std::pair{u"ffmpegMissing"_s, u"ffmpeg가 필요합니다. 예: winget install Gyan.FFmpeg"_s},
                                     std::pair{u"jsMissing"_s, u"YouTube를 읽으려면 Node.js 22 이상 또는 Deno 2.3 이상이 필요합니다."_s}}) {
        QLabel* note = makeLabel(text, m_toolsContent, name);
        note->setProperty("small", true);
        setTone(note, u"bad"_s);
        content->addWidget(note);
    }
    QLabel* busy = makeLabel(u"작업 중…"_s, m_toolsContent, u"toolBusy"_s);
    busy->setProperty("small", true);
    content->addWidget(busy);
    QLabel* message = makeLabel(QString(), m_toolsContent, u"toolMessage"_s);
    message->setProperty("small", true);
    message->setWordWrap(true);
    content->addWidget(message);
    card->addWidget(m_toolsContent, 1);
    m_about = makeButton(u"정보"_s, m_toolsCard, u"about"_s);
    m_about->setToolTip(u"앱 정보와 라이선스"_s);
    card->addWidget(m_about, 0, Qt::AlignTop);
    page->addWidget(m_toolsCard);
    connect(install, &QPushButton::clicked, this, &MainWindow::confirmInstall);
    connect(update, &QPushButton::clicked, this, [this] { m_holder->updateYtDlp(); });
    connect(m_about, &QPushButton::clicked, this, &MainWindow::showAbout);

    // ---- input row: the playlist button, the field, 조회 ----
    auto* inputRow = new QGridLayout();
    inputRow->setHorizontalSpacing(8);
    inputRow->setVerticalSpacing(4);
    m_playlistButton = new QToolButton(central);
    m_playlistButton->setObjectName(u"playlists"_s);
    m_playlistButton->setText(u"플레이리스트 ▾"_s);
    m_playlistButton->setPopupMode(QToolButton::InstantPopup);
    m_playlistButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_playlistMenu = new QMenu(m_playlistButton);
    m_playlistMenu->setObjectName(u"playlistMenu"_s);
    m_playlistButton->setMenu(m_playlistMenu);
    connect(m_playlistMenu, &QMenu::aboutToShow, this, &MainWindow::fillPlaylistMenu);
    m_input = makeLineEdit(central, u"input"_s);
    QLabel* inputCaption = makeLabel(u"재생목록 ID 또는 영상 주소"_s, central);
    inputCaption->setProperty("role", u"caption"_s);
    inputCaption->setBuddy(m_input);
    m_resolve = makeButton(u"조회"_s, central, u"resolve"_s, true);
    inputRow->addWidget(inputCaption, 0, 1);
    inputRow->addWidget(m_playlistButton, 1, 0);
    inputRow->addWidget(m_input, 1, 1);
    inputRow->addWidget(m_resolve, 1, 2);
    inputRow->setColumnStretch(1, 1);
    page->addLayout(inputRow);
    connect(m_input, &QLineEdit::textEdited, this, [this](const QString& text) { m_holder->onInput(text); });
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::submit);
    connect(m_resolve, &QPushButton::clicked, this, &MainWindow::submit);

    // ---- error banner ----
    m_errorBanner = new QFrame(central);
    m_errorBanner->setObjectName(u"errorBanner"_s);
    auto* banner = new QHBoxLayout(m_errorBanner);
    banner->setContentsMargins(12, 8, 8, 8);
    m_errorText = makeLabel(QString(), m_errorBanner, u"errorText"_s);
    m_errorText->setWordWrap(true);
    m_errorText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    banner->addWidget(m_errorText, 1);
    QPushButton* errorClose = makeButton(u"닫기"_s, m_errorBanner, u"errorClose"_s);
    banner->addWidget(errorClose, 0, Qt::AlignTop);
    page->addWidget(m_errorBanner);
    connect(errorClose, &QPushButton::clicked, this, [this] { m_holder->dismissError(); });

    // ---- the resolved section ----
    m_resolved = new QWidget(central);
    m_resolved->setObjectName(u"resolved"_s);
    auto* resolved = new QVBoxLayout(m_resolved);
    resolved->setContentsMargins(0, 0, 0, 0);
    resolved->setSpacing(10);

    auto* infoLine = new QHBoxLayout();
    infoLine->setSpacing(12);
    m_infoTitle = makeLabel(QString(), m_resolved, u"infoTitle"_s);
    m_infoTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // a long title must not widen the window
    m_truncated = makeLabel(u"999개를 넘어 앞 999개만 표시합니다."_s, m_resolved, u"truncated"_s);
    m_truncated->setProperty("small", true);
    setTone(m_truncated, u"bad"_s);
    m_videoOnly = makeButton(u"이 영상만 받기"_s, m_resolved, u"videoOnly"_s);
    infoLine->addWidget(m_infoTitle, 1);
    infoLine->addWidget(m_truncated);
    infoLine->addWidget(m_videoOnly);
    resolved->addLayout(infoLine);
    connect(m_videoOnly, &QPushButton::clicked, this, [this] { m_holder->switchToVideoOnly(); });

    // The options: two groups that a job turns off, with the destination line between them (its button stays usable while a job
    // runs: looking at the folder is harmless), as index.html's two fieldsets.
    m_options = new QWidget(m_resolved);
    m_options->setObjectName(u"options"_s);
    auto* options = new QHBoxLayout(m_options);
    options->setContentsMargins(0, 0, 0, 0);
    options->setSpacing(8);
    m_outputDir = makeLineEdit(m_options, u"outputDir"_s);
    options->addWidget(field(u"출력 폴더"_s, m_outputDir, m_options), 1);
    QPushButton* pick = makeButton(u"폴더 선택"_s, m_options, u"pickFolder"_s);
    options->addWidget(pick, 0, Qt::AlignBottom);
    resolved->addWidget(m_options);
    connect(m_outputDir, &QLineEdit::textEdited, this, [this](const QString& text) { m_holder->onOutputDir(text); });
    connect(pick, &QPushButton::clicked, this, &MainWindow::pickFolder);

    m_destinationRow = new QWidget(m_resolved);
    m_destinationRow->setObjectName(u"destinationRow"_s);
    auto* destination = new QHBoxLayout(m_destinationRow);
    destination->setContentsMargins(0, 0, 0, 0);
    m_destination = makeLabel(QString(), m_destinationRow, u"destination"_s);
    m_destination->setProperty("small", true);
    m_destination->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_destination->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openFolder = makeButton(u"탐색기에서 보기"_s, m_destinationRow, u"openFolder"_s);
    destination->addWidget(m_destination, 1);
    destination->addWidget(m_openFolder);
    resolved->addWidget(m_destinationRow);
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        if (const std::optional<QString> path = ui::destinationPath(m_holder->state())) m_prompts.openFolder(*path);
    });

    m_optionsMore = new QWidget(m_resolved);
    m_optionsMore->setObjectName(u"optionsMore"_s);
    auto* more = new QVBoxLayout(m_optionsMore);
    more->setContentsMargins(0, 0, 0, 0);
    more->setSpacing(8);
    m_album = makeLineEdit(m_optionsMore, u"album"_s);
    QWidget* albumField = field(u"앨범명 (폴더명)"_s, m_album, m_optionsMore);
    QLabel* albumHint =
        makeLabel(u"저장 폴더 이름과 앨범 태그가 모두 이 이름입니다. 비우면 재생목록 제목(영상 1개는 폴더 없이 영상 자체의 앨범)"_s, albumField);
    albumHint->setProperty("small", true);
    albumHint->setWordWrap(true);
    albumField->layout()->addWidget(albumHint);
    more->addWidget(albumField);
    auto* checks = new QHBoxLayout();
    checks->setSpacing(20);
    m_optOverwrite = new QCheckBox(u"기존 파일 덮어쓰기"_s, m_optionsMore);
    m_optOverwrite->setObjectName(u"optOverwrite"_s);
    m_optRank = new QCheckBox(u"파일명에 순번 포함"_s, m_optionsMore);
    m_optRank->setObjectName(u"optRank"_s);
    m_optLyrics = new QCheckBox(u"가사가 없으면 인터넷에서 검색"_s, m_optionsMore);
    m_optLyrics->setObjectName(u"optLyrics"_s);
    checks->addWidget(m_optOverwrite);
    checks->addWidget(m_optRank);
    checks->addWidget(m_optLyrics);
    checks->addStretch(1);
    more->addLayout(checks);
    auto* concurrency = new QHBoxLayout();
    concurrency->setSpacing(12);
    QLabel* concurrencyLabel = makeLabel(u"동시 다운로드"_s, m_optionsMore);
    concurrencyLabel->setProperty("role", u"caption"_s);
    concurrency->addWidget(concurrencyLabel, 0, Qt::AlignBottom);
    concurrency->addWidget(makeLabel(u"코어 수의 70% (자동)"_s, m_optionsMore), 0, Qt::AlignBottom);
    concurrency->addSpacing(24);
    m_singleRank = makeLineEdit(m_optionsMore, u"singleRank"_s);
    m_singleRank->setInputMethodHints(Qt::ImhDigitsOnly);
    m_singleRank->setMaximumWidth(96);
    m_rankField = field(u"순위 번호"_s, m_singleRank, m_optionsMore);
    m_rankField->setObjectName(u"rankField"_s);
    concurrency->addWidget(m_rankField, 0, Qt::AlignBottom);
    concurrency->addStretch(1);
    more->addLayout(concurrency);
    resolved->addWidget(m_optionsMore);
    connect(m_album, &QLineEdit::textEdited, this, [this](const QString& text) { m_holder->onAlbumName(text); });
    connect(m_optOverwrite, &QCheckBox::clicked, this, [this](bool checked) { m_holder->onOverwrite(checked); });
    connect(m_optRank, &QCheckBox::clicked, this, [this](bool checked) { m_holder->onIncludeRank(checked); });
    connect(m_optLyrics, &QCheckBox::clicked, this, [this](bool checked) { m_holder->onSearchLyricsOnline(checked); });
    connect(m_singleRank, &QLineEdit::textEdited, this, [this] {
        if (!composing(m_singleRank)) filterRank();
    });

    m_list = new QTreeWidget(m_resolved);
    m_list->setObjectName(u"list"_s);
    m_list->setAccessibleName(u"곡 목록"_s);
    m_list->setColumnCount(3);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(false);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setAlternatingRowColors(true);
    m_list->setWordWrap(true);
    m_list->setMinimumHeight(120);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(kStatusColumn, QHeaderView::Interactive);
    m_list->header()->resizeSection(kStatusColumn, 240);
    resolved->addWidget(m_list, 1);

    // The buttons under the list: made once, shown by phase (main.ts renderActions) in this order.
    m_actions = new QWidget(m_resolved);
    m_actions->setObjectName(u"actions"_s);
    m_actionsLayout = new QHBoxLayout(m_actions);
    m_actionsLayout->setContentsMargins(0, 0, 0, 0);
    m_actionsLayout->setSpacing(8);
    QLabel* summary = makeLabel(QString(), m_actions, u"summary"_s);
    summary->setAccessibleDescription(u"status"_s);
    m_actionsLayout->addWidget(summary);
    m_actionsLayout->addStretch(1);
    const struct {
        QString name;
        QString text;
        bool primary;
        void (ui::AppStateHolder::*call)();
    } buttons[] = {
        {u"start"_s, u"다운로드 시작"_s, true, &ui::AppStateHolder::startDownload},
        {u"cancel"_s, u"취소"_s, true, &ui::AppStateHolder::cancel},
        {u"retry"_s, u"실패 항목 재시도"_s, true, &ui::AppStateHolder::retryFailed},
        {u"again"_s, u"다시 다운로드"_s, false, &ui::AppStateHolder::startDownload},
        {u"reset"_s, u"새로 시작"_s, false, &ui::AppStateHolder::reset},
    };
    for (const auto& spec : buttons) {
        QPushButton* button = makeButton(spec.text, m_actions, spec.name, spec.primary);
        connect(button, &QPushButton::clicked, this, [this, call = spec.call] { (m_holder.get()->*call)(); });
        m_actionsLayout->addWidget(button);
    }
    resolved->addWidget(m_actions);
    page->addWidget(m_resolved, 1);

    m_spacer = new QWidget(central);
    m_spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    page->addWidget(m_spacer, 1);

    setCentralWidget(central);

    for (QLineEdit* box : {m_input, m_outputDir, m_album, m_singleRank}) box->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // The composing mark is set or cleared before the box handles the event, so the textEdited of a commit sees it cleared.
    if (event->type() == QEvent::InputMethod) {
        const auto* ime = static_cast<const QInputMethodEvent*>(event);
        if (ime->preeditString().isEmpty()) m_composing.remove(watched);
        else m_composing.insert(watched);
    } else if (event->type() == QEvent::FocusOut) {
        m_composing.remove(watched);
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::composing(const QObject* box) const {
    return m_composing.contains(box);
}

void MainWindow::showText(QLineEdit* box, const QString& value) {
    // A text box is never written to while an IME composes in it (a Korean syllable under construction would break).
    if (box->text() != value && !composing(box)) box->setText(value);
}

void MainWindow::filterRank() {
    // Only digits stay in the rank box (main.ts filterRank); a composition is filtered when it commits.
    const QString text = ui::rankInputText(m_singleRank->text());
    if (m_singleRank->text() != text) m_singleRank->setText(text);
    if (const std::optional<int> rank = ui::rankFromInput(text)) m_holder->onSingleRank(*rank);
}

void MainWindow::submit() {
    // The form's submit: Enter does nothing while 조회 is off, as with a disabled submit button.
    if (m_resolve->isEnabled()) m_holder->resolve();
}

void MainWindow::render() {
    const ui::UiState& state = m_holder->state();
    renderTools(state);

    showText(m_input, state.input);
    m_resolve->setEnabled(ui::canResolve(state) && !kt::trim(state.input).isEmpty());
    // The playlist button fills the input, so it is off whenever a lookup cannot start (design spec §5.2).
    m_playlistButton->setEnabled(ui::canResolve(state));
    m_errorBanner->setVisible(state.error.has_value());
    setTextIfChanged(m_errorText, state.error.value_or(QString()));

    const std::optional<ResolveResult>& resolved = state.resolved;
    m_resolved->setVisible(resolved.has_value());
    m_spacer->setVisible(!resolved.has_value());
    if (resolved) {
        const QString count = resolved->kind == InputKind::Playlist ? u" · "_s + QString::number(resolved->items.size()) + u"개"_s : QString();
        const QString title = resolved->playlistTitle.value_or(u"단일 영상"_s) + count;
        setTextIfChanged(m_infoTitle, title);
        m_infoTitle->setToolTip(title);
        m_truncated->setVisible(resolved->truncated);
        m_videoOnly->setVisible(resolved->alsoVideoId.has_value() && ui::canResolve(state));
    }

    const bool running = state.phase == ui::Phase::Running;
    m_options->setEnabled(!running);
    m_optionsMore->setEnabled(!running);
    showText(m_outputDir, state.outputDir);
    showText(m_album, ui::albumNameText(state));
    m_optOverwrite->setChecked(state.overwrite);
    m_optRank->setChecked(state.includeRank);
    m_optLyrics->setChecked(state.searchLyricsOnline);
    const std::optional<QString> label = ui::destinationLabel(state);
    m_destinationRow->setVisible(label.has_value());
    setTextIfChanged(m_destination, label.value_or(QString()));
    m_destination->setToolTip(label.value_or(QString()));
    m_openFolder->setVisible(ui::destinationPath(state).has_value());
    const bool rankShown = resolved && resolved->kind == InputKind::Video;
    m_rankField->setVisible(rankShown);
    // The raw text stays while the user retypes the number (the box may even be empty): the box is looked at only when the rank
    // itself changed (or the box was just shown), and rewritten only if it says another rank (main.ts syncedRank).
    if (!rankShown) {
        m_syncedRank.reset();
    } else if (m_syncedRank != state.singleRank) {
        m_syncedRank = state.singleRank;
        if (!composing(m_singleRank) && ui::rankFromInput(m_singleRank->text()) != state.singleRank) {
            m_singleRank->setText(QString::number(state.singleRank));
        }
    }

    renderList(state);
    renderActions(state);
}

void MainWindow::renderTools(const ui::UiState& state) {
    const ShownTools shown{state.tools, state.toolBusy, state.toolMessage, state.phase == ui::Phase::Running};
    if (m_shownTools == shown) return;
    m_shownTools = shown;
    auto child = [this](const QString& name) { return m_toolsContent->findChild<QWidget*>(name); };
    const std::optional<ToolsStatus>& tools = state.tools;
    child(u"toolsChecking"_s)->setVisible(!tools);
    const std::pair<QString, const ToolInfo*> entries[] = {
        {u"yt-dlp"_s, tools ? &tools->ytDlp : nullptr},
        {u"ffmpeg"_s, tools ? &tools->ffmpeg : nullptr},
        {u"JS 런타임"_s, tools ? &tools->jsRuntime : nullptr},
    };
    const QString names[] = {u"toolYtDlp"_s, u"toolFfmpeg"_s, u"toolJs"_s};
    for (int i = 0; i < 3; ++i) {
        auto* line = static_cast<QLabel*>(child(names[i]));
        const ToolInfo* info = entries[i].second;
        line->setVisible(info != nullptr);
        if (!info) continue;
        line->setText(info->found ? entries[i].first + u' ' + info->version.value_or(QString()) + u" ✓"_s : entries[i].first + u" ✗"_s);
        setTone(line, info->found ? QString() : u"bad"_s);
    }
    QWidget* install = child(u"install"_s);
    QWidget* update = child(u"update"_s);
    QWidget* help = child(u"formatHelp"_s);
    const bool ytDlp = tools && tools->ytDlp.found;
    install->setVisible(tools && !ytDlp);
    install->setEnabled(!state.toolBusy);
    update->setVisible(ytDlp);
    update->setEnabled(!state.toolBusy && shown.running == false);
    help->setVisible(ytDlp);
    child(u"ffmpegMissing"_s)->setVisible(tools && !tools->ffmpeg.found);
    child(u"jsMissing"_s)->setVisible(tools && !tools->jsRuntime.found);
    child(u"toolBusy"_s)->setVisible(state.toolBusy);
    auto* message = static_cast<QLabel*>(child(u"toolMessage"_s));
    message->setVisible(state.toolMessage.has_value());
    message->setText(state.toolMessage.value_or(QString()));
}

void MainWindow::renderList(const ui::UiState& state) {
    const QList<ui::ItemRow>& rows = state.rows;
    if (rows == m_shownRows && m_list->topLevelItemCount() == rows.size()) return;
    bool sameShape = rows.size() == m_shownRows.size() && m_list->topLevelItemCount() == rows.size();
    for (qsizetype i = 0; sameShape && i < rows.size(); ++i) {
        sameShape = rows.at(i).item.rank == m_shownRows.at(i).item.rank && rows.at(i).item.videoId == m_shownRows.at(i).item.videoId;
    }
    if (sameShape) {
        for (qsizetype i = 0; i < rows.size(); ++i) {
            if (!(rows.at(i) == m_shownRows.at(i))) patchRow(int(i), rows.at(i));
        }
    } else {
        m_list->clear();
        QList<QTreeWidgetItem*> items;
        items.reserve(rows.size());
        for (const ui::ItemRow& row : rows) {
            auto* item = new QTreeWidgetItem();
            item->setText(0, ui::rankLabel(row.item.rank));
            item->setTextAlignment(0, Qt::AlignRight | Qt::AlignTop);
            item->setTextAlignment(kNameColumn, Qt::AlignLeft | Qt::AlignTop);
            item->setTextAlignment(kStatusColumn, Qt::AlignLeft | Qt::AlignTop);
            if (!row.item.available) {
                const QBrush grey(palette().color(QPalette::Disabled, QPalette::Text));
                for (int column = 0; column < 3; ++column) item->setForeground(column, grey);
            }
            items.append(item);
        }
        m_list->addTopLevelItems(items);
        for (qsizetype i = 0; i < rows.size(); ++i) patchRow(int(i), rows.at(i));
    }
    m_shownRows = rows;
}

void MainWindow::patchRow(int index, const ui::ItemRow& row) {
    QTreeWidgetItem* item = m_list->topLevelItem(index);
    if (!item) return;
    const QString fileName = row.fileName.value_or(row.item.title);
    QString name = fileName;
    if (row.item.available && row.item.lowConfidence) name += u"\n⚠ 가수명을 채널명에서 추정했습니다"_s;
    if (item->text(kNameColumn) != name) item->setText(kNameColumn, name);
    item->setToolTip(kNameColumn, fileName);
    const QString status = ui::statusLabel(row.status);
    item->setText(kStatusColumn, status);
    item->setToolTip(kStatusColumn, status);
    if (row.item.available) {
        item->setForeground(kStatusColumn, row.status.kind == ui::ItemStatus::Kind::Failed ? QBrush(QColor(0xc6, 0x28, 0x28)) : QBrush());
    }
    auto* bar = qobject_cast<QProgressBar*>(m_list->itemWidget(item, kStatusColumn));
    if (row.status.kind == ui::ItemStatus::Kind::Downloading && row.status.percent) {
        if (!bar) {
            bar = new QProgressBar();
            bar->setRange(0, 100);
            bar->setTextVisible(true);
            m_list->setItemWidget(item, kStatusColumn, bar);
        }
        bar->setValue(int(std::trunc(*row.status.percent)));
        bar->setFormat(status);
        bar->setAccessibleName(fileName + u" 다운로드 진행률"_s);
    } else if (bar) {
        m_list->removeItemWidget(item, kStatusColumn);
    }
}

void MainWindow::renderActions(const ui::UiState& state) {
    const std::optional<QString> summary = ui::summaryText(state);
    bool startable = false;
    for (const ui::ItemRow& row : state.rows) startable = startable || row.item.available;
    const bool failed = !ui::failedRanks(state).isEmpty();
    const QString key = QString::number(int(state.phase)) + u'|' + QString::number(startable) + u'|' + QString::number(failed) + u'|' +
                        summary.value_or(u"-"_s);
    if (m_shownActions == key) return;
    m_shownActions = key;
    QWidget* focus = focusWidget();
    const bool hadFocus = focus && m_actions->isAncestorOf(focus);

    auto button = [this](const QString& name) { return m_actions->findChild<QPushButton*>(name); };
    auto* summaryLabel = m_actions->findChild<QLabel*>(u"summary"_s);
    const ui::Phase phase = state.phase;
    button(u"start"_s)->setVisible(phase == ui::Phase::Preview);
    button(u"start"_s)->setEnabled(startable);
    button(u"cancel"_s)->setVisible(phase == ui::Phase::Running);
    button(u"retry"_s)->setVisible(phase == ui::Phase::Finished && failed);
    button(u"again"_s)->setVisible(phase == ui::Phase::Finished);
    button(u"reset"_s)->setVisible(phase == ui::Phase::Preview || phase == ui::Phase::Finished);
    summaryLabel->setVisible(phase == ui::Phase::Finished && summary.has_value());
    summaryLabel->setText(summary.value_or(QString()));

    // The button the user pressed may be gone: keep the keyboard there (on the main button, or on the first one when there is none).
    if (hadFocus && !(focus->isVisibleTo(m_actions) && focus->isEnabled())) {
        QPushButton* target = nullptr;
        for (QPushButton* candidate : m_actions->findChildren<QPushButton*>()) {
            if (!candidate->isVisibleTo(m_actions) || !candidate->isEnabled()) continue;
            if (!target || (candidate->property("primary").toBool() && !target->property("primary").toBool())) target = candidate;
        }
        if (target) target->setFocus();
    }
}

void MainWindow::fillPlaylistMenu() {
    m_playlists = m_playlistStore.load();
    m_playlistMenu->clear();
    if (m_playlists.isEmpty()) {
        QAction* none = m_playlistMenu->addAction(u"(목록 없음)"_s);
        none->setEnabled(false);
    }
    for (const ui::Playlist& entry : m_playlists) {
        QString text = entry.name;
        text.replace(u'&', u"&&"_s);  // a name is not a mnemonic
        QAction* action = m_playlistMenu->addAction(text);
        action->setToolTip(entry.pid);
        connect(action, &QAction::triggered, this, [this, pid = entry.pid] {
            // Only the input is filled; the user presses 조회 (design spec §5.2).
            if (ui::canResolve(m_holder->state())) m_holder->onInput(pid);
        });
    }
    m_playlistMenu->addSeparator();
    QAction* manage = m_playlistMenu->addAction(u"목록 관리…"_s);
    connect(manage, &QAction::triggered, this, &MainWindow::managePlaylists);
}

void MainWindow::managePlaylists() {
    PlaylistsDialog dialog(m_playlistStore, m_remote, m_playlistStore.load(), m_prompts.playlists, this);
    m_prompts.runDialog(dialog);
}

void MainWindow::pickFolder() {
    if (const std::optional<QString> chosen = m_prompts.pickFolder(this, m_holder->state().outputDir)) m_holder->onOutputDir(*chosen);
}

void MainWindow::confirmInstall() {
    if (m_prompts.confirmInstall(this)) m_holder->installYtDlp();
}

void MainWindow::showAbout() {
    AboutDialog dialog(this);
    m_prompts.runDialog(dialog);
}

void MainWindow::bringToFront() {
    if (isMinimized()) setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    raise();
    activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // The options that still wait for their quiet period are written before the window goes (main.ts: desktop.onClose →
    // flushSettings). The app takes the engine down after its event loop has ended (main.cpp), never from in here.
    m_holder->flushSettings();
    event->accept();
    Q_EMIT closed();
}

}  // namespace xgs::app
