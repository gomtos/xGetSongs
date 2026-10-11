// The desktop around the screen: the folder chooser, the file manager, the settings file and the closing of the window. The screen
// knows only `Desktop`; the window of the app gets the one of `createTauriDesktop` (the commands of the Rust side), the demo page
// (`index.html?demo`) the one of `createDemoDesktop`.

import { userSettings, type SettingsStore, type UserSettings } from "./state/types.ts";

/** What the Rust side sends when the user closes the window. The page finishes up and answers with `close_window`. */
export const CLOSE_REQUESTED_EVENT = "xgs-close-requested";

/** How long the page waits for the options to be written when the window closes. The Rust side closes the window itself after 1.5 s. */
const CLOSE_LIMIT_MS = 1000;

export interface Desktop {
  /** The store the screen logic is given: what the settings file held when the page started, and writes of what changes. */
  readonly settings: SettingsStore;
  /** Opens the system's folder chooser starting in `initial` (when that is a folder). The chosen path, or null when cancelled. */
  pickFolder(initial: string): Promise<string | null>;
  /** Shows the folder (the nearest one that exists) in the file manager. Returns at once; never fails. */
  openFolder(path: string): void;
  /** Runs `beforeClose` when the user closes the window, waits for the options to be written, and lets the window close. */
  onClose(beforeClose: () => void): Promise<void>;
}

/** The two things of the Tauri API the desktop uses, so that the tests can stand in for them. */
export interface TauriLike {
  invoke<T>(command: string, args?: Record<string, unknown>): Promise<T>;
  listen(event: string, handler: () => Promise<void> | void): Promise<unknown>;
}

export interface PendingSettingsStore extends SettingsStore {
  /** Resolves when every save made so far has been written (or has failed). */
  whenSaved(): Promise<void>;
}

/** The settings as the Rust side hands them over: all four keys with the right types, else the defaults. */
export function normalizeSettings(raw: unknown): UserSettings {
  if (typeof raw !== "object" || raw === null || Array.isArray(raw)) return userSettings();
  const value = raw as Record<string, unknown>;
  const { outputDir, overwrite, includeRank, searchLyricsOnline } = value;
  if (
    !(outputDir === null || typeof outputDir === "string") ||
    typeof overwrite !== "boolean" ||
    typeof includeRank !== "boolean" ||
    typeof searchLyricsOnline !== "boolean"
  ) {
    return userSettings();
  }
  return { outputDir, overwrite, includeRank, searchLyricsOnline };
}

/**
 * `initial` is what `load` hands out. Every `save` is written by `write`, one at a time and in the order of the saves (two writes
 * at once could end with the older one). A write that fails is nobody's business: the options are then not kept.
 */
export function createSettingsStore(initial: UserSettings, write: (settings: UserSettings) => Promise<unknown>): PendingSettingsStore {
  let pending: Promise<void> = Promise.resolve();
  return {
    load: () => initial,
    save(settings) {
      pending = pending.then(() => write(settings)).then(
        () => {},
        () => {},
      );
    },
    whenSaved: () => pending,
  };
}

/** Resolves when `promise` settles, or after `ms` at the latest. */
function atMost(promise: Promise<unknown>, ms: number): Promise<void> {
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, ms);
    const done = () => {
      clearTimeout(timer);
      resolve();
    };
    promise.then(done, done);
  });
}

/** The desktop of the window of the app. Loads the settings file first, so that the screen can be built with them. Never throws. */
export async function createTauriDesktop(tauri: TauriLike, { closeLimitMs = CLOSE_LIMIT_MS }: { closeLimitMs?: number } = {}): Promise<Desktop> {
  let stored = userSettings();
  try {
    stored = normalizeSettings(await tauri.invoke("load_settings"));
  } catch {
    // The file cannot be read: the defaults, as when there is none.
  }
  const settings = createSettingsStore(stored, (changed) => tauri.invoke("save_settings", { settings: changed }));
  return {
    settings,
    async pickFolder(initial) {
      try {
        return (await tauri.invoke<string | null>("pick_folder", { initial })) ?? null;
      } catch {
        return null;
      }
    },
    openFolder(path) {
      void Promise.resolve()
        .then(() => tauri.invoke("open_folder", { path }))
        .catch(() => {});
    },
    async onClose(beforeClose) {
      await tauri.listen(CLOSE_REQUESTED_EVENT, async () => {
        try {
          beforeClose();
          await atMost(settings.whenSaved(), closeLimitMs);
        } catch {
          // Finishing up failed: the window closes all the same.
        }
        await tauri.invoke("close_window").catch(() => {});
      });
      await tauri.invoke("page_listens_for_close");
    },
  };
}

export interface DemoDesktop extends Desktop {
  /** The folders the screen asked to be shown, in order. */
  readonly opened: string[];
  /** The settings the screen asked to be saved, in order. */
  readonly saved: UserSettings[];
}

/** A desktop for the page without a window of the app: the chooser hands out made-up folders and nothing leaves the page. */
export function createDemoDesktop(): DemoDesktop {
  const opened: string[] = [];
  const saved: UserSettings[] = [];
  let chosen = 0;
  return {
    opened,
    saved,
    settings: { load: () => userSettings(), save: (settings) => void saved.push(settings) },
    async pickFolder() {
      chosen += 1;
      return `C:\\Users\\사용자\\Music\\선택한 폴더 ${chosen}`;
    },
    openFolder: (path) => void opened.push(path),
    async onClose() {},
  };
}
