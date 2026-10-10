// A developer page, not part of the product: it runs every command of the bridge against the real sidecar and reports what the
// page saw to the file named by XGS_BRIDGE_CHECK_FILE (see the plan 2026-10-11-tauri-rust-bridge.md). Open it with
// `--config check.conf.json`.
import { Channel, invoke } from "@tauri-apps/api/core";
import { errorMessage } from "./bridge/bridge.ts";
import { bridge } from "./bridge/tauri.ts";

const status = document.querySelector<HTMLParagraphElement>("#status")!;
const out = document.querySelector<HTMLPreElement>("#out")!;

const pageStart = performance.now();
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
const lines: string[] = [];

/** Shows the result on the page and writes it to the check file. */
async function record(key: string, value: unknown) {
  const text = typeof value === "string" ? value : JSON.stringify(value);
  lines.push(`${key}=${text}`);
  out.textContent = lines.join("\n");
  await invoke("check_record", { key, value: text });
}

/** What a call rejected with, as the message the page would show. */
async function rejection(run: () => Promise<unknown>): Promise<string> {
  try {
    await run();
    return "(no error)";
  } catch (reason) {
    return errorMessage(reason);
  }
}

async function main() {
  // While the sidecar starts, the window must stay alive: count the frames, and time a command that needs no sidecar.
  let worstFrameGapMs = 0;
  let lastFrame = performance.now();
  let framing = true;
  const onFrame = (now: number) => {
    worstFrameGapMs = Math.max(worstFrameGapMs, now - lastFrame);
    lastFrame = now;
    if (framing) requestAnimationFrame(onFrame);
  };
  requestAnimationFrame(onFrame);
  let worstPingMs = 0;
  let pings = 0;
  const pinger = setInterval(async () => {
    const sent = performance.now();
    await invoke("check_ping");
    worstPingMs = Math.max(worstPingMs, performance.now() - sent);
    pings += 1;
  }, 50);

  await bridge.ready();
  const waitedMs = Math.round(performance.now() - pageStart);
  clearInterval(pinger);
  framing = false;
  await record("waitedMs", waitedMs);
  await record("worstFrameGapMs", Math.round(worstFrameGapMs));
  await record("worstPingMs", Math.round(worstPingMs));
  await record("pings", pings);
  status.textContent = "사이드카 준비됨";

  const tools = await bridge.tools();
  await record("tools", Object.keys(tools).join(","));
  await record("toolsFound", `${typeof tools.ytDlp.found},${typeof tools.ffmpeg.found},${typeof tools.jsRuntime.found}`);

  // With no yt-dlp the update has nothing to do and fails at once. (It is skipped when yt-dlp exists, because it would
  // really update it. The install call is never made here: it downloads a file.)
  await record(
    "updateWithoutYtDlp",
    tools.ytDlp.found ? "(skipped: yt-dlp is installed)" : await rejection(() => bridge.updateYtDlp()),
  );

  await record("resolveEmpty", await rejection(() => bridge.resolve("")));
  await record("resolveUnknown", await rejection(() => bridge.resolve("한글")));
  await record("startJobExpired", await rejection(() => bridge.startJob({ resolveId: "nope" })));
  await record("cancelUnknown", await rejection(() => bridge.cancelJob("nope")));
  await record("cancelBadId", await rejection(() => bridge.cancelJob("../tools")));
  let eventsOfUnknownJob = 0;
  await record(
    "watchUnknown",
    await rejection(() => bridge.watchJob("nope", () => (eventsOfUnknownJob += 1))),
  );
  await record("watchUnknownEvents", eventsOfUnknownJob);

  // The event path of the web view without a download: 5,000 numbered events, as fast as Rust can send them.
  const total = 5000;
  const channel = new Channel<{ seq: number }>();
  let received = 0;
  let outOfOrder = 0;
  let expected = 0;
  channel.onmessage = ({ seq }) => {
    if (seq !== expected) outOfOrder += 1;
    expected = seq + 1;
    received += 1;
  };
  const burstStart = performance.now();
  await invoke("check_burst", { count: total, onEvent: channel });
  const commandEndedMs = Math.round(performance.now() - burstStart);
  const receivedWhenTheCommandEnded = received;
  await sleep(500);
  await record("burstCommandMs", commandEndedMs);
  await record("burstReceivedWhenTheCommandEnded", receivedWhenTheCommandEnded);
  await record("burstReceived", received);
  await record("burstOutOfOrder", outOfOrder);

  await record("done", 1);
}

main().catch(async (reason) => {
  status.textContent = `실패: ${errorMessage(reason)}`;
  await invoke("check_record", { key: "error", value: errorMessage(reason) }).catch(() => {});
});
