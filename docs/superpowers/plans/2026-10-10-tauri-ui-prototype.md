# Tauri UI 프로토타입 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 사이드카 없이 Tauri 웹뷰에 지금의 xGetSongs 화면을 가짜 데이터로 다시 그려서 Compose 화면과 나란히 비교하고, 설계 §5.2의 UI 관문(U1~U4)을 판정한다.

**Architecture:** Tauri 앱 `shell/`을 스캐폴드하고, 프레임워크 없이 `vanilla-ts`로 화면을 만든다. 글꼴·테마·폼 컨트롤은 모두 시스템 것을 쓴다. 화면의 문구와 상태 표시는 `Labels.kt`와 `ui/*.kt`에서 옮긴다. 데이터는 가짜 목록 100곡과 진행률이 움직이는 가짜 다운로드 작업이고, 화면 상태는 하단의 전환 막대로 바꾼다. 서버와 사이드카는 쓰지 않는다.

**Tech Stack:** Tauri 2 (`vanilla-ts`), TypeScript, 순수 CSS, Node 24(테스트는 `node --test`로 `.ts`를 직접 실행), Rust stable(Tauri 빌드용).

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md) §5.2 (UI 관문)

## Global Constraints

- 이 계획은 UI 품질을 확인하는 것이 전부다. 서버, 사이드카, Compose 앱(`app/`)의 코드는 수정하지 않는다.
- UI 방향: **시스템 글꼴**(`"Segoe UI Variable Text", "Segoe UI", "Malgun Gothic", system-ui`), **시스템 테마**(`color-scheme: light dark`), **시스템 강조색**(`AccentColor`), **기본 폼 컨트롤**, 시맨틱 HTML. 커스텀 디자인 시스템을 만들지 않는다. 프레임워크를 쓰지 않는다.
- 화면의 문구는 Compose 화면과 같다(`Labels.kt`, `ui/*.kt`에 있는 그대로). 달라지면 비교가 공정하지 않다.
- 프로토타입은 가짜 데이터만 쓴다. 네트워크와 파일 접근이 없다.
- 시스템 설정(화면 배율, 라이트·다크 모드, Narrator)은 사용자가 직접 바꾼다. 에이전트는 바꾸지 않는다.
- 소프트웨어 설치(Rust)와 내려받기는 사용자에게 알리고 허락을 받은 뒤에 한다.
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구와 커밋 메시지는 한국어. `main`에 직접 커밋한다. 커밋 메시지 끝에는 이 세션이 정한 `Co-Authored-By` 줄을 붙인다.
- 이 계획의 TypeScript 코드(Task 2~3)는 계획을 쓰는 시점에 임시 폴더에서 `tsc`(오류 없음), `node --test`(11개 통과), Vite 개발 서버로 렌더링과 상호작용(조회, 다운로드 시작, 취소, 재시도, 상태 전환)을 확인했다. Tauri 창(WebView2)에서의 동작은 확인하지 못했다(이 PC에 Rust가 없음). 그것이 이 계획의 목적이다.

## Review Focus

이 프로토타입이 틀린 결론을 내게 만들기 쉬운 것, 가장 가능성 높은 순서:

1. **가짜 화면이 실제 화면과 달라서 비교가 불공정하다** (문구, 배치, 상태의 누락). → Task 2의 `labels.test.ts`(문구를 `Labels.kt`와 같은 값으로 고정), Task 3 Step 6의 화면 대응표 확인
2. **진행률이 갱신되는 동안 한글 조합 입력이 끊긴다.** 웹 화면은 DOM을 다시 그리면 입력란의 조합이 깨질 수 있다. → Task 3은 입력란을 다시 그리지 않고 행만 고치게 짰다(`patchRow`). Task 4 Step 3이 다운로드 중에 주소 입력란에 한글을 입력해서 확인한다
3. **라이트와 다크 어느 한쪽에서 대비가 부족하다** (오류 빨강, 경고 노랑, 진행률 막대). → Task 4 Step 5가 두 모드를 다 본다
4. **창을 좁히면 배치가 깨진다** (Compose는 체크박스 줄이 `FlowRow`로 넘어간다). → Task 3 Step 7이 700px 폭을 본다
5. **비교 캡처에 전환 막대가 찍힌다.** → Task 4 Step 2가 캡처 전에 `F2`로 막대를 숨기게 한다. 화면 배율을 바꿔 다시 비교할 때는 같은 상태를 다시 만들어야 하므로 막대의 해시(`#preview` 등)로 바로 연다

## File Structure

| 파일 | 책임 |
|---|---|
| `shell/` (새, 스캐폴더가 만든다) | Tauri 앱: `package.json`, `src-tauri/`, `tsconfig.json`, `vite.config.ts` |
| `shell/src/prototype/labels.ts` (새) | 상태 문구, 요약, 경과 시간, 순위 표시. `Labels.kt`의 포팅 |
| `shell/src/prototype/fake.ts` (새) | 가짜 목록 100곡, 파일명 규칙, 가짜 다운로드 작업(진행률), 도구 상태 |
| `shell/src/main.ts` (바꿈) | 화면 상태, 그리기, 사용자 동작, 상태 전환 막대 |
| `shell/index.html` (바꿈) | 화면의 마크업 |
| `shell/src/prototype.css` (새) | 시스템 글꼴·테마·색을 쓰는 스타일 |
| `shell/tests/labels.test.ts`, `shell/tests/fake.test.ts` (새) | `node --test`로 도는 로직 테스트(`tsconfig`의 `include: ["src"]` 밖) |
| `docs/superpowers/specs/2026-10-10-tauri-ui-prototype-report.md` (새) | U1~U4 판정 |

화면 대응표(Compose → 프로토타입):

| Compose | 프로토타입 |
|---|---|
| `ToolsPanel` (Card, 도구 상태, `yt-dlp 업데이트`, 안내문) | `#tools` (`.card`, `setTools`) |
| `InputPanel` (`OutlinedTextField` + `조회`) | `#input-form` (`<form>`, `<label>` + `<input>` + 기본 버튼; `Enter`로 조회) |
| `ErrorBanner` | `#error` (`role="alert"`) |
| `ResolveInfo` (제목 · N개) | `#info-title` |
| `OptionsPanel` | `#options` (`<fieldset>`; 실행 중에는 `disabled`) |
| `PreviewList` / `PreviewRow` / `StatusCell` | `#list` (`<ul>`), `.item` (격자: 순위 40px, 이름, 상태 220px), `<progress>` |
| `ActionBar` | `#actions` |

---

### Task 1: Rust와 Tauri 앱 만들기

**Files:**
- Create: `shell/` (스캐폴더가 만드는 Tauri 앱)
- Modify: `shell/src-tauri/tauri.conf.json`, `shell/tsconfig.json`(필요하면)

