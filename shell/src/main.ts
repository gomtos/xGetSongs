// The screen: it draws what the AppStateHolder says and tells the holder what the user does. All the logic is in the holder
// (src/state), which the tests of tests/holder.test.ts hold to the scenarios of the Compose app. `index.html?demo` runs the screen on
// a made-up server (src/demo.ts); without it the screen talks to the sidecar through the Tauri commands (src/bridge).

import "./app.css";
import { createDemoApi } from "./demo.ts";
import { trim } from "./logic/text.ts";
import { AppStateHolder } from "./state/holder.ts";
import {
  FORMAT_FAILURE_HELP,
  UPDATE_YT_DLP_LABEL,
  albumNameText,
  destinationLabel,
  rankFromInput,
  rankInputText,
  rankLabel,
  statusLabel,
  summaryText,
} from "./state/labels.ts";
import { canResolve, failedRanks, type ItemRow, type UiState, type XgsApi } from "./state/types.ts";

// ---- elements -------------------------------------------------------------------------------

function $<T extends HTMLElement>(selector: string): T {
  const element = document.querySelector<T>(selector);
  if (!element) throw new Error(`missing element ${selector}`);
  return element;
}

function el<K extends keyof HTMLElementTagNameMap>(tag: K, className = "", text = ""): HTMLElementTagNameMap[K] {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text) element.textContent = text;
  return element;
}

function button(label: string, onClick: () => void, className = ""): HTMLButtonElement {
  const created = el("button", className, label);
  created.type = "button";
  created.addEventListener("click", onClick);
  return created;
}

// ---- the screen -----------------------------------------------------------------------------

interface RowView {
  file: HTMLElement;
  label: HTMLElement;
  bar: HTMLProgressElement;
}

