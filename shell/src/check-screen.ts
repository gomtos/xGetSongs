// Part of the developer tooling, not of the product: with `index.html?check` the real screen is used the way a person does (types
// into the boxes, presses the buttons) while the real sidecar and the real yt-dlp answer, and what it sees is reported to the
// file of the check (see check.html, which sends the window here once its own checks are done). The page is the top-level page
// on purpose: Tauri sends the messages of a `Channel` to the top-level page only, so a screen in a frame never sees its events.
// See the plan 2026-10-11-tauri-frontend-port.md.

import { invoke } from "@tauri-apps/api/core";
import { errorMessage } from "./bridge/bridge.ts";

const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

async function record(key: string, value: unknown): Promise<void> {
  await invoke("check_record", { key, value: typeof value === "string" ? value : JSON.stringify(value) });
}

/** A value the check script gave: an address of a video, an address of a playlist, a folder for the files. */
async function given(name: string): Promise<string> {
  const value = await invoke<string | null>("check_env", { name });
  if (!value) throw new Error(`the check script gave no ${name}`);
  return value;
}

/**
 * Counts the frames of the page from now on. The function it returns stops the counting and gives the longest wait between two
 * frames in milliseconds: a screen that stalls while progress events arrive shows a long one.
 */
function watchFrames(): () => number {
  let worst = 0;
  let last = performance.now();
  let watching = true;
  const onFrame = (now: number) => {
    worst = Math.max(worst, now - last);
    last = now;
    if (watching) requestAnimationFrame(onFrame);
  };
  requestAnimationFrame(onFrame);
  return () => {
    watching = false;
    return Math.round(worst);
  };
}

export async function runScreenChecks(): Promise<void> {
  try {
    // One of the checks of the desktop features, or the whole way through a video and a playlist.
    const desktopCheck = await invoke<string | null>("check_env", { name: "XGS_CHECK_DESKTOP" });
    if (desktopCheck) await checkDesktop(desktopCheck);
    else await check();
    await record("screenDone", 1);
  } catch (error) {
    await record("error", errorMessage(error));
  }
  await record("done", 1);
}

/** What a person does on the screen: look at it, type into it, press its buttons. */
function screen() {
  const $ = <T extends HTMLElement>(selector: string): T => {
    const found = document.querySelector<T>(selector);
    if (!found) throw new Error(`the screen has no ${selector}`);
    return found;
  };
  const type = (selector: string, text: string) => {
    const box = $<HTMLInputElement>(selector);
    box.value = text;
    box.dispatchEvent(new Event("input", { bubbles: true }));
  };
  const buttons = () => [...document.querySelectorAll<HTMLButtonElement>("#actions button")].map((candidate) => candidate.textContent ?? "");
  const press = (label: string) => {
    const target = [...document.querySelectorAll<HTMLButtonElement>("#actions button")].find((candidate) => candidate.textContent === label);
    if (!target) throw new Error(`no button "${label}" among ${JSON.stringify(buttons())}`);
    target.click();
  };
  const labels = () => [...document.querySelectorAll<HTMLElement>("#list .label")].map((candidate) => candidate.textContent ?? "");
  const errorText = () => (document.querySelector<HTMLElement>("#error")?.hidden === false ? $("#error-text").textContent : null);
  const summary = () => document.querySelector<HTMLElement>("#actions .summary")?.textContent ?? null;
  const until = async (what: string, test: () => boolean, ms: number) => {
    const started = performance.now();
    while (!test()) {
      if (errorText() !== null) throw new Error(`while waiting for ${what}: the screen shows an error: ${errorText()}`);
      if (performance.now() - started > ms) throw new Error(`gave up waiting for ${what} after ${Math.round(ms / 1000)} s`);
      await sleep(100);
    }
    return Math.round(performance.now() - started);
  };
  const count = (test: (label: string) => boolean) => labels().filter(test).length;
  return { $, type, buttons, press, labels, errorText, summary, until, count };
}

