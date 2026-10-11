// Guards the port: every test of the Kotlin suites that these files were ported from has a test of the same name here (the Rust
// ports under the same name in snake case). The Kotlin files are the specification; when one of them gains a test, this fails until
// the port gains it too.
//
// It looks the Kotlin files up in the repository. When `app/` is gone (stage 6 of the migration deletes it) the app's suites are
// skipped; `shared/` stays, so the two suites of `shared` keep being compared.

import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import test from "node:test";

const repository = new URL("../../", import.meta.url);

/** The names of the tests of a Kotlin file: `@Test fun name(`. */
function kotlinTests(path: URL): string[] {
  return [...readFileSync(path, "utf8").matchAll(/@Test\s+fun\s+(\w+)\(/g)].map((match) => match[1]);
}

const PORTS: ReadonlyArray<readonly [kotlin: string, port: string]> = [
  ["shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt", "filename.test.ts"],
  ["shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt", "input.test.ts"],
  ["app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt", "labels.test.ts"],
  ["app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt", "holder.test.ts"],
];

for (const [kotlin, port] of PORTS) {
  const kotlinPath = new URL(kotlin, repository);
  // Only the app's suites may be absent (stage 6 deletes `app/`). A missing `shared` file means it was moved or renamed: fail.
  const skip = kotlin.startsWith("app/") && !existsSync(kotlinPath) && "the Kotlin file is not in this checkout";
  test(`every test of ${kotlin.split("/").pop()} is ported to ${port}`, { skip }, () => {
    const names = kotlinTests(kotlinPath);
    const ported = new Set([...readFileSync(new URL(port, import.meta.url), "utf8").matchAll(/^test\("(\w+)"/gm)].map((match) => match[1]));

    assert.ok(names.length > 0, "the Kotlin file has tests");
    assert.deepEqual(
      names.filter((name) => !ported.has(name)),
      [],
      `ported ${names.filter((name) => ported.has(name)).length} of ${names.length}`,
    );
  });
}

// The desktop code that moved to Rust (stage 4): the tests of the Kotlin files are ported to `#[test] fn` of the same name in snake
// case (`aMissingFileGivesTheDefaults` is `a_missing_file_gives_the_defaults`), where Kotlin's `null` is Rust's `None` and what
// Kotlin does not `throw` Rust does not `panic` at.
const RUST_PORTS: ReadonlyArray<readonly [kotlin: string, rust: string]> = [
  ["app/src/desktopTest/kotlin/com/xgetsongs/app/ExplorerOpenerTest.kt", "sidecar-host/src/folder.rs"],
  ["app/src/desktopTest/kotlin/com/xgetsongs/app/JsonSettingsStoreTest.kt", "sidecar-host/src/settings.rs"],
];

/** `aPathBelowARegularFile` is `a_path_below_a_regular_file`; a word of one letter (`A`) is a word. */
const snakeCase = (name: string) =>
  name
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .replace(/([A-Z])([A-Z][a-z])/g, "$1_$2")
    .toLowerCase();
const rustName = (name: string) =>
  snakeCase(name)
    .split("_")
    .map((word) => (word === "null" ? "none" : word === "throw" ? "panic" : word))
    .join("_");

for (const [kotlin, rust] of RUST_PORTS) {
  const kotlinPath = new URL(kotlin, repository);
  const skip = !existsSync(kotlinPath) && "the Kotlin file is not in this checkout";
  test(`every test of ${kotlin.split("/").pop()} is ported to ${rust}`, { skip }, () => {
    const names = kotlinTests(kotlinPath);
    const ported = new Set([...readFileSync(new URL(rust, repository), "utf8").matchAll(/^\s*fn\s+(\w+)\s*\(/gm)].map((match) => match[1]));

    assert.ok(names.length > 0, "the Kotlin file has tests");
    assert.deepEqual(
      names.filter((name) => !ported.has(rustName(name))),
      [],
      `ported ${names.filter((name) => ported.has(rustName(name))).length} of ${names.length}`,
    );
  });
}

test("theNamesOfTheKotlinTestsBecomeTheNamesOfTheRustOnes", () => {
  assert.equal(rustName("aMissingFileGivesTheDefaults"), "a_missing_file_gives_the_defaults");
  assert.equal(rustName("aUtf16FileGivesTheDefaults"), "a_utf16_file_gives_the_defaults");
  assert.equal(rustName("aPathBelowARegularFileReturnsTheDirectoryAroundTheFile"), "a_path_below_a_regular_file_returns_the_directory_around_the_file");
  assert.equal(rustName("aBlankPathReturnsNull"), "a_blank_path_returns_none");
  assert.equal(rustName("saveOverADirectoryThatIsNotEmptyDoesNotThrow"), "save_over_a_directory_that_is_not_empty_does_not_panic");
});
