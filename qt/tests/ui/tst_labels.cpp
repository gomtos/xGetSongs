#include <QtTest>

#include <QSet>

#include <optional>

#include "fake_app_api.h"
#include "labels.h"

// The tests of shell/tests/labels.test.ts (the cases of the Compose app's LabelsTest.kt), one slot per test and under the
// same name, every assertion kept. (Kotlin's `Duration` is milliseconds here, as in TypeScript.)

using namespace Qt::StringLiterals;
using namespace xgs::ui;
using xgs::InputKind;
using xgs::JobStatus;
using xgs::JobSummary;
using xgs::LyricsOutcome;
using xgs::ui::test::LyricsOutcomes;
using xgs::ui::test::resolvedState;

namespace {

ItemStatus done(std::optional<LyricsOutcome> lyrics = std::nullopt) {
    return ItemStatus::done(lyrics);
}

qint64 seconds(double value) {
    return qint64(value * 1000);
}

std::optional<QString> some(const QString& text) {
    return text;
}

const std::optional<QString> none;

UiState finishedState(std::optional<JobStatus> status, JobSummary summary, std::optional<qint64> elapsedMs = std::nullopt) {
    UiState state;
    state.jobStatus = status;
    state.summary = summary;
    state.elapsedMs = elapsedMs;
    return state;
}

UiState withOutputDir(const QString& dir) {
    UiState state;
    state.outputDir = dir;
    return state;
}

}  // namespace

class LabelsTest : public QObject {
    Q_OBJECT

private slots:
    void statusLabels() {
        QCOMPARE(statusLabel(ItemStatus::ready()), u"준비됨"_s);
        QCOMPARE(statusLabel(ItemStatus::waiting()), u"대기 중"_s);
        QCOMPARE(statusLabel(ItemStatus::downloading(std::nullopt)), u"다운로드 중…"_s);
        QCOMPARE(statusLabel(ItemStatus::downloading(42.9)), u"다운로드 42%"_s);
        QCOMPARE(statusLabel(ItemStatus::finishing()), u"마무리 중…"_s);
        QCOMPARE(statusLabel(done()), u"완료"_s);
        QCOMPARE(statusLabel(ItemStatus::skipped(u"비공개 영상"_s)), u"건너뜀: 비공개 영상"_s);
        QCOMPARE(statusLabel(ItemStatus::failed(u"boom"_s)), u"실패: boom"_s);
    }

    void theFormatFailureHelpNamesTheFailureTheUpdateButtonByItsRealLabelAndTheJsRuntimes() {
        const QString help = formatFailureHelp();
        QVERIFY(help.contains(u"“m4a 오디오 형식이 없습니다.”"_s));
        QVERIFY(help.contains(u"앱 위쪽의 "_s + updateYtDlpLabel() + u" 버튼"_s));
        QVERIFY(help.contains(u"JavaScript 런타임(Node.js, Deno)"_s));
        QVERIFY(help.contains(u"Node.js 22+ 또는 Deno 2.3+"_s));
    }

    void aFinishedRowSaysWhereItsLyricsCameFromOrWhyItHasNone() {
        QCOMPARE(statusLabel(done(std::nullopt)), u"완료"_s);
        QCOMPARE(statusLabel(done(LyricsOutcome::Description)), u"완료 · 가사 ✓ 설명란"_s);
        QCOMPARE(statusLabel(done(LyricsOutcome::Online)), u"완료 · 가사 ✓ 인터넷"_s);
        QCOMPARE(statusLabel(done(LyricsOutcome::NotFound)), u"완료 · 가사 없음"_s);
        QCOMPARE(statusLabel(done(LyricsOutcome::SearchOff)), u"완료 · 가사 없음 (검색 끔)"_s);
    }

    void aFinishedRowWithoutAKnownOutcomeIsTheSameAsTheDefault() {
        QCOMPARE(done(std::nullopt), done());
        QCOMPARE(statusLabel(done()), statusLabel(done(std::nullopt)));
    }

