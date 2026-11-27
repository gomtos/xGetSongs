#include <QtTest>

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QTemporaryDir>

#include <optional>

#include "app_test_support.h"
#include "playlist_entry_dialog.h"
#include "playlist_entry_check.h"
#include "playlist_store.h"
#include "playlists_dialog.h"

// The 목록 관리 dialog and the add/edit dialog of design spec §5.2 on the offscreen platform: every change is in the file at once,
// the reset merges the remote lists (§5.4) through a stand-in for the transfer (no network), its buttons are off while the lists
// come, and closing the dialog then cancels the transfer. The playlist file is in a QTemporaryDir; names and IDs are made up.

using namespace Qt::StringLiterals;
using xgs::app::PlaylistEntryDialog;
using xgs::app::PlaylistsDialog;
using xgs::app::PlaylistsDialogPrompts;
using xgs::app::test::FakeRemotePlaylists;
using xgs::ui::Playlist;
using xgs::ui::PlaylistStore;
using xgs::ui::RemoteEntry;
using xgs::ui::RemoteOutcome;

namespace {

QString pid(int n) {
    return u"PLdummyplaylistid000000000000000000"_s + QString::number(n);
}

const QList<Playlist> two = {{u"더미 목록 하나"_s, pid(1)}, {u"더미 목록 둘"_s, pid(2)}};

struct Rig {
    QTemporaryDir dir;
    PlaylistStore store{dir.filePath(u"playlists.json"_s)};
    QNetworkAccessManager network;
    FakeRemotePlaylists remote{network};
    std::optional<Playlist> nextEntry;
    QStringList entryTitles;
    QList<int> editingIndexes;
    QStringList deleteQuestions;
    bool deleteAnswer = false;
    std::unique_ptr<PlaylistsDialog> dialog;

    void open(const QList<Playlist>& start) {
        QVERIFY(store.save(start));
        PlaylistsDialogPrompts prompts;
        prompts.editEntry = [this](QWidget*, const QString& title, const Playlist&, const QList<Playlist>&, int editingIndex) {
            entryTitles.append(title);
            editingIndexes.append(editingIndex);
            return nextEntry;
        };
        prompts.confirmDelete = [this](QWidget*, const QString& name) {
            deleteQuestions.append(name);
            return deleteAnswer;
        };
        dialog = std::make_unique<PlaylistsDialog>(store, remote, store.load(), prompts);
        dialog->show();
    }

    QPushButton* button(const QString& name) const { return dialog->findChild<QPushButton*>(name); }
    QListWidget* list() const { return dialog->findChild<QListWidget*>(u"list"_s); }
    QString status() const { return dialog->findChild<QLabel*>(u"status"_s)->text(); }

    QStringList shownNames() const {
        QStringList names;
        for (int i = 0; i < list()->count(); ++i) names.append(list()->item(i)->text());
        return names;
    }

    void select(int row) { list()->setCurrentRow(row); }
    void click(const QString& name) { QTest::mouseClick(button(name), Qt::LeftButton); }
};

}  // namespace

class PlaylistsDialogTest : public QObject {
    Q_OBJECT

private slots:
    void theDialogHasTheButtonsOfTheSpec() {
        Rig rig;
        rig.open(two);
        QCOMPARE(rig.dialog->windowTitle(), u"목록 관리"_s);
        const QList<std::pair<QString, QString>> buttons = {{u"add"_s, u"추가"_s},     {u"edit"_s, u"수정"_s},
                                                            {u"remove"_s, u"삭제"_s},  {u"up"_s, u"위로"_s},
                                                            {u"down"_s, u"아래로"_s},  {u"reset"_s, u"초기화"_s},
                                                            {u"close"_s, u"닫기"_s}};
        for (const auto& [name, text] : buttons) {
            QVERIFY2(rig.button(name), qPrintable(name));
            QCOMPARE(rig.button(name)->text(), text);
        }
        QCOMPARE(rig.shownNames(), (QStringList{u"더미 목록 하나"_s, u"더미 목록 둘"_s}));
    }

    void theButtonsFollowTheSelection() {
        Rig rig;
        rig.open(two);
        rig.list()->setCurrentRow(-1);
        QVERIFY(rig.button(u"add"_s)->isEnabled());
        QVERIFY(!rig.button(u"edit"_s)->isEnabled());
        QVERIFY(!rig.button(u"remove"_s)->isEnabled());
        QVERIFY(!rig.button(u"up"_s)->isEnabled());
        QVERIFY(!rig.button(u"down"_s)->isEnabled());
        rig.select(0);
        QVERIFY(rig.button(u"edit"_s)->isEnabled());
        QVERIFY(rig.button(u"remove"_s)->isEnabled());
        QVERIFY(!rig.button(u"up"_s)->isEnabled());
        QVERIFY(rig.button(u"down"_s)->isEnabled());
        rig.select(1);
        QVERIFY(rig.button(u"up"_s)->isEnabled());
        QVERIFY(!rig.button(u"down"_s)->isEnabled());
    }

