#include <QtTest>

#include "playlist_merge.h"

// Spec §5.4 (the Reset button merges, it does not overwrite) and the cases of §5.6. Names and IDs are made up.

using namespace Qt::StringLiterals;
using xgs::ui::MergeResult;
using xgs::ui::Playlist;
using xgs::ui::RemoteEntry;
using xgs::ui::mergeRemote;

namespace {

Playlist mine(int n, const QString& name = QString()) {
    return Playlist{name.isEmpty() ? u"내 목록 %1"_s.arg(n) : name, u"PLmine%1"_s.arg(n)};
}

RemoteEntry remote(int sn, const QString& pid, const QString& name = QString()) {
    return RemoteEntry{sn, name.isEmpty() ? u"원격 %1"_s.arg(pid) : name, pid};
}

QStringList pids(const QList<Playlist>& list) {
    QStringList result;
    for (const Playlist& entry : list) result.append(entry.pid);
    return result;
}

}  // namespace

class PlaylistMergeTest : public QObject {
    Q_OBJECT

private slots:
    void mergingIntoAnEmptyListAddsKoreanThenEnglishBySn() {
        const MergeResult result = mergeRemote({}, {remote(1, u"K1"_s), remote(2, u"K2"_s)}, {remote(1, u"E1"_s)});
        QCOMPARE(pids(result.list), (QStringList{u"K1"_s, u"K2"_s, u"E1"_s}));
        QCOMPARE(result.added, 3);
        QCOMPARE(result.alreadyThere, 0);
    }

    void anInputWithSnOutOfOrderIsSortedBySnInEachLanguage() {
        const MergeResult result = mergeRemote({}, {remote(3, u"K3"_s), remote(1, u"K1"_s), remote(2, u"K2"_s)},
                                               {remote(20, u"E20"_s), remote(10, u"E10"_s)});
        QCOMPARE(pids(result.list), (QStringList{u"K1"_s, u"K2"_s, u"K3"_s, u"E10"_s, u"E20"_s}));
    }

    void entriesWithTheSameSnKeepTheirOrder() {
        const MergeResult result = mergeRemote({}, {remote(5, u"Ka"_s), remote(1, u"Kb"_s), remote(5, u"Kc"_s), remote(5, u"Kd"_s)}, {});
        QCOMPARE(pids(result.list), (QStringList{u"Kb"_s, u"Ka"_s, u"Kc"_s, u"Kd"_s}));
    }

    void aKoreanEntryWithAHigherSnStillComesBeforeEveryEnglishOne() {
        const MergeResult result = mergeRemote({}, {remote(99, u"K99"_s)}, {remote(1, u"E1"_s)});
        QCOMPARE(pids(result.list), (QStringList{u"K99"_s, u"E1"_s}));
    }

    void someOverlapAddsOnlyTheNewOnesAtTheEnd() {
        const QList<Playlist> current = {mine(1), Playlist{u"고친 이름"_s, u"K2"_s}, mine(2)};
        const MergeResult result = mergeRemote(current, {remote(1, u"K1"_s), remote(2, u"K2"_s)}, {remote(1, u"E1"_s)});
        QCOMPARE(pids(result.list), (QStringList{u"PLmine1"_s, u"K2"_s, u"PLmine2"_s, u"K1"_s, u"E1"_s}));
        QCOMPARE(result.added, 2);
        QCOMPARE(result.alreadyThere, 1);
    }

    void aFullOverlapChangesNothing() {
        const QList<Playlist> current = {Playlist{u"내가 지은 이름"_s, u"K1"_s}, Playlist{u"다른 이름"_s, u"E1"_s}};
        const MergeResult result = mergeRemote(current, {remote(1, u"K1"_s)}, {remote(1, u"E1"_s)});
        QCOMPARE(result.list, current);
        QCOMPARE(result.added, 0);
        QCOMPARE(result.alreadyThere, 2);
    }

    void aPidInBothLanguagesIsAddedOnceWithTheKoreanName() {
        const MergeResult result = mergeRemote({}, {remote(1, u"SAME"_s, u"한국어 이름"_s)}, {remote(1, u"SAME"_s, u"English name"_s), remote(2, u"E2"_s)});
        QCOMPARE(result.list, (QList<Playlist>{{u"한국어 이름"_s, u"SAME"_s}, {u"원격 E2"_s, u"E2"_s}}));
        QCOMPARE(result.added, 2);
        QCOMPARE(result.alreadyThere, 1);
    }

    void aPidTwiceInOneLanguageIsAddedOnce() {
        const MergeResult result = mergeRemote({}, {remote(2, u"DUP"_s, u"둘째"_s), remote(1, u"DUP"_s, u"첫째"_s)}, {});
        QCOMPARE(result.list, (QList<Playlist>{{u"첫째"_s, u"DUP"_s}}));
        QCOMPARE(result.alreadyThere, 1);
    }

    void thePidMustMatchInCaseAndTheNameIsNotLookedAt() {
        const QList<Playlist> current = {Playlist{u"같은 이름"_s, u"plabc"_s}};
        const MergeResult result = mergeRemote(current, {remote(1, u"PLABC"_s, u"같은 이름"_s), remote(2, u"plabc"_s, u"다른 이름"_s)}, {});
        QCOMPARE(pids(result.list), (QStringList{u"plabc"_s, u"PLABC"_s}));
        QCOMPARE(result.added, 1);
        QCOMPARE(result.alreadyThere, 1);
    }

    void anEntryTheUserDeletedComesBackWhenTheRemoteHasIt() {
        const MergeResult result = mergeRemote({mine(1)}, {remote(1, u"K1"_s), remote(2, u"K2"_s)}, {});
        QCOMPARE(pids(result.list), (QStringList{u"PLmine1"_s, u"K1"_s, u"K2"_s}));
    }

    void nothingFromTheRemoteLeavesTheListAsItWas() {
        const QList<Playlist> current = {mine(1), mine(2)};
        const MergeResult result = mergeRemote(current, {}, {});
        QCOMPARE(result.list, current);
        QCOMPARE(result.added, 0);
        QCOMPARE(result.alreadyThere, 0);
    }

    void oneLanguageEmptyStillMergesTheOther() {
        QCOMPARE(pids(mergeRemote({}, {}, {remote(1, u"E1"_s)}).list), QStringList{u"E1"_s});
        QCOMPARE(pids(mergeRemote({}, {remote(1, u"K1"_s)}, {}).list), QStringList{u"K1"_s});
    }

    void theCurrentListIsNeverReorderedOrRenamed() {
        const QList<Playlist> current = {mine(3, u"c"_s), mine(1, u"a"_s), mine(2, u"b"_s)};
        const MergeResult result = mergeRemote(current, {remote(1, u"PLmine1"_s, u"원격 이름"_s), remote(2, u"K2"_s)}, {});
        QCOMPARE(result.list.mid(0, 3), current);
        QCOMPARE(result.list.size(), 4);
    }
};

QTEST_APPLESS_MAIN(PlaylistMergeTest)
#include "tst_playlist_merge.moc"
