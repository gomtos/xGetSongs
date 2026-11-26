#include <QtTest>

#include "playlist_entry_check.h"

// The Add and Edit dialog's check (spec §5.2) and the cleaning of names (§5.5). Every ID is made up but has the shape of a real
// playlist ID, because the main input field's classifier is the judge.

using namespace Qt::StringLiterals;
using xgs::ui::cleanName;
using xgs::ui::EntryCheck;
using xgs::ui::Playlist;
using xgs::ui::checkEntry;
using xgs::ui::playlistIdOf;

namespace {

QString pid(int n) {
    return u"PLdummy"_s + QString::number(n).rightJustified(27, u'0');   // 34 units like a real one
}

const QString videoId = u"dQw4w9WgXcQ"_s;

}  // namespace

class PlaylistEntryCheckTest : public QObject {
    Q_OBJECT

private slots:
    void aNameAndABarePlaylistIdAreAccepted() {
        const EntryCheck result = checkEntry(u"더미 목록"_s, pid(1), {});
        QVERIFY(result.entry.has_value());
        QCOMPARE(*result.entry, (Playlist{u"더미 목록"_s, pid(1)}));
        QVERIFY(result.reason.isEmpty());
    }

    void aPlaylistAddressGivesItsId() {
        const EntryCheck result = checkEntry(u"더미"_s, u"https://www.youtube.com/playlist?list="_s + pid(2) + u"&si=abc"_s, {});
        QVERIFY(result.entry.has_value());
        QCOMPARE(result.entry->pid, pid(2));
    }

    void aWatchAddressThatAlsoNamesAPlaylistIsTakenAsThePlaylist() {
        // The classifier of the input field takes it as the playlist (with the video remembered), so the lookup would list
        // the playlist; only the ID is stored.
        const EntryCheck result = checkEntry(u"더미"_s, u"https://www.youtube.com/watch?v="_s + videoId + u"&list="_s + pid(3), {});
        QVERIFY(result.entry.has_value());
        QCOMPARE(result.entry->pid, pid(3));
    }

    void anAddressOfOneVideoIsRefused() {
        const EntryCheck result = checkEntry(u"더미"_s, u"https://www.youtube.com/watch?v="_s + videoId, {});
        QVERIFY(!result.entry.has_value());
        QCOMPARE(result.reason, u"영상 하나의 주소입니다. 재생목록 ID나 주소를 넣으세요"_s);
        QVERIFY(!checkEntry(u"더미"_s, videoId, {}).entry.has_value());
        QVERIFY(!checkEntry(u"더미"_s, u"https://youtu.be/"_s + videoId, {}).entry.has_value());
    }

    void aWatchAddressWithAMixListIsAVideoAndRefused() {
        QVERIFY(!checkEntry(u"더미"_s, u"https://www.youtube.com/watch?v="_s + videoId + u"&list=RD"_s + videoId, {}).entry.has_value());
    }

    void aMixListAloneIsRefusedWithItsOwnReason() {
        const EntryCheck result = checkEntry(u"더미"_s, u"https://www.youtube.com/playlist?list=RDCLAK5uy_abcdefgh"_s, {});
        QVERIFY(!result.entry.has_value());
        QCOMPARE(result.reason, u"믹스 재생목록은 쓸 수 없습니다"_s);
    }

    void somethingThatIsNotAnIdOrAnAddressIsRefused() {
        for (const QString& text : {u"hello world"_s, u"PLshort"_s, u"https://evil.example.com/playlist?list="_s + pid(4)}) {
            const EntryCheck result = checkEntry(u"더미"_s, text, {});
            QVERIFY2(!result.entry.has_value(), qPrintable(text));
            QCOMPARE(result.reason, u"재생목록 ID나 주소가 아닙니다"_s);
        }
    }

    void anEmptyIdIsAskedFor() {
        for (const QString& text : {QString(), u"   "_s}) {
            const EntryCheck result = checkEntry(u"더미"_s, text, {});
            QVERIFY(!result.entry.has_value());
            QCOMPARE(result.reason, u"재생목록 ID나 주소를 입력하세요"_s);
        }
    }

    void anEmptyNameIsRefused() {
        for (const QString& name : {QString(), u"   "_s, u"\t\n"_s, u" \x01 "_s}) {
            const EntryCheck result = checkEntry(name, pid(5), {});
            QVERIFY(!result.entry.has_value());
            QCOMPARE(result.reason, u"이름을 입력하세요"_s);
        }
    }

    void theNameIsAskedForBeforeTheId() {
        QCOMPARE(checkEntry(QString(), QString(), {}).reason, u"이름을 입력하세요"_s);
    }

    void theNameIsTrimmedAndLosesControlCharacters() {
        const EntryCheck result = checkEntry(u"  더미\x07 목록\n "_s, pid(6), {});
        QVERIFY(result.entry.has_value());
        QCOMPARE(result.entry->name, u"더미 목록"_s);
    }

