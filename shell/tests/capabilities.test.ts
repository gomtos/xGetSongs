// What the page may do. The webview gets the permissions of the capability file and nothing else; every plugin permission in it is a
// door for the page into that plugin. The shell uses the dialog plugin from Rust only (`pick_folder`) and no plugin from the page, so the
// page needs `core:default` (invoke and events) and no more. Adding a plugin to the page is a decision: this test says so.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import test from "node:test";

const read = (path: string) => readFileSync(new URL(path, import.meta.url), "utf8");

test("thePageIsGivenNoPluginPermissions", () => {
  const capability = JSON.parse(read("../src-tauri/capabilities/default.json")) as { permissions: string[] };

  assert.deepEqual(capability.permissions, ["core:default"]);
});

/** The capability files, parsed: what each grants and to which windows. */
function capabilities(): Array<{ file: string; windows?: string[]; remote?: unknown }> {
  const folder = new URL("../src-tauri/capabilities/", import.meta.url);
  return readdirSync(folder)
    .filter((name) => name.endsWith(".json"))
    .map((file) => ({ file, ...(JSON.parse(readFileSync(new URL(file, folder), "utf8")) as object) }));
}

test("everyCapabilityFileGrantsTheMainWindowOnly", () => {
  const all = capabilities();

  assert.ok(all.length > 0, "the capability files were found");
  for (const capability of all) {
    assert.deepEqual(capability.windows, ["main"], `${capability.file}: only the window of the app may be given permissions`);
  }
});

// The hidden browser window of the Google lyrics lookup shows pages of other people (Google's). Tauri refuses the commands of the app to
// a page from outside unless a capability names the window or lists remote addresses; this keeps both from happening by accident.
test("theHiddenBrowserWindowIsInNoCapability", () => {
  const label = /pub const BROWSER_LABEL: &str = "([^"]+)";/.exec(read("../src-tauri/src/browser_window.rs"))?.[1];

  assert.ok(label, "browser_window.rs names the label of the hidden window");
  assert.notEqual(label, "main");
  for (const capability of capabilities()) {
    assert.ok(!(capability.windows ?? []).includes(label), `${capability.file} names the hidden browser window`);
    assert.equal(capability.remote, undefined, `${capability.file} lets pages from outside use the commands of the app`);
  }
  assert.deepEqual(
    [...read("../src-tauri/tauri.conf.json").matchAll(/"label"\s*:\s*"([^"]+)"/g)].map((match) => match[1]),
    [],
    "tauri.conf.json makes no window of its own with a label (the only one is the main window)",
  );
});

test("thePageUsesNoTauriPlugin", () => {
  const manifest = JSON.parse(read("../package.json")) as { dependencies: Record<string, string> };

  assert.deepEqual(Object.keys(manifest.dependencies), ["@tauri-apps/api"]);
});

test("theShellUsesNoPluginThatNothingCalls", () => {
  const cargo = read("../src-tauri/Cargo.toml");
  const shell = read("../src-tauri/src/lib.rs");

  // The plugins the shell registers are the plugins it depends on, and the other way round.
  const registered = [...shell.matchAll(/\.plugin\(\s*(tauri_plugin_[a-z_]+)::/g)].map((match) => match[1].replaceAll("_", "-")).sort();
  const depended = [...cargo.matchAll(/^(tauri-plugin-[a-z-]+)\s*=/gm)].map((match) => match[1]).sort();

  assert.deepEqual(registered, depended);
  assert.deepEqual(depended, ["tauri-plugin-dialog", "tauri-plugin-single-instance"]);
});
