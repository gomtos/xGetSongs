// Ported from shared/src/commonMain/kotlin/com/xgetsongs/shared/input/InputClassifier.kt. The screen checks what the user typed
// before it asks the server anything, and the server runs the Kotlin original on what it receives: tests/input.test.ts holds
// the cases of InputClassifierTest.kt.

import { trim } from "./text.ts";

/** A user input that has been validated and reduced to YouTube IDs. */
export type ParsedInput =
  | { kind: "playlist"; id: string; alsoVideoId: string | null }
  | { kind: "video"; id: string };

export const REJECT_MESSAGES = {
  EMPTY: "재생목록 ID 또는 영상 주소를 입력하세요.",
  UNSUPPORTED_HOST: "YouTube 주소만 사용할 수 있습니다.",
  MIX_PLAYLIST: "자동 생성 믹스 목록은 지원하지 않습니다.",
  UNRECOGNIZED: "재생목록 ID 또는 영상 주소로 인식할 수 없습니다.",
} as const;

export type RejectReason = keyof typeof REJECT_MESSAGES;

export type ClassifyResult = { ok: true; input: ParsedInput } | { ok: false; reason: RejectReason };

/** Canonical URL rebuilt from the ID only. The raw user text is never passed on. */
export function canonicalUrl(input: ParsedInput): string {
  return input.kind === "playlist"
    ? `https://www.youtube.com/playlist?list=${input.id}`
    : `https://www.youtube.com/watch?v=${input.id}`;
}

const PLAYLIST_PREFIXES = ["PL", "UU", "LL", "FL", "OL"];
const MIX_PREFIX = "RD";
const MIN_PLAYLIST_ID_LENGTH = 12;
const VIDEO_ID_LENGTH = 11;
const ALLOWED_HOSTS: ReadonlySet<string> = new Set([
  "youtube.com",
  "www.youtube.com",
  "m.youtube.com",
  "music.youtube.com",
  "youtu.be",
]);
const VIDEO_PATH_PREFIXES = ["shorts", "embed", "live"];

export function classify(raw: string): ClassifyResult {
  const text = trim(raw);
  if (text === "") return rejected("EMPTY");
  if (isIdChars(text)) return classifyBareId(text);
  if (!text.includes(".") && !text.includes("/")) return rejected("UNRECOGNIZED");
  return classifyUrl(text);
}

// An ID is made of ASCII letters and digits, `_` and `-` only.
function isIdChars(text: string): boolean {
  return /^[A-Za-z0-9_-]+$/.test(text);
}

function isPlaylistId(text: string): boolean {
  return text.length >= MIN_PLAYLIST_ID_LENGTH && isIdChars(text) && PLAYLIST_PREFIXES.some((prefix) => text.startsWith(prefix));
}

function isMixId(text: string): boolean {
  return text.length >= MIN_PLAYLIST_ID_LENGTH && isIdChars(text) && text.startsWith(MIX_PREFIX);
}

function isVideoId(text: string): boolean {
  return text.length === VIDEO_ID_LENGTH && isIdChars(text);
}

function classifyBareId(text: string): ClassifyResult {
  if (isPlaylistId(text)) return ok({ kind: "playlist", id: text, alsoVideoId: null });
  if (isMixId(text)) return rejected("MIX_PLAYLIST");
  if (isVideoId(text)) return ok({ kind: "video", id: text });
  return rejected("UNRECOGNIZED");
}

function classifyUrl(text: string): ClassifyResult {
  const scheme = text.indexOf("://");
  const afterScheme = scheme < 0 ? text : text.slice(scheme + 3);
  const hostEnd = [...afterScheme].findIndex((character) => character === "/" || character === "?" || character === "#");
  const host = (hostEnd < 0 ? afterScheme : afterScheme.slice(0, hostEnd)).toLowerCase();
  if (!ALLOWED_HOSTS.has(host)) return rejected("UNSUPPORTED_HOST");

  const rest = hostEnd < 0 ? "" : afterScheme.slice(hostEnd);
  const beforeFragment = rest.split("#")[0];
  const questionMark = beforeFragment.indexOf("?");
  const path = questionMark < 0 ? beforeFragment : beforeFragment.slice(0, questionMark);
  const query = questionMark < 0 ? "" : beforeFragment.slice(questionMark + 1);
  const params = new Map<string, string>();
  for (const pair of query.split("&")) {
    const equals = pair.indexOf("=");
    if (equals >= 0) params.set(pair.slice(0, equals), pair.slice(equals + 1));
  }

  const segments = path.split("/").filter((segment) => segment !== "");
  let videoCandidate: string | undefined;
  if (host === "youtu.be") videoCandidate = segments[0];
  else if (segments.length > 0 && VIDEO_PATH_PREFIXES.includes(segments[0])) videoCandidate = segments[1];
  else videoCandidate = params.get("v");
  const videoId = videoCandidate !== undefined && isVideoId(videoCandidate) ? videoCandidate : null;
  const listId = params.get("list");

  if (listId !== undefined && isPlaylistId(listId)) return ok({ kind: "playlist", id: listId, alsoVideoId: videoId });
  if (listId !== undefined && isMixId(listId)) {
    return videoId !== null ? ok({ kind: "video", id: videoId }) : rejected("MIX_PLAYLIST");
  }
  if (videoId !== null) return ok({ kind: "video", id: videoId });
  return rejected("UNRECOGNIZED");
}

function ok(input: ParsedInput): ClassifyResult {
  return { ok: true, input };
}

function rejected(reason: RejectReason): ClassifyResult {
  return { ok: false, reason };
}
