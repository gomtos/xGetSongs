// The desktop around the screen: the folder chooser, the file manager, the settings file and the closing of the window. The Rust side
// is reached through `invoke` and `listen`, which the tests replace with a recorder.

import assert from "node:assert/strict";
import test from "node:test";
import {
  CLOSE_REQUESTED_EVENT,
  createDemoDesktop,
  createSettingsStore,
  createTauriDesktop,
  normalizeSettings,
  type TauriLike,
} from "../src/desktop.ts";
import { userSettings } from "../src/state/types.ts";
import { Deferred, flush } from "./support/fakes.ts";

interface Call {
  command: string;
  args: Record<string, unknown> | undefined;
}

/** `invoke` and `listen` of a window whose Rust side answers with whatever the test put in `answers`. */
function recorder(answers: Record<string, (args: Record<string, unknown> | undefined) => unknown> = {}) {
  const calls: Call[] = [];
  const listeners = new Map<string, () => Promise<void> | void>();
  const tauri: TauriLike = {
    async invoke(command, args) {
      calls.push({ command, args });
      const answer = answers[command];
      return (answer ? await answer(args) : undefined) as never;
    },
    async listen(event, handler) {
      calls.push({ command: `listen:${event}`, args: undefined });
      listeners.set(event, handler);
    },
  };
  return { tauri, calls, listeners, names: () => calls.map((call) => call.command) };
}

const korean = userSettings({ outputDir: "D:\\음악\\내 재생목록", overwrite: true, includeRank: false, searchLyricsOnline: false });

// ---- the settings the Rust side hands over ----

test("settingsInTheShapeOfTheFileAreTakenAsTheyAre", () => {
  assert.deepEqual(normalizeSettings(korean), korean);
  assert.deepEqual(normalizeSettings({ outputDir: null, overwrite: false, includeRank: true, searchLyricsOnline: true }), userSettings());
});

test("settingsThatAreNotWhatWasExpectedAreTheDefaults", () => {
  for (const raw of [undefined, null, "x", 5, [], {}, { outputDir: 5 }, { outputDir: null, overwrite: "yes", includeRank: true, searchLyricsOnline: true }]) {
    assert.deepEqual(normalizeSettings(raw), userSettings(), JSON.stringify(raw));
  }
});

// ---- the store the screen is given ----

test("theStoreHandsOutWhatItWasStartedWith", () => {
  const store = createSettingsStore(korean, async () => {});

  assert.deepEqual(store.load(), korean);
  assert.deepEqual(store.load(), korean);
});

test("everySaveIsWrittenInTheOrderItWasMadeEvenWhenTheFirstWriteIsSlow", async () => {
  const written: string[] = [];
  const gate = new Deferred();
  const store = createSettingsStore(userSettings(), async (settings) => {
    if (settings.outputDir === "first") await gate.promise;
    written.push(settings.outputDir ?? "");
  });

  store.save(userSettings({ outputDir: "first" }));
  store.save(userSettings({ outputDir: "second" }));
  store.save(userSettings({ outputDir: "third" }));
  await flush();
  assert.deepEqual(written, [], "the first write has not finished, so nothing may overtake it");

  gate.resolve();
  await store.whenSaved();

  assert.deepEqual(written, ["first", "second", "third"]);
});

test("aWriteThatFailsDoesNotStopTheNextOnesAndNeverThrows", async () => {
  const written: string[] = [];
  const store = createSettingsStore(userSettings(), async (settings) => {
    if (settings.outputDir === "bad") throw new Error("disk full");
    written.push(settings.outputDir ?? "");
  });

  store.save(userSettings({ outputDir: "bad" }));
  store.save(userSettings({ outputDir: "good" }));
  await store.whenSaved();

  assert.deepEqual(written, ["good"]);
});

test("aWriteThatThrowsAtOnceIsTreatedLikeOneThatFails", async () => {
  const written: string[] = [];
  const store = createSettingsStore(userSettings(), (settings) => {
    if (settings.outputDir === "bad") throw new Error("not even a promise");
    written.push(settings.outputDir ?? "");
    return Promise.resolve();
  });

  store.save(userSettings({ outputDir: "bad" }));
  store.save(userSettings({ outputDir: "good" }));
  await store.whenSaved();

  assert.deepEqual(written, ["good"]);
});

test("whenSavedHasNothingToWaitForWithoutASave", async () => {
  const store = createSettingsStore(userSettings(), async () => {});

  await store.whenSaved();
});

// ---- the desktop of the real window ----

test("theDesktopLoadsTheSettingsFileBeforeItIsHandedOver", async () => {
  const { tauri, names } = recorder({ load_settings: () => korean });

  const desktop = await createTauriDesktop(tauri);

  assert.deepEqual(desktop.settings.load(), korean);
  assert.deepEqual(names(), ["load_settings"]);
});

test("aSettingsFileThatCannotBeLoadedMeansTheDefaultsAndNoFailure", async () => {
  const { tauri } = recorder({
    load_settings: () => {
      throw new Error("no such command");
    },
  });

  const desktop = await createTauriDesktop(tauri);

  assert.deepEqual(desktop.settings.load(), userSettings());
});