**Interfaces:**
- Consumes: 없음
- Produces: 실행 가능한 빈 Tauri 앱 `shell/`(`npm run tauri dev`로 창이 뜬다), 창 크기 1000×760과 제목 `xGetSongs`. Task 2·3이 이 폴더에 파일을 넣고, 0b 스파이크 계획서의 Task 6 Step 2가 이 `shell/`을 그대로 쓴다.

- [ ] **Step 1: Rust를 설치한다 (이미 있으면 건너뜀)**

`rustc --version`이 나오면 건너뛴다. 아니면 사용자에게 알린다: "Rust 툴체인(rustup, 약 300MB)을 `winget`으로 설치합니다. 허락하시나요?" 허락을 받으면:

```powershell
winget install --id Rustlang.Rustup -e
```

새 PowerShell을 열어 확인한다:

```powershell
rustup default stable-x86_64-pc-windows-msvc
rustc --version
cargo --version
```

Expected: 두 버전이 나온다. (Visual Studio 2022의 C++ 도구와 WebView2 런타임은 이 PC에 이미 있다.)

- [ ] **Step 2: Tauri 앱을 만든다**

저장소 루트에서 (프롬프트에는 식별자 `com.xgetsongs.shell`, 패키지 매니저 `npm`으로 답한다):

```powershell
npm create tauri-app@latest shell -- --template vanilla-ts
cd shell
npm install
cd ..
git status --short
```

Expected: `shell/` 아래 `package.json`, `index.html`, `src/main.ts`, `src-tauri/Cargo.toml`, `src-tauri/tauri.conf.json`이 생긴다. 스캐폴더가 만든 `shell/.gitignore`와 `shell/src-tauri/.gitignore`가 `node_modules`, `dist`, `target`을 무시하는지 `git status --short`로 확인한다. 이 폴더들이 `??`로 나오면 루트 `.gitignore`에 `/shell/node_modules/`, `/shell/dist/`, `/shell/src-tauri/target/`을 추가한다.

- [ ] **Step 3: 창을 Compose 앱과 같은 크기로 맞춘다**

Compose 앱은 1000×760 창으로 뜬다(`Main.kt`). `shell/src-tauri/tauri.conf.json`의 `app.windows[0]`을 다음 값으로 바꾼다(다른 키는 그대로 둔다):

```json
{
  "title": "xGetSongs",
  "width": 1000,
  "height": 760
}
```

- [ ] **Step 4: TypeScript 설정을 확인한다**

`shell/tsconfig.json`에 `"allowImportingTsExtensions": true`와 `"include": ["src"]`가 있는지 본다(스캐폴더의 `vanilla-ts` 템플릿에 보통 있다). `allowImportingTsExtensions`가 없으면 `compilerOptions`에 추가한다. 이 계획의 코드는 `import ... from "./labels.ts"`처럼 확장자를 붙여서 가져온다. 그래야 Node가 같은 파일을 테스트에서 직접 실행할 수 있다.

- [ ] **Step 5: 빈 앱이 뜨는지 확인한다**

```powershell
cd shell
npm run tauri dev
```

Expected: 첫 실행은 Tauri 의존성을 컴파일하느라 5~15분 걸린다. 끝나면 제목 `xGetSongs`, 1000×760 창이 뜬다(내용은 템플릿 화면). 창을 닫고 터미널에서 `Ctrl+C`로 끝낸다. Rust 컴파일 오류가 나면 메시지대로 고친다(0b 스파이크의 첫 컴파일 검증을 겸한다).

- [ ] **Step 6: Commit**

```bash
git add shell .gitignore
git commit -m "feat(shell): Tauri 앱을 스캐폴드하고 창을 Compose 앱과 같은 크기로 맞춘다"
```

---

### Task 2: 화면 문구와 가짜 데이터 (TypeScript, TDD)

**Files:**
- Create: `shell/src/prototype/labels.ts`, `shell/src/prototype/fake.ts`
- Test: `shell/tests/labels.test.ts`, `shell/tests/fake.test.ts`

**Interfaces:**
- Consumes: 없음
- Produces (Task 3이 쓴다):
  - `labels.ts`: `type ItemStatus`(`ready | waiting | downloading{percent} | finishing | done{lyrics} | skipped{reason} | failed{message}`), `type JobStatus = "COMPLETED" | "CANCELLED" | "FAILED"`, `UPDATE_YT_DLP_LABEL`, `FORMAT_FAILURE_HELP`, `statusLabel(status): string`, `elapsedLabel(seconds): string`, `summaryText(summary, status, elapsedSeconds): string | null`, `rankLabel(rank): string`
  - `fake.ts`: `interface Row`, `type ToolsStatus`, `TOOLS_OK`, `TOOLS_BROKEN`, `PLAYLIST_TITLE`, `SAMPLE_URL`, `buildRows(count?, includeRank?): Row[]`, `fileNameOf(row, includeRank): string`, `outcomeFor(rank, retry): ItemStatus`, `startFakeJob(rows, onChange, onEnd, options?): FakeJob`(`cancel()`)

- [ ] **Step 1: Write the failing test for the texts**

`shell/tests/labels.test.ts`:

```ts
import assert from "node:assert/strict";
import test from "node:test";
import { elapsedLabel, rankLabel, statusLabel, summaryText } from "../src/prototype/labels.ts";

test("each status has its text", () => {
  assert.equal(statusLabel({ kind: "ready" }), "준비됨");
  assert.equal(statusLabel({ kind: "waiting" }), "대기 중");
  assert.equal(statusLabel({ kind: "downloading", percent: null }), "다운로드 중…");
  assert.equal(statusLabel({ kind: "downloading", percent: 42.9 }), "다운로드 42%");
  assert.equal(statusLabel({ kind: "finishing" }), "마무리 중…");
  assert.equal(statusLabel({ kind: "skipped", reason: "비공개 영상" }), "건너뜀: 비공개 영상");
  assert.equal(statusLabel({ kind: "failed", message: "m4a 오디오 형식이 없습니다." }), "실패: m4a 오디오 형식이 없습니다.");
});

test("a finished row says where its lyrics came from or why it has none", () => {
  assert.equal(statusLabel({ kind: "done", lyrics: null }), "완료");
  assert.equal(statusLabel({ kind: "done", lyrics: "DESCRIPTION" }), "완료 · 가사 ✓ 설명란");
  assert.equal(statusLabel({ kind: "done", lyrics: "ONLINE" }), "완료 · 가사 ✓ 인터넷");
  assert.equal(statusLabel({ kind: "done", lyrics: "NOT_FOUND" }), "완료 · 가사 없음");
  assert.equal(statusLabel({ kind: "done", lyrics: "SEARCH_OFF" }), "완료 · 가사 없음 (검색 끔)");
});

test("the elapsed time is in whole seconds and never negative", () => {
  assert.equal(elapsedLabel(42), "42초");
  assert.equal(elapsedLabel(59.9), "59초");
  assert.equal(elapsedLabel(185), "3분 05초");
  assert.equal(elapsedLabel(3725), "1시간 02분 05초");
  assert.equal(elapsedLabel(-5), "0초");
});

test("the summary names the end of the job and the counts, with the time when it is known", () => {
  const summary = { succeeded: 8, skipped: 0, failed: 1 };
  assert.equal(summaryText(summary, "COMPLETED", 185), "완료 — 성공 8 · 건너뜀 0 · 실패 1 · 소요 3분 05초");
  assert.equal(summaryText(summary, "CANCELLED", null), "취소됨 — 성공 8 · 건너뜀 0 · 실패 1");
  assert.equal(summaryText(summary, "FAILED", 7), "중단됨 — 성공 8 · 건너뜀 0 · 실패 1 · 소요 7초");
  assert.equal(summaryText(null, "COMPLETED", 7), null);
  assert.equal(summaryText(summary, null, 7), null);
});

test("the rank has three digits", () => {
  assert.equal(rankLabel(7), "007");
  assert.equal(rankLabel(100), "100");
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd shell; node --test "tests/labels.test.ts"`
Expected: FAIL — `ERR_MODULE_NOT_FOUND` (`../src/prototype/labels.ts`가 없다).

