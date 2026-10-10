// What the sidecar's server says, as the bridge passes it on. It mirrors
// shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt: the server writes every field, null included
// (`encodeDefaults = true`), and a request may leave out the fields that have a default.

export type InputKind = "PLAYLIST" | "VIDEO";
export type Stage = "DOWNLOADING" | "FINISHING";
export type JobStatus = "COMPLETED" | "CANCELLED" | "FAILED";
export type LyricsOutcome = "DESCRIPTION" | "ONLINE" | "NOT_FOUND" | "SEARCH_OFF";

export interface ToolInfo {
  found: boolean;
  version: string | null;
  path: string | null;
}

export interface ToolsStatus {
  ytDlp: ToolInfo;
  ffmpeg: ToolInfo;
  jsRuntime: ToolInfo;
}

export interface ActionResult {
  message: string;
}

export interface ResolvedItem {
  rank: number;
  videoId: string;
  title: string;
  channel: string | null;
  artist: string;
  track: string;
  lowConfidence: boolean;
  available: boolean;
  unavailableReason: string | null;
  expectedFileName: string | null;
}

export interface ResolveResponse {
  resolveId: string;
  kind: InputKind;
  playlistTitle: string | null;
  items: ResolvedItem[];
  truncated: boolean;
  alsoVideoId: string | null;
}

export interface JobOptions {
  outputDir: string | null;
  overwrite: boolean;
  singleRank: number;
  includeRank: boolean;
  searchLyricsOnline: boolean;
  albumName: string | null;
}

export interface JobRequest {
  resolveId: string;
  options?: Partial<JobOptions>;
  /** Only these ranks (to retry the failed ones); absent or null means every available item. */
  ranks?: number[] | null;
}

export interface JobCreated {
  jobId: string;
}

export interface JobSummary {
  succeeded: number;
  skipped: number;
  failed: number;
}

/** An event of a job. `type` is the SSE event name and the serial name of the Kotlin class. */
export type JobEvent =
  | { type: "item-started"; rank: number; videoId: string; fileName: string }
  | { type: "progress"; rank: number; stage: Stage; percent: number | null }
  | { type: "item-done"; rank: number; fileName: string; lyrics: LyricsOutcome | null }
  | { type: "item-skipped"; rank: number; reason: string }
  | { type: "item-failed"; rank: number; message: string }
  | { type: "job-done"; status: JobStatus; summary: JobSummary };