function mount(holder: AppStateHolder): void {
  const toolsCard = $<HTMLElement>("#tools");
  const form = $<HTMLFormElement>("#input-form");
  const input = $<HTMLInputElement>("#input");
  const resolveButton = $<HTMLButtonElement>("#resolve");
  const errorBanner = $<HTMLElement>("#error");
  const errorText = $<HTMLElement>("#error-text");
  const errorClose = $<HTMLButtonElement>("#error-close");
  const resolvedSection = $<HTMLElement>("#resolved");
  const infoTitle = $<HTMLElement>("#info-title");
  const truncated = $<HTMLElement>("#truncated");
  const videoOnly = $<HTMLButtonElement>("#video-only");
  const options = $<HTMLFieldSetElement>("#options");
  const outputDir = $<HTMLInputElement>("#output-dir");
  const destinationRow = $<HTMLElement>("#destination-row");
  const destination = $<HTMLElement>("#destination");
  const album = $<HTMLInputElement>("#album");
  const optOverwrite = $<HTMLInputElement>("#opt-overwrite");
  const optRank = $<HTMLInputElement>("#opt-rank");
  const optLyrics = $<HTMLInputElement>("#opt-lyrics");
  const rankField = $<HTMLElement>("#rank-field");
  const singleRank = $<HTMLInputElement>("#single-rank");
  const list = $<HTMLElement>("#list");
  const actions = $<HTMLElement>("#actions");
  const installDialog = $<HTMLDialogElement>("#install-dialog");
  const installForm = $<HTMLFormElement>("#install-form");

  // Text boxes are never written to while an IME is composing in them (a Korean syllable under construction would break).
  const composing = new Set<HTMLInputElement>();
  for (const box of [input, outputDir, album, singleRank]) {
    box.addEventListener("compositionstart", () => composing.add(box));
    box.addEventListener("compositionend", () => composing.delete(box));
  }
  function showText(box: HTMLInputElement, value: string): void {
    if (box.value !== value && !composing.has(box)) box.value = value;
  }

  // ---- tools panel ----

  let shownTools: { tools: UiState["tools"]; busy: boolean; message: string | null; running: boolean } | null = null;

  function renderTools(state: UiState): void {
    const running = state.phase === "RUNNING";
    if (
      shownTools &&
      shownTools.tools === state.tools &&
      shownTools.busy === state.toolBusy &&
      shownTools.message === state.toolMessage &&
      shownTools.running === running
    ) {
      return;
    }
    shownTools = { tools: state.tools, busy: state.toolBusy, message: state.toolMessage, running };
    toolsCard.replaceChildren();
    const tools = state.tools;
    if (!tools) {
      toolsCard.append(el("p", "", "도구 확인 중…"));
    } else {
      const line = el("div", "tool-line");
      const entries: Array<[string, typeof tools.ytDlp]> = [
        ["yt-dlp", tools.ytDlp],
        ["ffmpeg", tools.ffmpeg],
        ["JS 런타임", tools.jsRuntime],
      ];
      for (const [name, info] of entries) {
        line.append(el("span", info.found ? "" : "bad", info.found ? `${name} ${info.version ?? ""} ✓` : `${name} ✗`));
      }
      const row = el("div", "row-flex");
      if (!tools.ytDlp.found) {
        const install = button("yt-dlp 설치", () => installDialog.showModal(), "primary");
        install.disabled = state.toolBusy;
        row.append(install);
      } else {
        const update = button(UPDATE_YT_DLP_LABEL, () => holder.updateYtDlp());
        update.disabled = state.toolBusy || running;
        row.append(update, el("p", "small muted grow", FORMAT_FAILURE_HELP));
      }
      toolsCard.append(line, row);
      if (!tools.ffmpeg.found) toolsCard.append(el("p", "small bad", "ffmpeg가 필요합니다. 예: winget install Gyan.FFmpeg"));
      if (!tools.jsRuntime.found) {
        toolsCard.append(el("p", "small bad", "YouTube를 읽으려면 Node.js 22 이상 또는 Deno 2.3 이상이 필요합니다."));
      }
    }
    if (state.toolBusy) toolsCard.append(el("p", "small", "작업 중…"));
    if (state.toolMessage !== null) toolsCard.append(el("p", "small", state.toolMessage));
  }

  // The form of the dialog closes it by itself; what is pressed is read when the form is submitted (the dialog's own `close`
  // event is queued and comes later, or not at all while the window is hidden).
  installForm.addEventListener("submit", (event) => {
    if ((event as SubmitEvent).submitter?.getAttribute("value") === "install") holder.installYtDlp();
  });

  // ---- the list: built when the rows change shape, patched row by row when only their status moves ----

  let shownRows: ItemRow[] = [];
  let views: RowView[] = [];

  function buildList(rows: ItemRow[]): void {
    list.replaceChildren();
    views = rows.map((row) => {
      const item = el("li", row.item.available ? "item" : "item unavailable");
      const file = el("span", "file");
      const name = el("div", "name");
      name.append(file);
      if (row.item.available && row.item.lowConfidence) name.append(el("span", "warn", "⚠ 가수명을 채널명에서 추정했습니다"));
      const label = el("span", "label");
      const bar = el("progress");
      bar.max = 100;
      bar.hidden = true;
      const status = el("div", "status");
      status.append(label, bar);
      item.append(el("span", "rank", rankLabel(row.item.rank)), name, status);
      list.append(item);
      const view = { file, label, bar };
      patchRow(view, row);
      return view;
    });
  }

  /** Writes what a row says now into its elements; the rest of the list (and every input) is left alone. */
  function patchRow(view: RowView, row: ItemRow): void {
    const fileName = row.fileName ?? row.item.title;
    view.file.textContent = fileName;
    view.label.textContent = statusLabel(row.status);
    view.label.classList.toggle("failed", row.status.kind === "failed");
    if (row.status.kind === "downloading" && row.status.percent !== null) {
      view.bar.hidden = false;
      view.bar.value = row.status.percent;
      view.bar.setAttribute("aria-label", `${fileName} 다운로드 진행률`);
    } else {
      view.bar.hidden = true;
    }
  }

  function renderList(rows: ItemRow[]): void {
    if (rows === shownRows) return;
    const sameShape =
      rows.length === shownRows.length &&
      rows.every((row, index) => row.item.rank === shownRows[index].item.rank && row.item.videoId === shownRows[index].item.videoId);
    if (sameShape) {
      rows.forEach((row, index) => {
        if (row !== shownRows[index]) patchRow(views[index], row);
      });
    } else {
      buildList(rows);
    }
    shownRows = rows;
  }

  // ---- the buttons under the list ----

  let shownActions = "";

  function renderActions(state: UiState): void {
    const summary = summaryText(state);
    const startable = state.rows.some((row) => row.item.available);
    const key = [state.phase, startable, failedRanks(state).length > 0, summary].join("|");
    if (key === shownActions) return;
    shownActions = key;
    const hadFocus = actions.contains(document.activeElement);
    actions.replaceChildren();
    switch (state.phase) {
      case "PREVIEW": {
        const start = button("다운로드 시작", () => holder.startDownload(), "primary");
        start.disabled = !startable;
        actions.append(start, button("새로 시작", () => holder.reset()));
        break;
      }
      case "RUNNING":
        actions.append(button("취소", () => holder.cancel(), "primary"));
        break;
      case "FINISHED":
        if (summary !== null) {
          const line = el("span", "summary", summary);
          line.setAttribute("role", "status");
          actions.append(line);
        }
        if (failedRanks(state).length > 0) actions.append(button("실패 항목 재시도", () => holder.retryFailed(), "primary"));
        actions.append(button("다시 다운로드", () => holder.startDownload()), button("새로 시작", () => holder.reset()));
        break;
      default:
        break;
    }
    // The button the user pressed is gone: keep the keyboard where it was.
    if (hadFocus) actions.querySelector<HTMLButtonElement>("button.primary")?.focus();
  }

  // ---- everything else ----

  function render(state: UiState): void {
    renderTools(state);

    showText(input, state.input);
    resolveButton.disabled = !canResolve(state) || trim(state.input) === "";
    errorBanner.hidden = state.error === null;
    errorText.textContent = state.error ?? "";

    const resolved = state.resolved;
    resolvedSection.hidden = resolved === null;
    if (resolved !== null) {
      const count = resolved.kind === "PLAYLIST" ? ` · ${resolved.items.length}개` : "";
      infoTitle.textContent = (resolved.playlistTitle ?? "단일 영상") + count;
      truncated.hidden = !resolved.truncated;
      videoOnly.hidden = resolved.alsoVideoId === null || !canResolve(state);
    }

    options.disabled = state.phase === "RUNNING";
    showText(outputDir, state.outputDir);
    showText(album, albumNameText(state));
    optOverwrite.checked = state.overwrite;
    optRank.checked = state.includeRank;
    optLyrics.checked = state.searchLyricsOnline;
    const label = destinationLabel(state);
    destinationRow.hidden = label === null;
    destination.textContent = label ?? "";
    rankField.hidden = resolved?.kind !== "VIDEO";
    // The raw text stays while the user retypes the number: the box is only rewritten when it says another rank.
    if (!rankField.hidden && !composing.has(singleRank) && rankFromInput(singleRank.value) !== state.singleRank) {
      singleRank.value = String(state.singleRank);
    }

    renderList(state.rows);
    renderActions(state);
  }

  // ---- what the user does ----

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    holder.resolve();
  });
  input.addEventListener("input", () => holder.onInput(input.value));
  outputDir.addEventListener("input", () => holder.onOutputDir(outputDir.value));
  album.addEventListener("input", () => holder.onAlbumName(album.value));
  optOverwrite.addEventListener("change", () => holder.onOverwrite(optOverwrite.checked));
  optRank.addEventListener("change", () => holder.onIncludeRank(optRank.checked));
  optLyrics.addEventListener("change", () => holder.onSearchLyricsOnline(optLyrics.checked));
  singleRank.addEventListener("input", () => {
    const text = rankInputText(singleRank.value);
    if (singleRank.value !== text) singleRank.value = text;
    const rank = rankFromInput(text);
    if (rank !== null) holder.onSingleRank(rank);
  });
  videoOnly.addEventListener("click", () => holder.switchToVideoOnly());
  errorClose.addEventListener("click", () => holder.dismissError());

  // A reload would forget the job that runs: the server lets one reader follow a job, so the page could not follow it again.
  window.addEventListener("keydown", (event) => {
    const reload = event.key === "F5" || ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "r");
    if (reload) event.preventDefault();
  });

  holder.subscribe(render);
  render(holder.getState());
  holder.refreshTools();
}