    void addIsSavedAtOnce() {
        Rig rig;
        rig.open(two);
        QSignalSpy changed(rig.dialog.get(), &PlaylistsDialog::changed);
        rig.nextEntry = Playlist{u"더미 목록 셋"_s, pid(3)};
        rig.click(u"add"_s);
        QCOMPARE(rig.entryTitles, QStringList{u"플레이리스트 추가"_s});
        QCOMPARE(rig.editingIndexes, QList<int>{-1});
        const QList<Playlist> expected = {two.at(0), two.at(1), Playlist{u"더미 목록 셋"_s, pid(3)}};
        QCOMPARE(rig.store.load(), expected);
        QCOMPARE(rig.dialog->playlists(), expected);
        QCOMPARE(rig.shownNames().last(), u"더미 목록 셋"_s);
        QCOMPARE(rig.list()->currentRow(), 2);
        QCOMPARE(changed.count(), 1);

        // A cancelled add changes nothing.
        rig.nextEntry.reset();
        rig.click(u"add"_s);
        QCOMPARE(rig.store.load(), expected);
        QCOMPARE(changed.count(), 1);
    }

    void editIsSavedAtOnce() {
        Rig rig;
        rig.open(two);
        rig.select(1);
        rig.nextEntry = Playlist{u"고친 이름"_s, pid(2)};
        rig.click(u"edit"_s);
        QCOMPARE(rig.entryTitles, QStringList{u"플레이리스트 수정"_s});
        QCOMPARE(rig.editingIndexes, QList<int>{1});
        const QList<Playlist> expected = {two.at(0), Playlist{u"고친 이름"_s, pid(2)}};
        QCOMPARE(rig.store.load(), expected);
        QCOMPARE(rig.shownNames(), (QStringList{u"더미 목록 하나"_s, u"고친 이름"_s}));
    }

    void deleteAsksAndIsSavedAtOnce() {
        Rig rig;
        rig.open(two);
        rig.select(0);
        rig.click(u"remove"_s);
        QCOMPARE(rig.deleteQuestions, QStringList{u"더미 목록 하나"_s});
        QCOMPARE(rig.store.load(), two);
        rig.deleteAnswer = true;
        rig.click(u"remove"_s);
        QCOMPARE(rig.store.load(), QList<Playlist>{two.at(1)});
        QCOMPARE(rig.shownNames(), QStringList{u"더미 목록 둘"_s});
    }

    void upAndDownAreSavedAtOnce() {
        Rig rig;
        rig.open({two.at(0), two.at(1), Playlist{u"더미 목록 셋"_s, pid(3)}});
        rig.select(2);
        rig.click(u"up"_s);
        QCOMPARE(rig.store.load(), (QList<Playlist>{two.at(0), Playlist{u"더미 목록 셋"_s, pid(3)}, two.at(1)}));
        QCOMPARE(rig.list()->currentRow(), 1);
        rig.click(u"up"_s);
        QCOMPARE(rig.store.load(), (QList<Playlist>{Playlist{u"더미 목록 셋"_s, pid(3)}, two.at(0), two.at(1)}));
        rig.click(u"down"_s);
        rig.click(u"down"_s);
        QCOMPARE(rig.store.load(), (QList<Playlist>{two.at(0), two.at(1), Playlist{u"더미 목록 셋"_s, pid(3)}}));
        QCOMPARE(rig.list()->currentRow(), 2);
    }

    void resetMergesTheRemoteListsAndSaysHowMany() {
        Rig rig;
        rig.open(two);
        rig.click(u"reset"_s);
        QCOMPARE(rig.remote.fetchCalls, 1);
        QCOMPARE(rig.status(), u"받는 중…"_s);
        for (const QString& name : {u"add"_s, u"edit"_s, u"remove"_s, u"up"_s, u"down"_s, u"reset"_s}) {
            QVERIFY2(!rig.button(name)->isEnabled(), qPrintable(name));
        }
        QVERIFY(rig.button(u"close"_s)->isEnabled());

        RemoteOutcome outcome;
        outcome.ko = {RemoteEntry{2, u"원격 둘"_s, pid(4)}, RemoteEntry{1, u"원격 하나"_s, pid(1)}};
        outcome.en = {RemoteEntry{1, u"Remote"_s, pid(5)}};
        rig.remote.answer(outcome);
        const QList<Playlist> expected = {two.at(0), two.at(1), Playlist{u"원격 둘"_s, pid(4)}, Playlist{u"Remote"_s, pid(5)}};
        QCOMPARE(rig.store.load(), expected);
        QCOMPARE(rig.shownNames().size(), 4);
        QCOMPARE(rig.status(), u"2개를 더했습니다 (이미 있는 것 1개)"_s);
        QVERIFY(rig.button(u"reset"_s)->isEnabled());
        QVERIFY(rig.button(u"add"_s)->isEnabled());
    }

