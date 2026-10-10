import "./prototype.css";
import {
  FORMAT_FAILURE_HELP,
  UPDATE_YT_DLP_LABEL,
  rankLabel,
  statusLabel,
  summaryText,
  type JobStatus,
} from "./prototype/labels.ts";
import {
  PLAYLIST_TITLE,
  SAMPLE_URL,
  TOOLS_BROKEN,
  TOOLS_OK,
  buildRows,
  fileNameOf,
  outcomeFor,
  startFakeJob,
  type FakeJob,
  type Row,
  type ToolsStatus,
} from "./prototype/fake.ts";

type Phase = "idle" | "resolving" | "preview" | "running" | "finished";

const state = {
  phase: "idle" as Phase,
  error: null as string | null,
  rows: [] as Row[],
  jobStatus: null as JobStatus | null,
  startedAt: 0,
  elapsedSeconds: 0,
  job: null as FakeJob | null,
};

// ---- elements -------------------------------------------------------------------------------

function $<T extends HTMLElement>(selector: string): T {
  const element = document.querySelector<T>(selector);
  if (!element) throw new Error(`missing element ${selector}`);
  return element;
}

const toolsCard = $<HTMLElement>("#tools");
const form = $<HTMLFormElement>("#input-form");
const input = $<HTMLInputElement>("#input");
const resolveButton = $<HTMLButtonElement>("#resolve");
const errorBanner = $<HTMLElement>("#error");
const errorText = $<HTMLElement>("#error-text");
const errorClose = $<HTMLButtonElement>("#error-close");
const resolvedSection = $<HTMLElement>("#resolved");
const infoTitle = $<HTMLElement>("#info-title");
const options = $<HTMLFieldSetElement>("#options");
const outputDir = $<HTMLInputElement>("#output-dir");
const pickFolder = $<HTMLButtonElement>("#pick-folder");
const destination = $<HTMLElement>("#destination");
const openFolder = $<HTMLButtonElement>("#open-folder");
const album = $<HTMLInputElement>("#album");
const optRank = $<HTMLInputElement>("#opt-rank");
const list = $<HTMLElement>("#list");
const actions = $<HTMLElement>("#actions");
const dev = $<HTMLElement>("#dev");

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

// ---- tools panel ----------------------------------------------------------------------------

let updateButton: HTMLButtonElement | null = null;

function setTools(tools: ToolsStatus | null) {
  toolsCard.replaceChildren();
  updateButton = null;
  if (!tools) {
    toolsCard.append(el("p", "", "도구 확인 중…"));
    return;
  }
  const line = el("div", "tool-line");
  const entries: Array<[string, ToolsStatus["ytDlp"]]> = [
    ["yt-dlp", tools.ytDlp],
    ["ffmpeg", tools.ffmpeg],
    ["JS 런타임", tools.jsRuntime],
  ];
  for (const [name, info] of entries) {
    line.append(el("span", info.found ? "" : "bad", info.found ? `${name} ${info.version ?? ""} ✓` : `${name} ✗`));
  }
  const row = el("div", "row-flex");
  updateButton = button(UPDATE_YT_DLP_LABEL, () => {});
  updateButton.disabled = state.phase === "running";
  row.append(updateButton, el("p", "small muted grow", FORMAT_FAILURE_HELP));
  toolsCard.append(line, row);
  if (!tools.ffmpeg.found) toolsCard.append(el("p", "small bad", "ffmpeg가 필요합니다. 예: winget install Gyan.FFmpeg"));
  if (!tools.jsRuntime.found) {
    toolsCard.append(el("p", "small bad", "YouTube를 읽으려면 Node.js 22 이상 또는 Deno 2.3 이상이 필요합니다."));
  }
}

// ---- the list -------------------------------------------------------------------------------

interface RowView {
  file: HTMLElement;
  label: HTMLElement;
  bar: HTMLProgressElement;
}

const views = new Map<number, RowView>();

