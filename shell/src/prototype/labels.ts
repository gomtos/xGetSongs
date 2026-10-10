// The texts of the screen, ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt.

export type LyricsOutcome = "DESCRIPTION" | "ONLINE" | "NOT_FOUND" | "SEARCH_OFF";

export type ItemStatus =
  | { kind: "ready" }
  | { kind: "waiting" }
  | { kind: "downloading"; percent: number | null }
  | { kind: "finishing" }
  | { kind: "done"; lyrics: LyricsOutcome | null }
  | { kind: "skipped"; reason: string }
  | { kind: "failed"; message: string };

export type JobStatus = "COMPLETED" | "CANCELLED" | "FAILED";

export interface JobSummary {
  succeeded: number;
  skipped: number;
  failed: number;
}

/** The label of the button that updates yt-dlp; FORMAT_FAILURE_HELP points to it by this name. */
export const UPDATE_YT_DLP_LABEL = "yt-dlp 업데이트";

export const FORMAT_FAILURE_HELP =
  "재생목록의 모든 곡이 “m4a 오디오 형식이 없습니다.”로 실패하면 영상 문제가 아니라 yt-dlp가 낡았거나 " +
  "JavaScript 런타임(Node.js, Deno)에 문제가 있는 것일 수 있습니다. 앱 위쪽의 " +
  UPDATE_YT_DLP_LABEL +
  " 버튼으로 최신 버전으로 바꾸고 Node.js 22+ 또는 Deno 2.3+가 설치돼 있는지 확인한 뒤 다시 받으세요.";

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

/** `42초`, `3분 05초` or `1시간 02분 05초`; never negative. */
export function elapsedLabel(elapsedSeconds: number): string {
  const total = Math.max(0, Math.trunc(elapsedSeconds));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const seconds = total % 60;
  const two = (value: number) => String(value).padStart(2, "0");
  if (hours > 0) return `${hours}시간 ${two(minutes)}분 ${two(seconds)}초`;
  if (minutes > 0) return `${minutes}분 ${two(seconds)}초`;
  return `${seconds}초`;
}

/** The one-line result after a job ends; null while there is nothing to report. */
export function summaryText(
  summary: JobSummary | null,
  status: JobStatus | null,
  elapsedSeconds: number | null,
): string | null {
  if (summary === null || status === null) return null;
  const took = elapsedSeconds === null ? "" : ` · 소요 ${elapsedLabel(elapsedSeconds)}`;
  const counts = `성공 ${summary.succeeded} · 건너뜀 ${summary.skipped} · 실패 ${summary.failed}${took}`;
  switch (status) {
    case "COMPLETED":
      return `완료 — ${counts}`;
    case "CANCELLED":
      return `취소됨 — ${counts}`;
    case "FAILED":
      return `중단됨 — ${counts}`;
  }
}

/** The zero-padded rank shown in the list, e.g. `007`. */
export function rankLabel(rank: number): string {
  return String(rank).padStart(3, "0");
}
