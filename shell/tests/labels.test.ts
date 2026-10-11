// The cases of app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt, one test per Kotlin test and under the same
// name. (Kotlin's `Duration` is milliseconds here.)

import assert from "node:assert/strict";
import test from "node:test";
import {
  FORMAT_FAILURE_HELP,
  UPDATE_YT_DLP_LABEL,
  albumNameText,
  destinationLabel,
  destinationPath,
  elapsedLabel,
  rankFromInput,
  rankInputText,
  rankLabel,
  statusLabel,
  summaryText,
} from "../src/state/labels.ts";
import type { LyricsOutcome } from "../src/bridge/types.ts";
import { initialUiState, type ItemStatus } from "../src/state/types.ts";
import { LYRICS_OUTCOMES, resolvedState } from "./support/fixtures.ts";

const done = (lyrics: LyricsOutcome | null = null): ItemStatus => ({ kind: "done", lyrics });
const seconds = (value: number) => value * 1000;

test("statusLabels", () => {
  assert.equal(statusLabel({ kind: "ready" }), "준비됨");
  assert.equal(statusLabel({ kind: "waiting" }), "대기 중");
  assert.equal(statusLabel({ kind: "downloading", percent: null }), "다운로드 중…");
  assert.equal(statusLabel({ kind: "downloading", percent: 42.9 }), "다운로드 42%");
  assert.equal(statusLabel({ kind: "finishing" }), "마무리 중…");
  assert.equal(statusLabel(done()), "완료");
  assert.equal(statusLabel({ kind: "skipped", reason: "비공개 영상" }), "건너뜀: 비공개 영상");
  assert.equal(statusLabel({ kind: "failed", message: "boom" }), "실패: boom");
});

test("theFormatFailureHelpNamesTheFailureTheUpdateButtonByItsRealLabelAndTheJsRuntimes", () => {
  assert.ok(FORMAT_FAILURE_HELP.includes("“m4a 오디오 형식이 없습니다.”"));
  assert.ok(FORMAT_FAILURE_HELP.includes(`앱 위쪽의 ${UPDATE_YT_DLP_LABEL} 버튼`));
  assert.ok(FORMAT_FAILURE_HELP.includes("JavaScript 런타임(Node.js, Deno)"));
  assert.ok(FORMAT_FAILURE_HELP.includes("Node.js 22+ 또는 Deno 2.3+"));
});

test("aFinishedRowSaysWhereItsLyricsCameFromOrWhyItHasNone", () => {
  assert.equal(statusLabel(done(null)), "완료");
  assert.equal(statusLabel(done("DESCRIPTION")), "완료 · 가사 ✓ 설명란");
  assert.equal(statusLabel(done("ONLINE")), "완료 · 가사 ✓ 인터넷");
  assert.equal(statusLabel(done("NOT_FOUND")), "완료 · 가사 없음");
  assert.equal(statusLabel(done("SEARCH_OFF")), "완료 · 가사 없음 (검색 끔)");
});

test("aFinishedRowWithoutAKnownOutcomeIsTheSameAsTheDefault", () => {
  assert.deepEqual(done(null), done());
  assert.equal(statusLabel(done()), statusLabel(done(null)));
});

test("everyLyricsOutcomeHasItsOwnLabelStartingWithDone", () => {
  const labels = LYRICS_OUTCOMES.map((outcome) => statusLabel(done(outcome)));

  assert.equal(new Set(labels).size, labels.length, `no two outcomes may look alike: ${labels}`);
  assert.ok(labels.every((label) => label.startsWith("완료 · 가사 ")), String(labels));
});

test("theLongestFinishedLabelStillFitsOnOneLineOfTheStatusCell", () => {
  // The Compose status cell was 220.dp wide with 12.sp text, so a label of at most 17 characters never wrapped. The web
  // list keeps the same bound on the text (the cell of the web list is as wide).
  const longest = Math.max(...[...LYRICS_OUTCOMES, null].map((outcome) => statusLabel(done(outcome)).length));

  assert.ok(longest * 12 <= 220, `the longest finished label has ${longest} characters`);
});