- [ ] **Step 3: Write the texts**

`shell/src/prototype/labels.ts`:

```ts
// The texts of the screen, ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt.

export type LyricsOutcome = "DESCRIPTION" | "ONLINE" | "NOT_FOUND" | "SEARCH_OFF";

export type ItemStatus =
  | { kind: "ready" }
  | { kind: "waiting" }
  | { kind: "downloading"; percent: number | null }
  | { kind: "finishing" }
  | { kind: "done"; lyrics: LyricsOutcome | null }
  | { kind: "skipped"; reason: string }
  | { kind: "failed"; message: string };

export type JobStatus = "COMPLETED" | "CANCELLED" | "FAILED";

export interface JobSummary {
  succeeded: number;
  skipped: number;
  failed: number;
}

/** The label of the button that updates yt-dlp; FORMAT_FAILURE_HELP points to it by this name. */
export const UPDATE_YT_DLP_LABEL = "yt-dlp 업데이트";

export const FORMAT_FAILURE_HELP =
  "재생목록의 모든 곡이 “m4a 오디오 형식이 없습니다.”로 실패하면 영상 문제가 아니라 yt-dlp가 낡았거나 " +
  "JavaScript 런타임(Node.js, Deno)에 문제가 있는 것일 수 있습니다. 앱 위쪽의 " +
  UPDATE_YT_DLP_LABEL +
  " 버튼으로 최신 버전으로 바꾸고 Node.js 22+ 또는 Deno 2.3+가 설치돼 있는지 확인한 뒤 다시 받으세요.";

/** The text shown in an item's status cell. */
export function statusLabel(status: ItemStatus): string {
  switch (status.kind) {
    case "ready":
      return "준비됨";
    case "waiting":
      return "대기 중";
    case "downloading":
      return status.percent === null ? "다운로드 중…" : `다운로드 ${Math.trunc(status.percent)}%`;
    case "finishing":
      return "마무리 중…";
    case "done":
      return doneLabel(status.lyrics);
    case "skipped":
      return `건너뜀: ${status.reason}`;
    case "failed":
      return `실패: ${status.message}`;
  }
}

function doneLabel(lyrics: LyricsOutcome | null): string {
  switch (lyrics) {
    case null:
      return "완료";
    case "DESCRIPTION":
      return "완료 · 가사 ✓ 설명란";
    case "ONLINE":
      return "완료 · 가사 ✓ 인터넷";
    case "NOT_FOUND":
      return "완료 · 가사 없음";
    case "SEARCH_OFF":
      return "완료 · 가사 없음 (검색 끔)";
  }
}

/** `42초`, `3분 05초` or `1시간 02분 05초`; never negative. */
export function elapsedLabel(elapsedSeconds: number): string {
  const total = Math.max(0, Math.trunc(elapsedSeconds));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const seconds = total % 60;
  const two = (value: number) => String(value).padStart(2, "0");
  if (hours > 0) return `${hours}시간 ${two(minutes)}분 ${two(seconds)}초`;
  if (minutes > 0) return `${minutes}분 ${two(seconds)}초`;
  return `${seconds}초`;
}

/** The one-line result after a job ends; null while there is nothing to report. */
export function summaryText(
  summary: JobSummary | null,
  status: JobStatus | null,
  elapsedSeconds: number | null,
): string | null {
  if (summary === null || status === null) return null;
  const took = elapsedSeconds === null ? "" : ` · 소요 ${elapsedLabel(elapsedSeconds)}`;
  const counts = `성공 ${summary.succeeded} · 건너뜀 ${summary.skipped} · 실패 ${summary.failed}${took}`;
  switch (status) {
    case "COMPLETED":
      return `완료 — ${counts}`;
    case "CANCELLED":
      return `취소됨 — ${counts}`;
    case "FAILED":
      return `중단됨 — ${counts}`;
  }
}

/** The zero-padded rank shown in the list, e.g. `007`. */
export function rankLabel(rank: number): string {
  return String(rank).padStart(3, "0");
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd shell; node --test "tests/labels.test.ts"`
Expected: PASS (5 tests).

- [ ] **Step 5: Write the failing test for the fake data**

`shell/tests/fake.test.ts`:

