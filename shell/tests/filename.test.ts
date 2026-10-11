// The cases of shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt, one test per Kotlin test
// and under the same name, so that the port and the original are held to the same table.

import assert from "node:assert/strict";
import test from "node:test";
import {
  MAX_ARTIST_LENGTH,
  MAX_BASE_LENGTH,
  MAX_FOLDER_LENGTH,
  UNTITLED_PLAYLIST,
  destinationFolder,
  folderName,
  format,
} from "../src/logic/filename.ts";

const DEVICES = ["CON", "PRN", "AUX", "NUL", ...Array.from({ length: 9 }, (_, index) => [`COM${index + 1}`, `LPT${index + 1}`]).flat()];

/** No lone surrogate in [text]: every high one is followed by a low one and every low one follows a high one. */
function assertNoLoneSurrogate(text: string) {
  for (let index = 0; index < text.length; index += 1) {
    const code = text.charCodeAt(index);
    const next = text.charCodeAt(index + 1);
    const previous = text.charCodeAt(index - 1);
    if (code >= 0xd800 && code <= 0xdbff) assert.ok(next >= 0xdc00 && next <= 0xdfff, `lone high surrogate at ${index}`);
    if (code >= 0xdc00 && code <= 0xdfff) assert.ok(previous >= 0xd800 && previous <= 0xdbff, `lone low surrogate at ${index}`);
  }
}

const withoutExtension = (name: string) => name.replace(/\.m4a$/, "");

