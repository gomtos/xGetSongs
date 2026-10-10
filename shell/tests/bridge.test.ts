import assert from "node:assert/strict";
import { test } from "node:test";
import { createBridge, errorMessage, type ChannelLike } from "../src/bridge/bridge.ts";
import type { JobEvent } from "../src/bridge/types.ts";

interface Call {
  command: string;
  args?: Record<string, unknown>;
}

/** A bridge over a fake `invoke`: every call is recorded, and [answer] says how it ends. */
function fakeBridge(answer: (call: Call) => Promise<unknown>, lastEventWaitMs = 200) {
  const calls: Call[] = [];
  const channels: ChannelLike<JobEvent>[] = [];
  const bridge = createBridge({
    invoke: ((command: string, args?: Record<string, unknown>) => {
      const call = { command, args };
      calls.push(call);
      return answer(call);
    }) as never,
    channel: (<T>() => {
      const channel: ChannelLike<T> = { onmessage: () => {} };
      channels.push(channel as unknown as ChannelLike<JobEvent>);
      return channel;
    }) as never,
    lastEventWaitMs,
  });
  return { bridge, calls, channels };
}

const progress = (percent: number): JobEvent => ({ type: "progress", rank: 1, stage: "DOWNLOADING", percent });
const jobDone: JobEvent = { type: "job-done", status: "COMPLETED", summary: { succeeded: 1, skipped: 0, failed: 0 } };
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

test("each call is the command of the Rust bridge with its arguments", async () => {
  const { bridge, calls } = fakeBridge(async () => ({}));

  await bridge.ready();
  await bridge.tools();
  await bridge.installYtDlp();
  await bridge.updateYtDlp();
  await bridge.resolve("PLabc 한글");
  await bridge.startJob({ resolveId: "r1", options: { albumName: "앨범" }, ranks: [1, 3] });
  await bridge.cancelJob("j-1");

  assert.deepEqual(calls, [
    { command: "sidecar_ready", args: undefined },
    { command: "tools", args: undefined },
    { command: "install_yt_dlp", args: undefined },
    { command: "update_yt_dlp", args: undefined },
    { command: "resolve", args: { input: "PLabc 한글" } },
    { command: "start_job", args: { request: { resolveId: "r1", options: { albumName: "앨범" }, ranks: [1, 3] } } },
    { command: "cancel_job", args: { jobId: "j-1" } },
  ]);
});

test("watchJob gives the events to the listener in order and resolves after job-done", async () => {
  const { bridge, calls, channels } = fakeBridge(async () => {
    channels[0].onmessage(progress(10));
    channels[0].onmessage(progress(20));
    channels[0].onmessage(jobDone);
  });
  const seen: JobEvent[] = [];

  await bridge.watchJob("j-1", (event) => seen.push(event));

  assert.deepEqual(seen, [progress(10), progress(20), jobDone]);
  assert.equal(calls[0].command, "watch_job");
  assert.equal(calls[0].args?.jobId, "j-1");
  assert.equal(calls[0].args?.onEvent, channels[0], "the channel is what the Rust side sends to");
});

test("watchJob waits for job-done when the command ended before the page handled it", async () => {
  const { bridge, channels } = fakeBridge(async () => {
    setTimeout(() => channels[0].onmessage(jobDone), 50);
  });
  const seen: JobEvent[] = [];

  await bridge.watchJob("j-1", (event) => seen.push(event));

  assert.deepEqual(seen, [jobDone]);
});

test("watchJob rejects when the command ended and job-done never reached the page", async () => {
  const { bridge } = fakeBridge(async () => {}, 100);

  await assert.rejects(bridge.watchJob("j-1", () => {}), { message: "이벤트를 모두 받지 못했습니다." });
});

test("watchJob rejects with the message of the failure, without waiting for the last event", async () => {
  const { bridge } = fakeBridge(async () => {
    throw { message: "작업을 찾을 수 없습니다." };
  }, 5000);
  const started = Date.now();

  await assert.rejects(bridge.watchJob("nope", () => {}), { message: "작업을 찾을 수 없습니다." });

  assert.ok(Date.now() - started < 1000);
});

test("a late job-done after the wait was given up does no harm", async () => {
  const { bridge, channels } = fakeBridge(async () => {}, 50);
  await assert.rejects(bridge.watchJob("j-1", () => {}));

  channels[0].onmessage(jobDone);
  await sleep(20);
});

test("a listener that throws does not stop the events that come after it", async () => {
  // Tauri's Channel stops delivering for good when a handler throws, so the handler it is given must never throw.
  const { bridge, channels } = fakeBridge(async () => {});
  const seen: JobEvent[] = [];
  const logged: unknown[][] = [];
  const consoleError = console.error;
  console.error = (...args: unknown[]) => logged.push(args);
  try {
    const watching = bridge.watchJob("j-1", (event) => {
      seen.push(event);
      if (seen.length === 1) throw new TypeError("the screen has a bug");
    });

    assert.doesNotThrow(() => channels[0].onmessage(progress(10)));
    channels[0].onmessage(progress(20));
    channels[0].onmessage(jobDone);
    await watching;
  } finally {
    console.error = consoleError;
  }

  assert.deepEqual(seen, [progress(10), progress(20), jobDone], "every event reached the listener");
  assert.equal(logged.length, 1, "the listener's failure was reported once");
});

test("errorMessage reads a BridgeError, an Error and a string", () => {
  assert.equal(errorMessage({ message: "서버와의 연결이 끊어졌습니다." }), "서버와의 연결이 끊어졌습니다.");
  assert.equal(errorMessage(new Error("boom")), "boom");
  assert.equal(errorMessage("plain"), "plain");
  assert.equal(errorMessage(42), "42");
  assert.equal(errorMessage(null), "null");
});