```ts
import assert from "node:assert/strict";
import test from "node:test";
import { buildRows, fileNameOf, outcomeFor, startFakeJob, type FakeJobEnd, type Row } from "../src/prototype/fake.ts";

test("a resolved playlist has 100 rows, two of them not available", () => {
  const rows = buildRows();
  assert.equal(rows.length, 100);
  assert.deepEqual(
    rows.filter((row) => !row.available).map((row) => row.rank),
    [7, 42],
  );
  assert.deepEqual(rows[6].status, { kind: "skipped", reason: "비공개 영상" });
  assert.deepEqual(rows[41].status, { kind: "skipped", reason: "삭제된 영상" });
  assert.ok(rows.some((row) => row.lowConfidence), "some artists are guessed");
  assert.ok(rows.every((row) => row.status.kind === "ready" || row.status.kind === "skipped"));
});

test("the file name follows the rank option", () => {
  const row = { rank: 1, artist: "소연 (SOYEON)", track: "퇴사할게여 (Narr. 기안84)" };
  assert.equal(fileNameOf(row, true), "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
  assert.equal(fileNameOf(row, false), "소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
  assert.equal(buildRows(3, false)[0].fileName, "소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
});

test("ranks 13 and 58 fail, unless it is a retry", () => {
  assert.deepEqual(outcomeFor(13, false), { kind: "failed", message: "m4a 오디오 형식이 없습니다." });
  assert.equal(outcomeFor(58, false).kind, "failed");
  assert.equal(outcomeFor(13, true).kind, "done");
  assert.equal(outcomeFor(2, false).kind, "done");
});

/** Runs a fake job to its end with a fast clock. */
function run(rows: Row[], options: { retry?: boolean } = {}) {
  return new Promise<{ end: FakeJobEnd; changes: number }>((resolve) => {
    let changes = 0;
    startFakeJob(
      rows,
      (changed) => {
        changes += changed.length;
      },
      (end) => resolve({ end, changes }),
      { tickMs: 1, speed: 10, ...options },
    );
  });
}

test("a fake job ends every available row as done or failed and skips the others", async () => {
  const rows = buildRows(60);
  const { end, changes } = await run(rows);
  assert.equal(end, "COMPLETED");
  assert.ok(changes > 0);
  for (const row of rows) {
    if (!row.available) assert.equal(row.status.kind, "skipped");
    else assert.ok(row.status.kind === "done" || row.status.kind === "failed", `${row.rank}: ${row.status.kind}`);
  }
  assert.deepEqual(
    rows.filter((row) => row.status.kind === "failed").map((row) => row.rank),
    [13, 58],
  );
});

test("a retry runs only the rows it is given and they succeed", async () => {
  const rows = buildRows(60);
  await run(rows);
  const failed = rows.filter((row) => row.status.kind === "failed");
  const others = rows.filter((row) => row.status.kind !== "failed").map((row) => row.status);

  const { end } = await run(failed, { retry: true });

  assert.equal(end, "COMPLETED");
  assert.ok(failed.every((row) => row.status.kind === "done"));
  assert.deepEqual(
    rows.filter((row) => !failed.includes(row)).map((row) => row.status),
    others,
    "the other rows are untouched",
  );
});

test("cancelling puts unfinished rows back to ready and keeps the finished ones", async () => {
  const rows = buildRows(60);
  const result = await new Promise<FakeJobEnd>((resolve) => {
    const job = startFakeJob(rows, () => {}, resolve, { tickMs: 1, speed: 10 });
    setTimeout(() => job.cancel(), 30);
  });
  assert.equal(result, "CANCELLED");
  for (const row of rows) {
    assert.ok(["ready", "skipped", "done", "failed"].includes(row.status.kind), `${row.rank}: ${row.status.kind}`);
  }
  assert.ok(rows.some((row) => row.status.kind === "ready" && row.available), "something was still unfinished");
});
```

- [ ] **Step 6: Run test to verify it fails**

Run: `cd shell; node --test "tests/fake.test.ts"`
Expected: FAIL — `ERR_MODULE_NOT_FOUND` (`../src/prototype/fake.ts`가 없다).

- [ ] **Step 7: Write the fake data**

`shell/src/prototype/fake.ts`:

```ts
// Fake data for the prototype: a resolved playlist of 100 songs and a download that moves like the real one.

import { rankLabel, type ItemStatus, type LyricsOutcome } from "./labels.ts";

export interface Row {
  rank: number;
  artist: string;
  track: string;
  /** The name shown in the list; it follows the "파일명에 순번 포함" option. */
  fileName: string;
  available: boolean;
  /** True when the artist was guessed from the channel name (the list shows a warning). */
  lowConfidence: boolean;
  status: ItemStatus;
}

export interface ToolInfo {
  found: boolean;
  version?: string;
}

export interface ToolsStatus {
  ytDlp: ToolInfo;
  ffmpeg: ToolInfo;
  jsRuntime: ToolInfo;
}

export const PLAYLIST_TITLE = "Melon Daily Top 100";
export const SAMPLE_URL = "https://www.youtube.com/playlist?list=PLabcdefghijklmnop";

export const TOOLS_OK: ToolsStatus = {
  ytDlp: { found: true, version: "2026.10.01" },
  ffmpeg: { found: true, version: "8.1" },
  jsRuntime: { found: true, version: "24.11.1" },
};

export const TOOLS_BROKEN: ToolsStatus = {
  ytDlp: { found: true, version: "2026.10.01" },
  ffmpeg: { found: false },
  jsRuntime: { found: false },
};

// Titles from the design document's fixtures: Korean and English, parentheses, a long one that needs two lines.
const SAMPLE: ReadonlyArray<readonly [string, string]> = [
  ["소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)"],
  ["RESCENE (리센느)", "LOVE ATTACK"],
  ["아이오아이 (I.O.I)", "갑자기 (Suddenly)"],
  ["ATEEZ(에이티즈)", "BAD"],
  ["태연 (TAEYEON)", "만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"],
  ["WOODZ", "Drowning"],
  ["진영&최유리", "생각을 멈추다 보면"],
  ["성시경", "너의 모든 순간"],
  ["Girls' Generation-HRS 소녀시대-효리수", "Skibidi"],
  ["볼빨간사춘기 BOL4", "여름아 부탁해"],
  ["AKMU", "어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)"],
];

export function fileNameOf(row: Pick<Row, "rank" | "artist" | "track">, includeRank: boolean): string {
  return `${includeRank ? `${rankLabel(row.rank)} ` : ""}${row.artist} - ${row.track}.m4a`;
}

/** A resolved playlist: two videos that are not available (ranks 7 and 42) and every 17th with a guessed artist. */
export function buildRows(count = 100, includeRank = true): Row[] {
  return Array.from({ length: count }, (_, index): Row => {
    const rank = index + 1;
    if (rank === 7 || rank === 42) {
      return {
        rank,
        artist: "",
        track: "",
        fileName: rank === 7 ? "[Private video]" : "[Deleted video]",
        available: false,
        lowConfidence: false,
        status: { kind: "skipped", reason: rank === 7 ? "비공개 영상" : "삭제된 영상" },
      };
    }
    const [artist, track] = SAMPLE[index % SAMPLE.length];
    const row = { rank, artist, track };
    return {
      ...row,
      fileName: fileNameOf(row, includeRank),
      available: true,
      lowConfidence: rank % 17 === 0,
      status: { kind: "ready" },
    };
  });
}

const LYRICS: LyricsOutcome[] = ["DESCRIPTION", "ONLINE", "NOT_FOUND", "ONLINE"];

/** How a fake download ends: ranks 13 and 58 fail (unless it is a retry), the others get lyrics from varying places. */
export function outcomeFor(rank: number, retry: boolean): ItemStatus {
  if (!retry && rank === 13) return { kind: "failed", message: "m4a 오디오 형식이 없습니다." };
  if (!retry && rank === 58) return { kind: "failed", message: "태그를 쓰지 못했습니다. (지원하지 않는 m4a 구조)" };
  return { kind: "done", lyrics: LYRICS[rank % LYRICS.length] };
}

export type FakeJobEnd = "COMPLETED" | "CANCELLED";

export interface FakeJob {
  cancel(): void;
}

export interface FakeJobOptions {
  /** How many songs run at once. */
  concurrency?: number;
  tickMs?: number;
  /** 1 makes a song take 5 to 25 seconds at the default tick, long enough to look at the screen; the tests use more. */
  speed?: number;
  /** True for "실패 항목 재시도": the songs that failed before succeed now. */
  retry?: boolean;
}

/**
 * Runs the available [rows] like the real job: [concurrency] at a time, each one's progress moving every tick (10 times a
 * second by default), then a short "마무리 중", then its outcome. [onChange] gets the rows that changed in a tick.
 */
export function startFakeJob(
  rows: Row[],
  onChange: (changed: Row[]) => void,
  onEnd: (end: FakeJobEnd) => void,
  { concurrency = 8, tickMs = 100, speed = 1, retry = false }: FakeJobOptions = {},
): FakeJob {
  const queue = rows.filter((row) => row.available);
  for (const row of queue) row.status = { kind: "waiting" };
  const active = new Map<Row, { percent: number; step: number; finishingTicks: number }>();
  let next = 0;
  let ended = false;

  const end = (how: FakeJobEnd) => {
    ended = true;
    clearInterval(timer);
    onEnd(how);
  };

  const timer = setInterval(() => {
    const changed: Row[] = [];
    while (active.size < concurrency && next < queue.length) {
      const row = queue[next++];
      active.set(row, { percent: 0, step: (0.4 + Math.random() * 1.6) * speed, finishingTicks: 0 });
      row.status = { kind: "downloading", percent: 0 };
      changed.push(row);
    }
    for (const [row, work] of active) {
      if (work.percent < 100) {
        work.percent = Math.min(100, work.percent + work.step);
        row.status = work.percent >= 100 ? { kind: "finishing" } : { kind: "downloading", percent: work.percent };
        changed.push(row);
      } else {
        work.finishingTicks += 1;
        if (work.finishingTicks >= 6) {
          row.status = outcomeFor(row.rank, retry);
          active.delete(row);
          changed.push(row);
        }
      }
    }
    if (changed.length > 0) onChange(changed);
    if (active.size === 0 && next >= queue.length) end("COMPLETED");
  }, tickMs);

  return {
    cancel() {
      if (ended) return;
      const changed: Row[] = [];
      for (const row of queue) {
        const kind = row.status.kind;
        if (kind === "waiting" || kind === "downloading" || kind === "finishing") {
          row.status = { kind: "ready" };
          changed.push(row);
        }
      }
      onChange(changed);
      end("CANCELLED");
    },
  };
}
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `cd shell; node --test "tests/*.test.ts"`
Expected: PASS (11 tests: labels 5, fake 6). 가짜 작업 테스트 두 개는 각각 2초쯤 걸린다.

- [ ] **Step 9: Commit**

```bash
git add shell/src/prototype shell/tests
git commit -m "feat(shell): 프로토타입의 화면 문구와 가짜 목록·가짜 다운로드 작업을 만든다"
```

---

### Task 3: 화면 만들기

**Files:**
- Modify: `shell/index.html`, `shell/src/main.ts`
- Create: `shell/src/prototype.css`
- Delete: 스캐폴더가 만든 `shell/src/styles.css`, `shell/src/assets/`(있으면. 쓰지 않는다)

**Interfaces:**
- Consumes: Task 2의 `labels.ts`와 `fake.ts`의 모든 export (위 Interfaces 목록)
- Produces: 화면 `#tools`, `#input-form`, `#error`, `#resolved`, `#actions`와 하단의 전환 막대 `#dev`. 막대의 버튼은 `빈 화면`, `미리보기`, `다운로드 중`, `완료`, `오류`, `도구 이상`이고, `F2`로 숨기며 주소에 `?clean`이 있으면 처음부터 숨긴다. 주소의 해시 `#idle`, `#preview`, `#running`, `#finished`, `#error`, `#tools-broken`으로 그 상태를 바로 연다. Task 4가 이 화면에서 U1~U4를 확인한다.

