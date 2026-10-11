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

export async function runScreenChecks(): Promise<void> {
  try {
    await check();
    await record("screenDone", 1);
  } catch (error) {
    await record("error", errorMessage(error));
  }
  await record("done", 1);
}

async function check(): Promise<void> {
  const videoUrl = await given("XGS_CHECK_VIDEO_URL");
  const playlistUrl = await given("XGS_CHECK_PLAYLIST_URL");
  const outputDir = await given("XGS_CHECK_OUTPUT_DIR");

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
  press("다운로드 시작");
  await sleep(300);
  await record("screenVideoRunning", `${buttons().join(",")} | options disabled ${$<HTMLFieldSetElement>("#options").disabled}`);
  const tookMs = await until("the video to be downloaded", () => buttons().includes("다시 다운로드"), 180_000);
  await record("screenVideoResult", `${summary()} | ${labels()[0]} | ${$("#list .file").textContent} | ${tookMs} ms`);

  // A playlist: look it up, start it, cancel it as soon as it reports progress.
  press("새로 시작");
  type("#input", playlistUrl);
  $<HTMLButtonElement>("#resolve").click();
  await until("the playlist to be looked up", () => !$("#resolved").hidden, 90_000);
  await record("screenPlaylistPreview", `${$("#info-title").textContent} | ${labels().length} rows`);
  type("#output-dir", `${outputDir}\\cancelled`);
  if (!($("#destination").textContent ?? "").includes(outputDir)) throw new Error("the destination is not the folder of the check");
  press("다운로드 시작");
  await until("a song to report progress", () => labels().some((label) => label.startsWith("다운로드")), 120_000);
  const started = performance.now();
  press("취소");
  await until("the cancelled job to end", () => buttons().includes("다시 다운로드"), 60_000);
  await record(
    "screenCancelResult",
    `${summary()} | ready ${count((label) => label === "준비됨")} done ${count((label) => label.startsWith("완료"))} still moving ${count((label) =>
      /^(다운로드|대기|마무리)/.test(label),
    )} | ${Math.round(performance.now() - started)} ms after the button`,
  );
  await record("screenError", errorText() ?? "none");
}
