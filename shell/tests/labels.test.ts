import assert from "node:assert/strict";
import test from "node:test";
import { elapsedLabel, rankLabel, statusLabel, summaryText } from "../src/prototype/labels.ts";

test("each status has its text", () => {
  assert.equal(statusLabel({ kind: "ready" }), "준비됨");
  assert.equal(statusLabel({ kind: "waiting" }), "대기 중");
  assert.equal(statusLabel({ kind: "downloading", percent: null }), "다운로드 중…");
  assert.equal(statusLabel({ kind: "downloading", percent: 42.9 }), "다운로드 42%");
  assert.equal(statusLabel({ kind: "finishing" }), "마무리 중…");
  assert.equal(statusLabel({ kind: "skipped", reason: "비공개 영상" }), "건너뜀: 비공개 영상");
  assert.equal(statusLabel({ kind: "failed", message: "m4a 오디오 형식이 없습니다." }), "실패: m4a 오디오 형식이 없습니다.");
});

test("a finished row says where its lyrics came from or why it has none", () => {
  assert.equal(statusLabel({ kind: "done", lyrics: null }), "완료");
  assert.equal(statusLabel({ kind: "done", lyrics: "DESCRIPTION" }), "완료 · 가사 ✓ 설명란");
  assert.equal(statusLabel({ kind: "done", lyrics: "ONLINE" }), "완료 · 가사 ✓ 인터넷");
  assert.equal(statusLabel({ kind: "done", lyrics: "NOT_FOUND" }), "완료 · 가사 없음");
  assert.equal(statusLabel({ kind: "done", lyrics: "SEARCH_OFF" }), "완료 · 가사 없음 (검색 끔)");
});

test("the elapsed time is in whole seconds and never negative", () => {
  assert.equal(elapsedLabel(42), "42초");
  assert.equal(elapsedLabel(59.9), "59초");
  assert.equal(elapsedLabel(185), "3분 05초");
  assert.equal(elapsedLabel(3725), "1시간 02분 05초");
  assert.equal(elapsedLabel(-5), "0초");
});

test("the summary names the end of the job and the counts, with the time when it is known", () => {
  const summary = { succeeded: 8, skipped: 0, failed: 1 };
  assert.equal(summaryText(summary, "COMPLETED", 185), "완료 — 성공 8 · 건너뜀 0 · 실패 1 · 소요 3분 05초");
  assert.equal(summaryText(summary, "CANCELLED", null), "취소됨 — 성공 8 · 건너뜀 0 · 실패 1");
  assert.equal(summaryText(summary, "FAILED", 7), "중단됨 — 성공 8 · 건너뜀 0 · 실패 1 · 소요 7초");
  assert.equal(summaryText(null, "COMPLETED", 7), null);
  assert.equal(summaryText(summary, null, 7), null);
});

test("the rank has three digits", () => {
  assert.equal(rankLabel(7), "007");
  assert.equal(rankLabel(100), "100");
});
