// Guards the port: every test of the four Kotlin suites that these files were ported from has a test of the same name here.
// The Kotlin files are the specification; when one of them gains a test, this fails until the port gains it too.
//
// It looks the Kotlin files up in the repository. When `app/` is gone (stage 6 of the migration deletes it) the app's suites are
// skipped; `shared/` stays, so the two suites of `shared` keep being compared.

import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import test from "node:test";

const repository = new URL("../../", import.meta.url);

const PORTS: ReadonlyArray<readonly [kotlin: string, port: string]> = [
  ["shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt", "filename.test.ts"],
  ["shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt", "input.test.ts"],
  ["app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt", "labels.test.ts"],
  ["app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt", "holder.test.ts"],
];

for (const [kotlin, port] of PORTS) {
  const kotlinPath = new URL(kotlin, repository);
  test(`every test of ${kotlin.split("/").pop()} is ported to ${port}`, { skip: !existsSync(kotlinPath) && "the Kotlin file is not in this checkout" }, () => {
    const names = [...readFileSync(kotlinPath, "utf8").matchAll(/@Test\s+fun\s+(\w+)\(/g)].map((match) => match[1]);
    const ported = new Set([...readFileSync(new URL(port, import.meta.url), "utf8").matchAll(/^test\("(\w+)"/gm)].map((match) => match[1]));

    assert.ok(names.length > 0, "the Kotlin file has tests");
    assert.deepEqual(
      names.filter((name) => !ported.has(name)),
      [],
      `ported ${names.filter((name) => ported.has(name)).length} of ${names.length}`,
    );
  });
}