function buildList(rows: Row[]) {
  list.replaceChildren();
  views.clear();
  for (const row of rows) {
    const item = el("li", row.available ? "item" : "item unavailable");
    const file = el("span", "file");
    const name = el("div", "name");
    name.append(file);
    if (row.available && row.lowConfidence) name.append(el("span", "warn", "⚠ 가수명을 채널명에서 추정했습니다"));
    const label = el("span", "label");
    const bar = el("progress");
    bar.max = 100;
    bar.hidden = true;
    const status = el("div", "status");
    status.append(label, bar);
    item.append(el("span", "rank", rankLabel(row.rank)), name, status);
    list.append(item);
    views.set(row.rank, { file, label, bar });
    patchRow(row);
  }
}

/** Writes what a row says now into its elements; the rest of the list (and every input) is left alone. */
function patchRow(row: Row) {
  const view = views.get(row.rank);
  if (!view) return;
  view.file.textContent = row.fileName;
  view.label.textContent = statusLabel(row.status);
  view.label.classList.toggle("failed", row.status.kind === "failed");
  if (row.status.kind === "downloading" && row.status.percent !== null) {
    view.bar.hidden = false;
    view.bar.value = row.status.percent;
    view.bar.setAttribute("aria-label", `${row.fileName} 다운로드 진행률`);
  } else {
    view.bar.hidden = true;
  }
}

// ---- rendering ------------------------------------------------------------------------------

function updateResolveEnabled() {
  resolveButton.disabled = state.phase === "resolving" || state.phase === "running" || input.value.trim() === "";
}

function updateDestination() {
  const folder = album.value.trim() || PLAYLIST_TITLE;
  const base = outputDir.value.replace(/[\\/]+$/, "");
  destination.textContent = `저장 위치: ${base}\\${folder}`;
}

function summary(): string | null {
  const rows = state.rows;
  return summaryText(
    {
      succeeded: rows.filter((row) => row.status.kind === "done").length,
      skipped: rows.filter((row) => row.available && row.status.kind === "skipped").length,
      failed: rows.filter((row) => row.status.kind === "failed").length,
    },
    state.jobStatus,
    state.elapsedSeconds,
  );
}

function renderActions() {
  const hadFocus = actions.contains(document.activeElement);
  actions.replaceChildren();
  switch (state.phase) {
    case "preview":
      actions.append(button("다운로드 시작", startDownload, "primary"), button("새로 시작", reset));
      break;
    case "running":
      actions.append(button("취소", () => state.job?.cancel(), "primary"));
      break;
    case "finished": {
      const text = summary();
      if (text) {
        const line = el("span", "summary", text);
        line.setAttribute("role", "status");
        actions.append(line);
      }
      if (state.rows.some((row) => row.status.kind === "failed")) {
        actions.append(button("실패 항목 재시도", retryFailed, "primary"));
      }
      actions.append(button("다시 다운로드", startDownload), button("새로 시작", reset));
      break;
    }
    default:
      break;
  }
  // The button the user pressed is gone: keep the keyboard where it was.
  if (hadFocus) actions.querySelector<HTMLButtonElement>("button.primary")?.focus();
}

function render() {
  updateResolveEnabled();
  errorBanner.hidden = state.error === null;
  errorText.textContent = state.error ?? "";
  resolvedSection.hidden = state.rows.length === 0;
  options.disabled = state.phase === "running";
  if (updateButton) updateButton.disabled = state.phase === "running";
  infoTitle.textContent = `${PLAYLIST_TITLE} · ${state.rows.length}개`;
  updateDestination();
  renderActions();
}

// ---- what the user does ---------------------------------------------------------------------

function resolveNow() {
  state.rows = buildRows(100, optRank.checked);
  state.phase = "preview";
  state.jobStatus = null;
  album.value = PLAYLIST_TITLE;
  buildList(state.rows);
  render();
}