    void anIdThatIsAlreadyInTheListIsRefused() {
        const QList<Playlist> current = {{u"하나"_s, pid(7)}, {u"둘"_s, pid(8)}};
        const EntryCheck result = checkEntry(u"새 이름"_s, pid(8), current);
        QVERIFY(!result.entry.has_value());
        QCOMPARE(result.reason, u"이미 목록에 있습니다"_s);
        // The same ID typed as an address is the same ID.
        QVERIFY(!checkEntry(u"새 이름"_s, u"https://www.youtube.com/playlist?list="_s + pid(8), current).entry.has_value());
    }

    void theSameNameWithAnotherIdIsFine() {
        QVERIFY(checkEntry(u"하나"_s, pid(9), {{u"하나"_s, pid(7)}}).entry.has_value());
    }

    void idsAreComparedWithCase() {
        const QList<Playlist> current = {{u"하나"_s, u"PLdummy"_s + QString(27, u'a')}};
        QVERIFY(checkEntry(u"둘"_s, u"PLdummy"_s + QString(27, u'A'), current).entry.has_value());
    }

    void editingAnEntryMayKeepItsOwnId() {
        const QList<Playlist> current = {{u"하나"_s, pid(7)}, {u"둘"_s, pid(8)}};
        const EntryCheck result = checkEntry(u"고친 이름"_s, pid(8), current, 1);
        QVERIFY(result.entry.has_value());
        QCOMPARE(*result.entry, (Playlist{u"고친 이름"_s, pid(8)}));
    }

    void editingAnEntryCannotTakeAnotherOnesId() {
        const QList<Playlist> current = {{u"하나"_s, pid(7)}, {u"둘"_s, pid(8)}};
        const EntryCheck result = checkEntry(u"고친 이름"_s, pid(7), current, 1);
        QVERIFY(!result.entry.has_value());
        QCOMPARE(result.reason, u"이미 목록에 있습니다"_s);
    }

    void anEditingIndexOutsideTheListIsLikeAdding() {
        const QList<Playlist> current = {{u"하나"_s, pid(7)}};
        QVERIFY(!checkEntry(u"x"_s, pid(7), current, 5).entry.has_value());
        QVERIFY(!checkEntry(u"x"_s, pid(7), current, -1).entry.has_value());
    }

    // ---- cleaning (also what a remote name goes through) ----

    void cleanNameTrimsAndDropsControlCharacters() {
        QCOMPARE(cleanName(u"  가\x00나\x1f다\x7f\x85라\r\n "_s), u"가나다라"_s);
        QCOMPARE(cleanName(u"　가  "_s), u"가"_s);   // trimmed like Kotlin's trim(): ideographic and no-break spaces too
        QVERIFY(cleanName(u" \x01\x02 "_s).isEmpty());
        QVERIFY(cleanName(QString()).isEmpty());
    }

    void cleanNameKeepsInnerSpacesAndNonLatinText() {
        QCOMPARE(cleanName(u"  더미  목록 ♪ Dummy  "_s), u"더미  목록 ♪ Dummy"_s);
    }

    void cleanNameCapsTheLengthInUnits() {
        QCOMPARE(cleanName(QString(100, u'가'), 100), QString(100, u'가'));
        QCOMPARE(cleanName(QString(101, u'가'), 100), QString(100, u'가'));
        QCOMPARE(cleanName(QString(5000, u'가'), 100).size(), 100);
        QCOMPARE(cleanName(QString(300, u'가')).size(), 300);   // no limit given
    }

    void cleanNameNeverCutsASurrogatePairInHalf() {
        const QString pair = u"\U0001F3B5"_s;   // one code point, two units
        const QString text = QString(99, u'가') + pair + u"끝"_s;
        const QString cut = cleanName(text, 100);
        QCOMPARE(cut, QString(99, u'가'));   // the pair would have ended at unit 101
        QCOMPARE(cleanName(QString(98, u'가') + pair + u"끝"_s, 100), QString(98, u'가') + pair);
    }

    void cleanNameDoesNotLeaveASpaceAtTheCut() {
        QCOMPARE(cleanName(QString(99, u'가') + u"  나"_s, 100), QString(99, u'가'));
    }

    void playlistIdOfTakesOnlyPlaylists() {
        QCOMPARE(playlistIdOf(pid(1)), std::optional<QString>(pid(1)));
        QVERIFY(!playlistIdOf(videoId).has_value());
        QVERIFY(!playlistIdOf(QString()).has_value());
        QVERIFY(!playlistIdOf(u"http://evil.example.com/"_s).has_value());
    }
};

QTEST_APPLESS_MAIN(PlaylistEntryCheckTest)
#include "tst_playlist_entry_check.moc"
