import type {
  ActionResult,
  JobCreated,
  JobEvent,
  JobRequest,
  ResolveResponse,
  ToolsStatus,
} from "./types.ts";

/** What a rejected bridge call carries: one message fit for the user, as the Rust side wrote it. */
export interface BridgeError {
  message: string;
}

/** The message of whatever a call rejected with: a `BridgeError`, an `Error`, or a plain string. */
export function errorMessage(reason: unknown): string {
  if (typeof reason === "string") return reason;
  if (typeof reason === "object" && reason !== null && "message" in reason) {
    const { message } = reason as { message: unknown };
    if (typeof message === "string") return message;
  }
  return String(reason);
}

/** `invoke` of `@tauri-apps/api/core`, or a stand-in in a test. */
export type Invoke = <T>(command: string, args?: Record<string, unknown>) => Promise<T>;

/** The part of a Tauri `Channel` the bridge uses. */
export interface ChannelLike<T> {
  onmessage: (message: T) => void;
}

export interface BridgeDeps {
  invoke: Invoke;
  channel: <T>() => ChannelLike<T>;
  /** How long `watchJob` waits for `job-done` to be delivered after the command has ended (default 3000). */
  lastEventWaitMs?: number;
}

const LAST_EVENT_WAIT_MS = 3000;
const MISSING_LAST_EVENT = "이벤트를 모두 받지 못했습니다.";

/**
 * The calls the page can make to the server. Each one is a Tauri command of the Rust bridge, which holds the port and the
 * token: nothing here, and nothing the commands return, tells the page where the server is or how to get in.
 */
export function createBridge(deps: BridgeDeps) {
  const { invoke } = deps;
  return {
    /** Resolves when the sidecar is ready; rejects with the reason when it could not start. */
    ready: () => invoke<void>("sidecar_ready"),
    tools: () => invoke<ToolsStatus>("tools"),
    installYtDlp: () => invoke<ActionResult>("install_yt_dlp"),
    updateYtDlp: () => invoke<ActionResult>("update_yt_dlp"),
    resolve: (input: string) => invoke<ResolveResponse>("resolve", { input }),
    startJob: (request: JobRequest) => invoke<JobCreated>("start_job", { request }),
    cancelJob: (jobId: string) => invoke<void>("cancel_job", { jobId }),

    /**
     * Calls [onEvent] with each event of the job, in order, and resolves after `job-done` has been delivered. Rejects with the
     * server's message when it sends an `error` event, and when the connection is lost before `job-done`.
     */
    watchJob(jobId: string, onEvent: (event: JobEvent) => void): Promise<void> {
      const channel = deps.channel<JobEvent>();
      let delivered: () => void = () => {};
      const jobDone = new Promise<void>((resolve) => {
        delivered = resolve;
      });
      channel.onmessage = (event) => {
        onEvent(event);
        if (event.type === "job-done") delivered();
      };
      // The command ends after the last event was sent, but the page may still be handling it: wait for `job-done`.
      return invoke<void>("watch_job", { jobId, onEvent: channel }).then(() =>
        within(jobDone, deps.lastEventWaitMs ?? LAST_EVENT_WAIT_MS),
      );
    },
  };
}

export type Bridge = ReturnType<typeof createBridge>;

function within(promise: Promise<void>, ms: number): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    const timer = setTimeout(() => reject({ message: MISSING_LAST_EVENT } satisfies BridgeError), ms);
    promise.then(
      () => {
        clearTimeout(timer);
        resolve();
      },
      (reason) => {
        clearTimeout(timer);
        reject(reason);
      },
    );
  });
}