function resolve() {
  if (resolveButton.disabled) return;
  state.phase = "resolving";
  state.error = null;
  render();
  window.setTimeout(resolveNow, 600);
}

function runJob(rows: Row[], retry: boolean) {
  state.phase = "running";
  state.error = null;
  state.jobStatus = null;
  state.startedAt = performance.now();
  state.job = startFakeJob(rows, (changed) => changed.forEach(patchRow), finish, { retry });
  rows.forEach(patchRow);
  render();
}

function finish(end: JobStatus) {
  state.job = null;
  state.phase = "finished";
  state.jobStatus = end;
  state.elapsedSeconds = (performance.now() - state.startedAt) / 1000;
  render();
}

function startDownload() {
  if (state.phase !== "preview" && state.phase !== "finished") return;
  runJob(state.rows, false);
}

function retryFailed() {
  const failed = state.rows.filter((row) => row.status.kind === "failed");
  if (failed.length === 0 || state.phase !== "finished") return;
  runJob(failed, true);
}

function reset() {
  state.job?.cancel();
  state.phase = "idle";
  state.error = null;
  state.rows = [];
  state.jobStatus = null;
  state.elapsedSeconds = 0;
  list.replaceChildren();
  views.clear();
  input.value = "";
  album.value = "";
  render();
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  resolve();
});
input.addEventListener("input", updateResolveEnabled);
outputDir.addEventListener("input", updateDestination);
album.addEventListener("input", updateDestination);
errorClose.addEventListener("click", () => {
  state.error = null;
  render();
});
pickFolder.addEventListener("click", () => {
  // The real app opens the folder chooser of the system here.
  outputDir.value = "D:\\Music\\xGetSongs";
  updateDestination();
});
openFolder.addEventListener("click", () => {
  // The real app shows the folder in Explorer here.
});
optRank.addEventListener("change", () => {
  if (state.phase !== "preview") return;
  for (const row of state.rows) {
    if (!row.available) continue;
    row.fileName = fileNameOf(row, optRank.checked);
    patchRow(row);
  }
});

// ---- states to look at ----------------------------------------------------------------------

const PRESETS: ReadonlyArray<readonly [string, string]> = [
  ["빈 화면", "idle"],
  ["미리보기", "preview"],
  ["다운로드 중", "running"],
  ["완료", "finished"],
  ["오류", "error"],
  ["도구 이상", "tools-broken"],
];

function applyPreset(name: string) {
  reset();
  setTools(name === "tools-broken" ? TOOLS_BROKEN : TOOLS_OK);
  switch (name) {
    case "preview":
      input.value = SAMPLE_URL;
      resolveNow();
      break;
    case "running":
      input.value = SAMPLE_URL;
      resolveNow();
      runJob(state.rows, false);
      break;
    case "finished":
      input.value = SAMPLE_URL;
      resolveNow();
      for (const row of state.rows) {
        if (row.available) row.status = outcomeFor(row.rank, false);
        patchRow(row);
      }
      state.phase = "finished";
      state.jobStatus = "COMPLETED";
      state.elapsedSeconds = 185;
      break;
    case "error":
      state.error = "재생목록을 찾을 수 없거나 비공개입니다. 주소를 확인하세요.";
      break;
    default:
      break;
  }
  render();
}

for (const [label, name] of PRESETS) {
  dev.append(
    button(
      label,
      () => {
        applyPreset(name);
        history.replaceState(null, "", `#${name}`);
      },
      "small-button",
    ),
  );
}
if (new URLSearchParams(location.search).has("clean")) dev.hidden = true;
window.addEventListener("keydown", (event) => {
  if (event.key === "F2") dev.hidden = !dev.hidden;
});

// ---- start ----------------------------------------------------------------------------------

setTools(null);
render();
const initial = location.hash.slice(1);
if (PRESETS.some(([, name]) => name === initial)) {
  applyPreset(initial);
} else {
  window.setTimeout(() => setTools(TOOLS_OK), 500);
}
