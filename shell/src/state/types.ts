// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/UiState.kt and app/.../settings/*.kt (the Compose app, deleted in
// stage 6 of the migration: `git show f9dbcba:<that path>`).

import type {
  ActionResult,
  JobCreated,
  JobEvent,
  JobRequest,
  JobStatus,
  JobSummary,
  LyricsOutcome,
  ResolveResponse,
  ResolvedItem,
  ToolsStatus,
} from "../bridge/types.ts";

export type ItemStatus =
  /** Shown in the preview before a download starts. */
  | { kind: "ready" }
  /** Part of a running job but not started yet. */
  | { kind: "waiting" }
  | { kind: "downloading"; percent: number | null }
  | { kind: "finishing" }
  /** `lyrics` is what the finished file got as lyrics (or why it got none); null when the server did not say. */
  | { kind: "done"; lyrics: LyricsOutcome | null }
  | { kind: "skipped"; reason: string }
  | { kind: "failed"; message: string };

export interface ItemRow {
  item: ResolvedItem;
  /** The file name shown to the user: the preview's guess, replaced by the real one once known. */
  fileName: string | null;
  status: ItemStatus;
}

export type Phase = "IDLE" | "RESOLVING" | "PREVIEW" | "RUNNING" | "FINISHED";

export interface UiState {
  phase: Phase;
  input: string;
  /** `outputDir`, `overwrite`, `includeRank` and `searchLyricsOnline` are the options the app remembers between runs. */
  outputDir: string;
  overwrite: boolean;
  /** Whether file names start with the rank (`001 `). Sent with the job; the preview names follow it. */
  includeRank: boolean;
  /** Whether a song whose description has no lyrics is looked up on the internet. Sent with the job; the preview does not change. */
  searchLyricsOnline: boolean;
  /** Not remembered: it belongs to the video that is on screen. */
  singleRank: number;
  /**
   * The album name the user typed, as typed; it is also the name of the folder the files are saved in. Null means the user
   * has not touched the field: it shows the default (see `albumNameText`) and sends nothing. Once touched, the text stays as
   * it is, and blank means the default again. Not remembered, like `singleRank`; `reset` clears it.
   */
  albumName: string | null;
  resolved: ResolveResponse | null;
  rows: ItemRow[];
  error: string | null;
  jobStatus: JobStatus | null;
  summary: JobSummary | null;
  /** How long the job that `summary` belongs to took, from the press of the button to the job-done event; null while there is none. */
  elapsedMs: number | null;
  tools: ToolsStatus | null;
  toolBusy: boolean;
  toolMessage: string | null;
}

export function initialUiState(overrides: Partial<UiState> = {}): UiState {
  return {
    phase: "IDLE",
    input: "",
    outputDir: "",
    overwrite: false,
    includeRank: true,
    searchLyricsOnline: true,
    singleRank: 1,
    albumName: null,
    resolved: null,
    rows: [],
    error: null,
    jobStatus: null,
    summary: null,
    elapsedMs: null,
    tools: null,
    toolBusy: false,
    toolMessage: null,
    ...overrides,
  };
}

export function failedRanks(state: UiState): number[] {
  return state.rows.filter((row) => row.status.kind === "failed").map((row) => row.item.rank);
}

export function canResolve(state: UiState): boolean {
  return state.phase !== "RESOLVING" && state.phase !== "RUNNING";
}

/**
 * The options the app restores on the next start. The typed playlist or video address and the rank of a single video are
 * not part of it. `outputDir` is null (or blank) when the user never chose a folder: the app then uses its default.
 * `searchLyricsOnline` is on for a settings file written before the option existed.
 */
export interface UserSettings {
  outputDir: string | null;
  overwrite: boolean;
  includeRank: boolean;
  searchLyricsOnline: boolean;
}

export function userSettings(overrides: Partial<UserSettings> = {}): UserSettings {
  return { outputDir: null, overwrite: false, includeRank: true, searchLyricsOnline: true, ...overrides };
}

/** Where the remembered options live. */
export interface SettingsStore {
  /** The saved options, or the defaults when there are none or they cannot be read. Never throws. */
  load(): UserSettings;
  /** Stores `settings`. A store that cannot write does nothing. Never throws. */
  save(settings: UserSettings): void;
}

/** Remembers nothing: loads the defaults and drops what it is asked to save. */
export const noSettingsStore: SettingsStore = {
  load: () => userSettings(),
  save: () => {},
};

/** Everything the screen logic needs from the server; the `Bridge` of `bridge/bridge.ts` is one. */
export interface XgsApi {
  tools(): Promise<ToolsStatus>;
  installYtDlp(): Promise<ActionResult>;
  updateYtDlp(): Promise<ActionResult>;
  resolve(input: string): Promise<ResolveResponse>;
  startJob(request: JobRequest): Promise<JobCreated>;
  /** Calls `onEvent` for each event and resolves after `job-done`; rejects when the stream fails. */
  watchJob(jobId: string, onEvent: (event: JobEvent) => void): Promise<void>;
  cancelJob(jobId: string): Promise<void>;
}