    void everyLyricsOutcomeHasItsOwnLabelStartingWithDone() {
        QStringList labels;
        for (LyricsOutcome outcome : LyricsOutcomes) labels.append(statusLabel(done(outcome)));

        QVERIFY2(QSet<QString>(labels.begin(), labels.end()).size() == labels.size(),
                 qPrintable(u"no two outcomes may look alike: "_s + labels.join(u',')));
        for (const QString& label : labels) QVERIFY2(label.startsWith(u"완료 · 가사 "_s), qPrintable(labels.join(u',')));
    }

    void theLongestFinishedLabelStillFitsOnOneLineOfTheStatusCell() {
        // The Compose status cell was 220.dp wide with 12.sp text, so a label of at most 17 characters never wrapped. The web
        // list kept the same bound on the text (the cell of the web list is as wide).
        qsizetype longest = statusLabel(done(std::nullopt)).size();
        for (LyricsOutcome outcome : LyricsOutcomes) longest = qMax(longest, statusLabel(done(outcome)).size());

        QVERIFY2(longest * 12 <= 220, qPrintable(u"the longest finished label has %1 characters"_s.arg(longest)));
    }

    void summaryTextByJobStatus() {
        const JobSummary summary{2, 1, 3};
        QCOMPARE(summaryText(finishedState(JobStatus::Completed, summary)), some(u"완료 — 성공 2 · 건너뜀 1 · 실패 3"_s));
        QCOMPARE(summaryText(finishedState(JobStatus::Cancelled, summary)), some(u"취소됨 — 성공 2 · 건너뜀 1 · 실패 3"_s));
        QCOMPARE(summaryText(finishedState(JobStatus::Failed, summary)), some(u"중단됨 — 성공 2 · 건너뜀 1 · 실패 3"_s));
    }

    void summaryTextAddsHowLongTheJobTookWhenItIsKnown() {
        const JobSummary summary{2, 1, 3};

        QCOMPARE(summaryText(finishedState(JobStatus::Completed, summary, seconds(185))),
                 some(u"완료 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 3분 05초"_s));
        QCOMPARE(summaryText(finishedState(JobStatus::Cancelled, summary, seconds(42))),
                 some(u"취소됨 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 42초"_s));
        QCOMPARE(summaryText(finishedState(JobStatus::Failed, summary, 400)), some(u"중단됨 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 0초"_s));
    }

    void elapsedLabelShowsWholeSecondsMinutesAndHours() {
        QCOMPARE(elapsedLabel(0), u"0초"_s);
        QCOMPARE(elapsedLabel(seconds(59.9)), u"59초"_s);
        QCOMPARE(elapsedLabel(seconds(60)), u"1분 00초"_s);
        QCOMPARE(elapsedLabel(seconds(185)), u"3분 05초"_s);
        QCOMPARE(elapsedLabel(seconds(3599)), u"59분 59초"_s);
        QCOMPARE(elapsedLabel(seconds(3600)), u"1시간 00분 00초"_s);
        QCOMPARE(elapsedLabel(seconds(7384)), u"2시간 03분 04초"_s);
        QVERIFY2(elapsedLabel(seconds(-5)) == u"0초"_s, "a clock that went backwards says nothing negative");
    }

    void noSummaryUntilAJobEnds() {
        QCOMPARE(summaryText(UiState()), none);
        QCOMPARE(summaryText(finishedState(std::nullopt, JobSummary{1, 0, 0})), none);
    }

    // ---- the text the album name field shows, which is also the name of the folder ----

    void anUntouchedFieldShowsTheOriginalPlaylistTitle() {
        QVERIFY2(albumNameText(resolvedState(InputKind::Playlist, u"Best: Of?"_s)) == u"Best: Of?"_s,
                 "the original title, which is what the files get");
        QCOMPARE(albumNameText(resolvedState(InputKind::Playlist, std::nullopt)), QString());
    }

