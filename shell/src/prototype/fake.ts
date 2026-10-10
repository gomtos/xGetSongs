// Fake data for the prototype: a resolved playlist of 100 songs and a download that moves like the real one.

import { rankLabel, type ItemStatus, type LyricsOutcome } from "./labels.ts";

export interface Row {
  rank: number;
  artist: string;
  track: string;
  /** The name shown in the list; it follows the "파일명에 순번 포함" option. */
  fileName: string;
  available: boolean;
  /** True when the artist was guessed from the channel name (the list shows a warning). */
  lowConfidence: boolean;
  status: ItemStatus;
}

export interface ToolInfo {
  found: boolean;
  version?: string;
}

export interface ToolsStatus {
  ytDlp: ToolInfo;
  ffmpeg: ToolInfo;
  jsRuntime: ToolInfo;
}

export const PLAYLIST_TITLE = "Melon Daily Top 100";
export const SAMPLE_URL = "https://www.youtube.com/playlist?list=PLabcdefghijklmnop";

export const TOOLS_OK: ToolsStatus = {
  ytDlp: { found: true, version: "2026.10.01" },
  ffmpeg: { found: true, version: "8.1" },
  jsRuntime: { found: true, version: "24.11.1" },
};

export const TOOLS_BROKEN: ToolsStatus = {
  ytDlp: { found: true, version: "2026.10.01" },
  ffmpeg: { found: false },
  jsRuntime: { found: false },
};

// Titles from the design document's fixtures: Korean and English, parentheses, a long one that needs two lines.
const SAMPLE: ReadonlyArray<readonly [string, string]> = [
  ["소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)"],
  ["RESCENE (리센느)", "LOVE ATTACK"],
  ["아이오아이 (I.O.I)", "갑자기 (Suddenly)"],
  ["ATEEZ(에이티즈)", "BAD"],
  ["태연 (TAEYEON)", "만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"],
  ["WOODZ", "Drowning"],
  ["진영&최유리", "생각을 멈추다 보면"],
  ["성시경", "너의 모든 순간"],
  ["Girls' Generation-HRS 소녀시대-효리수", "Skibidi"],
  ["볼빨간사춘기 BOL4", "여름아 부탁해"],
  ["AKMU", "어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)"],
];

export function fileNameOf(row: Pick<Row, "rank" | "artist" | "track">, includeRank: boolean): string {
  return `${includeRank ? `${rankLabel(row.rank)} ` : ""}${row.artist} - ${row.track}.m4a`;
}

/** A resolved playlist: two videos that are not available (ranks 7 and 42) and every 17th with a guessed artist. */
export function buildRows(count = 100, includeRank = true): Row[] {
  return Array.from({ length: count }, (_, index): Row => {
    const rank = index + 1;
    if (rank === 7 || rank === 42) {
      return {
        rank,
        artist: "",
        track: "",
        fileName: rank === 7 ? "[Private video]" : "[Deleted video]",
        available: false,
        lowConfidence: false,
        status: { kind: "skipped", reason: rank === 7 ? "비공개 영상" : "삭제된 영상" },
      };
    }
    const [artist, track] = SAMPLE[index % SAMPLE.length];
    const row = { rank, artist, track };
    return {
      ...row,
      fileName: fileNameOf(row, includeRank),
      available: true,
      lowConfidence: rank % 17 === 0,
      status: { kind: "ready" },
    };
  });
}

const LYRICS: LyricsOutcome[] = ["DESCRIPTION", "ONLINE", "NOT_FOUND", "ONLINE"];

/** How a fake download ends: ranks 13 and 58 fail (unless it is a retry), the others get lyrics from varying places. */
export function outcomeFor(rank: number, retry: boolean): ItemStatus {
  if (!retry && rank === 13) return { kind: "failed", message: "m4a 오디오 형식이 없습니다." };
  if (!retry && rank === 58) return { kind: "failed", message: "태그를 쓰지 못했습니다. (지원하지 않는 m4a 구조)" };
  return { kind: "done", lyrics: LYRICS[rank % LYRICS.length] };
}

export type FakeJobEnd = "COMPLETED" | "CANCELLED";

export interface FakeJob {
  cancel(): void;
}

export interface FakeJobOptions {
  /** How many songs run at once. */
  concurrency?: number;
  tickMs?: number;
  /** 1 makes a song take 5 to 25 seconds at the default tick, long enough to look at the screen; the tests use more. */
  speed?: number;
  /** True for "실패 항목 재시도": the songs that failed before succeed now. */
  retry?: boolean;
}

/**
 * Runs the available [rows] like the real job: [concurrency] at a time, each one's progress moving every tick (10 times a
 * second by default), then a short "마무리 중", then its outcome. [onChange] gets the rows that changed in a tick.
 */
export function startFakeJob(
  rows: Row[],
  onChange: (changed: Row[]) => void,
  onEnd: (end: FakeJobEnd) => void,
  { concurrency = 8, tickMs = 100, speed = 1, retry = false }: FakeJobOptions = {},
): FakeJob {
  const queue = rows.filter((row) => row.available);
  for (const row of queue) row.status = { kind: "waiting" };
  const active = new Map<Row, { percent: number; step: number; finishingTicks: number }>();
  let next = 0;
  let ended = false;

  const end = (how: FakeJobEnd) => {
    ended = true;
    clearInterval(timer);
    onEnd(how);
  };

  const timer = setInterval(() => {
    const changed: Row[] = [];
    while (active.size < concurrency && next < queue.length) {
      const row = queue[next++];
      active.set(row, { percent: 0, step: (0.4 + Math.random() * 1.6) * speed, finishingTicks: 0 });
      row.status = { kind: "downloading", percent: 0 };
      changed.push(row);
    }
    for (const [row, work] of active) {
      if (work.percent < 100) {
        work.percent = Math.min(100, work.percent + work.step);
        row.status = work.percent >= 100 ? { kind: "finishing" } : { kind: "downloading", percent: work.percent };
        changed.push(row);
      } else {
        work.finishingTicks += 1;
        if (work.finishingTicks >= 6) {
          row.status = outcomeFor(row.rank, retry);
          active.delete(row);
          changed.push(row);
        }
      }
    }
    if (changed.length > 0) onChange(changed);
    if (active.size === 0 && next >= queue.length) end("COMPLETED");
  }, tickMs);

  return {
    cancel() {
      if (ended) return;
      const changed: Row[] = [];
      for (const row of queue) {
        const kind = row.status.kind;
        if (kind === "waiting" || kind === "downloading" || kind === "finishing") {
          row.status = { kind: "ready" };
          changed.push(row);
        }
      }
      onChange(changed);
      end("CANCELLED");
    },
  };
}
