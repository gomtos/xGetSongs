// Fixed pages for the check of the hidden browser window (check.ps1; the window itself is src-tauri/src/browser_window.rs).
//   node fixture.mjs <port> [<log file>]
// No Google and no lyrics: the page that is called "card" only has the marker (`div[data-lyricid]`) the real probe looks for.
import { appendFileSync } from "node:fs";
import { createServer } from "node:http";

const port = Number(process.argv[2]);
const logFile = process.argv[3];

/** A page that counts timer ticks (every 50 ms) and shows its card after [delay] ms. */
function page(name, delay, extra = "") {
  return `<!doctype html><html><head><meta charset="utf-8"><title>${name}</title></head><body><p id="status">loading ${name}</p>
<script>
document.getElementById("status").textContent += " cookie[" + document.cookie + "]";
window.__ticks = 0;
setInterval(() => { window.__ticks++; }, 50);
setTimeout(() => { document.body.insertAdjacentHTML("beforeend", '<div data-lyricid="1">card of ${name}</div>'); }, ${delay});
${extra}
</script></body></html>`;
}

const pages = {
  // Sets a cookie that lasts an hour: a window that keeps nothing shows no cookie on the next page.
  "/first": { cookie: "seen=1; Path=/; Max-Age=3600", body: page("first", 700) },
  "/second": { body: page("second", 1500) },
  // Tries to open another window and to download a file, the two things the hidden window refuses.
  "/popup": {
    body: page(
      "popup",
      500,
      `setTimeout(() => { window.open("/popup-child", "_blank"); }, 300);
setTimeout(() => { const link = document.createElement("a"); link.href = "/download.bin"; link.download = "xgs-check-download.bin"; document.body.append(link); link.click(); }, 600);`,
    ),
  },
  // What the popup would show: the fixture sees the request only if the window was allowed to open.
  "/popup-child": { body: page("popup-child", 100000) },
  // A page that stops answering: an endless loop, 300 ms after it loaded.
  "/hang": { body: page("hang", 100000, `setTimeout(() => { for (;;) {} }, 300);`) },
};

createServer((request, response) => {
  const path = (request.url ?? "/").split("?")[0];
  if (logFile) appendFileSync(logFile, `${path}\n`);
  if (path === "/download.bin") {
    response.writeHead(200, { "content-type": "application/octet-stream", "content-disposition": 'attachment; filename="xgs-check-download.bin"' });
    response.end(Buffer.alloc(1024, 1));
    return;
  }
  const found = pages[path];
  if (!found) {
    response.writeHead(404).end();
    return;
  }
  response.writeHead(200, { "content-type": "text/html; charset=utf-8", ...(found.cookie ? { "set-cookie": found.cookie } : {}) });
  response.end(found.body);
}).listen(port, "127.0.0.1", () => console.log(`listening on ${port}`));
