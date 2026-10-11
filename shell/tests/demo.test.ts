// The demo server (`index.html?demo`) answers like the real one, so that the screen can be tried and looked at without it.

import assert from "node:assert/strict";
import test from "node:test";
import type { JobEvent } from "../src/bridge/types.ts";
import { createDemoApi } from "../src/demo.ts";

const fast = { resolveDelayMs: 1, tickMs: 1, speed: 400 };
const playlistUrl = "https://www.youtube.com/playlist?list=PLabcdefghijklmnop";

async function watch(api: ReturnType<typeof createDemoApi>, jobId: string): Promise<JobEvent[]> {
  const events: JobEvent[] = [];
  await api.watchJob(jobId, (event) => events.push(event));
  return events;
}

test("a resolved playlist has 100 rows, two of them not available", async () => {
  const resolved = await createDemoApi(fast).resolve(playlistUrl);

  assert.equal(resolved.kind, "PLAYLIST");
  assert.equal(resolved.items.length, 100);
  assert.deepEqual(resolved.items.filter((item) => !item.available).map((item) => item.rank), [7, 42]);
  assert.equal(resolved.items[0].expectedFileName, "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
});

test("a video address gives a single video and a watch address with a list remembers the video", async () => {
  const api = createDemoApi(fast);

  assert.equal((await api.resolve("dQw4w9WgXcQ")).kind, "VIDEO");
  const both = await api.resolve("https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PLabcdefghijklmnop");
  assert.equal(both.kind, "PLAYLIST");
  assert.equal(both.alsoVideoId, "dQw4w9WgXcQ");
});

test("input the server would refuse is refused with the server's message", async () => {
  await assert.rejects(createDemoApi(fast).resolve("https://evil.com/x"), { message: "YouTube 주소만 사용할 수 있습니다." });
});

test("a demo job runs every available song, fails ranks 13 and 58, and ends with job-done", async () => {
  const api = createDemoApi(fast);
  const resolved = await api.resolve(playlistUrl);

  const { jobId } = await api.startJob({ resolveId: resolved.resolveId, options: { includeRank: true } });
  const events = await watch(api, jobId);

  const last = events[events.length - 1];
  assert.equal(last.type, "job-done");
  assert.deepEqual(last.type === "job-done" && last.summary, { succeeded: 96, skipped: 0, failed: 2 });
  assert.deepEqual(events.filter((event) => event.type === "item-failed").map((event) => event.rank).sort((a, b) => a - b), [13, 58]);
  assert.equal(events.filter((event) => event.type === "item-started").length, 98);
});

test("a retry of the failed ranks succeeds", async () => {
  const api = createDemoApi(fast);
  const resolved = await api.resolve(playlistUrl);
  await watch(api, (await api.startJob({ resolveId: resolved.resolveId })).jobId);

  const retry = await watch(api, (await api.startJob({ resolveId: resolved.resolveId, ranks: [13, 58] })).jobId);

  const last = retry[retry.length - 1];
  assert.deepEqual(last.type === "job-done" && last.summary, { succeeded: 2, skipped: 0, failed: 0 });
});

test("a cancelled demo job ends with job-done CANCELLED", async () => {
  const api = createDemoApi({ ...fast, speed: 1 });
  const resolved = await api.resolve(playlistUrl);
  const { jobId } = await api.startJob({ resolveId: resolved.resolveId });

  const watching = watch(api, jobId);
  await api.cancelJob(jobId);
  const events = await watching;

  const last = events[events.length - 1];
  assert.equal(last.type === "job-done" && last.status, "CANCELLED");
});

test("an unknown job and an expired lookup say what the server says", async () => {
  const api = createDemoApi(fast);

  await assert.rejects(api.startJob({ resolveId: "nope" }), { message: "조회 결과가 만료되었습니다. 다시 조회하세요." });
  await assert.rejects(api.watchJob("nope", () => {}), { message: "작업을 찾을 수 없습니다." });
  await assert.rejects(api.cancelJob("nope"), { message: "작업을 찾을 수 없습니다." });
});