    void aConfigThatCannotBeReadChangesNothing() {
        Rig rig;
        rig.open(two);
        rig.click(u"reset"_s);
        RemoteOutcome outcome;
        outcome.configFailed = true;
        rig.remote.answer(outcome);
        QCOMPARE(rig.store.load(), two);
        QCOMPARE(rig.status(), u"원격 설정을 받지 못했습니다. 목록은 그대로입니다."_s);
    }

    void aLanguageThatFailedIsNamed() {
        Rig rig;
        rig.open(two);
        rig.click(u"reset"_s);
        RemoteOutcome outcome;
        outcome.enFailed = true;
        outcome.ko = {RemoteEntry{1, u"원격"_s, pid(6)}};
        rig.remote.answer(outcome);
        QCOMPARE(rig.store.load().size(), 3);
        QCOMPARE(rig.status(), u"1개를 더했습니다 (이미 있는 것 0개)\n영어 목록을 받지 못했습니다"_s);

        rig.click(u"reset"_s);
        RemoteOutcome both;
        both.koFailed = true;
        both.enFailed = true;
        rig.remote.answer(both);
        QCOMPARE(rig.store.load().size(), 3);
        QCOMPARE(rig.status(), u"0개를 더했습니다 (이미 있는 것 0개)\n한국어 목록을 받지 못했습니다\n영어 목록을 받지 못했습니다"_s);
    }

    void closingWhileTheListsComeCancelsTheTransfer() {
        Rig rig;
        rig.open(two);
        rig.click(u"reset"_s);
        QCOMPARE(rig.remote.cancelCalls, 0);
        rig.click(u"close"_s);
        QCOMPARE(rig.remote.cancelCalls, 1);
        QVERIFY(!rig.dialog->isVisible());

        // Closed without a transfer: nothing to cancel.
        Rig idle;
        idle.open(two);
        idle.dialog->reject();
        QCOMPARE(idle.remote.cancelCalls, 0);
    }

    void theEntryDialogSaysWhyItRefuses() {
        PlaylistEntryDialog dialog(u"플레이리스트 추가"_s, Playlist{}, two, -1);
        dialog.show();
        QLineEdit* name = dialog.findChild<QLineEdit*>(u"name"_s);
        QLineEdit* id = dialog.findChild<QLineEdit*>(u"id"_s);
        QLabel* reason = dialog.findChild<QLabel*>(u"reason"_s);
        QPushButton* save = dialog.findChild<QPushButton*>(u"save"_s);
        QVERIFY(name && id && reason && save);
        QCOMPARE(dialog.windowTitle(), u"플레이리스트 추가"_s);

        QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(reason->text(), xgs::ui::checkEntry(QString(), QString(), two).reason);
        QVERIFY(reason->isVisible());

        name->setText(u"더미 새 목록"_s);
        id->setText(pid(1));
        QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(reason->text(), u"이미 목록에 있습니다"_s);
        QVERIFY(!dialog.entry());

        id->setText(u"https://www.youtube.com/playlist?list="_s + pid(7));
        QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.entry(), (std::optional<Playlist>(Playlist{u"더미 새 목록"_s, pid(7)})));
    }

    void theEntryDialogKeepsTheIdOfTheEntryBeingEdited() {
        PlaylistEntryDialog dialog(u"플레이리스트 수정"_s, two.at(1), two, 1);
        QCOMPARE(dialog.findChild<QLineEdit*>(u"name"_s)->text(), two.at(1).name);
        QCOMPARE(dialog.findChild<QLineEdit*>(u"id"_s)->text(), two.at(1).pid);
        QTest::mouseClick(dialog.findChild<QPushButton*>(u"save"_s), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.entry(), std::optional<Playlist>(two.at(1)));
    }
};

XGS_OFFSCREEN_TEST_MAIN(PlaylistsDialogTest)
#include "tst_playlists_dialog.moc"
