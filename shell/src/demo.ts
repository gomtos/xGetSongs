// A server that is not there: the same calls as the real one (`XgsApi`), answered with a made-up playlist of 100 songs and a
// download that moves like the real one. `index.html?demo` runs the screen on it, so the whole screen can be tried (and
// looked at in a browser) without Tauri, the sidecar or the network.

import type { JobEvent, JobRequest, ResolveResponse, ResolvedItem, ToolsStatus } from "./bridge/types.ts";
import { format } from "./logic/filename.ts";
import { REJECT_MESSAGES, classify } from "./logic/input.ts";
import type { XgsApi } from "./state/types.ts";

export const DEMO_PLAYLIST_TITLE = "Melon Daily Top 100";

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

const LYRICS = ["DESCRIPTION", "ONLINE", "NOT_FOUND", "ONLINE"] as const;

/** Two unavailable videos (ranks 7 and 42) and every 17th song with an artist guessed from the channel name. */
function demoItem(rank: number): ResolvedItem {
  const pad = (value: number, length: number) => String(value).padStart(length, "0");
  if (rank === 7 || rank === 42) {
    return {
      rank,
      videoId: `demo${pad(rank, 7)}`,
      title: rank === 7 ? "[Private video]" : "[Deleted video]",
      channel: null,
      artist: "",
      track: "",
      lowConfidence: false,
      available: false,
      unavailableReason: rank === 7 ? "비공개 영상" : "삭제된 영상",
      expectedFileName: null,
    };
  }
  const [artist, track] = SAMPLE[(rank - 1) % SAMPLE.length];
  return {
    rank,
    videoId: `demo${pad(rank, 7)}`,
    title: `${artist} - ${track}`,
    channel: null,
    artist,
    track,
    lowConfidence: rank % 17 === 0,
    available: true,
    unavailableReason: null,
    expectedFileName: format(rank, artist, track, true),
  };
}

export interface DemoOptions {
  /** `"broken"` has no yt-dlp, ffmpeg or JavaScript runtime. */
  tools?: "ok" | "broken";
  /** How long a lookup takes. */
  resolveDelayMs?: number;
  /** How often a running job moves (default 100: ten times a second). */
  tickMs?: number;
  /** 1 makes a song take 5 to 25 seconds at the default tick, long enough to look at the screen; the tests use more. */
  speed?: number;
  /** How many songs run at once. */
  concurrency?: number;
}

interface DemoJob {
  items: ResolvedItem[];
  includeRank: boolean;
  cancelled: boolean;
}

const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

