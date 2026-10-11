// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/AppStateHolder.kt: all the screen logic. The DOM code only
// renders `getState()` and calls these functions. Coroutines became promises; the 400 ms wait before the options are saved and
// the clock behind the elapsed time are given in, so that the tests run in no time at all.

import type { JobEvent, JobOptions, JobRequest, ResolvedItem } from "../bridge/types.ts";
import { MAX_RANK, MIN_RANK, format } from "../logic/filename.ts";
import { canonicalUrl, classify, REJECT_MESSAGES } from "../logic/input.ts";
import { isNullOrBlank } from "../logic/text.ts";
import {
  canResolve,
  failedRanks,
  initialUiState,
  noSettingsStore,
  type ItemRow,
  type SettingsStore,
  type UiState,
  type UserSettings,
  type XgsApi,
} from "./types.ts";

/** What the holder needs of the timers: `window.setTimeout` and `clearTimeout`, or a fake one in the tests. */
export interface Scheduler {
  setTimeout(callback: () => void, ms: number): unknown;
  clearTimeout(handle: unknown): void;
}

const realScheduler: Scheduler = {
  setTimeout: (callback, ms) => globalThis.setTimeout(callback, ms),
  clearTimeout: (handle) => globalThis.clearTimeout(handle as ReturnType<typeof globalThis.setTimeout>),
};

export interface HolderOptions {
  api: XgsApi;
  defaultOutputDir: string;
  settings?: SettingsStore;
  /** Milliseconds from any fixed point; the time a job took is measured with it. */
  now?: () => number;
  scheduler?: Scheduler;
}

/** How long the options must stay unchanged before they are written. */
const SAVE_DELAY_MS = 400;

const LOST_CONNECTION = "서버와의 연결이 끊어졌습니다.";

export class AppStateHolder {
  private readonly api: XgsApi;
  private readonly settings: SettingsStore;
  private readonly now: () => number;
  private readonly scheduler: Scheduler;
  private readonly listeners = new Set<(state: UiState) => void>();

  private state: UiState;
  private jobId: string | null = null;
  /** When the job that runs now was started; the time shown at its end is measured from it. */
  private jobStart: number | null = null;
  /** Bumped by `reset`: whatever a job that ran before it reports afterwards is ignored. */
  private epoch = 0;
  /** Counts the jobs started: what an earlier job's watch reports after a later job has begun is ignored too. */
  private run = 0;
  private saveHandle: unknown = null;

  /**
   * The options as they are known to be stored: what the screen started with (what the settings hold, with a blank folder
   * replaced by the default), then whatever was saved last. Nothing is written while the screen still shows exactly these, so
   * a file that could not be read is not replaced by defaults and the default folder does not end up in the file unless the
   * user changes something.
   */
  private lastKnown: UserSettings;

  // The default clock is monotonic like Kotlin's `TimeSource.Monotonic`: a change of the wall clock must not skew "소요".
  constructor({ api, defaultOutputDir, settings = noSettingsStore, now = () => performance.now(), scheduler = realScheduler }: HolderOptions) {
    this.api = api;
    this.settings = settings;
    this.now = now;
    this.scheduler = scheduler;
    const stored = settings.load();
    this.state = initialUiState({
      outputDir: !isNullOrBlank(stored.outputDir) ? (stored.outputDir as string) : defaultOutputDir,
      overwrite: stored.overwrite,
      includeRank: stored.includeRank,
      searchLyricsOnline: stored.searchLyricsOnline,
    });
    this.lastKnown = optionsOf(this.state);
  }

  getState(): UiState {
    return this.state;
  }