// ---- start ----------------------------------------------------------------------------------

/** The folder the app saves into until the user chooses another: `Music\xGetSongs` in the home folder, as before. */
async function defaultOutputDirectory(): Promise<string> {
  try {
    const { homeDir, join } = await import("@tauri-apps/api/path");
    return await join(await homeDir(), "Music", "xGetSongs");
  } catch {
    return "";
  }
}

async function boot(): Promise<void> {
  const params = new URLSearchParams(location.search);
  const demo = params.get("demo");
  let api: XgsApi;
  let defaultOutputDir: string;
  if (demo !== null) {
    // `?demo=broken` has no tools; `&speed=30` makes the made-up downloads 30 times faster.
    api = createDemoApi({ tools: demo === "broken" ? "broken" : "ok", speed: Number(params.get("speed")) || 1 });
    defaultOutputDir = "C:\\Users\\사용자\\Music\\xGetSongs";
  } else {
    api = (await import("./bridge/tauri.ts")).bridge;
    defaultOutputDir = await defaultOutputDirectory();
  }
  mount(new AppStateHolder({ api, defaultOutputDir }));
  // The developer tooling uses the screen like a person does (see check-screen.ts); a normal start has no `?check`.
  if (params.has("check")) void import("./check-screen.ts").then((module) => module.runScreenChecks());
}

void boot();