이 단계는 시각 작업이라 단위 테스트가 없다. 검증은 Step 5~7의 `tsc`, 화면 확인, 대응표 확인이다. 로직(문구, 파일명 규칙, 가짜 작업)은 Task 2의 테스트가 이미 지킨다.

- [ ] **Step 1: 마크업**

`shell/index.html` 전체를 다음으로 바꾼다:

```html
<!doctype html>
<html lang="ko">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <meta name="color-scheme" content="light dark" />
    <title>xGetSongs</title>
    <script type="module" src="/src/main.ts" defer></script>
  </head>
  <body>
    <div id="app">
      <section id="tools" class="card" aria-label="외부 도구"></section>

      <form id="input-form" class="row-flex align-end">
        <label class="field grow">
          <span class="field-label">재생목록 ID 또는 영상 주소</span>
          <input id="input" type="text" autocomplete="off" spellcheck="false" />
        </label>
        <button id="resolve" type="submit" class="primary" disabled>조회</button>
      </form>

      <div id="error" class="error-banner" role="alert" hidden>
        <span id="error-text" class="grow"></span>
        <button id="error-close" type="button">닫기</button>
      </div>

      <section id="resolved" class="resolved" hidden>
        <h2 id="info-title"></h2>

        <fieldset id="options" class="options">
          <div class="row-flex align-end">
            <label class="field grow">
              <span class="field-label">출력 폴더</span>
              <input id="output-dir" type="text" value="C:\Users\사용자\Music\xGetSongs" spellcheck="false" />
            </label>
            <button id="pick-folder" type="button">폴더 선택</button>
          </div>
          <div class="row-flex">
            <span id="destination" class="small grow ellipsis"></span>
            <button id="open-folder" type="button" class="small-button">탐색기에서 보기</button>
          </div>
          <label class="field">
            <span class="field-label">앨범명 (폴더명)</span>
            <input id="album" type="text" />
            <span class="hint">저장 폴더 이름과 앨범 태그가 모두 이 이름입니다. 비우면 재생목록 제목(영상 1개는 폴더 없이 영상 자체의 앨범)</span>
          </label>
          <div class="checks">
            <label class="check"><input id="opt-overwrite" type="checkbox" /> 기존 파일 덮어쓰기</label>
            <label class="check"><input id="opt-rank" type="checkbox" checked /> 파일명에 순번 포함</label>
            <label class="check"><input id="opt-lyrics" type="checkbox" checked /> 가사가 없으면 인터넷에서 검색</label>
          </div>
          <div class="row-flex">
            <span class="concurrency-label">동시 다운로드</span>
            <span>코어 수의 70% (자동)</span>
          </div>
        </fieldset>

        <ul id="list" class="list" aria-label="곡 목록"></ul>
        <div id="actions" class="row-flex actions"></div>
      </section>
    </div>

    <nav id="dev" class="dev" aria-label="프로토타입 상태 전환"></nav>
  </body>
</html>
```

