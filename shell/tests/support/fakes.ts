// The stand-ins the holder tests need: FakeApi.kt, FakeSettingsStore.kt, and a timer that only moves when the test says so.

import type {
  ActionResult,
  JobCreated,
  JobEvent,
  JobRequest,
  ResolveResponse,
  ToolInfo,
  ToolsStatus,
} from "../../src/bridge/types.ts";
import type { Scheduler } from "../../src/state/holder.ts";
import { userSettings, type SettingsStore, type UserSettings, type XgsApi } from "../../src/state/types.ts";
import { playlist } from "./fixtures.ts";

/** A failure the way the Rust bridge reports one: a plain object with a message fit for the user (Kotlin's `ApiError`). */
export function apiError(message: string): { message: string } {
  return { message };
}

export class Deferred<T = void> {
  readonly promise: Promise<T>;
  resolve!: (value: T) => void;
  reject!: (reason: unknown) => void;

  constructor() {
    this.promise = new Promise<T>((resolve, reject) => {
      this.resolve = resolve;
      this.reject = reject;
    });
  }
}

/** Lets every promise that can settle settle: the equivalent of the coroutine tests' `runCurrent()`. */
export function flush(): Promise<void> {
  return new Promise((resolve) => setImmediate(resolve));
}

const found = (version: string): ToolInfo => ({ found: true, version, path: null });

/** What the "server" sends for the running job: buffered until the screen listens, ended by `closeEvents`. */
class EventQueue {
  private readonly items: JobEvent[] = [];
  private closed = false;
  private wake: (() => void) | null = null;

  push(event: JobEvent): void {
    this.items.push(event);
    this.wake?.();
  }

  close(): void {
    this.closed = true;
    this.wake?.();
  }

  /** Delivers the events in order, and returns when the queue is closed and empty. */
  async drain(onEvent: (event: JobEvent) => void): Promise<void> {
    for (;;) {
      const next = this.items.shift();
      if (next !== undefined) {
        onEvent(next);
        continue;
      }
      if (this.closed) return;
      await new Promise<void>((resolve) => {
        this.wake = () => {
          this.wake = null;
          resolve();
        };
      });
    }
  }
}

export class FakeApi implements XgsApi {
  toolsStatus: ToolsStatus = { ytDlp: found("1"), ffmpeg: found("1"), jsRuntime: found("24") };
  resolveResponse: ResolveResponse = playlist();
  resolveError: unknown = null;
  /** When set, `resolve` waits for it before it answers (or fails), so a test can change things while a lookup is held. */
  resolveGate: Deferred | null = null;
  /** When set, `startJob` waits for it before it answers, so a test can act while the new job has no id yet. */
  startGate: Deferred | null = null;
  startError: unknown = null;
  toolsError: unknown = null;
  installMessage = "installed";

  readonly resolveInputs: string[] = [];
  readonly jobRequests: JobRequest[] = [];
  readonly cancelled: string[] = [];
  installCalls = 0;

  private events = new EventQueue();
  /** The event stream of every job so far, the first job's first. */
  private readonly streams: EventQueue[] = [];

  /** Sends `event` to the screen as the running job's next event. */
  send(...events: JobEvent[]): void {
    for (const event of events) this.events.push(event);
  }

  /** Ends the running job's event stream. */
  closeEvents(): void {
    this.events.close();
  }

  /** Ends the event stream of the [jobNumber]th job (1 is the first), which may be one that is no longer the running job. */
  closeEventsOf(jobNumber: number): void {
    this.streams[jobNumber - 1].close();
  }

  async tools(): Promise<ToolsStatus> {
    if (this.toolsError !== null) throw this.toolsError;
    return this.toolsStatus;
  }

  async installYtDlp(): Promise<ActionResult> {
    if (this.toolsError !== null) throw this.toolsError;
    this.installCalls += 1;
    return { message: this.installMessage };
  }

  async updateYtDlp(): Promise<ActionResult> {
    return { message: "updated" };
  }

  async resolve(input: string): Promise<ResolveResponse> {
    this.resolveInputs.push(input);
    if (this.resolveGate !== null) await this.resolveGate.promise;
    if (this.resolveError !== null) throw this.resolveError;
    return this.resolveResponse;
  }

  async startJob(request: JobRequest): Promise<JobCreated> {
    if (this.startError !== null) throw this.startError;
    this.jobRequests.push(request);
    this.events = new EventQueue();
    this.streams.push(this.events);
    const jobId = `job-${this.jobRequests.length}`;
    if (this.startGate !== null) await this.startGate.promise;
    return { jobId };
  }

  watchJob(_jobId: string, onEvent: (event: JobEvent) => void): Promise<void> {
    return this.events.drain(onEvent);
  }

  async cancelJob(jobId: string): Promise<void> {
    this.cancelled.push(jobId);
  }
}

/** Hands out `stored` on `load` and records every `save`, in order. */
export class FakeSettingsStore implements SettingsStore {
  stored: UserSettings;
  readonly saved: UserSettings[] = [];
  loadCalls = 0;

  constructor(stored: UserSettings = userSettings()) {
    this.stored = stored;
  }

  load(): UserSettings {
    this.loadCalls += 1;
    return this.stored;
  }

  save(settings: UserSettings): void {
    this.saved.push(settings);
  }
}

/** Timers on a clock that moves only by `advance`: `setTimeout` callbacks run when the time they wait for has passed. */
export class FakeTimers implements Scheduler {
  private time = 0;
  private nextId = 1;
  private readonly tasks = new Map<number, { at: number; callback: () => void }>();

  setTimeout(callback: () => void, ms: number): number {
    const id = this.nextId++;
    this.tasks.set(id, { at: this.time + ms, callback });
    return id;
  }

  clearTimeout(handle: unknown): void {
    this.tasks.delete(handle as number);
  }

  /** Moves the clock forward by `ms`, running (in order) every callback that falls due on the way, the one at the end included. */
  advance(ms: number): void {
    const target = this.time + ms;
    for (;;) {
      let nextId: number | null = null;
      for (const [id, task] of this.tasks) {
        if (task.at <= target && (nextId === null || task.at < (this.tasks.get(nextId) as { at: number }).at)) nextId = id;
      }
      if (nextId === null) break;
      const task = this.tasks.get(nextId) as { at: number; callback: () => void };
      this.tasks.delete(nextId);
      this.time = task.at;
      task.callback();
    }
    this.time = target;
  }

  /** How many callbacks are waiting. */
  get pending(): number {
    return this.tasks.size;
  }
}
