// Kotlin's white space and trimming, which the file names of the preview depend on. Not in the Kotlin files: this is what the port
// needs to be sure of. JavaScript's own `trim()` differs from Kotlin's at U+001C..U+001F and at U+FEFF.

import assert from "node:assert/strict";
import test from "node:test";
import { isNullOrBlank, isWhitespace, trim, trimEnd, trimEndChars, trimStart, trimStartChars } from "../src/logic/text.ts";

/**
 * Every UTF-16 unit for which Kotlin's `Char.isWhitespace()` is true on the JVM (`Character.isWhitespace(c) ||
 * Character.isSpaceChar(c)`), as listed by a JVM (JBR 21.0.9) on 2026-10-11 for the whole range U+0000..U+FFFF.
 */
const KOTLIN_WHITESPACE = [
  0x9, 0xa, 0xb, 0xc, 0xd, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0xa0, 0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007,
  0x2008, 0x2009, 0x200a, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000,
];

test("exactlyTheUnitsKotlinCallsWhiteSpaceAreWhiteSpace", () => {
  const found: number[] = [];
  for (let code = 0; code <= 0xffff; code += 1) if (isWhitespace(String.fromCharCode(code))) found.push(code);

  assert.deepEqual(found, KOTLIN_WHITESPACE);
});

test("aByteOrderMarkIsNotWhiteSpaceAndSurvivesTrimmingAsInKotlin", () => {
  assert.equal(trim("\uFEFFA\uFEFF"), "\uFEFFA\uFEFF");
  assert.equal("\uFEFFA\uFEFF".trim(), "A", "JavaScript's own trim() would have removed them");
});

test("theSeparatorsOfTheControlRangeAreWhiteSpaceAsInKotlin", () => {
  assert.equal(trim("\u001CA\u001F"), "A");
  assert.equal("\u001CA\u001F".trim(), "\u001CA\u001F", "JavaScript's own trim() keeps them");
});

test("trimmingWorksFromEachEndAndForSpecificCharacters", () => {
  assert.equal(trimStart(" \u00A0\u3000A "), "A ");
  assert.equal(trimEnd(" A\u2003 "), " A");
  assert.equal(trimEndChars("Mix. . ", ". "), "Mix");
  assert.equal(trimStartChars("  Mix", " "), "Mix");
  assert.equal(trimEndChars("C:\\Music\\/", "\\/"), "C:\\Music");
  assert.equal(trim(""), "");
  assert.equal(trim("   "), "");
});

test("blankMeansNothingButWhiteSpace", () => {
  assert.equal(isNullOrBlank(null), true);
  assert.equal(isNullOrBlank(undefined), true);
  assert.equal(isNullOrBlank(""), true);
  assert.equal(isNullOrBlank(" \t\n\u3000"), true);
  assert.equal(isNullOrBlank("\uFEFF"), false);
  assert.equal(isNullOrBlank("a"), false);
});