/** The checks of what the desktop does for the screen. The script outside the window works the system dialog and the file manager. */
async function checkDesktop(mode: string): Promise<void> {
  const { $, type, until, labels } = screen();
  const outputDir = await given("XGS_CHECK_OUTPUT_DIR");
  const box = () => $<HTMLInputElement>("#output-dir").value;
  const lookUpTheVideo = async () => {
    type("#input", await given("XGS_CHECK_VIDEO_URL"));
    $<HTMLButtonElement>("#resolve").click();
    await until("the video to be looked up", () => !$("#resolved").hidden, 90_000);
    await record("screenVideoPreview", `${$("#info-title").textContent} | ${labels().length} row`);
  };

  if (mode === "settings") {
    // The settings file the script prepared was read when the page started; what the screen shows is what it held.
    await until("the screen", () => $("#tools").innerText.length > 0, 30_000);
    const options = () => ({
      outputDir: box(),
      overwrite: $<HTMLInputElement>("#opt-overwrite").checked,
      includeRank: $<HTMLInputElement>("#opt-rank").checked,
      searchLyricsOnline: $<HTMLInputElement>("#opt-lyrics").checked,
    });
    const before = options();
    await record("screenSettingsLoaded", before);
    // Change two of them and close the window at once, within the quiet period of the save: only the write of the closing can keep them.
    $<HTMLInputElement>("#opt-overwrite").click();
    type("#output-dir", outputDir);
    await record("screenSettingsChanged", options());
    await invoke("check_close");
    await sleep(10_000); // the window closes; the script reads the file
    return;
  }

  if (mode === "hang") {
    await until("the screen", () => $("#tools").innerText.length > 0, 30_000);
    await record("screenHangStarts", 1);
    // A page that is stuck: it does not answer the request to close. (The Rust side closes the window after 1.5 s all the same.)
    setTimeout(() => {
      const end = performance.now() + 8_000;
      while (performance.now() < end) {
        // busy
      }
    }, 50);
    await sleep(60_000);
    return;
  }

  await lookUpTheVideo();
  type("#output-dir", outputDir);

  if (mode === "picker") {
    await record("screenPickerStart", box());
    // First the chooser is cancelled by the script, then it is used to choose a folder.
    $<HTMLButtonElement>("#pick-folder").click();
    await record("screenPickerClicked", 1);
    await invoke("check_wait", { name: "picker-cancelled" });
    await sleep(300);
    await record("screenPickerAfterCancel", box());
    $<HTMLButtonElement>("#pick-folder").click();
    await record("screenPickerClickedAgain", 1);
    await invoke("check_wait", { name: "picker-chosen" });
    await sleep(300);
    await record("screenPickerAfterChoice", box());
    await record("screenPickerDestination", $("#destination").textContent ?? "");
    return;
  }

  if (mode === "explorer") {
    // A folder that does not exist yet: the nearest one that does is shown.
    type("#output-dir", `${outputDir}\\not\\there\\yet`);
    await record("screenExplorerDestination", $("#destination").textContent ?? "");
    $<HTMLButtonElement>("#open-folder").click();
    await record("screenExplorerClicked", 1);
    await invoke("check_wait", { name: "explorer-checked" });
    return;
  }

  throw new Error(`the check of the desktop has no mode ${mode}`);
}

async function check(): Promise<void> {
  const videoUrl = await given("XGS_CHECK_VIDEO_URL");
  const playlistUrl = await given("XGS_CHECK_PLAYLIST_URL");
  const outputDir = await given("XGS_CHECK_OUTPUT_DIR");
  const { $, type, buttons, press, labels, errorText, summary, until, count } = screen();

  // The screen starts: the tools are asked for (the sidecar may still be starting) and the default folder is chosen.
  const toolsAfter = await until("the tools", () => $("#tools").innerText.includes("✓"), 90_000);
  await record("screenTools", `${$("#tools").innerText.split("\n").slice(0, 3).join(" | ")} (after ${toolsAfter} ms)`);
  await record("screenDefaultFolder", $<HTMLInputElement>("#output-dir").value);

  // One video: look it up, choose the folder and the rank, download it.
  type("#input", videoUrl);
  $<HTMLButtonElement>("#resolve").click();
  await until("the video to be looked up", () => !$("#resolved").hidden, 90_000);
  await record("screenVideoPreview", `${$("#info-title").textContent} | ${labels().length} row | ${$("#list .file").textContent}`);
  type("#output-dir", outputDir);
  const destination = $("#destination").textContent ?? "";
  await record("screenDestination", destination);
  // Nothing may be written to the folder the screen starts with (the user's Music folder).
  if (!destination.includes(outputDir)) throw new Error(`the destination is not the folder of the check: ${destination}`);
  type("#single-rank", "42");
  await record("screenVideoRankedName", $("#list .file").textContent ?? "");
  const videoFrames = watchFrames();
  press("다운로드 시작");
  await sleep(300);
  await record("screenVideoRunning", `${buttons().join(",")} | options disabled ${$<HTMLFieldSetElement>("#options").disabled}`);
  const tookMs = await until("the video to be downloaded", () => buttons().includes("다시 다운로드"), 180_000);
  await record("screenVideoResult", `${summary()} | ${labels()[0]} | ${$("#list .file").textContent} | ${tookMs} ms`);
  await record("screenVideoFrameGapMs", videoFrames());

  // A playlist: look it up, start it, cancel it as soon as it reports progress.
  press("새로 시작");
  type("#input", playlistUrl);
  $<HTMLButtonElement>("#resolve").click();
  await until("the playlist to be looked up", () => !$("#resolved").hidden, 90_000);
  await record("screenPlaylistPreview", `${$("#info-title").textContent} | ${labels().length} rows`);
  type("#output-dir", `${outputDir}\\cancelled`);
  if (!($("#destination").textContent ?? "").includes(outputDir)) throw new Error("the destination is not the folder of the check");
  const playlistFrames = watchFrames();
  press("다운로드 시작");
  await until("a song to report progress", () => labels().some((label) => label.startsWith("다운로드")), 120_000);
  const started = performance.now();
  press("취소");
  await until("the cancelled job to end", () => buttons().includes("다시 다운로드"), 60_000);
  await record("screenPlaylistFrameGapMs", playlistFrames());
  await record(
    "screenCancelResult",
    `${summary()} | ready ${count((label) => label === "준비됨")} done ${count((label) => label.startsWith("완료"))} still moving ${count((label) =>
      /^(다운로드|대기|마무리)/.test(label),
    )} | ${Math.round(performance.now() - started)} ms after the button`,
  );
  await record("screenError", errorText() ?? "none");
}