    void anUntouchedFieldOfAVideoAndBeforeAnythingIsResolvedIsEmpty() {
        QCOMPARE(albumNameText(resolvedState(InputKind::Video, u"Sample"_s)), QString());
        QCOMPARE(albumNameText(UiState()), QString());
    }

    void aTouchedFieldShowsExactlyWhatWasTypedEvenWhenItIsBlank() {
        QCOMPARE(albumNameText(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\Music"_s, u"내 앨범 "_s)), u"내 앨범 "_s);
        QVERIFY2(albumNameText(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\Music"_s, u""_s)) == QString(),
                 "cleared by the user: the field stays empty");
    }

    void aClearedAlbumFieldMeansTheDefaultFolderInTheDestination() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\Music"_s, u""_s)), some(u"D:\\Music\\Sample"_s));
    }

    void aTypedAlbumNameReplacesThePlaylistFolderInTheDestination() {
        const UiState state = resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\Music"_s, u"내 앨범"_s);

        QCOMPARE(destinationPath(state), some(u"D:\\Music\\내 앨범"_s));
        QCOMPARE(destinationLabel(state), some(u"저장 위치: D:\\Music\\내 앨범"_s));
    }

    void aTypedAlbumNameGivesASingleVideoAFolderInTheDestination() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Video, std::nullopt, u"D:\\Music"_s, u"내 앨범"_s)), some(u"D:\\Music\\내 앨범"_s));
    }

    void aTypedAlbumNameIsShownAsAFolderTheWayTheServerWillMakeIt() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Video, std::nullopt, u"D:\\Music"_s, u" a\\b/c "_s)), some(u"D:\\Music\\a＼b／c"_s));
    }

    void aBlankTypedAlbumNameLeavesTheDestinationAsItWas() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\Music"_s, u"  "_s)), some(u"D:\\Music\\Sample"_s));
        QCOMPARE(destinationPath(resolvedState(InputKind::Video, std::nullopt, u"D:\\Music"_s, u"  "_s)), some(u"D:\\Music"_s));
    }

    void destinationIsNullBeforeAnythingIsResolved() {
        QCOMPARE(destinationLabel(withOutputDir(u"D:\\Music"_s)), none);
    }

    void destinationIsNullWhileTheOutputFolderIsBlank() {
        QCOMPARE(destinationLabel(resolvedState(InputKind::Playlist, u"Sample"_s, u""_s)), none);
        QCOMPARE(destinationLabel(resolvedState(InputKind::Video, std::nullopt, u"   "_s)), none);
    }

    void aVideoIsSavedInTheOutputFolderItself() {
        QCOMPARE(destinationLabel(resolvedState(InputKind::Video)), some(u"저장 위치: D:\\Music"_s));
        // a title on a video is ignored: only the kind decides whether there is a sub-folder
        QCOMPARE(destinationLabel(resolvedState(InputKind::Video, u"Sample"_s)), some(u"저장 위치: D:\\Music"_s));
    }

    void aPlaylistIsSavedInAFolderNamedAfterIt() {
        QCOMPARE(destinationLabel(resolvedState(InputKind::Playlist, u"Sample"_s)), some(u"저장 위치: D:\\Music\\Sample"_s));
    }

    void trailingSlashesOfTheOutputFolderAreDroppedBeforeTheFolderName() {
        QCOMPARE(destinationLabel(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\Music\\"_s)), some(u"저장 위치: D:\\Music\\Sample"_s));
        QCOMPARE(destinationLabel(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:/Music/"_s)), some(u"저장 위치: D:/Music\\Sample"_s));
        QCOMPARE(destinationLabel(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\"_s)), some(u"저장 위치: D:\\Sample"_s));
    }

    void theFolderNameIsSanitizedLikeTheServerDoesIt() {
        // full-width colon and question mark
        QCOMPARE(destinationLabel(resolvedState(InputKind::Playlist, u"Best: Of?"_s)), some(u"저장 위치: D:\\Music\\Best： Of？"_s));
        QCOMPARE(destinationLabel(resolvedState(InputKind::Playlist, std::nullopt)), some(u"저장 위치: D:\\Music\\재생목록"_s));
    }

    // ---- destinationPath: the path part of the destination label ----

    void destinationPathIsNullBeforeAnythingIsResolved() {
        QCOMPARE(destinationPath(withOutputDir(u"D:\\Music"_s)), none);
    }

    void destinationPathIsNullWhileTheOutputFolderIsBlank() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, u"Sample"_s, u""_s)), none);
        QCOMPARE(destinationPath(resolvedState(InputKind::Video, std::nullopt, u"   "_s)), none);
    }

    void theDestinationPathOfAVideoIsTheOutputFolderItself() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Video)), some(u"D:\\Music"_s));
        QCOMPARE(destinationPath(resolvedState(InputKind::Video, u"Sample"_s)), some(u"D:\\Music"_s));
    }

    void theDestinationPathOfAPlaylistIsAFolderNamedAfterIt() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, u"Sample"_s)), some(u"D:\\Music\\Sample"_s));
    }

    void theDestinationPathDropsTrailingSeparatorsOfTheOutputFolder() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\Music\\"_s)), some(u"D:\\Music\\Sample"_s));
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:/Music/"_s)), some(u"D:/Music\\Sample"_s));
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\"_s)), some(u"D:\\Sample"_s));
    }

    void theDestinationPathUsesTheSanitizedFolderName() {
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, u"Best: Of?"_s)), some(u"D:\\Music\\Best： Of？"_s));
        QCOMPARE(destinationPath(resolvedState(InputKind::Playlist, std::nullopt)), some(u"D:\\Music\\재생목록"_s));
    }

    void theDestinationLabelIsThePathWithItsCaptionAndNullWhenThereIsNoPath() {
        const QList<UiState> states = {
            withOutputDir(u"D:\\Music"_s),
            resolvedState(InputKind::Playlist, u"Sample"_s, u""_s),
            resolvedState(InputKind::Video),
            resolvedState(InputKind::Playlist, u"Sample"_s, u"D:\\Music\\"_s),
            resolvedState(InputKind::Playlist, std::nullopt),
        };
        for (const UiState& state : states) {
            const std::optional<QString> path = destinationPath(state);
            QCOMPARE(destinationLabel(state), path ? some(u"저장 위치: "_s + *path) : none);
        }
        QCOMPARE(destinationLabel(resolvedState(InputKind::Playlist, std::nullopt)), some(u"저장 위치: D:\\Music\\재생목록"_s));
    }

    void rankIsZeroPadded() {
        QCOMPARE(rankLabel(7), u"007"_s);
        QCOMPARE(rankLabel(999), u"999"_s);
    }

    void rankInputKeepsOnlyUpToThreeDigits() {
        QCOMPARE(rankInputText(u"12a3"_s), u"123"_s);
        QCOMPARE(rankInputText(u"5000"_s), u"500"_s);
        QCOMPARE(rankInputText(u""_s), u""_s);
        QCOMPARE(rankInputText(u"abc"_s), u""_s);
    }

    void rankFromInputIsNullForEmptyText() {
        QCOMPARE(rankFromInput(u""_s), std::optional<int>());
    }

    void rankFromInputClampsToTheValidRange() {
        QCOMPARE(rankFromInput(u"0"_s), std::optional<int>(1));
        QCOMPARE(rankFromInput(u"7"_s), std::optional<int>(7));
        QCOMPARE(rankFromInput(u"999"_s), std::optional<int>(999));
        QCOMPARE(rankFromInput(u"000"_s), std::optional<int>(1));
    }
};

QTEST_APPLESS_MAIN(LabelsTest)
#include "tst_labels.moc"
