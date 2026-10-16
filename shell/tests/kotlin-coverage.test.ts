// Guards the port: every test of the Kotlin suites of `shared/` that `src/logic` was ported from has a test of the same name here. The
// Kotlin files are the specification; when one of them gains a test, this fails until the port gains it too.
//
// The suites of the Compose app (`app/`), which `holder.test.ts` and `labels.test.ts` and the Rust ports in `sidecar-host` were ported
// from, are gone with the app (stage 6 of the migration deleted it): they can be read in the git history (`git show f9dbcba:app/src/...`),
// and what was ported from them is now tested only by the ports themselves.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const repository = new URL("../../", import.meta.url);

/** The names of the tests of a Kotlin file: `@Test fun name(`. */
function kotlinTests(path: URL): string[] {
  return [...readFileSync(path, "utf8").matchAll(/@Test\s+fun\s+(\w+)\(/g)].map((match) => match[1]);
}

const PORTS: ReadonlyArray<readonly [kotlin: string, port: string]> = [
  ["shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt", "filename.test.ts"],
  ["shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt", "input.test.ts"],
];

for (const [kotlin, port] of PORTS) {
  test(`every test of ${kotlin.split("/").pop()} is ported to ${port}`, () => {
    // A missing file means it was moved or renamed (readFileSync throws): fail.
    const names = kotlinTests(new URL(kotlin, repository));
    const ported = new Set([...readFileSync(new URL(port, import.meta.url), "utf8").matchAll(/^test\("(\w+)"/gm)].map((match) => match[1]));

    assert.ok(names.length > 0, "the Kotlin file has tests");
    assert.deepEqual(
      names.filter((name) => !ported.has(name)),
      [],
      `ported ${names.filter((name) => ported.has(name)).length} of ${names.length}`,
    );
  });
}
