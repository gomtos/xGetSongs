// The names the page and the shell must agree on are strings on two sides: the commands the page invokes and the event the shell sends.
// Nothing else ties them together (a typo is a command that does not exist at run time, and a close that waits for the fallback).

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const read = (path: string) => readFileSync(new URL(path, import.meta.url), "utf8");
const rust = read("../src-tauri/src/lib.rs");

/** The commands the shell registers: `generate_handler![ a, b, ... ]`. */
function handlers(source: string): Set<string> {
  const list = /generate_handler!\[([\s\S]*?)\]/.exec(source)?.[1] ?? "";
  return new Set(list.split(",").map((name) => name.trim()).filter(Boolean));
}

/** The commands a TypeScript file invokes: `invoke("name"`, `invoke<T>("name"`, `tauri.invoke("name"`. */
function invoked(source: string): string[] {
  return [...source.matchAll(/\binvoke(?:<[^>]*>)?\(\s*"([a-z_]+)"/g)].map((match) => match[1]);
}

test("everyCommandThePageInvokesIsOneTheShellRegisters", () => {
  const registered = handlers(rust);
  assert.ok(registered.size > 10, "the handler list was found");
  for (const file of ["../src/desktop.ts", "../src/bridge/bridge.ts"]) {
    const names = invoked(read(file));
    assert.ok(names.length > 0, `${file} invokes commands`);
    assert.deepEqual(names.filter((name) => !registered.has(name)), [], file);
  }
});

test("theCloseRequestEventIsTheSameOnBothSides", () => {
  const inRust = /const CLOSE_REQUESTED_EVENT: &str = "([^"]+)"/.exec(rust)?.[1];
  const inTypeScript = /export const CLOSE_REQUESTED_EVENT = "([^"]+)"/.exec(read("../src/desktop.ts"))?.[1];

  assert.ok(inRust, "the event is named in lib.rs");
  assert.equal(inTypeScript, inRust);
});
