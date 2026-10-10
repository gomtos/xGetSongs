import assert from "node:assert/strict";
import test from "node:test";
import { buildRows, fileNameOf, outcomeFor, startFakeJob, type FakeJobEnd, type Row } from "../src/prototype/fake.ts";

test("a resolved playlist has 100 rows, two of them not available", () => {
  const rows = buildRows();
  assert.equal(rows.length, 100);
  assert.deepEqual(
    rows.filter((row) => !row.available).map((row) => row.rank),
    [7, 42],
  );
  assert.deepEqual(rows[6].status, { kind: "skipped", reason: "비공개 영상" });
  assert.deepEqual(rows[41].status, { kind: "skipped", reason: "삭제된 영상" });
  assert.ok(rows.some((row) => row.lowConfidence), "some artists are guessed");
  assert.ok(rows.every((row) => row.status.kind === "ready" || row.status.kind === "skipped"));
});

test("the file name follows the rank option", () => {
  const row = { rank: 1, artist: "소연 (SOYEON)", track: "퇴사할게여 (Narr. 기안84)" };
  assert.equal(fileNameOf(row, true), "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
  assert.equal(fileNameOf(row, false), "소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
  assert.equal(buildRows(3, false)[0].fileName, "소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
});

test("ranks 13 and 58 fail, unless it is a retry", () => {
  assert.deepEqual(outcomeFor(13, false), { kind: "failed", message: "m4a 오디오 형식이 없습니다." });
  assert.equal(outcomeFor(58, false).kind, "failed");
  assert.equal(outcomeFor(13, true).kind, "done");
  assert.equal(outcomeFor(2, false).kind, "done");
});

/** Runs a fake job to its end with a fast clock. */
function run(rows: Row[], options: { retry?: boolean } = {}) {
  return new Promise<{ end: FakeJobEnd; changes: number }>((resolve) => {
    let changes = 0;
    startFakeJob(
      rows,
      (changed) => {
        changes += changed.length;
      },
      (end) => resolve({ end, changes }),
      { tickMs: 1, speed: 10, ...options },
    );
  });
}

test("a fake job ends every available row as done or failed and skips the others", async () => {
  const rows = buildRows(60);
  const { end, changes } = await run(rows);
  assert.equal(end, "COMPLETED");
  assert.ok(changes > 0);
  for (const row of rows) {
    if (!row.available) assert.equal(row.status.kind, "skipped");
    else assert.ok(row.status.kind === "done" || row.status.kind === "failed", `${row.rank}: ${row.status.kind}`);
  }
  assert.deepEqual(
    rows.filter((row) => row.status.kind === "failed").map((row) => row.rank),
    [13, 58],
  );
});

test("a retry runs only the rows it is given and they succeed", async () => {
  const rows = buildRows(60);
  await run(rows);
  const failed = rows.filter((row) => row.status.kind === "failed");
  const others = rows.filter((row) => row.status.kind !== "failed").map((row) => row.status);

  const { end } = await run(failed, { retry: true });

  assert.equal(end, "COMPLETED");
  assert.ok(failed.every((row) => row.status.kind === "done"));
  assert.deepEqual(
    rows.filter((row) => !failed.includes(row)).map((row) => row.status),
    others,
    "the other rows are untouched",
  );
});

test("cancelling puts unfinished rows back to ready and keeps the finished ones", async () => {
  const rows = buildRows(60);
  const result = await new Promise<FakeJobEnd>((resolve) => {
    const job = startFakeJob(rows, () => {}, resolve, { tickMs: 1, speed: 10 });
    setTimeout(() => job.cancel(), 30);
  });
  assert.equal(result, "CANCELLED");
  for (const row of rows) {
    assert.ok(["ready", "skipped", "done", "failed"].includes(row.status.kind), `${row.rank}: ${row.status.kind}`);
  }
  assert.ok(rows.some((row) => row.status.kind === "ready" && row.available), "something was still unfinished");
});