- [ ] **Step 2: 스타일**

`shell/src/prototype.css`:

```css
/*
  The look is the operating system's: its fonts, its light or dark setting (color-scheme), its accent colour, and the
  browser's own form controls. There is no design system here on purpose.
*/
:root {
  color-scheme: light dark;
  --muted: color-mix(in srgb, CanvasText 62%, transparent);
  --line: color-mix(in srgb, CanvasText 16%, transparent);
  --card: color-mix(in srgb, CanvasText 4%, Canvas);
  --error: #c42b1c;
  --warn: #9d5d00;
}

@media (prefers-color-scheme: dark) {
  :root {
    --error: #ff99a4;
    --warn: #fce100;
  }
}

* {
  box-sizing: border-box;
}

[hidden] {
  display: none !important;
}

html,
body {
  height: 100%;
}

body {
  margin: 0;
  background: Canvas;
  color: CanvasText;
  font: 14px/1.45 "Segoe UI Variable Text", "Segoe UI", "Malgun Gothic", system-ui, sans-serif;
}

#app {
  display: flex;
  flex-direction: column;
  gap: 12px;
  height: 100%;
  padding: 16px;
}

h2 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
}

p {
  margin: 0;
}

/* ---- layout helpers ---- */
.row-flex {
  display: flex;
  align-items: center;
  gap: 8px;
}

/* A text box with a label above it next to a button: the button lines up with the box, not with the label and box together. */
.align-end {
  align-items: flex-end;
}

.grow {
  flex: 1;
  min-width: 0;
}

.small {
  font-size: 12px;
}

.muted,
.hint {
  color: var(--muted);
}

.hint {
  font-size: 12px;
}

.bad {
  color: var(--error);
}

.ellipsis {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

/* ---- controls ---- */
button,
input[type="text"] {
  font: inherit;
}

button {
  /* The same height as a text box (6px padding and a 1px border), so a button next to one lines up with it. */
  padding: 6px 14px;
  border: 1px solid var(--line);
  border-radius: 4px;
  background: ButtonFace;
  color: ButtonText;
  white-space: nowrap;
}

button:hover:not(:disabled) {
  background: color-mix(in srgb, ButtonFace 88%, CanvasText);
}

button:disabled {
  opacity: 0.5;
}

button.primary {
  border-color: transparent;
  background: Highlight;
  background: AccentColor;
  color: HighlightText;
  color: AccentColorText;
}

button.primary:hover:not(:disabled) {
  filter: brightness(1.1);
}

button.small-button {
  padding: 2px 12px;
  font-size: 12px;
}

:focus-visible {
  outline: 2px solid Highlight;
  outline: 2px solid AccentColor;
  outline-offset: 2px;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.field-label {
  font-size: 12px;
  color: var(--muted);
}

input[type="text"] {
  width: 100%;
  padding: 6px 8px;
  border: 1px solid var(--line);
  border-bottom-color: color-mix(in srgb, CanvasText 45%, transparent);
  border-radius: 4px;
  background: Field;
  color: FieldText;
}

input[type="text"]:focus {
  outline: none;
  padding-bottom: 5px;
  border-bottom: 2px solid Highlight;
  border-bottom: 2px solid AccentColor;
}

input[type="text"]:disabled {
  opacity: 0.6;
}

input[type="checkbox"] {
  accent-color: AccentColor;
}

fieldset {
  min-width: 0;
  margin: 0;
  padding: 0;
  border: 0;
}

/* ---- sections ---- */
.card {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 12px;
  border: 1px solid var(--line);
  border-radius: 8px;
  background: var(--card);
}

.tool-line {
  display: flex;
  gap: 16px;
}

.error-banner {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--error);
}

.resolved {
  display: flex;
  flex: 1;
  flex-direction: column;
  gap: 12px;
  min-height: 0;
}

.options {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.checks {
  display: flex;
  flex-wrap: wrap;
  gap: 0 16px;
}

.check {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 0;
}

.concurrency-label {
  min-width: 100px;
}

/* ---- the list ---- */
.list {
  display: flex;
  flex: 1;
  flex-direction: column;
  gap: 6px;
  min-height: 0;
  margin: 0;
  padding: 0;
  overflow-y: auto;
  list-style: none;
}

.item {
  display: grid;
  grid-template-columns: 40px 1fr 220px;
  gap: 12px;
  align-items: center;
}

.rank {
  font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
}

.file,
.label {
  display: -webkit-box;
  overflow: hidden;
  -webkit-box-orient: vertical;
  -webkit-line-clamp: 2;
}

.label {
  font-size: 12px;
}

.label.failed {
  color: var(--error);
}

.item.unavailable .file {
  color: var(--muted);
}

.warn {
  display: block;
  font-size: 11px;
  color: var(--warn);
}

progress {
  width: 100%;
  height: 4px;
  margin-top: 2px;
  border: 0;
  border-radius: 2px;
  appearance: none;
  background: var(--line);
}

progress::-webkit-progress-bar {
  border-radius: 2px;
  background: var(--line);
}

progress::-webkit-progress-value {
  border-radius: 2px;
  background: Highlight;
  background: AccentColor;
}

.actions .summary {
  margin-right: 4px;
}

/* ---- the bar that switches the prototype between states (F2 hides it) ---- */
.dev {
  position: fixed;
  right: 8px;
  bottom: 8px;
  display: flex;
  gap: 4px;
  padding: 6px;
  border: 1px dashed var(--line);
  border-radius: 6px;
  background: Canvas;
  opacity: 0.92;
}
```

- [ ] **Step 3: 화면 로직**

`shell/src/main.ts` 전체를 다음으로 바꾼다:

```ts
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
```

스캐폴더가 만든 `shell/src/styles.css`와 `shell/src/assets/`가 있으면 지운다(`main.ts`가 더는 가져오지 않는다).

- [ ] **Step 4: 타입 검사와 빌드**

Run: `cd shell; npm run build`
Expected: `tsc`가 오류 없이 끝나고 `vite build`가 `dist/`를 만든다. `noUnusedLocals` 오류가 나면 안 쓰는 import나 변수를 지운다.

- [ ] **Step 5: 브라우저에서 한 번 본다**

```powershell
cd shell
npm run dev
```

`http://localhost:1420`(Tauri 템플릿의 기본 포트. 터미널에 나온 주소를 쓴다)을 연다. Expected: 빈 화면(도구 카드와 입력란), 하단 오른쪽에 전환 막대. 입력란에 `PLabc`를 쓰고 `Enter` → 0.6초 뒤 목록 100곡과 옵션이 나타난다. `다운로드 시작` → 8곡의 진행률 막대가 움직이고 옵션과 `yt-dlp 업데이트`가 비활성이 된다. `취소` → `취소됨 — 성공 0 · 건너뜀 0 · 실패 0 · 소요 N초`가 나오고 행이 `준비됨`으로 돌아간다. `Ctrl+C`로 서버를 끝낸다.

