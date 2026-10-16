// What the page may do. The webview gets the permissions of the capability file and nothing else; every plugin permission in it is a
// door for the page into that plugin. The shell uses the dialog plugin from Rust only (`pick_folder`) and no plugin from the page, so the
// page needs `core:default` (invoke and events) and no more. Adding a plugin to the page is a decision: this test says so.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const read = (path: string) => readFileSync(new URL(path, import.meta.url), "utf8");

test("thePageIsGivenNoPluginPermissions", () => {
  const capability = JSON.parse(read("../src-tauri/capabilities/default.json")) as { permissions: string[] };

  assert.deepEqual(capability.permissions, ["core:default"]);
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