test("formatsTheDocumentedExample", () => {
  assert.equal(format(1, "소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)"), "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
});

test("padsRankToThreeDigits", () => {
  assert.ok(format(7, "A", "B").startsWith("007 "));
  assert.ok(format(42, "A", "B").startsWith("042 "));
  assert.ok(format(999, "A", "B").startsWith("999 "));
});

test("rejectsRankOutsideOneTo999", () => {
  assert.throws(() => format(0, "A", "B"), RangeError);
  assert.throws(() => format(1000, "A", "B"), RangeError);
});

test("replacesForbiddenCharactersWithFullWidthOnes", () => {
  assert.equal(
    format(5, "태연 (TAEYEON)", "만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"),
    "005 태연 (TAEYEON) - 만찬가 (晩餐歌 ／ BANSANKA) ： J-POP REMAKE Vol.1.m4a",
  );
  assert.equal(format(1, "A", '\\/:*?"<>|'), "001 A - ＼／：＊？＂＜＞｜.m4a");
});

test("dropsControlCharactersAndTrailingDotsAndSpaces", () => {
  assert.equal(format(1, "A", "Mr.\t\n. "), "001 A - Mr.m4a");
  assert.equal(format(1, "A", "B.C"), "001 A - B.C.m4a");
});

test("emptyTitleGetsAPlaceholder", () => {
  assert.equal(format(1, "A", "  "), "001 A - untitled.m4a");
});

test("longTitleIsTruncatedWithEllipsisKeepingRankAndArtist", () => {
  const base = withoutExtension(format(12, "ARTIST", "가".repeat(500)));
  assert.equal(base.length, MAX_BASE_LENGTH);
  assert.ok(base.startsWith("012 ARTIST - 가"));
  assert.ok(base.endsWith("…"));
});

test("veryLongArtistIsTruncatedToo", () => {
  const base = withoutExtension(format(1, "A".repeat(300), "Song"));
  assert.ok(base.length <= MAX_BASE_LENGTH);
  assert.ok(base.startsWith(`001 ${"A".repeat(10)}`));
  assert.ok(base.endsWith(" - Song"));
  assert.ok(base.includes("…"));
});

test("truncationNeverSplitsASurrogatePair", () => {
  const base = withoutExtension(format(1, "A", "😀".repeat(200)));
  assert.ok(base.length <= MAX_BASE_LENGTH);
  assertNoLoneSurrogate(base);
});

// ---- without the rank ----

test("theRankIsIncludedUnlessTheCallerLeavesItOut", () => {
  assert.equal(format(1, "A", "B"), "001 A - B.m4a");
  assert.equal(format(1, "A", "B", true), "001 A - B.m4a");
  assert.equal(format(42, "소연", "노래"), format(42, "소연", "노래", true));
});

test("withoutTheRankTheNameIsArtistDashTitle", () => {
  const name = format(1, "A", "B", false);

  assert.equal(name, "A - B.m4a");
  assert.ok(!name.startsWith(" "), "no leading space where the rank would have been");
});

test("withoutTheRankTheDocumentedExampleLosesOnlyThePrefix", () => {
  assert.equal(format(1, "소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)", false), "소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
});

test("withoutTheRankTheNameDoesNotDependOnTheRank", () => {
  const names = [1, 7, 999].map((rank) => format(rank, "A", "B", false));

  assert.deepEqual([...new Set(names)], ["A - B.m4a"]);
});

test("withoutTheRankTheRankIsStillValidated", () => {
  assert.throws(() => format(0, "A", "B", false), RangeError);
  assert.throws(() => format(1000, "A", "B", false), RangeError);
});

test("withoutTheRankForbiddenCharactersAreStillReplaced", () => {
  assert.equal(format(1, "A", '\\/:*?"<>|', false), "A - ＼／：＊？＂＜＞｜.m4a");
  assert.equal(format(1, "AC/DC", "Who Made Who?", false), "AC／DC - Who Made Who？.m4a");
});

test("withoutTheRankControlCharactersAndTrailingDotsAndSpacesAreStillDropped", () => {
  assert.equal(format(1, "A", "Mr.\t\n. ", false), "A - Mr.m4a");
  assert.equal(format(1, "A", "B.C", false), "A - B.C.m4a");
});

test("withoutTheRankAnEmptyTitleStillGetsAPlaceholder", () => {
  assert.equal(format(1, "A", "  ", false), "A - untitled.m4a");
  assert.equal(format(1, "A", "", false), "A - untitled.m4a");
});

test("withoutTheRankALongTitleFillsTheWholeBaseBudget", () => {
  const base = withoutExtension(format(12, "ARTIST", "가".repeat(500), false));

  assert.equal(base.length, MAX_BASE_LENGTH);
  assert.ok(base.startsWith("ARTIST - 가"), base);
  assert.ok(base.endsWith("…"));
  // 180 - "ARTIST - ".length = 171 title units: 170 characters and the ellipsis.
  assert.equal(base, "ARTIST - " + "가".repeat(170) + "…");
});

test("withoutTheRankAnArtistOfEightyCharactersIsKeptAndTheTitleGetsTheRest", () => {
  const artist = "A".repeat(MAX_ARTIST_LENGTH);

  const base = withoutExtension(format(1, artist, "T".repeat(500), false));

  assert.equal(base.length, MAX_BASE_LENGTH);
  assert.ok(base.startsWith(`${artist} - T`), base);
  assert.equal(base, `${artist} - ` + "T".repeat(96) + "…");
});

test("withoutTheRankAnArtistOverEightyCharactersIsCutWithAnEllipsis", () => {
  const expected = "A".repeat(MAX_ARTIST_LENGTH - 1) + "… - Song.m4a";

  assert.equal(format(1, "A".repeat(300), "Song", false), expected);
  assert.equal(format(1, "A".repeat(81), "Song", false), expected);
});

test("withoutTheRankTruncationNeverSplitsASurrogatePair", () => {
  const base = withoutExtension(format(1, "A", "😀".repeat(200), false));

  assert.ok(base.length <= MAX_BASE_LENGTH);
  assert.ok(base.startsWith("A - 😀"), base);
  assertNoLoneSurrogate(base);
});

test("withoutTheRankADeviceNameAsTheArtistGetsAnUnderscoreAfterTheStem", () => {
  assert.equal(format(1, "NUL.x", "Song", false), "NUL_.x - Song.m4a");
  assert.equal(format(1, "Con.", "Song", false), "Con_. - Song.m4a");
  assert.equal(format(1, "com1.x", "Song", false), "com1_.x - Song.m4a");
  assert.equal(format(1, "LPT9.x", "Song", false), "LPT9_.x - Song.m4a");
  assert.equal(format(1, "aux.tar.gz", "Song", false), "aux_.tar.gz - Song.m4a");
  assert.equal(format(1, "CON .x", "Song", false), "CON_ .x - Song.m4a");
});

test("withoutTheRankEveryDeviceNameAsTheArtistIsCaught", () => {
  for (const device of DEVICES) {
    assert.equal(format(1, `${device}.x`, "B", false), `${device}_.x - B.m4a`, device);
  }
});

test("withoutTheRankNamesThatMerelyResembleDeviceNamesStayUnchanged", () => {
  assert.equal(format(1, "Console", "Song", false), "Console - Song.m4a");
  assert.equal(format(1, "Con", "Song", false), "Con - Song.m4a");
  assert.equal(format(1, "LPT9", "Song", false), "LPT9 - Song.m4a");
  assert.equal(format(1, "NULL", "Song", false), "NULL - Song.m4a");
  assert.equal(format(1, "CON TEST", "Song", false), "CON TEST - Song.m4a");
  assert.equal(format(1, "COM0", "Song", false), "COM0 - Song.m4a");
  assert.equal(format(1, "my.con", "Song", false), "my.con - Song.m4a");
});

test("withTheRankADeviceNameAsTheArtistIsLeftAlone", () => {
  assert.equal(format(1, "NUL.x", "Song"), "001 NUL.x - Song.m4a");
  assert.equal(format(1, "Con", "Song", true), "001 Con - Song.m4a");
  assert.equal(format(42, "LPT9", "Song", true), "042 LPT9 - Song.m4a");
});

test("theUnderscoreForADeviceNameArtistStillKeepsTheNameWithinTheBudget", () => {
  const artist = "NUL." + "A".repeat(300);

  const base = withoutExtension(format(1, artist, "T".repeat(500), false));

  assert.equal(base.length, MAX_BASE_LENGTH);
  assert.ok(base.startsWith("NUL_.AAA"), base);
  assert.ok(base.endsWith("…"));
  // The artist is cut to 80 units first, the `_` makes it 81, and the title gets 180 - 81 - " - ".length = 96.
  assert.equal(base, "NUL_." + "A".repeat(75) + "… - " + "T".repeat(95) + "…");
});

test("missingOrBlankPlaylistTitleGetsTheUntitledFolderName", () => {
  assert.equal(folderName(null), "재생목록");
  assert.equal(folderName(""), "재생목록");
  assert.equal(folderName("   "), "재생목록");
  assert.equal(folderName("\t\n"), "재생목록");
  assert.equal(folderName(null), UNTITLED_PLAYLIST);
});

test("ordinaryAndKoreanPlaylistTitlesStayUnchanged", () => {
  assert.equal(folderName("Melon Daily Top 100"), "Melon Daily Top 100");
  assert.equal(folderName("멜론 일간 차트 TOP 100"), "멜론 일간 차트 TOP 100");
});

test("folderNameReplacesForbiddenCharactersWithFullWidthOnes", () => {
  assert.equal(folderName("Best: Hits? <2024>"), "Best： Hits？ ＜2024＞");
  assert.equal(folderName('\\/:*?"<>|'), "＼／：＊？＂＜＞｜");
});

test("folderNameDropsTrailingDotsAndSpaces", () => {
  assert.equal(folderName("Mix... "), "Mix");
  assert.equal(folderName("Mix. . "), "Mix");
  assert.equal(folderName("Mix.Vol.1."), "Mix.Vol.1");
});

test("folderNameThatIsOnlyDotsOrSpacesGetsTheUntitledFolderName", () => {
  assert.equal(folderName("..."), "재생목록");
  assert.equal(folderName(" . . "), "재생목록");
});

test("folderNameDropsControlCharacters", () => {
  assert.equal(folderName("My\u0000Mix\u001F"), "MyMix");
  assert.equal(folderName("\tMy \u007FMix\n"), "My Mix");
  assert.equal(folderName("\u0001\u0002"), "재생목록");
});

test("folderNameIsCutToEightyUnitsWithAnEllipsis", () => {
  const name = folderName("a".repeat(81));
  assert.equal(name.length, MAX_FOLDER_LENGTH);
  assert.equal(name, "a".repeat(79) + "…");
  assert.equal(folderName("a".repeat(80)), "a".repeat(80));
});

test("folderNameTruncationNeverEndsInALoneHighSurrogate", () => {
  const name = folderName("🎵".repeat(100));
  assert.ok(name.length <= MAX_FOLDER_LENGTH);
  assert.ok(name.endsWith("…"));
  assertNoLoneSurrogate(name);
});

test("reservedWindowsDeviceNamesGetAnUnderscoreAfterTheStem", () => {
  assert.equal(folderName("CON"), "CON_");
  assert.equal(folderName("nul"), "nul_");
  assert.equal(folderName("Com1"), "Com1_");
  assert.equal(folderName("LPT9"), "LPT9_");
  assert.equal(folderName("con.txt"), "con_.txt");
  assert.equal(folderName("aux.tar.gz"), "aux_.tar.gz");
  assert.equal(folderName("CON .txt"), "CON_ .txt");
  for (const device of DEVICES) {
    assert.equal(folderName(device), `${device}_`, device);
  }
});

test("namesThatMerelyResembleReservedOnesStayUnchanged", () => {
  assert.equal(folderName("Console"), "Console");
  assert.equal(folderName("CON TEST"), "CON TEST");
  assert.equal(folderName("NULL"), "NULL");
  assert.equal(folderName("COM0"), "COM0");
  assert.equal(folderName("COM10"), "COM10");
  assert.equal(folderName("my.con"), "my.con");
});

test("aPlaylistWithoutATypedFolderNameIsSavedInAFolderNamedAfterIt", () => {
  assert.equal(destinationFolder(true, "Best: Of?", null), "Best： Of？");
  assert.equal(destinationFolder(true, "Mix", ""), "Mix");
  assert.equal(destinationFolder(true, "Mix", "  \t"), "Mix");
  assert.equal(destinationFolder(true, null, null), "재생목록");
});

test("aSingleVideoWithoutATypedFolderNameHasNoFolder", () => {
  assert.equal(destinationFolder(false, "Mix", null), null);
  assert.equal(destinationFolder(false, null, "   "), null);
});

test("aTypedFolderNameWinsForAPlaylistAndAVideoAndIsMadeSafeLikeAnyFolderName", () => {
  assert.equal(destinationFolder(true, "Mix", "My Songs"), "My Songs");
  assert.equal(destinationFolder(false, "Mix", "  My Songs "), "My Songs");
  assert.equal(destinationFolder(false, null, "a\\b/c: d"), "a＼b／c： d");
  assert.equal(destinationFolder(false, null, ".."), "재생목록");
  assert.equal(destinationFolder(true, "Mix", "CON"), "CON_");
});
