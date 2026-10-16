// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt (the Compose app, deleted in stage 6 of the migration:
// `git show f9dbcba:<that path>`): the texts of the screen.

import type { LyricsOutcome } from "../bridge/types.ts";
import { MAX_RANK, MIN_RANK, destinationFolder } from "../logic/filename.ts";
import { trim, trimEndChars } from "../logic/text.ts";
import type { ItemStatus, UiState } from "./types.ts";

/** The label of the button that updates yt-dlp; FORMAT_FAILURE_HELP points to it by this name. */
export const UPDATE_YT_DLP_LABEL = "yt-dlp 업데이트";

/**
 * The help shown next to the update button of the tools panel. "m4a 오디오 형식이 없습니다." (the engine's message) is also what
 * an outdated yt-dlp or a broken JavaScript runtime makes every song fail with, so the user is told to check those first.
 */
export const FORMAT_FAILURE_HELP =
  "재생목록의 모든 곡이 “m4a 오디오 형식이 없습니다.”로 실패하면 영상 문제가 아니라 yt-dlp가 낡았거나 " +
  `JavaScript 런타임(Node.js, Deno)에 문제가 있는 것일 수 있습니다. 앱 위쪽의 ${UPDATE_YT_DLP_LABEL} 버튼으로 ` +
  "최신 버전으로 바꾸고 Node.js 22+ 또는 Deno 2.3+가 설치돼 있는지 확인한 뒤 다시 받으세요.";

/** The text shown in an item's status cell. */
export function statusLabel(status: ItemStatus): string {
  switch (status.kind) {
    case "ready":
      return "준비됨";
    case "waiting":
      return "대기 중";
    case "downloading":
      return status.percent === null ? "다운로드 중…" : `다운로드 ${Math.trunc(status.percent)}%`;
    case "finishing":
      return "마무리 중…";
    case "done":
      return doneLabel(status.lyrics);
    case "skipped":
      return `건너뜀: ${status.reason}`;
    case "failed":
      return `실패: ${status.message}`;
  }
}

/** A finished row says where its lyrics came from or why it has none. Plain `완료` when the server did not report an outcome. */
function doneLabel(lyrics: LyricsOutcome | null): string {
  switch (lyrics) {
    case null:
      return "완료";
    case "DESCRIPTION":
      return "완료 · 가사 ✓ 설명란";
    case "ONLINE":
      return "완료 · 가사 ✓ 인터넷";
    case "NOT_FOUND":
      return "완료 · 가사 없음";
    case "SEARCH_OFF":
      return "완료 · 가사 없음 (검색 끔)";
  }
}

/** The one-line result shown after a job ends, with how long it took when that is known; null while there is nothing to report. */
export function summaryText(state: UiState): string | null {
  const summary = state.summary;
  if (summary === null) return null;
  const took = state.elapsedMs === null ? "" : ` · 소요 ${elapsedLabel(state.elapsedMs)}`;
  const counts = `성공 ${summary.succeeded} · 건너뜀 ${summary.skipped} · 실패 ${summary.failed}${took}`;
  switch (state.jobStatus) {
    case "COMPLETED":
      return `완료 — ${counts}`;
    case "CANCELLED":
      return `취소됨 — ${counts}`;
    case "FAILED":
      return `중단됨 — ${counts}`;
    case null:
      return null;
  }
}

/** `elapsedMs` in whole seconds as `42초`, `3분 05초` or `1시간 02분 05초`; never negative. */
export function elapsedLabel(elapsedMs: number): string {
  const total = Math.max(Math.trunc(elapsedMs / 1000), 0);
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const seconds = total % 60;
  const two = (value: number) => String(value).padStart(2, "0");
  if (hours > 0) return `${hours}시간 ${two(minutes)}분 ${two(seconds)}초`;
  if (minutes > 0) return `${minutes}분 ${two(seconds)}초`;
  return `${seconds}초`;
}

/**
 * Where the files will be saved: a folder named after the album name the user typed inside the output folder, else a folder
 * named after the playlist for a playlist, else the output folder itself for a video (the same names the server uses). Null
 * until something is resolved and an output folder is typed.
 */
export function destinationPath(state: UiState): string | null {
  const resolved = state.resolved;
  if (resolved === null) return null;
  if (trim(state.outputDir) === "") return null;
  const folder = destinationFolder(resolved.kind === "PLAYLIST", resolved.playlistTitle, state.albumName);
  if (folder === null) return state.outputDir;
  return trimEndChars(state.outputDir, "\\/") + "\\" + folder;
}

/**
 * What the album name field shows: the text the user typed, else the title of the playlist, which the files get as their
 * album when the field is left alone (a single video keeps the album of its own then), else nothing. The folder is named
 * after the same text (see `destinationPath`).
 */
export function albumNameText(state: UiState): string {
  if (state.albumName !== null) return state.albumName;
  const resolved = state.resolved;
  return resolved !== null && resolved.kind === "PLAYLIST" ? (resolved.playlistTitle ?? "") : "";
}

/** The line shown above the options: `destinationPath` with its caption; null when there is no destination yet. */
export function destinationLabel(state: UiState): string | null {
  const path = destinationPath(state);
  return path === null ? null : `저장 위치: ${path}`;
}

/** The zero-padded rank shown in the list, e.g. `007`. */
export function rankLabel(rank: number): string {
  return String(rank).padStart(3, "0");
}

/** What the rank text field may hold while the user types: digits only, at most three of them. (ASCII digits only.) */
export function rankInputText(raw: string): string {
  return raw.replace(/[^0-9]/g, "").slice(0, 3);
}

/** The rank typed so far, kept within the valid range; null while the field is empty. */
export function rankFromInput(text: string): number | null {
  if (!/^[0-9]+$/.test(text)) return null;
  return Math.min(Math.max(Number.parseInt(text, 10), MIN_RANK), MAX_RANK);
}
