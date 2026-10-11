// The cases of shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt, one test per Kotlin test and
// under the same name.

import assert from "node:assert/strict";
import test from "node:test";
import { REJECT_MESSAGES, canonicalUrl, classify, type ClassifyResult, type ParsedInput, type RejectReason } from "../src/logic/input.ts";

const playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV";
const videoId = "dQw4w9WgXcQ";

const ok = (input: ParsedInput): ClassifyResult => ({ ok: true, input });
const rejected = (reason: RejectReason): ClassifyResult => ({ ok: false, reason });
const playlist = (id: string, alsoVideoId: string | null = null): ParsedInput => ({ kind: "playlist", id, alsoVideoId });
const video = (id: string): ParsedInput => ({ kind: "video", id });

/** The input of a result that has to be Ok. */
function accepted(result: ClassifyResult): ParsedInput {
  assert.ok(result.ok, `rejected: ${JSON.stringify(result)}`);
  return result.input;
}

test("barePlaylistId", () => {
  assert.deepEqual(classify(playlistId), ok(playlist(playlistId)));
});

test("bareVideoId", () => {
  assert.deepEqual(classify(videoId), ok(video(videoId)));
});

test("elevenCharIdStartingWithPlIsAVideo", () => {
  assert.deepEqual(classify("PLxxxxxxxxx"), ok(video("PLxxxxxxxxx")));
});

test("playlistUrl", () => {
  assert.deepEqual(classify(`https://www.youtube.com/playlist?list=${playlistId}`), ok(playlist(playlistId)));
});

test("watchUrl", () => {
  assert.deepEqual(classify(`https://www.youtube.com/watch?v=${videoId}`), ok(video(videoId)));
});

test("shortUrlsAndShorts", () => {
  assert.deepEqual(classify(`https://youtu.be/${videoId}?si=abc`), ok(video(videoId)));
  assert.deepEqual(classify(`https://www.youtube.com/shorts/${videoId}`), ok(video(videoId)));
});

test("urlWithoutScheme", () => {
  assert.deepEqual(classify(`music.youtube.com/watch?v=${videoId}`), ok(video(videoId)));
});

test("watchUrlWithListIsPlaylistAndRemembersVideo", () => {
  assert.deepEqual(classify(`https://www.youtube.com/watch?v=${videoId}&list=${playlistId}`), ok(playlist(playlistId, videoId)));
});

test("mixListWithVideoFallsBackToVideo", () => {
  assert.deepEqual(classify(`https://www.youtube.com/watch?v=${videoId}&list=RD${videoId}`), ok(video(videoId)));
});

test("mixListAloneIsRejected", () => {
  assert.deepEqual(classify("https://www.youtube.com/playlist?list=RDCLAK5uy_abcdefgh"), rejected("MIX_PLAYLIST"));
});

test("blankIsRejected", () => {
  assert.deepEqual(classify("   "), rejected("EMPTY"));
});

test("foreignHostsAreRejected", () => {
  assert.deepEqual(classify(`https://evil.com/watch?v=${videoId}`), rejected("UNSUPPORTED_HOST"));
  assert.deepEqual(classify(`https://www.youtube.com.evil.com/watch?v=${videoId}`), rejected("UNSUPPORTED_HOST"));
  assert.deepEqual(classify(`https://youtube.com@evil.com/watch?v=${videoId}`), rejected("UNSUPPORTED_HOST"));
});

test("garbageIsRejected", () => {
  assert.deepEqual(classify("PLshort"), rejected("UNRECOGNIZED"));
  assert.deepEqual(classify("hello world"), rejected("UNRECOGNIZED"));
  assert.deepEqual(classify("PLabc; rm -rf /"), rejected("UNSUPPORTED_HOST"));
});

test("canonicalUrlIsRebuiltFromIdOnly", () => {
  const input = accepted(classify(`https://www.youtube.com/watch?v=${videoId}&list=PLx%20--exec&t=10s`));
  assert.equal(canonicalUrl(input), `https://www.youtube.com/watch?v=${videoId}`);
});

test("playlistCanonicalUrl", () => {
  assert.equal(canonicalUrl(accepted(classify(playlistId))), `https://www.youtube.com/playlist?list=${playlistId}`);
});

// Not in the Kotlin file: the messages the screen shows are the ones the server says for the same rejection.
test("theRejectMessagesAreTheOnesTheServerSays", () => {
  assert.deepEqual(REJECT_MESSAGES, {
    EMPTY: "재생목록 ID 또는 영상 주소를 입력하세요.",
    UNSUPPORTED_HOST: "YouTube 주소만 사용할 수 있습니다.",
    MIX_PLAYLIST: "자동 생성 믹스 목록은 지원하지 않습니다.",
    UNRECOGNIZED: "재생목록 ID 또는 영상 주소로 인식할 수 없습니다.",
  });
});