test("savedSettingsGoToTheRustSideUnderTheNameOfTheCommand", async () => {
  const { tauri, calls } = recorder({ load_settings: () => userSettings() });
  const desktop = await createTauriDesktop(tauri);

  desktop.settings.save(korean);
  await flush();

  assert.deepEqual(calls.at(-1), { command: "save_settings", args: { settings: korean } });
});

test("theFolderChooserStartsWhereTheScreenSaysAndHandsBackWhatWasChosen", async () => {
  const { tauri, calls } = recorder({ load_settings: () => userSettings(), pick_folder: () => "D:\\음악\\새 폴더" });
  const desktop = await createTauriDesktop(tauri);

  const chosen = await desktop.pickFolder("D:\\음악");

  assert.equal(chosen, "D:\\음악\\새 폴더");
  assert.deepEqual(calls.at(-1), { command: "pick_folder", args: { initial: "D:\\음악" } });
});

test("aCancelledFolderChooserHandsBackNothing", async () => {
  const { tauri } = recorder({ load_settings: () => userSettings(), pick_folder: () => null });
  const desktop = await createTauriDesktop(tauri);

  assert.equal(await desktop.pickFolder("D:\\음악"), null);
});

test("aFolderChooserThatFailsIsACancelledOne", async () => {
  const { tauri } = recorder({
    load_settings: () => userSettings(),
    pick_folder: () => {
      throw new Error("the dialog could not open");
    },
  });
  const desktop = await createTauriDesktop(tauri);

  assert.equal(await desktop.pickFolder("D:\\음악"), null);
});

test("showingAFolderIsAskedOfTheRustSideAndNeverFails", async () => {
  const { tauri, calls } = recorder({
    load_settings: () => userSettings(),
    open_folder: () => {
      throw new Error("explorer is gone");
    },
  });
  const desktop = await createTauriDesktop(tauri);

  desktop.openFolder("D:\\음악\\내 재생목록");
  await flush();

  assert.deepEqual(calls.at(-1), { command: "open_folder", args: { path: "D:\\음악\\내 재생목록" } });
});

// ---- closing the window ----

test("theRustSideIsToldThePageListensOnlyOnceTheListenerIsInPlace", async () => {
  const { tauri, names } = recorder({ load_settings: () => userSettings() });
  const desktop = await createTauriDesktop(tauri);

  await desktop.onClose(() => {});

  assert.deepEqual(names(), ["load_settings", `listen:${CLOSE_REQUESTED_EVENT}`, "page_listens_for_close"]);
});

test("whenTheWindowIsClosedTheOptionsAreWrittenFirstAndThenTheWindowIsClosed", async () => {
  const gate = new Deferred();
  const { tauri, calls, listeners } = recorder({
    load_settings: () => userSettings(),
    save_settings: () => gate.promise,
  });
  const desktop = await createTauriDesktop(tauri);
  await desktop.onClose(() => desktop.settings.save(korean));

  const closing = listeners.get(CLOSE_REQUESTED_EVENT)?.();
  await flush();
  assert.equal(calls.some((call) => call.command === "close_window"), false, "the options are still being written");

  gate.resolve();
  await closing;

  assert.deepEqual(calls.map((call) => call.command).slice(-2), ["save_settings", "close_window"]);
});

test("aWriteThatNeverEndsDoesNotKeepTheWindowOpenLongerThanTheLimit", async () => {
  const { tauri, calls, listeners } = recorder({
    load_settings: () => userSettings(),
    save_settings: () => new Promise(() => {}),
  });
  const desktop = await createTauriDesktop(tauri, { closeLimitMs: 30 });
  await desktop.onClose(() => desktop.settings.save(korean));

  await listeners.get(CLOSE_REQUESTED_EVENT)?.();

  assert.equal(calls.at(-1)?.command, "close_window");
});

test("theWindowIsClosedEvenWhenFinishingUpFails", async () => {
  const { tauri, calls, listeners } = recorder({ load_settings: () => userSettings() });
  const desktop = await createTauriDesktop(tauri);
  await desktop.onClose(() => {
    throw new Error("the screen is broken");
  });

  await listeners.get(CLOSE_REQUESTED_EVENT)?.();

  assert.equal(calls.at(-1)?.command, "close_window");
});

// ---- the desktop of the demo page ----

test("theDemoChooserHandsOutAnotherFolderEachTime", async () => {
  const demo = createDemoDesktop();

  const first = await demo.pickFolder("C:\\x");
  const second = await demo.pickFolder(first ?? "");

  assert.ok(first && second && first !== second);
  assert.match(first, /^C:\\Users\\사용자\\Music\\/);
});

test("theDemoRemembersWhatWasShownAndWhatWasSaved", () => {
  const demo = createDemoDesktop();

  demo.openFolder("C:\\a");
  demo.openFolder("C:\\b");
  demo.settings.save(korean);

  assert.deepEqual(demo.opened, ["C:\\a", "C:\\b"]);
  assert.deepEqual(demo.settings.load(), userSettings(), "what it hands out is what it started with: a page that is loaded again starts again");
  assert.deepEqual(demo.saved, [korean]);
});