  /** Calls `listener` after every change of the state; returns the function that stops it. */
  subscribe(listener: (state: UiState) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private update(change: (state: UiState) => UiState): void {
    const next = change(this.state);
    if (next === this.state) return;
    this.state = next;
    for (const listener of [...this.listeners]) listener(next);
  }

  // ---- simple setters -----------------------------------------------------------------

  onInput(text: string): void {
    this.update((state) => ({ ...state, input: text }));
  }

  onOutputDir(dir: string): void {
    this.changeOptions((state) => ({ ...state, outputDir: dir }));
  }

  onOverwrite(value: boolean): void {
    this.changeOptions((state) => ({ ...state, overwrite: value }));
  }

  /**
   * Turning the rank in the file name on or off rewrites the names shown in the preview. Rows of a running or finished job
   * are left alone: they show the names the server really used, and the rows of a job that ended before it started them
   * (cancelled or aborted) keep the preview names they had.
   */
  onIncludeRank(value: boolean): void {
    this.changeOptions((state) =>
      state.phase !== "PREVIEW"
        ? { ...state, includeRank: value }
        : { ...state, includeRank: value, rows: state.rows.map((row) => withFileName(row, value)) },
    );
  }

  /** Whether a song whose description has no lyrics is looked up on the internet. It only matters when a job starts. */
  onSearchLyricsOnline(value: boolean): void {
    this.changeOptions((state) => ({ ...state, searchLyricsOnline: value }));
  }

  /** The album name for the tags of the next job, which is also the name of the folder it saves into; blank keeps the default. Not remembered. */
  onAlbumName(text: string): void {
    this.update((state) => ({ ...state, albumName: text }));
  }

  /** Changing the rank of a single video rewrites its file name in the preview. */
  onSingleRank(value: number): void {
    this.update((state) => {
      const rank = Math.min(Math.max(value, MIN_RANK), MAX_RANK);
      if (state.resolved?.kind !== "VIDEO") return { ...state, singleRank: rank };
      return { ...state, singleRank: rank, rows: state.rows.map((row) => withRank(row, rank, state.includeRank)) };
    });
  }

  dismissError(): void {
    this.update((state) => ({ ...state, error: null }));
  }

  // ---- remembered options -----------------------------------------------------------------

  /** Applies `change` and, if it altered one of the remembered options, schedules a save. */
  private changeOptions(change: (state: UiState) => UiState): void {
    const old = this.state;
    const next = change(old);
    const altered = !sameOptions(optionsOf(next), optionsOf(old));
    this.update(() => next);
    if (altered) this.scheduleSave();
  }

  /** Each change restarts the wait, so typing a folder name produces one save, with the finished text. */
  private scheduleSave(): void {
    if (this.saveHandle !== null) this.scheduler.clearTimeout(this.saveHandle);
    this.saveHandle = this.scheduler.setTimeout(() => {
      this.saveHandle = null;
      this.saveIfChanged();
    }, SAVE_DELAY_MS);
  }

  /**
   * Saves the options as they are now, at once, and drops a save that is still waiting. Call it when the window closes,
   * because a change made in the last moments would otherwise be lost. Writes nothing when the options are the ones already
   * stored (see `lastKnown`).
   */
  flushSettings(): void {
    if (this.saveHandle !== null) this.scheduler.clearTimeout(this.saveHandle);
    this.saveHandle = null;
    this.saveIfChanged();
  }

  private saveIfChanged(): void {
    const current = optionsOf(this.state);
    if (sameOptions(current, this.lastKnown)) return;
    this.settings.save(current);
    this.lastKnown = current;
  }

  reset(): void {
    this.epoch += 1;
    this.jobId = null;
    this.update((state) =>
      initialUiState({
        outputDir: state.outputDir,
        overwrite: state.overwrite,
        includeRank: state.includeRank,
        searchLyricsOnline: state.searchLyricsOnline,
        tools: state.tools,
      }),
    );
  }

  // ---- tools ----------------------------------------------------------------------------

  refreshTools(): void {
    void (async () => {
      try {
        const tools = await this.api.tools();
        this.update((state) => ({ ...state, tools }));
      } catch (error) {
        this.update((state) => ({ ...state, error: failureMessage(error) }));
      }
    })();
  }

  installYtDlp(): void {
    this.runToolAction(async () => (await this.api.installYtDlp()).message);
  }

  updateYtDlp(): void {
    this.runToolAction(async () => (await this.api.updateYtDlp()).message);
  }

  private runToolAction(action: () => Promise<string>): void {
    this.update((state) => ({ ...state, toolBusy: true, toolMessage: null }));
    void (async () => {
      try {
        const message = await action();
        const tools = await this.api.tools();
        this.update((state) => ({ ...state, toolBusy: false, toolMessage: message, tools }));
      } catch (error) {
        this.update((state) => ({ ...state, toolBusy: false, toolMessage: failureMessage(error) }));
      }
    })();
  }

  // ---- resolving ------------------------------------------------------------------------

  resolve(): void {
    const current = this.state;
    if (!canResolve(current)) return;
    const result = classify(current.input);
    if (!result.ok) {
      this.update((state) => ({ ...state, error: REJECT_MESSAGES[result.reason] }));
      return;
    }
    this.doResolve(current.input);
  }

  /** For a watch URL that also carried a playlist: look up just that video instead. */
  switchToVideoOnly(): void {
    const videoId = this.state.resolved?.alsoVideoId;
    if (!videoId) return;
    const url = canonicalUrl({ kind: "video", id: videoId });
    this.update((state) => ({ ...state, input: url }));
    this.doResolve(url);
  }

  private doResolve(input: string): void {
    const before = this.state;
    this.update((state) => ({ ...state, phase: "RESOLVING", error: null }));
    void (async () => {
      try {
        const response = await this.api.resolve(input);
        this.update((state) => ({
          ...state,
          phase: "PREVIEW",
          resolved: response,
          rows: response.items.map((item) => previewRow(item, state.singleRank, response.kind, state.includeRank)),
          summary: null,
          jobStatus: null,
        }));
      } catch (error) {
        // A failed lookup must not strip the buttons from a preview that is still on screen. The user may have changed the
        // rank option while the lookup ran (the rows were not rewritten then), so a restored preview shows names that
        // follow the option as it is now.
        const phase = before.resolved !== null ? before.phase : "IDLE";
        this.update((state) => ({
          ...state,
          phase,
          rows: phase === "PREVIEW" ? state.rows.map((row) => withFileName(row, state.includeRank)) : state.rows,
          error: failureMessage(error),
        }));
      }
    })();
  }

  // ---- downloading ----------------------------------------------------------------------

  startDownload(): void {
    const state = this.state;
    const resolved = state.resolved;
    if (resolved === null) return;
    if (state.phase !== "PREVIEW" && state.phase !== "FINISHED") return;
    if (isNullOrBlank(state.outputDir)) {
      this.update((current) => ({ ...current, error: "출력 폴더를 지정하세요." }));
      return;
    }
    this.update((current) => ({
      ...current,
      phase: "RUNNING",
      error: null,
      summary: null,
      jobStatus: null,
      elapsedMs: null,
      rows: current.rows.map((row) => (row.item.available ? { ...row, status: { kind: "waiting" } } : row)),
    }));
    this.runJob({ resolveId: resolved.resolveId, options: jobOptions(state), ranks: null }, state);
  }

  /** Runs a new job for just the items that failed last time. */
  retryFailed(): void {
    const state = this.state;
    const resolved = state.resolved;
    if (resolved === null) return;
    const ranks = failedRanks(state);
    if (ranks.length === 0 || state.phase !== "FINISHED") return;
    this.update((current) => ({
      ...current,
      phase: "RUNNING",
      error: null,
      summary: null,
      jobStatus: null,
      elapsedMs: null,
      rows: current.rows.map((row) => (ranks.includes(row.item.rank) ? { ...row, status: { kind: "waiting" } } : row)),
    }));
    // A single video is always re-run as a whole; its rank is chosen by the user, not by the list.
    const retryRanks = resolved.kind !== "VIDEO" ? ranks : null;
    this.runJob({ resolveId: resolved.resolveId, options: jobOptions(state), ranks: retryRanks }, state);
  }

  cancel(): void {
    const id = this.jobId;
    if (id === null) return;
    void (async () => {
      try {
        await this.api.cancelJob(id);
      } catch (error) {
        this.update((state) => ({ ...state, error: failureMessage(error) }));
      }
    })();
  }

  /**
   * `before` is the screen as it was when the user pressed the button. It is restored when the job cannot even be started, so
   * a failed retry does not wipe the results of the previous run.
   */
  private runJob(request: JobRequest, before: UiState): void {
    this.jobStart = this.now();
    // The id of an earlier job is not this job's: until the server answers there is nothing for `cancel` to cancel.
    this.jobId = null;
    const epoch = this.epoch;
    const run = ++this.run;
    const current = () => epoch === this.epoch && run === this.run;
    void (async () => {
      let started = false;
      try {
        const created = await this.api.startJob(request);
        started = true;
        if (!current()) return;
        this.jobId = created.jobId;
        await this.api.watchJob(created.jobId, (event) => {
          if (current()) this.update((state) => this.apply(state, event));
        });
        if (!current()) return;
        this.update((state) =>
          state.phase === "RUNNING"
            ? { ...state, phase: "FINISHED", error: LOST_CONNECTION, rows: state.rows.map(resetTransient) }
            : state,
        );
      } catch (error) {
        if (!current()) return;
        this.update((state) =>
          started
            ? { ...state, phase: "FINISHED", error: failureMessage(error), rows: state.rows.map(resetTransient) }
            : {
                ...state,
                phase: before.phase,
                rows: before.rows,
                summary: before.summary,
                jobStatus: before.jobStatus,
                error: failureMessage(error),
              },
        );
      }
    })();
  }

  private apply(state: UiState, event: JobEvent): UiState {
    switch (event.type) {
      case "item-started":
        return updateRow(state, event.rank, (row) => ({ ...row, fileName: event.fileName, status: { kind: "downloading", percent: null } }));
      case "progress":
        return updateRow(state, event.rank, (row) => ({
          ...row,
          status: event.stage === "DOWNLOADING" ? { kind: "downloading", percent: event.percent ?? null } : { kind: "finishing" },
        }));
      case "item-done":
        return updateRow(state, event.rank, (row) => ({ ...row, fileName: event.fileName, status: { kind: "done", lyrics: event.lyrics ?? null } }));
      case "item-skipped":
        return updateRow(state, event.rank, (row) => ({ ...row, status: { kind: "skipped", reason: event.reason } }));
      case "item-failed":
        return updateRow(state, event.rank, (row) => ({ ...row, status: { kind: "failed", message: event.message } }));
      case "job-done":
        return {
          ...state,
          phase: "FINISHED",
          jobStatus: event.status,
          summary: event.summary,
          elapsedMs: this.jobStart === null ? null : this.now() - this.jobStart,
          rows: state.rows.map(resetTransient),
        };
      default:
        return state; // an event this version of the screen does not know
    }
  }
}

// ---- helpers --------------------------------------------------------------------------

/** The four options that are remembered; nothing else on the screen is. */
function optionsOf(state: UiState): UserSettings {
  return { outputDir: state.outputDir, overwrite: state.overwrite, includeRank: state.includeRank, searchLyricsOnline: state.searchLyricsOnline };
}

function sameOptions(a: UserSettings, b: UserSettings): boolean {
  return a.outputDir === b.outputDir && a.overwrite === b.overwrite && a.includeRank === b.includeRank && a.searchLyricsOnline === b.searchLyricsOnline;
}

function jobOptions(state: UiState): JobOptions {
  return {
    outputDir: state.outputDir,
    overwrite: state.overwrite,
    singleRank: state.singleRank,
    includeRank: state.includeRank,
    searchLyricsOnline: state.searchLyricsOnline,
    albumName: state.albumName !== null && !isNullOrBlank(state.albumName) ? state.albumName : null,
  };
}

/** The server's `expectedFileName` always carries the rank, so the shown name is built here to honour `includeRank`. */
function previewRow(item: ResolvedItem, singleRank: number, kind: "PLAYLIST" | "VIDEO", includeRank: boolean): ItemRow {
  const row: ItemRow = {
    item,
    fileName: item.expectedFileName,
    status: item.available ? { kind: "ready" } : { kind: "skipped", reason: item.unavailableReason ?? "" },
  };
  return kind === "VIDEO" ? withRank(row, singleRank, includeRank) : withFileName(row, includeRank);
}

function withRank(row: ItemRow, rank: number, includeRank: boolean): ItemRow {
  if (!row.item.available) return row;
  return withFileName({ ...row, item: { ...row.item, rank } }, includeRank);
}

function withFileName(row: ItemRow, includeRank: boolean): ItemRow {
  if (!row.item.available) return row;
  return { ...row, fileName: format(row.item.rank, row.item.artist, row.item.track, includeRank) };
}

/**
 * A cancelled or aborted job sends no final event for the items that were still waiting or in flight, so those rows go back
 * to ready. Finished, skipped and failed rows keep their result.
 */
function resetTransient(row: ItemRow): ItemRow {
  switch (row.status.kind) {
    case "waiting":
    case "downloading":
    case "finishing":
      return { ...row, status: { kind: "ready" } };
    default:
      return row;
  }
}

function updateRow(state: UiState, rank: number, change: (row: ItemRow) => ItemRow): UiState {
  return { ...state, rows: state.rows.map((row) => (row.item.rank === rank && row.item.available ? change(row) : row)) };
}

/**
 * What the user sees for a failure: the server's own message (a `BridgeError` from the Rust side is a plain object with a
 * `message`), or a generic one for anything else (an exception of the screen's own, a rejection that is just text).
 */
export function failureMessage(error: unknown): string {
  if (typeof error === "object" && error !== null && !(error instanceof Error) && typeof (error as { message?: unknown }).message === "string") {
    return (error as { message: string }).message;
  }
  const detail = error instanceof Error ? error.message : String(error);
  return `서버와 통신 중 오류가 발생했습니다: ${detail}`;
}
