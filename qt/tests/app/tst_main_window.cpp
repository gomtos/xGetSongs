#include <QtTest>

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

#include "about_dialog.h"
#include "app_services.h"
#include "app_test_support.h"
#include "labels.h"
#include "lyrics/google/browser_channel.h"
#include "lyrics/google/google_lyrics_provider.h"
#include "main_window.h"
#include "playlist_store.h"
#include "ui/fake_app_api.h"

// The main window on the offscreen platform (no window reaches the screen), driven with the fake engine of the screen logic
// tests and a playlist file in a QTemporaryDir: the elements and their enabled rules of shell/index.html and shell/src/main.ts,
// the playlist button of design spec §5.2, the about button, the IME rule, and the close. Names and IDs are made up.

using namespace Qt::StringLiterals;
using xgs::app::AboutDialog;
using xgs::app::AppPaths;
using xgs::app::AppServices;
using xgs::app::MainWindow;
using xgs::app::MainWindowPrompts;
using xgs::app::test::FakeRemotePlaylists;
using xgs::JobDone;
using xgs::JobStatus;
using xgs::JobSummary;
using xgs::ui::Phase;
using xgs::ui::Playlist;
using xgs::ui::PlaylistStore;
using namespace xgs::ui::test;

namespace {

/// Everything one window needs, in a QTemporaryDir.
struct Rig {
    QTemporaryDir dir;
    FakeAppApi api;
    FakeSettingsStore settings;
    PlaylistStore playlists{dir.filePath(u"playlists.json"_s)};
    QNetworkAccessManager network;
    FakeRemotePlaylists remote{network};
    int installQuestions = 0;
    bool installAnswer = false;
    // What the window ran as dialogs (recorded while they exist: they live on the window's stack).
    QStringList dialogs;
    QString aboutText;
    QStringList opened;
    std::optional<QString> picked;
    std::unique_ptr<MainWindow> window;

    void open() {
        MainWindowPrompts prompts;
        prompts.confirmInstall = [this](QWidget*) {
            installQuestions += 1;
            return installAnswer;
        };
        prompts.pickFolder = [this](QWidget*, const QString&) { return picked; };
        prompts.openFolder = [this](const QString& path) { opened.append(path); };
        prompts.runDialog = [this](QDialog& dialog) {
            dialogs.append(dialog.objectName());
            if (const auto* about = qobject_cast<AboutDialog*>(&dialog)) aboutText = about->text();
        };
        window = std::make_unique<MainWindow>(api, settings, playlists, remote, prompts);
        window->show();
    }

    template <class T>
    T* find(const QString& name) const {
        T* found = window->findChild<T*>(name);
        if (!found) qWarning("no widget %s", qPrintable(name));
        return found;
    }

    /// The buttons under the list, by their text, in order.
    QStringList actionTexts() const {
        QStringList texts;
        const QWidget* actions = find<QWidget>(u"actions"_s);
        for (const QPushButton* button : actions->findChildren<QPushButton*>()) {
            if (button->isVisibleTo(actions)) texts.append(button->text());
        }
        return texts;
    }

    QPushButton* action(const QString& text) const {
        for (QPushButton* button : find<QWidget>(u"actions"_s)->findChildren<QPushButton*>()) {
            if (button->text() == text) return button;
        }
        return nullptr;
    }

    void typeInput(const QString& text) {
        QLineEdit* input = find<QLineEdit>(u"input"_s);
        input->setFocus();
        input->selectAll();
        QTest::keyClicks(input, text);
    }

    /// Looks the fake playlist up and lands in PREVIEW.
    void resolvePlaylist() {
        typeInput(u"PLdummyplaylistid0000000000000000001"_s);
        QTest::mouseClick(find<QPushButton>(u"resolve"_s), Qt::LeftButton);
        api.flush();
    }