- [ ] **Step 6: 화면 대응표를 확인한다**

Compose 화면의 문구가 프로토타입에 빠짐없이 있는지 본다: `재생목록 ID 또는 영상 주소`, `조회`, `출력 폴더`, `폴더 선택`, `저장 위치: …`, `탐색기에서 보기`, `앨범명 (폴더명)`과 설명문, `기존 파일 덮어쓰기`, `파일명에 순번 포함`, `가사가 없으면 인터넷에서 검색`, `동시 다운로드`, `코어 수의 70% (자동)`, `다운로드 시작`, `새로 시작`, `취소`, `실패 항목 재시도`, `다시 다운로드`, `⚠ 가수명을 채널명에서 추정했습니다`. (`순위 번호` 입력란은 영상 1개일 때만 나오는 것이라 프로토타입에 넣지 않았다.) 빠진 것이 있으면 추가한다.

- [ ] **Step 7: 좁은 창을 본다**

개발자 도구(`F12`)의 기기 모드나 창 폭을 줄여서 폭 700px에서 다음을 확인한다: 체크박스 세 개가 줄바꿈되고(`.checks`의 `flex-wrap`), 목록의 상태 칸(220px)과 이름이 겹치지 않고, 버튼이 잘리지 않는다. 문제가 있으면 `prototype.css`를 고친다.

- [ ] **Step 8: Tauri 창에서 연다**

```powershell
cd shell
npm run tauri dev
```

Expected: 1000×760 창에 같은 화면이 뜨고 Step 5의 흐름이 같게 동작한다. 창을 닫고 `Ctrl+C`로 끝낸다.

- [ ] **Step 9: Commit**

```bash
git add shell
git commit -m "feat(shell): 현재 화면을 시스템 글꼴·테마로 다시 그린 UI 프로토타입을 만든다"
```

---

### Task 4: U1~U4 판정과 보고서

**Files:**
- Create: `docs/superpowers/specs/2026-10-10-tauri-ui-prototype-report.md`

**Interfaces:**
- Consumes: Task 3의 프로토타입(`npm run tauri dev`), 기존 Compose 앱(`.\run.bat`)
- Produces: 설계 §5.2의 U1~U4 판정과 §5.1의 목록 갱신 한도 확인. 0b 스파이크를 시작할지의 근거.

이 작업은 사용자와 함께 한다. 눈으로 보는 판단(U1), 시스템 설정 변경(화면 배율, 다크 모드, Narrator), 직접 입력해 보는 확인(U2)은 사용자가 한다. 에이전트는 환경을 준비하고, 같은 절차를 두 앱에 적용하도록 안내하고, 결과를 기록한다.

- [ ] **Step 1: 두 앱을 준비한다**

터미널 두 개를 연다. 하나에서 `cd shell; npm run tauri dev`(프로토타입), 다른 하나에서 `.\run.bat`(Compose 앱, 첫 실행은 빌드로 1~2분). 두 창을 나란히 놓는다. 캡처 저장 폴더를 만든다(저장소 밖):

```powershell
New-Item -ItemType Directory -Force "$env:TEMP\xgs-ui-compare" | Out-Null
```

- [ ] **Step 2: 같은 상태를 나란히 캡처한다 (U1의 자료)**

캡처는 `Win+Shift+S`로 하고 `%TEMP%\xgs-ui-compare\`에 `compose-<상태>.png`, `tauri-<상태>.png`로 저장한다. 프로토타입 캡처 전에 `F2`로 전환 막대를 숨긴다.

| 상태 | Compose | 프로토타입 |
|---|---|---|
| 빈 화면 | 앱을 막 켠 상태 | 막대의 `빈 화면` |
| 미리보기 | 작은 공개 재생목록(10곡 안팎)을 조회 | `미리보기` |
| 다운로드 중 | (선택) 같은 재생목록을 임시 폴더로 받는 중 | `다운로드 중` |
| 오류 | 잘못된 주소로 조회 | `오류` |

- [ ] **Step 3: U2 — 한글 입력을 두 앱에서 같은 절차로 해 본다**

메모장에 `한글 입력 테스트 가나다라 ABC 123`을 써 두고, 두 앱의 **주소 입력란**과 **출력 폴더 입력란**에서 다음을 하고 `통과/어색함/실패`로 적는다:

| 항목 | 방법 |
|---|---|
| 조합 중인 글자 | `한글`을 천천히 쳐서 조합 중 글자(밑줄, 후보)와 커서 위치가 자연스러운가 |
| 한/영 전환 | `ABC`를 치고 한/영 키로 전환해 `가나다` 입력 |
| 붙여넣기 | 메모장 문장을 `Ctrl+V` |
| 선택 | 마우스 드래그, 단어 더블클릭, 한 줄 삼중 클릭, `Ctrl+A` |
| 복사·잘라내기 | `Ctrl+C`, `Ctrl+X` 뒤 다시 붙여넣기 |
| 우클릭 메뉴 | 우클릭에 잘라내기·복사·붙여넣기 메뉴가 뜨는가 |
| 목록의 글자 | 목록의 파일명 한 줄을 마우스로 선택해 복사할 수 있는가 (Compose의 `Text`는 기본이 선택 불가) |
| **갱신 중 입력** | 프로토타입을 `다운로드 중`으로 두고 진행률이 움직이는 동안 주소 입력란에 한글을 쳐서 조합이 끊기지 않는가 (Review Focus 2) |

- [ ] **Step 4: U3 — 글꼴**

Step 2의 캡처 중 같은 위치(입력란 라벨과 목록의 처음 다섯 행)를 확대해서 나란히 본다. 보는 것: 한글 획의 선명도, 가는 글자의 뭉개짐, 영문·숫자의 간격, 순위 숫자(고정폭)의 모양. 글꼴이 맑은 고딕과 Segoe UI인지 개발자 도구(프로토타입에서 `F12` → 계산된 글꼴)로 확인한다. 사용자가 Windows 화면 배율(설정 > 시스템 > 디스플레이)을 바꿔 볼 수 있으면 125%와 150%에서도 같은 위치를 비교한다. 배율을 바꾼 뒤에는 두 앱을 다시 열어야 할 수 있다. `통과/어색함/실패`로 적는다.

- [ ] **Step 5: U4 — 네이티브 느낌**

| 항목 | 방법 |
|---|---|
| 시스템 테마 | 사용자가 Windows 설정 > 개인 설정 > 색의 모드를 라이트와 다크로 바꾼다. 두 앱이 따라가는가 (Compose 쪽은 `MaterialTheme`의 기본 색을 쓰므로 라이트에 고정일 것으로 예상하지만 확인한다). 두 모드에서 오류 빨강, 경고 노랑, 진행률 막대의 대비가 충분한가 (Review Focus 3) |
| 강조색 | 사용자가 설정에서 강조색을 바꾸면 프로토타입의 `조회`·`다운로드 시작` 버튼과 체크박스, 진행률 막대가 따라가는가 |
| `Tab` 순서 | `Tab`으로 도구 → 주소 → 조회 → 옵션 → 목록 → 액션 순서로 자연스럽게 가는가, 포커스 표시가 보이는가 |
| `Enter` | 주소 입력 뒤 `Enter`로 조회되는가 |
| 키보드만 | 마우스 없이 `조회` → `다운로드 시작`까지 가능한가 (다운로드 시작 뒤 `취소` 버튼에 포커스가 남는가) |
| 낭독기 | `Win+Ctrl+Enter`로 Narrator를 켜고 입력란, 버튼, 체크박스, 진행률의 이름을 읽는가 (끌 때도 `Win+Ctrl+Enter`) |
| 창 | 두 앱에서 창 크기 조절, 최대화, 스냅(`Win+←`)이 정상인가 |

각 항목을 두 앱에 대해 `통과/어색함/실패`로 적는다.

- [ ] **Step 6: 목록 갱신 한도 (악화 한도)**

프로토타입을 `다운로드 중`으로 두고(8곡이 초당 10회 갱신) 목록을 마우스 휠로 빠르게 스크롤하고, 스크롤하면서 주소 입력란에 타이핑한다. 끊김이 눈에 띄는가 `없음/있음`으로 적는다. 있으면 개발자 도구의 성능 탭에서 긴 프레임을 본다.

- [ ] **Step 7: U1 — 사용자의 판단**

사용자에게 Step 2의 캡처 쌍을 보여 주고 묻는다: "같은 상태의 두 화면을 보고 프로토타입이 Compose보다 나은가요?" 상태별(빈 화면, 미리보기, 다운로드 중, 오류)로 `낫다/비슷하다/못하다`와 한 줄 이유를 받는다. 이 값이 U1이다.

- [ ] **Step 8: 보고서를 쓴다**

`docs/superpowers/specs/2026-10-10-tauri-ui-prototype-report.md`에 실제 결과로 채워서 쓴다. 구조는 다음과 같다(칸은 위 단계의 결과를 그대로 적는다):

```markdown
# Tauri UI 프로토타입 보고서

