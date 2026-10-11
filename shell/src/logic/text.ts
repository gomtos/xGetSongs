// Kotlin's idea of white space and of trimming, so that the ports of `shared` give exactly the answers the server gives.
// JavaScript's `trim()` differs at the edges (it also trims U+FEFF, and not U+001C..U+001F), and a file name shown in the
// preview must be the file name the server writes.

/** Kotlin `Char.isWhitespace()` on the JVM: Java's white space plus every Unicode space, line and paragraph separator. */
export function isWhitespace(character: string): boolean {
  const code = character.charCodeAt(0);
  if ((code >= 0x09 && code <= 0x0d) || (code >= 0x1c && code <= 0x20)) return true;
  return /^[\p{Zs}\p{Zl}\p{Zp}]$/u.test(character);
}

/** Kotlin `String.trim()`. */
export function trim(text: string): string {
  return trimEnd(trimStart(text));
}

/** Kotlin `String.trimStart()` (white space). */
export function trimStart(text: string): string {
  let start = 0;
  while (start < text.length && isWhitespace(text[start])) start += 1;
  return text.slice(start);
}

/** Kotlin `String.trimEnd()` (white space). */
export function trimEnd(text: string): string {
  let end = text.length;
  while (end > 0 && isWhitespace(text[end - 1])) end -= 1;
  return text.slice(0, end);
}

/** Kotlin `String.trimEnd(vararg chars)`. */
export function trimEndChars(text: string, characters: string): string {
  let end = text.length;
  while (end > 0 && characters.includes(text[end - 1])) end -= 1;
  return text.slice(0, end);
}

/** Kotlin `String.trimStart(vararg chars)`. */
export function trimStartChars(text: string, characters: string): string {
  let start = 0;
  while (start < text.length && characters.includes(text[start])) start += 1;
  return text.slice(start);
}

/** Kotlin `CharSequence.isNullOrBlank()`. */
export function isNullOrBlank(text: string | null | undefined): boolean {
  return text === null || text === undefined || trim(text) === "";
}

export function isHighSurrogate(character: string): boolean {
  const code = character.charCodeAt(0);
  return code >= 0xd800 && code <= 0xdbff;
}

export function isLowSurrogate(character: string): boolean {
  const code = character.charCodeAt(0);
  return code >= 0xdc00 && code <= 0xdfff;
}