export function createDemoApi({ tools = "ok", resolveDelayMs = 600, tickMs = 100, speed = 1, concurrency = 8 }: DemoOptions = {}): XgsApi {
  let toolsStatus: ToolsStatus =
    tools === "ok"
      ? {
          ytDlp: { found: true, version: "2026.10.01", path: null },
          ffmpeg: { found: true, version: "8.1", path: null },
          jsRuntime: { found: true, version: "24.11.1", path: null },
        }
      : {
          ytDlp: { found: false, version: null, path: null },
          ffmpeg: { found: false, version: null, path: null },
          jsRuntime: { found: false, version: null, path: null },
        };
  const resolutions = new Map<string, ResolveResponse>();
  const jobs = new Map<string, DemoJob>();
  /** The songs that already failed once: a retry succeeds. */
  const failedBefore = new Set<number>();
  let resolveCount = 0;
  let jobCount = 0;

  return {
    async tools() {
      await sleep(200);
      return toolsStatus;
    },

    async installYtDlp() {
      await sleep(1500);
      toolsStatus = { ...toolsStatus, ytDlp: { found: true, version: "2026.10.01", path: null } };
      return { message: "yt-dlp 2026.10.01 을(를) 설치했습니다." };
    },

    async updateYtDlp() {
      await sleep(1500);
      return { message: "yt-dlp는 이미 최신 버전입니다." };
    },

    async resolve(input) {
      await sleep(resolveDelayMs);
      const result = classify(input);
      if (!result.ok) throw { message: REJECT_MESSAGES[result.reason] };
      resolveCount += 1;
      const resolveId = `demo-resolve-${resolveCount}`;
      const response: ResolveResponse =
        result.input.kind === "video"
          ? {
              resolveId,
              kind: "VIDEO",
              playlistTitle: null,
              items: [{ ...demoItem(3), videoId: result.input.id }],
              truncated: false,
              alsoVideoId: null,
            }
          : {
              resolveId,
              kind: "PLAYLIST",
              playlistTitle: DEMO_PLAYLIST_TITLE,
              items: Array.from({ length: 100 }, (_, index) => demoItem(index + 1)),
              truncated: false,
              alsoVideoId: result.input.alsoVideoId,
            };
      resolutions.set(resolveId, response);
      return response;
    },

    async startJob(request: JobRequest) {
      await sleep(50);
      const resolved = resolutions.get(request.resolveId);
      if (!resolved) throw { message: "조회 결과가 만료되었습니다. 다시 조회하세요." };
      const options = request.options ?? {};
      const isVideo = resolved.kind === "VIDEO";
      const items = resolved.items
        .filter((item) => item.available && (request.ranks == null || request.ranks.includes(item.rank)))
        .map((item) => (isVideo ? { ...item, rank: options.singleRank ?? 1 } : item));
      if (items.length === 0) throw { message: "다운로드할 항목이 없습니다." };
      jobCount += 1;
      const jobId = `demo-job-${jobCount}`;
      jobs.set(jobId, { items, includeRank: options.includeRank ?? true, cancelled: false });
      return { jobId };
    },

    watchJob(jobId, onEvent) {
      const job = jobs.get(jobId);
      if (!job) return Promise.reject({ message: "작업을 찾을 수 없습니다." });
      return runJob(job, onEvent);
    },

    async cancelJob(jobId) {
      const job = jobs.get(jobId);
      if (!job) throw { message: "작업을 찾을 수 없습니다." };
      job.cancelled = true;
    },
  };

  /** Runs the items `concurrency` at a time: each one's progress moves every tick, then a short "마무리 중", then its outcome. */
  function runJob(job: DemoJob, emit: (event: JobEvent) => void): Promise<void> {
    return new Promise<void>((resolve) => {
      const queue = [...job.items];
      const active = new Map<number, { item: ResolvedItem; fileName: string; percent: number; step: number; finishingTicks: number }>();
      const summary = { succeeded: 0, skipped: 0, failed: 0 };

      const end = (status: "COMPLETED" | "CANCELLED") => {
        clearInterval(timer);
        emit({ type: "job-done", status, summary });
        resolve();
      };

      const timer = setInterval(() => {
        if (job.cancelled) return end("CANCELLED");
        while (active.size < concurrency && queue.length > 0) {
          const item = queue.shift() as ResolvedItem;
          const fileName = format(item.rank, item.artist, item.track, job.includeRank);
          active.set(item.rank, { item, fileName, percent: 0, step: (0.4 + Math.random() * 1.6) * speed, finishingTicks: 0 });
          emit({ type: "item-started", rank: item.rank, videoId: item.videoId, fileName });
        }
        for (const [rank, work] of [...active]) {
          if (work.percent < 100) {
            work.percent = Math.min(100, work.percent + work.step);
            emit({ type: "progress", rank, stage: work.percent >= 100 ? "FINISHING" : "DOWNLOADING", percent: work.percent >= 100 ? null : work.percent });
            continue;
          }
          work.finishingTicks += 1;
          if (work.finishingTicks < 6) continue;
          active.delete(rank);
          const shouldFail = (rank === 13 || rank === 58) && !failedBefore.has(rank);
          if (shouldFail) {
            failedBefore.add(rank);
            summary.failed += 1;
            emit({ type: "item-failed", rank, message: rank === 13 ? "m4a 오디오 형식이 없습니다." : "태그를 쓰지 못했습니다. (지원하지 않는 m4a 구조)" });
          } else {
            summary.succeeded += 1;
            emit({ type: "item-done", rank, fileName: work.fileName, lyrics: LYRICS[rank % LYRICS.length] });
          }
        }
        if (active.size === 0 && queue.length === 0) end("COMPLETED");
      }, tickMs);
    });
  }
}