    void startJob() {
        QTest::mouseClick(action(u"다운로드 시작"_s), Qt::LeftButton);
        api.flush();
    }
};

/// The top-level windows of this process that are visible, and the processes it started.
int visibleWindowsOfThisProcess() {
    struct Count {
        DWORD pid;
        int visible = 0;
    } count{GetCurrentProcessId()};
    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
            auto* c = reinterpret_cast<Count*>(data);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == c->pid && IsWindowVisible(hwnd)) c->visible += 1;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&count));
    return count.visible;
}

int childProcessesOfThisProcess() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int children = 0;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ParentProcessID == GetCurrentProcessId()) children += 1;
    }
    CloseHandle(snapshot);
    return children;
}

}  // namespace

class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void theWindowHasTheTitleAndSizeOfTheTauriWindow() {
        Rig rig;
        rig.open();
        QCOMPARE(rig.window->windowTitle(), u"xGetSongs"_s);
        QCOMPARE(rig.window->size(), QSize(1000, 760));
    }

    void theButtonsUnderTheListFollowThePhase() {
        Rig rig;
        rig.open();
        rig.api.flush();
        QCOMPARE(rig.actionTexts(), QStringList());
        QVERIFY(!rig.find<QWidget>(u"resolved"_s)->isVisible());

        rig.resolvePlaylist();
        QCOMPARE(rig.window->holder().state().phase, Phase::Preview);
        QCOMPARE(rig.actionTexts(), (QStringList{u"다운로드 시작"_s, u"새로 시작"_s}));
        QVERIFY(rig.action(u"다운로드 시작"_s)->isEnabled());
        QVERIFY(rig.find<QWidget>(u"resolved"_s)->isVisible());
        QCOMPARE(rig.find<QLabel>(u"infoTitle"_s)->text(), u"Sample · 3개"_s);
        QCOMPARE(rig.find<QTreeWidget>(u"list"_s)->topLevelItemCount(), 3);

        rig.startJob();
        QCOMPARE(rig.window->holder().state().phase, Phase::Running);
        QCOMPARE(rig.actionTexts(), QStringList{u"취소"_s});
        QVERIFY(!rig.find<QWidget>(u"options"_s)->isEnabled());
        QVERIFY(!rig.find<QWidget>(u"optionsMore"_s)->isEnabled());
        QVERIFY(rig.find<QPushButton>(u"openFolder"_s)->isVisible());
        QVERIFY(rig.find<QPushButton>(u"openFolder"_s)->isEnabled());
        QVERIFY(!rig.find<QPushButton>(u"resolve"_s)->isEnabled());

        QTest::mouseClick(rig.action(u"취소"_s), Qt::LeftButton);
        QCOMPARE(rig.api.cancelled, QStringList{u"job-1"_s});
        rig.api.send(JobDone{JobStatus::Cancelled, JobSummary{1, 0, 0}});
        rig.api.closeEvents();
        rig.api.flush();
        QCOMPARE(rig.window->holder().state().phase, Phase::Finished);
        QCOMPARE(rig.actionTexts(), (QStringList{u"다시 다운로드"_s, u"새로 시작"_s}));
        QVERIFY(rig.find<QLabel>(u"summary"_s)->text().startsWith(u"취소됨 — 성공 1 · 건너뜀 0 · 실패 0"_s));
        QVERIFY(rig.find<QWidget>(u"options"_s)->isEnabled());

        QTest::mouseClick(rig.action(u"새로 시작"_s), Qt::LeftButton);
        QCOMPARE(rig.window->holder().state().phase, Phase::Idle);
        QCOMPARE(rig.actionTexts(), QStringList());
    }

    void theRetryButtonAppearsWhenAnItemFailed() {
        Rig rig;
        rig.open();
        rig.resolvePlaylist();
        rig.startJob();
        rig.api.send(xgs::ItemFailed{1, u"더미 실패"_s}, JobDone{JobStatus::Completed, JobSummary{1, 0, 1}});
        rig.api.closeEvents();
        rig.api.flush();
        QCOMPARE(rig.actionTexts(), (QStringList{u"실패 항목 재시도"_s, u"다시 다운로드"_s, u"새로 시작"_s}));
        QCOMPARE(rig.find<QTreeWidget>(u"list"_s)->topLevelItem(0)->text(2), u"실패: 더미 실패"_s);
    }

    void startIsOffWhenNothingCanBeDownloaded() {
        Rig rig;
        rig.api.resolveResponse.items = {unavailableItem(1), unavailableItem(2)};
        rig.open();
        rig.resolvePlaylist();
        QVERIFY(!rig.action(u"다운로드 시작"_s)->isEnabled());
        QVERIFY(rig.action(u"새로 시작"_s)->isEnabled());
    }

    void theResolveButtonNeedsText() {
        Rig rig;
        rig.open();
        QPushButton* resolve = rig.find<QPushButton>(u"resolve"_s);
        QVERIFY(!resolve->isEnabled());
        rig.typeInput(u"   "_s);
        QVERIFY(!resolve->isEnabled());
        const QString id = u"PLdummyplaylistid0000000000000000001"_s;
        rig.typeInput(id);
        QVERIFY(resolve->isEnabled());
        QCOMPARE(rig.window->holder().state().input, id);
        // Enter in the field looks it up, as the form's submit did.
        QTest::keyClick(rig.find<QLineEdit>(u"input"_s), Qt::Key_Return);
        QCOMPARE(rig.api.resolveInputs, QStringList{id});
        // While it is looked up, 조회 is off and Enter does nothing.
        QVERIFY(!resolve->isEnabled());
        QTest::keyClick(rig.find<QLineEdit>(u"input"_s), Qt::Key_Return);
        QCOMPARE(rig.api.resolveInputs, QStringList{id});
    }

    void thePlaylistButtonIsOffWhileAJobRuns() {
        Rig rig;
        rig.open();
        QToolButton* playlists = rig.find<QToolButton>(u"playlists"_s);
        QCOMPARE(playlists->text(), u"플레이리스트 ▾"_s);
        QVERIFY(playlists->isEnabled());
        rig.typeInput(u"PLdummyplaylistid0000000000000000001"_s);
        QTest::mouseClick(rig.find<QPushButton>(u"resolve"_s), Qt::LeftButton);
        QCOMPARE(rig.window->holder().state().phase, Phase::Resolving);
        QVERIFY(!playlists->isEnabled());
        rig.api.flush();
        QVERIFY(playlists->isEnabled());
        rig.startJob();
        QVERIFY(!playlists->isEnabled());
        rig.api.send(JobDone{JobStatus::Completed, JobSummary{2, 0, 0}});
        rig.api.closeEvents();
        rig.api.flush();
        QVERIFY(playlists->isEnabled());
    }

    void choosingAPlaylistFillsTheInputOnly() {
        Rig rig;
        QVERIFY(rig.playlists.save({{u"더미 목록 하나"_s, u"PLdummyplaylistid0000000000000000001"_s},
                                    {u"더미 목록 둘"_s, u"PLdummyplaylistid0000000000000000002"_s}}));
        rig.open();
        rig.api.flush();
        QMenu* menu = rig.window->playlistMenu();
        emit menu->aboutToShow();
        const QList<QAction*> actions = menu->actions();
        QCOMPARE(actions.size(), 4);
        QCOMPARE(actions.at(0)->text(), u"더미 목록 하나"_s);
        QCOMPARE(actions.at(1)->text(), u"더미 목록 둘"_s);
        QVERIFY(actions.at(2)->isSeparator());
        QCOMPARE(actions.at(3)->text(), u"목록 관리…"_s);

        actions.at(1)->trigger();
        QCOMPARE(rig.find<QLineEdit>(u"input"_s)->text(), u"PLdummyplaylistid0000000000000000002"_s);
        QCOMPARE(rig.window->holder().state().input, u"PLdummyplaylistid0000000000000000002"_s);
        QVERIFY(rig.find<QPushButton>(u"resolve"_s)->isEnabled());
        rig.api.flush();
        QCOMPARE(rig.api.resolveInputs, QStringList());
        QCOMPARE(rig.window->holder().state().phase, Phase::Idle);
    }

    void anEmptyListShowsNoListEntry() {
        Rig rig;
        rig.open();
        QMenu* menu = rig.window->playlistMenu();
        emit menu->aboutToShow();
        const QList<QAction*> actions = menu->actions();
        QCOMPARE(actions.size(), 3);
        QCOMPARE(actions.at(0)->text(), u"(목록 없음)"_s);
        QVERIFY(!actions.at(0)->isEnabled());
        QVERIFY(actions.at(1)->isSeparator());
        QCOMPARE(actions.at(3 - 1)->text(), u"목록 관리…"_s);
        QVERIFY(actions.at(2)->isEnabled());
    }

    void manageOpensThePlaylistsDialogAndTheMenuFollowsIt() {
        Rig rig;
        rig.open();
        QMenu* menu = rig.window->playlistMenu();
        emit menu->aboutToShow();
        // The dialog is not run (the test's runDialog only records it); what it saved shows in the next menu.
        menu->actions().last()->trigger();
        QCOMPARE(rig.dialogs.size(), 1);
        QCOMPARE(rig.dialogs.at(0), u"playlistsDialog"_s);
        QVERIFY(rig.playlists.save({{u"더미 목록 셋"_s, u"PLdummyplaylistid0000000000000000003"_s}}));
        emit menu->aboutToShow();
        QCOMPARE(menu->actions().at(0)->text(), u"더미 목록 셋"_s);
    }

    void theInputIsNotRewrittenWhileAnImeComposes() {
        Rig rig;
        rig.open();
        QLineEdit* input = rig.find<QLineEdit>(u"input"_s);
        rig.typeInput(u"abc"_s);
        QInputMethodEvent composing(u"가"_s, {});
        QApplication::sendEvent(input, &composing);
        // Something else sets the input while the syllable is being composed: the field is left alone.
        rig.window->holder().onInput(u"PLdummyplaylistid0000000000000000009"_s);
        QCOMPARE(input->text(), u"abc"_s);
        QCOMPARE(input->inputMethodQuery(Qt::ImSurroundingText).toString(), u"abc"_s);
        // The composition ends: what it committed is the user's text.
        QInputMethodEvent commit;
        commit.setCommitString(u"가"_s);
        QApplication::sendEvent(input, &commit);
        QCOMPARE(input->text(), u"abc가"_s);
        QCOMPARE(rig.window->holder().state().input, u"abc가"_s);
        // Once nothing is composed, a change of the state shows again.
        rig.window->holder().onInput(u"PLx"_s);
        QCOMPARE(input->text(), u"PLx"_s);
    }

    void aPopupTakingTheFocusKeepsTheComposition() {
        Rig rig;
        rig.open();
        QLineEdit* input = rig.find<QLineEdit>(u"input"_s);
        rig.typeInput(u"abc"_s);
        QInputMethodEvent composing(u"가"_s, {});
        QApplication::sendEvent(input, &composing);
        // A popup (the context menu, an IME candidate list) takes the focus for a moment: the syllable is still being composed.
        QFocusEvent popup(QEvent::FocusOut, Qt::PopupFocusReason);
        QApplication::sendEvent(input, &popup);
        rig.window->holder().onInput(u"PLdummyplaylistid0000000000000000008"_s);
        QCOMPARE(input->text(), u"abc"_s);
        // Any other loss of focus ends the composition: the field shows the state again.
        QFocusEvent away(QEvent::FocusOut, Qt::TabFocusReason);
        QApplication::sendEvent(input, &away);
        rig.window->holder().onInput(u"PLdummyplaylistid0000000000000000007"_s);
        QCOMPARE(input->text(), u"PLdummyplaylistid0000000000000000007"_s);
    }

    void markupInTitlesAndNamesIsShownAsText() {
        Rig rig;
        rig.api.resolveResponse.playlistTitle = u"<b>x</b>"_s;
        rig.api.resolveResponse.items[0].title = u"<i>더미</i>"_s;
        rig.api.resolveResponse.items[0].expectedFileName.reset();
        rig.open();
        rig.resolvePlaylist();
        QLabel* title = rig.find<QLabel>(u"infoTitle"_s);
        QCOMPARE(title->text(), u"<b>x</b> · 3개"_s);
        QCOMPARE(title->textFormat(), Qt::PlainText);
        QCOMPARE(title->toolTip(), xgs::app::plainToolTip(u"<b>x</b> · 3개"_s));
        QVERIFY(title->toolTip().contains(u"&lt;b&gt;x&lt;/b&gt;"_s));
        QVERIFY(!title->toolTip().contains(u"<b>"_s));
        QLabel* destination = rig.find<QLabel>(u"destination"_s);  // the folder name is sanitized, the tool tip escaped all the same
        QCOMPARE(destination->toolTip(), xgs::app::plainToolTip(destination->text()));
        const QString name = rig.find<QTreeWidget>(u"list"_s)->topLevelItem(0)->toolTip(1);
        QVERIFY2(!name.contains(u"<i>"_s), qPrintable(name));
        QCOMPARE(xgs::app::plainToolTip(u"a\nb"_s), u"<p style='white-space:pre-wrap'>a\nb</p>"_s);
        QCOMPARE(xgs::app::plainToolTip(QString()), QString());
    }

    void theRankBoxKeepsDigitsOnlyAndLeavesACompositionAlone() {
        Rig rig;
        rig.api.resolveResponse = video();
        rig.open();
        rig.typeInput(u"https://youtu.be/dummyvideo1"_s);
        QTest::mouseClick(rig.find<QPushButton>(u"resolve"_s), Qt::LeftButton);
        rig.api.flush();
        QVERIFY(rig.find<QWidget>(u"rankField"_s)->isVisible());
        QLineEdit* rank = rig.find<QLineEdit>(u"singleRank"_s);
        QCOMPARE(rank->text(), u"1"_s);
        rank->setFocus();
        rank->selectAll();
        QTest::keyClicks(rank, u"4a2"_s);
        QCOMPARE(rank->text(), u"42"_s);
        QCOMPARE(rig.window->holder().state().singleRank, 42);
        // A composition in the box is not filtered while it lasts, only what it commits.
        QInputMethodEvent composing(u"ㄱ"_s, {});
        QApplication::sendEvent(rank, &composing);
        QCOMPARE(rank->text(), u"42"_s);
        QInputMethodEvent commit;
        commit.setCommitString(u"ㄱ"_s);
        QApplication::sendEvent(rank, &commit);
        QCOMPARE(rank->text(), u"42"_s);
        QCOMPARE(rig.window->holder().state().singleRank, 42);
        // Typing over everything leaves an empty box (the rank stays), and the box is left as the user has it.
        rank->selectAll();
        QTest::keyClick(rank, Qt::Key_Backspace);
        QCOMPARE(rank->text(), QString());
        QCOMPARE(rig.window->holder().state().singleRank, 42);
        QVERIFY(rank->contextMenuPolicy() != Qt::NoContextMenu);
    }

    void theToolsCardFollowsTheTools() {
        Rig rig;
        rig.api.toolsStatus.ytDlp = xgs::ToolInfo{false, std::nullopt, std::nullopt};
        rig.open();
        QWidget* tools = rig.find<QWidget>(u"tools"_s);
        auto texts = [tools] {
            QStringList all;
            for (const QLabel* label : tools->findChildren<QLabel*>()) {
                if (label->isVisibleTo(tools)) all.append(label->text());
            }
            return all;
        };
        QVERIFY(texts().contains(u"도구 확인 중…"_s));
        rig.api.flush();
        QVERIFY(texts().contains(u"yt-dlp ✗"_s));
        QVERIFY(texts().contains(u"ffmpeg 1 ✓"_s));
        QVERIFY(texts().contains(u"JS 런타임 24 ✓"_s));
        QPushButton* install = rig.find<QPushButton>(u"install"_s);
        QVERIFY(install->isEnabled());
        QTest::mouseClick(install, Qt::LeftButton);
        QCOMPARE(rig.installQuestions, 1);
        QCOMPARE(rig.api.installCalls, 0);
        rig.installAnswer = true;
        QTest::mouseClick(rig.find<QPushButton>(u"install"_s), Qt::LeftButton);
        QCOMPARE(rig.api.installCalls, 1);
        QVERIFY(texts().contains(u"작업 중…"_s));
        QVERIFY(!rig.find<QPushButton>(u"install"_s)->isEnabled());
        rig.api.toolsStatus.ytDlp = found(u"2026.08.19"_s);
        rig.api.flush();
        QVERIFY(texts().contains(u"yt-dlp 2026.08.19 ✓"_s));
        QVERIFY(texts().contains(u"installed"_s));
        QPushButton* update = rig.find<QPushButton>(u"update"_s);
        QCOMPARE(update->text(), u"yt-dlp 업데이트"_s);
        QVERIFY(update->isEnabled());
        QVERIFY(texts().contains(xgs::ui::formatFailureHelp()));

        // The update is off while a job runs.
        rig.resolvePlaylist();
        rig.startJob();
        QVERIFY(!rig.find<QPushButton>(u"update"_s)->isEnabled());
        // The about button stays where it is whatever the card shows.
        QVERIFY(rig.find<QPushButton>(u"about"_s)->isEnabled());
        QCOMPARE(rig.find<QPushButton>(u"about"_s)->text(), u"정보"_s);
    }

    void theMissingToolsAreExplained() {
        Rig rig;
        rig.api.toolsStatus.ffmpeg = xgs::ToolInfo{false, std::nullopt, std::nullopt};
        rig.api.toolsStatus.jsRuntime = xgs::ToolInfo{false, std::nullopt, std::nullopt};
        rig.open();
        rig.api.flush();
        QStringList all;
        for (const QLabel* label : rig.find<QWidget>(u"tools"_s)->findChildren<QLabel*>()) all.append(label->text());
        QVERIFY(all.contains(u"ffmpeg가 필요합니다. 예: winget install Gyan.FFmpeg"_s));
        QVERIFY(all.contains(u"YouTube를 읽으려면 Node.js 22 이상 또는 Deno 2.3 이상이 필요합니다."_s));
    }

    void theErrorBannerShowsAndCloses() {
        Rig rig;
        rig.api.resolveError = apiError(u"더미 오류"_s);
        rig.open();
        QWidget* banner = rig.find<QWidget>(u"errorBanner"_s);
        QVERIFY(!banner->isVisible());
        rig.resolvePlaylist();
        QVERIFY(banner->isVisible());
        QCOMPARE(rig.find<QLabel>(u"errorText"_s)->text(), u"더미 오류"_s);
        QTest::mouseClick(rig.find<QPushButton>(u"errorClose"_s), Qt::LeftButton);
        QVERIFY(!banner->isVisible());
    }

    void theOptionsReachTheHolderAndTheFolderButtons() {
        Rig rig;
        rig.open();
        rig.resolvePlaylist();
        QCheckBox* overwrite = rig.find<QCheckBox>(u"optOverwrite"_s);
        QVERIFY(!overwrite->isChecked());
        overwrite->click();
        QVERIFY(rig.window->holder().state().overwrite);
        rig.find<QCheckBox>(u"optRank"_s)->click();
        QVERIFY(!rig.window->holder().state().includeRank);
        rig.find<QCheckBox>(u"optLyrics"_s)->click();
        QVERIFY(!rig.window->holder().state().searchLyricsOnline);

        rig.picked = u"D:\\더미 폴더"_s;
        QTest::mouseClick(rig.find<QPushButton>(u"pickFolder"_s), Qt::LeftButton);
        QCOMPARE(rig.window->holder().state().outputDir, u"D:\\더미 폴더"_s);
        QCOMPARE(rig.find<QLineEdit>(u"outputDir"_s)->text(), u"D:\\더미 폴더"_s);
        QCOMPARE(rig.find<QLabel>(u"destination"_s)->text(), *xgs::ui::destinationLabel(rig.window->holder().state()));
        QTest::mouseClick(rig.find<QPushButton>(u"openFolder"_s), Qt::LeftButton);
        QCOMPARE(rig.opened, QStringList{*xgs::ui::destinationPath(rig.window->holder().state())});
        QCOMPARE(rig.find<QLineEdit>(u"album"_s)->text(), u"Sample"_s);
        QVERIFY(rig.find<QLineEdit>(u"outputDir"_s)->contextMenuPolicy() != Qt::NoContextMenu);
    }

    void closingSavesTheOptionsAtOnceAndSaysSo() {
        Rig rig;
        rig.open();
        rig.resolvePlaylist();
        rig.find<QCheckBox>(u"optOverwrite"_s)->click();
        QCOMPARE(rig.settings.saved.size(), 0);  // the 400 ms quiet period has not passed
        QSignalSpy closed(rig.window.get(), &MainWindow::closed);
        QVERIFY(rig.window->close());
        QCOMPARE(closed.count(), 1);
        QCOMPARE(rig.settings.saved.size(), 1);
        QVERIFY(rig.settings.saved.at(0).overwrite);
    }

    void theAboutButtonOpensTheAboutDialog() {
        Rig rig;
        rig.open();
        QTest::mouseClick(rig.find<QPushButton>(u"about"_s), Qt::LeftButton);
        QCOMPARE(rig.dialogs, QStringList{u"aboutDialog"_s});
        const QString text = rig.aboutText;
        QVERIFY(text.contains(u"xGetSongs "_s + QString::fromLatin1(XGS_APP_VERSION)));
        QVERIFY(text.contains(u"MIT"_s));
        QVERIFY(text.contains(QString::fromLatin1(qVersion())));
        QVERIFY(text.contains(u"LGPL"_s));
        QVERIFY(text.contains(u"WebView2"_s));
        QVERIFY(text.contains(u"THIRD-PARTY-NOTICES"_s));
    }

    void theServicesAreMadeAndTornDownWithoutAWindowOrAProcess() {
        // Only the parts are made: nothing is looked up, no tool is run and no lyrics are asked for, and every folder is a
        // QTemporaryDir (an empty bin folder).
        QTemporaryDir dir;
        AppPaths paths = AppPaths::fromValues(dir.filePath(u"roaming"_s), dir.filePath(u"local"_s), std::nullopt);
        QCOMPARE(paths.appData, dir.filePath(u"roaming"_s).replace(u'/', u'\\') + u"\\xGetSongs"_s);
        QCOMPARE(paths.binDir(), paths.appData + u"\\bin"_s);
        QCOMPARE(paths.settingsFile(), paths.appData + u"\\settings.json"_s);
        QCOMPARE(paths.playlistsFile(), paths.appData + u"\\playlists.json"_s);
        QCOMPARE(paths.webViewDataFolder(), dir.filePath(u"local"_s).replace(u'/', u'\\') + u"\\xGetSongs\\qt-webview2"_s);
        const int windowsBefore = visibleWindowsOfThisProcess();
        const int childrenBefore = childProcessesOfThisProcess();
        {
            AppServices services(paths);
            QCOMPARE(visibleWindowsOfThisProcess(), windowsBefore);
            QCOMPARE(childProcessesOfThisProcess(), childrenBefore);
        }
        QCOMPARE(visibleWindowsOfThisProcess(), windowsBefore);
        QCOMPARE(childProcessesOfThisProcess(), childrenBefore);
    }

    void closingTheGoogleProviderFirstEndsALookupAtOnce() {
        // The first two steps of ~AppServices on a lookup that waits on the hidden browser, without WebView2 and without a window:
        // an agent is attached to the channel but never answers. Closing the provider (the channel) and then stopping the job's
        // lookup ends it at once; stopping it first would leave it in DrivenBrowser's reset wait for 5 s.
        xgs::lyrics::google::BrowserChannel channel;
        std::atomic<bool> asked = false;
        channel.attach([&asked] { asked = true; });
        xgs::lyrics::google::FetcherOptions options;
        options.minInterval = std::chrono::nanoseconds(0);
        options.searchBase = u"http://127.0.0.1:9/search"_s;  // never opened: nobody answers the load
        xgs::lyrics::google::GoogleLyricsProvider provider(channel, nullptr, options);
        std::stop_source stop;
        std::atomic<bool> ended = false;
        std::atomic<bool> foundSomething = false;
        std::jthread lookup([&] {
            const std::optional<QString> found = provider.find(xgs::LyricsQuery{u"더미 가수"_s, u"더미 하나"_s, std::nullopt, std::nullopt},
                                                               stop.get_token());
            foundSomething = found.has_value();
            ended = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(asked.load(), 5000);  // the lookup now waits for the page
        QVERIFY(!ended.load());
        const auto start = std::chrono::steady_clock::now();
        provider.close();
        stop.request_stop();
        lookup.join();
        const auto took = std::chrono::steady_clock::now() - start;
        QVERIFY(ended.load());
        QVERIFY(!foundSomething.load());
        // Generous against a slow machine, and still far below the 5 s reset wait it guards against.
        QVERIFY2(took < std::chrono::seconds(2), qPrintable(QString::number(std::chrono::duration_cast<std::chrono::milliseconds>(took).count())));
    }

    void thePathsFollowTheEnvironmentLikeTheTauriApp() {
        AppPaths noAppData = AppPaths::fromValues(QString(), QString(), u"C:\\Users\\dummy"_s);
        QCOMPARE(noAppData.appData, u"C:\\Users\\dummy\\.xgetsongs"_s);
        QCOMPARE(noAppData.webViewDataFolder(), u"C:\\Users\\dummy\\.xgetsongs\\qt-webview2"_s);
        AppPaths both = AppPaths::fromValues(u"C:\\R"_s, u"C:\\L"_s, u"C:\\Users\\dummy"_s);
        QCOMPARE(both.workDir(), u"C:\\R\\xGetSongs\\work"_s);
    }

    void theWorkFolderIsOnTheDriveOfTheOutputFolder() {
        QCOMPARE(xgs::app::workRootFor(u"C:\\Users\\dummy\\Music\\Sample"_s, u"C:\\R\\xGetSongs\\work"_s), u"C:\\R\\xGetSongs\\work"_s);
        QCOMPARE(xgs::app::workRootFor(u"c:\\Music"_s, u"C:\\R\\xGetSongs\\work"_s), u"C:\\R\\xGetSongs\\work"_s);
        QCOMPARE(xgs::app::workRootFor(u"D:\\Music\\Sample"_s, u"C:\\R\\xGetSongs\\work"_s), u"D:\\Music\\Sample"_s);
        QCOMPARE(xgs::app::workRootFor(u"\\\\server\\share\\Music"_s, u"C:\\R\\xGetSongs\\work"_s), u"\\\\server\\share\\Music"_s);
    }
};

XGS_OFFSCREEN_TEST_MAIN(MainWindowTest)
#include "tst_main_window.moc"
