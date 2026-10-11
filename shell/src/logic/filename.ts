// Ported from shared/src/commonMain/kotlin/com/xgetsongs/shared/filename/FilenameFormatter.kt. The preview shows the names
// this builds, and the server builds the real ones with the Kotlin original: tests/filename.test.ts holds the cases of
// FilenameFormatterTest.kt so that the two cannot drift apart unnoticed.

import { isHighSurrogate, isNullOrBlank, trim, trimEnd, trimEndChars, trimStartChars } from "./text.ts";

export const MIN_RANK = 1;
export const MAX_RANK = 999;

/** Limit for the file name without extension. */
export const MAX_BASE_LENGTH = 180;
export const MAX_ARTIST_LENGTH = 80;

/** Limit for a playlist folder name, before a reserved-name `_` is added. */
export const MAX_FOLDER_LENGTH = 80;

/** Folder name used when a playlist has no usable title. */
export const UNTITLED_PLAYLIST = "재생목록";

const ELLIPSIS = "…";
const EXTENSION = ".m4a";
const EMPTY_TITLE_PLACEHOLDER = "untitled";

const FULLWIDTH: Readonly<Record<string, string>> = {
  "\\": "＼",
  "/": "／",
  ":": "：",
  "*": "＊",
  "?": "？",
  '"': "＂",
  "<": "＜",
  ">": "＞",
  "|": "｜",
};

/** Windows device names that cannot be used as a file or folder name, with or without an extension. */
const RESERVED_DEVICE_NAMES: ReadonlySet<string> = new Set([
  "CON",
  "PRN",
  "AUX",
  "NUL",
  ...Array.from({ length: 9 }, (_, index) => [`COM${index + 1}`, `LPT${index + 1}`]).flat(),
]);

/**
 * Builds `{rank 3 digits} {artist} - {title}.m4a`, or `{artist} - {title}.m4a` when [includeRank] is false. The length
 * limits are the same either way (MAX_BASE_LENGTH for the whole name, so the title gets what the prefix leaves). [rank]
 * must be in MIN_RANK..MAX_RANK even when it is not printed. Without the rank the artist starts the name, so an artist
 * whose text before the first `.` is a reserved Windows device name gets a `_` after it (`NUL.x` becomes `NUL_.x`).
 */
export function format(rank: number, artist: string, title: string, includeRank = true): string {
  if (!Number.isInteger(rank) || rank < MIN_RANK || rank > MAX_RANK) {
    throw new RangeError(`rank must be in ${MIN_RANK}..${MAX_RANK} but was ${rank}`);
  }
  const cutArtist = truncate(sanitize(artist), MAX_ARTIST_LENGTH);
  const cleanArtist = includeRank || !cutArtist.includes(".") ? cutArtist : escapeDeviceName(cutArtist);
  const rankPrefix = includeRank ? `${String(rank).padStart(3, "0")} ` : "";
  const prefix = `${rankPrefix}${cleanArtist} - `;
  const sanitizedTitle = sanitize(title);
  const cleanTitle = sanitizedTitle === "" ? EMPTY_TITLE_PLACEHOLDER : sanitizedTitle;
  const titleBudget = MAX_BASE_LENGTH - prefix.length;
  const base = trimEndChars(prefix + truncate(cleanTitle, titleBudget), ". ");
  return base + EXTENSION;
}

/**
 * The folder name for a playlist titled [title]: sanitized, cut to MAX_FOLDER_LENGTH, without trailing dots or spaces, and
 * with a `_` after a reserved Windows device name (`CON` becomes `CON_`, `con.txt` becomes `con_.txt`). Falls back to
 * UNTITLED_PLAYLIST.
 */
export function folderName(title: string | null | undefined): string {
  if (isNullOrBlank(title)) return UNTITLED_PLAYLIST;
  const name = trimStartChars(trimEndChars(truncate(sanitize(title as string), MAX_FOLDER_LENGTH), ". "), " ");
  if (name === "") return UNTITLED_PLAYLIST;
  return escapeDeviceName(name);
}

/**
 * The folder inside the output folder that a job saves into, or null for the output folder itself. A [typed] name (the
 * album name the user typed) that is not blank wins, for a playlist and for a single video alike; otherwise a playlist
 * gets a folder named after [playlistTitle] and a single video gets none. The name is made safe by [folderName] either way.
 */
export function destinationFolder(
  isPlaylist: boolean,
  playlistTitle: string | null | undefined,
  typed: string | null | undefined,
): string | null {
  if (!isNullOrBlank(typed)) return folderName(typed);
  if (isPlaylist) return folderName(playlistTitle);
  return null;
}

/**
 * Windows treats the text before the first `.` of a name (trailing spaces ignored) as a device when it is one of the
 * reserved device names, whatever the case. Puts a `_` right after that stem then; other names are returned as they are.
 */
function escapeDeviceName(name: string): string {
  const dot = name.indexOf(".");
  const stem = trimEndChars(dot < 0 ? name : name.slice(0, dot), " ");
  return RESERVED_DEVICE_NAMES.has(stem.toUpperCase()) ? stem + "_" + name.slice(stem.length) : name;
}

/** Replaces forbidden characters with full-width look-alikes and drops control characters. */
export function sanitize(text: string): string {
  let result = "";
  for (let index = 0; index < text.length; index += 1) {
    const character = text[index];
    const code = character.charCodeAt(0);
    if (code < 0x20 || code === 0x7f) continue;
    result += FULLWIDTH[character] ?? character;
  }
  return trim(result);
}

/** Cuts [text] to at most [max] UTF-16 units, ending with an ellipsis, never splitting a surrogate pair. */
function truncate(text: string, max: number): string {
  if (text.length <= max) return text;
  let end = max - ELLIPSIS.length;
  if (end > 0 && isHighSurrogate(text[end - 1])) end -= 1;
  return trimEnd(text.slice(0, Math.max(end, 0))) + ELLIPSIS;
}