test("summaryTextByJobStatus", () => {
  const summary = { succeeded: 2, skipped: 1, failed: 3 };
  assert.equal(summaryText(initialUiState({ jobStatus: "COMPLETED", summary })), "완료 — 성공 2 · 건너뜀 1 · 실패 3");
  assert.equal(summaryText(initialUiState({ jobStatus: "CANCELLED", summary })), "취소됨 — 성공 2 · 건너뜀 1 · 실패 3");
  assert.equal(summaryText(initialUiState({ jobStatus: "FAILED", summary })), "중단됨 — 성공 2 · 건너뜀 1 · 실패 3");
});

test("summaryTextAddsHowLongTheJobTookWhenItIsKnown", () => {
  const summary = { succeeded: 2, skipped: 1, failed: 3 };

  assert.equal(
    summaryText(initialUiState({ jobStatus: "COMPLETED", summary, elapsedMs: seconds(185) })),
    "완료 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 3분 05초",
  );
  assert.equal(
    summaryText(initialUiState({ jobStatus: "CANCELLED", summary, elapsedMs: seconds(42) })),
    "취소됨 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 42초",
  );
  assert.equal(
    summaryText(initialUiState({ jobStatus: "FAILED", summary, elapsedMs: 400 })),
    "중단됨 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 0초",
  );
});

test("elapsedLabelShowsWholeSecondsMinutesAndHours", () => {
  assert.equal(elapsedLabel(0), "0초");
  assert.equal(elapsedLabel(seconds(59.9)), "59초");
  assert.equal(elapsedLabel(seconds(60)), "1분 00초");
  assert.equal(elapsedLabel(seconds(185)), "3분 05초");
  assert.equal(elapsedLabel(seconds(3599)), "59분 59초");
  assert.equal(elapsedLabel(seconds(3600)), "1시간 00분 00초");
  assert.equal(elapsedLabel(seconds(7384)), "2시간 03분 04초");
  assert.equal(elapsedLabel(seconds(-5)), "0초", "a clock that went backwards says nothing negative");
});

test("noSummaryUntilAJobEnds", () => {
  assert.equal(summaryText(initialUiState()), null);
  assert.equal(summaryText(initialUiState({ summary: { succeeded: 1, skipped: 0, failed: 0 }, jobStatus: null })), null);
});

// ---- the text the album name field shows, which is also the name of the folder ----

test("anUntouchedFieldShowsTheOriginalPlaylistTitle", () => {
  assert.equal(albumNameText(resolvedState("PLAYLIST", "Best: Of?")), "Best: Of?", "the original title, which is what the files get");
  assert.equal(albumNameText(resolvedState("PLAYLIST", null)), "");
});

test("anUntouchedFieldOfAVideoAndBeforeAnythingIsResolvedIsEmpty", () => {
  assert.equal(albumNameText(resolvedState("VIDEO", "Sample")), "");
  assert.equal(albumNameText(initialUiState()), "");
});

test("aTouchedFieldShowsExactlyWhatWasTypedEvenWhenItIsBlank", () => {
  assert.equal(albumNameText(resolvedState("PLAYLIST", "Sample", "D:\\Music", "내 앨범 ")), "내 앨범 ");
  assert.equal(albumNameText(resolvedState("PLAYLIST", "Sample", "D:\\Music", "")), "", "cleared by the user: the field stays empty");
});

test("aClearedAlbumFieldMeansTheDefaultFolderInTheDestination", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:\\Music", "")), "D:\\Music\\Sample");
});

test("aTypedAlbumNameReplacesThePlaylistFolderInTheDestination", () => {
  const state = resolvedState("PLAYLIST", "Sample", "D:\\Music", "내 앨범");

  assert.equal(destinationPath(state), "D:\\Music\\내 앨범");
  assert.equal(destinationLabel(state), "저장 위치: D:\\Music\\내 앨범");
});

test("aTypedAlbumNameGivesASingleVideoAFolderInTheDestination", () => {
  assert.equal(destinationPath(resolvedState("VIDEO", null, "D:\\Music", "내 앨범")), "D:\\Music\\내 앨범");
});

test("aTypedAlbumNameIsShownAsAFolderTheWayTheServerWillMakeIt", () => {
  assert.equal(destinationPath(resolvedState("VIDEO", null, "D:\\Music", " a\\b/c ")), "D:\\Music\\a＼b／c");
});

test("aBlankTypedAlbumNameLeavesTheDestinationAsItWas", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:\\Music", "  ")), "D:\\Music\\Sample");
  assert.equal(destinationPath(resolvedState("VIDEO", null, "D:\\Music", "  ")), "D:\\Music");
});