작성일: 2026-10-10 (확인한 날로 고친다)
설계: [2026-10-10-tauri-kotlin-sidecar-design.md](2026-10-10-tauri-kotlin-sidecar-design.md) §5.2
계획: [2026-10-10-tauri-ui-prototype.md](../plans/2026-10-10-tauri-ui-prototype.md)

환경: Windows 버전, 화면 배율, WebView2 버전(`shell`의 설정이나 `edge://version`), 라이트/다크 설정

## UI 관문

| 번호 | 조건 | 결과 | 근거 |
|---|---|---|---|
| U1 | 사용자가 보고 "Compose보다 낫다" | 통과/실패 | 상태별 판단과 이유 |
| U2 | 한글 입력 | 통과/실패 | 아래 표 |
| U3 | 글꼴 | 통과/실패 | 아래 표 |
| U4 | 네이티브 느낌 | 통과/실패 | 아래 표 |

### U2 한글 입력

| 항목 | Compose | 프로토타입 |
|---|---|---|
| 조합 중인 글자 | | |
| 한/영 전환 | | |
| 붙여넣기 | | |
| 선택 | | |
| 복사·잘라내기 | | |
| 우클릭 메뉴 | | |
| 목록의 글자 선택·복사 | | |
| 갱신 중 입력 | (해당 없음) | |

### U3 글꼴

(배율별로 Compose와 프로토타입을 비교한 소견. 계산된 글꼴 이름.)

### U4 네이티브 느낌

| 항목 | Compose | 프로토타입 |
|---|---|---|
| 시스템 테마(라이트/다크) | | |
| 강조색 | | |
| Tab 순서 | | |
| Enter로 조회 | | |
| 키보드만으로 조회→다운로드 | | |
| 낭독기 | | |
| 창 조작 | | |

## 악화 한도

| 항목 | 결과 |
|---|---|
| 100행 목록 갱신 중 스크롤·입력이 끊기지 않음 | 없음/있음 |

## 막힌 점과 고친 것

(계획과 달랐던 점: Tauri 컴파일 오류, 템플릿 차이, 스타일 문제. 없으면 "없음".)

## 판정

(통과 / 중단 중 하나를 쓰고 이유를 적는다. U1이 "낫다"가 아니면 U2~U4가 통과해도 중단이다. 통과이면 다음은 0b 스파이크([계획서](../plans/2026-10-10-tauri-sidecar-spike.md))다. 중단이면 Compose를 유지하고, 실패한 항목(예: 한글 입력)을 Compose 안에서 고칠 수 있는지 따로 검토한다.)
```

- [ ] **Step 9: Commit**

```bash
git add docs/superpowers/specs/2026-10-10-tauri-ui-prototype-report.md
git commit -m "문서: Tauri UI 프로토타입의 U1~U4 판정"
```

---

## Self-Review

**1. Spec coverage** (설계 §5.2 대조)

- U1(사용자 판단) → Task 4 Step 2·7. U2(한글 입력: 조합, 붙여넣기, 선택·복사, 우클릭, 갱신 중 입력) → Task 4 Step 3. U3(글꼴, 배율) → Task 4 Step 4. U4(시스템 테마, `Tab`, `Enter`, 키보드만, Narrator) → Task 4 Step 5.
- §5.1 악화 한도의 "목록 갱신" → Task 4 Step 6. (`waitedMs`와 크기는 0b가 잰다.)
- §2 "UI 방향"(시스템 글꼴·테마·기본 폼 컨트롤, 프레임워크 없음) → Global Constraints와 `prototype.css`.
- 범위 밖: 서버·사이드카 연결(0b 이후), 영상 1개일 때의 `순위 번호` 입력란(프로토타입에 넣지 않았음을 Task 3 Step 6에 적음).

**2. Placeholder scan:** 코드 단계는 모두 실제 코드를 담았다. Task 4의 보고서 표의 빈 칸은 확인 중에 채우는 기록 칸이다.

**3. Type consistency:** `labels.ts`와 `fake.ts`의 export 이름과 시그니처는 Task 2의 Interfaces, 테스트, Task 3의 `main.ts` import가 같다(`startFakeJob(rows, onChange, onEnd, options)`, `outcomeFor(rank, retry)`, `buildRows(count, includeRank)`, `fileNameOf(row, includeRank)`, `summaryText(summary, status, elapsedSeconds)`). 화면 요소 id는 `index.html`과 `main.ts`의 `$()` 선택자가 같다.

**4. Review Focus:** 다섯 항목 모두 위 Review Focus 절의 Task와 Step에 대응한다.
