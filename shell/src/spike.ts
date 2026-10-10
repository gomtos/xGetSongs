import { invoke } from "@tauri-apps/api/core";

interface Probe {
  port: number;
  token: string;
  readyMs: number;
  toolsMs: number;
  tools: string;
}

const status = document.querySelector<HTMLParagraphElement>("#status")!;
const out = document.querySelector<HTMLPreElement>("#out")!;
const direct = document.querySelector<HTMLButtonElement>("#direct")!;
const directOut = document.querySelector<HTMLPreElement>("#direct-out")!;

const pageStart = performance.now();
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
const record = (key: string, value: string | number) => invoke("spike_record", { key, value: String(value) });

/** The sidecar takes seconds to start: ask again until Rust says it is ready. */
async function waitForSidecar(): Promise<Probe> {
  for (;;) {
    try {
      return await invoke<Probe>("sidecar_probe");
    } catch (reason) {
      if (reason !== "starting") throw reason;
      await sleep(200);
    }
  }
}

/** What a page of the web view gets when it calls the server itself: the Origin header is refused, so it must not get through. */
async function directFetch(probe: Probe): Promise<string> {
  try {
    const response = await fetch(`http://127.0.0.1:${probe.port}/tools`, {
      headers: { "X-XGS-Token": probe.token },
    });
    return `reached the server, status ${response.status}`;
  } catch (error) {
    return `blocked: ${error}`;
  }
}

async function main() {
  try {
    const probe = await waitForSidecar();
    const waitedMs = Math.round(performance.now() - pageStart);
    status.textContent = "사이드카 준비됨";
    // The token is shown for the spike's checks only (see Global Constraints in the plan).
    out.textContent = JSON.stringify(
      {
        port: probe.port,
        token: probe.token,
        readyMs: probe.readyMs,
        waitedMs,
        toolsMs: probe.toolsMs,
        tools: JSON.parse(probe.tools),
      },
      null,
      2,
    );
    await record("waitedMs", waitedMs);
    await record("toolsMs", probe.toolsMs);
    const result = await directFetch(probe);
    directOut.textContent = result;
    await record("directFetch", result);
    direct.disabled = false;
    direct.addEventListener("click", async () => {
      directOut.textContent = await directFetch(probe);
    });
  } catch (reason) {
    status.textContent = `실패: ${reason}`;
    await record("error", String(reason)).catch(() => {});
  }
}

main();