test("destinationIsNullBeforeAnythingIsResolved", () => {
  assert.equal(destinationLabel(initialUiState({ outputDir: "D:\\Music" })), null);
});

test("destinationIsNullWhileTheOutputFolderIsBlank", () => {
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample", "")), null);
  assert.equal(destinationLabel(resolvedState("VIDEO", null, "   ")), null);
});

test("aVideoIsSavedInTheOutputFolderItself", () => {
  assert.equal(destinationLabel(resolvedState("VIDEO")), "저장 위치: D:\\Music");
  // a title on a video is ignored: only the kind decides whether there is a sub-folder
  assert.equal(destinationLabel(resolvedState("VIDEO", "Sample")), "저장 위치: D:\\Music");
});

test("aPlaylistIsSavedInAFolderNamedAfterIt", () => {
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample")), "저장 위치: D:\\Music\\Sample");
});

test("trailingSlashesOfTheOutputFolderAreDroppedBeforeTheFolderName", () => {
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample", "D:\\Music\\")), "저장 위치: D:\\Music\\Sample");
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample", "D:/Music/")), "저장 위치: D:/Music\\Sample");
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample", "D:\\")), "저장 위치: D:\\Sample");
});

test("theFolderNameIsSanitizedLikeTheServerDoesIt", () => {
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Best: Of?")), "저장 위치: D:\\Music\\Best： Of？"); // full-width colon and question mark
  assert.equal(destinationLabel(resolvedState("PLAYLIST", null)), "저장 위치: D:\\Music\\재생목록");
});

// ---- destinationPath: the path part of the destination label ----

test("destinationPathIsNullBeforeAnythingIsResolved", () => {
  assert.equal(destinationPath(initialUiState({ outputDir: "D:\\Music" })), null);
});

test("destinationPathIsNullWhileTheOutputFolderIsBlank", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "")), null);
  assert.equal(destinationPath(resolvedState("VIDEO", null, "   ")), null);
});

test("theDestinationPathOfAVideoIsTheOutputFolderItself", () => {
  assert.equal(destinationPath(resolvedState("VIDEO")), "D:\\Music");
  assert.equal(destinationPath(resolvedState("VIDEO", "Sample")), "D:\\Music");
});

test("theDestinationPathOfAPlaylistIsAFolderNamedAfterIt", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample")), "D:\\Music\\Sample");
});

test("theDestinationPathDropsTrailingSeparatorsOfTheOutputFolder", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:\\Music\\")), "D:\\Music\\Sample");
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:/Music/")), "D:/Music\\Sample");
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:\\")), "D:\\Sample");
});

test("theDestinationPathUsesTheSanitizedFolderName", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Best: Of?")), "D:\\Music\\Best： Of？");
  assert.equal(destinationPath(resolvedState("PLAYLIST", null)), "D:\\Music\\재생목록");
});

test("theDestinationLabelIsThePathWithItsCaptionAndNullWhenThereIsNoPath", () => {
  const states = [
    initialUiState({ outputDir: "D:\\Music" }),
    resolvedState("PLAYLIST", "Sample", ""),
    resolvedState("VIDEO"),
    resolvedState("PLAYLIST", "Sample", "D:\\Music\\"),
    resolvedState("PLAYLIST", null),
  ];
  for (const state of states) {
    const path = destinationPath(state);
    assert.equal(destinationLabel(state), path === null ? null : `저장 위치: ${path}`);
  }
  assert.equal(destinationLabel(resolvedState("PLAYLIST", null)), "저장 위치: D:\\Music\\재생목록");
});

test("rankIsZeroPadded", () => {
  assert.equal(rankLabel(7), "007");
  assert.equal(rankLabel(999), "999");
});

test("rankInputKeepsOnlyUpToThreeDigits", () => {
  assert.equal(rankInputText("12a3"), "123");
  assert.equal(rankInputText("5000"), "500");
  assert.equal(rankInputText(""), "");
  assert.equal(rankInputText("abc"), "");
});

test("rankFromInputIsNullForEmptyText", () => {
  assert.equal(rankFromInput(""), null);
});

test("rankFromInputClampsToTheValidRange", () => {
  assert.equal(rankFromInput("0"), 1);
  assert.equal(rankFromInput("7"), 7);
  assert.equal(rankFromInput("999"), 999);
  assert.equal(rankFromInput("000"), 1);
});
