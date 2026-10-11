# Tauri 프런트엔드 이식 (3단계) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **실행 뒤 메모:** 이 계획서의 코드 블록은 실행 직전의 검증본이다. 실행한 뒤 전체 브랜치 리뷰가 지적한 것(`holder`의 경쟁 둘과 시계, 순위 칸의 IME·다시 채움, 바뀌지 않은 글자의 재기록, 작업 뒤 포커스, 대조 도구와 점검 테스트의 공백)은 저장소의 코드에서 고쳤고, 그 내용은 [보고서](../specs/2026-10-11-tauri-frontend-port-report.md)의 "최종 리뷰 뒤 고친 것"에 있다. 코드는 저장소의 것이 기준이다.

**Goal:** 웹뷰의 화면이 지금 Compose 앱과 같은 동작으로 실제 서버(2단계의 브리지)를 쓴다: 조회, 미리보기, 옵션, 다운로드, 진행, 취소, 재시도, yt-dlp 설치·갱신. 앱의 화면 논리(`AppStateHolder`, `Labels`, `UiState`)와 `shared`의 순수 로직(`FilenameFormatter`, `InputClassifier`)을 TypeScript로 옮기고, 원본 Kotlin 테스트의 시나리오를 같은 이름으로 모두 통과시킨다.

**Architecture:** 프레임워크 없이 TypeScript. `src/logic/`(`shared`의 두 로직과 Kotlin의 공백 규칙), `src/state/`(`UiState`, `Labels`, `AppStateHolder`: API와 타이머와 시계를 주입받아 테스트가 즉시 돈다), `src/demo.ts`(`XgsApi`를 가짜로 구현해 `?demo`로 화면만 돌려 볼 수 있다), `src/main.ts`(holder의 상태를 그리고 사용자의 동작을 holder에 알리는 화면). 화면은 `XgsApi`만 알고, 실제로는 2단계의 `bridge`가 그 자리에 들어간다. Kotlin 원본과 같음을 세 겹으로 지킨다: 원본 테스트 1:1(같은 이름), 원본 테스트 이름이 빠지면 실패하는 점검 테스트, 서버가 쓰는 Kotlin 구현과 무작위 입력으로 대조하는 도구. 확인은 데모 서버 위의 브라우저 시나리오, 그리고 실제 셸에서 점검 페이지가 화면을 최상위 페이지로 구동하는 종단 확인이다.

**Tech Stack:** TypeScript 6, Node 24 `node --test`, Vite 8, Tauri 2(`@tauri-apps/api/path`), Java(사이드카 이미지의 JBR)로 도는 대조 도구, PowerShell 확인 스크립트. 서버와 Kotlin 변경은 없다. 새 npm 패키지도 없다.

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md) §5의 3단계, §2("`shared`의 순수 로직"과 "프런트엔드 프레임워크"는 3단계에서 정한다), §5.2의 UI 방향. 옮기는 원본은 `app/src/commonMain/kotlin/com/xgetsongs/app/state/{AppStateHolder,UiState,Labels}.kt`, `app/.../ui/*.kt`(화면), `app/.../settings/*.kt`, `shared/.../filename/FilenameFormatter.kt`, `shared/.../input/InputClassifier.kt`와 그 테스트다.

## 이 계획이 내린 결정

설계 문서가 "3단계에서 정한다"고 둔 것과, 범위의 경계다. 사용자가 계획서를 검토할 때 바꿀 수 있다.

| 결정 | 내용 | 근거 |
|---|---|---|
| `shared` 로직 재사용 방식 | **TypeScript 포팅 + 같은 케이스 표 테스트.** Kotlin/JS(IR) 타깃을 만들지 않는다 | 옮길 것은 두 파일 약 200줄이다. JS 타깃은 Gradle 설정과 툴체인과 번들 연동이 따른다. 일치는 원본 테스트 1:1, 원본 이름 점검 테스트, JVM 대조 퍼징(80,000건에서 불일치 0건을 이 계획을 쓰며 확인했다)으로 지킨다 |
| 프런트엔드 프레임워크 | **없음(vanilla TS).** 화면은 holder의 상태를 그리는 함수 하나와 이벤트 연결이다 | 화면이 작다(컴포저블 약 560줄). 프로토타입이 이미 이 방식이고 사용자가 U1~U4에서 이 모양을 골랐다 |
| 이 단계가 미루는 것 | 폴더 선택 창, "탐색기에서 보기"(두 버튼은 `hidden`), `settings.json`(이 단계의 holder는 저장하지 않는 `noSettingsStore`를 쓴다), 단일 인스턴스, 창 닫을 때 `flushSettings` | 설계 §5의 4단계 몫이다. 출력 폴더는 직접 입력한다 |
| 기본 출력 폴더 | `<홈>\Music\xGetSongs`(Compose와 같다), `@tauri-apps/api/path`의 `homeDir`과 `join`으로 | 이 계획을 쓰며 실제 셸에서 `C:\Users\<이름>\Music\xGetSongs`가 나옴을 확인했다 |
| 새로고침 | F5와 Ctrl+R을 화면이 막는다 | 서버는 작업의 이벤트를 한 번만 받게 하므로(`JobRegistry.claim`) 새로 읽은 페이지는 돌고 있는 작업을 다시 볼 수 없다(2단계 리뷰). 우클릭 메뉴의 새로고침은 입력란의 복사·붙여넣기 때문에 막지 않는다. 남는 한계는 보고서에 적는다 |
| 데모 서버 | 프로토타입의 가짜 다운로드를 `XgsApi` 구현(`src/demo.ts`)으로 올려 `index.html?demo`로 화면을 서버 없이 돌린다 | 화면을 브라우저에서 눈으로 보고 조작해 확인하려면 가짜 서버가 필요하다. 제품 동작이 아니다 |
| 종단 확인 | 점검 페이지가 같은 창에서 실제 화면(`index.html?check`)으로 넘어가 사람처럼 조작한다 | Tauri는 `Channel` 메시지를 최상위 페이지에만 보낸다. 화면을 iframe에 넣어 점검하면 진행 이벤트가 닿지 않는다(계획을 쓰며 실제로 겪었다) |

## Global Constraints

- 서버(`server/`, `engine/`, `shared/`)와 `app/`(Compose 앱)은 바꾸지 않는다. `app/`은 6단계까지 남는다. 이 단계의 변경은 `shell/`(TypeScript, Rust 한 곳)과 문서뿐이다.
- 이 마이그레이션은 지금 동작을 그대로 옮긴다(설계 §3). 기능을 더하지 않는다. 사용자에게 보이는 문구는 Compose 앱의 문구와 글자까지 같다.
- UI는 시스템 글꼴, 시스템 테마(`color-scheme`), 기본 폼 컨트롤, 시맨틱 HTML이다. 커스텀 디자인 시스템을 만들지 않는다(설계 §5.2).
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구와 커밋 메시지는 한국어. `main`에 직접 커밋한다. 커밋 메시지 끝에는 이 세션이 정한 `Co-Authored-By` 줄을 붙인다.
- Bash 도구는 명령 텍스트의 백슬래시를 한 번 더 해석하고 이 PC의 도구 셸에는 `cargo`가 PATH에 없다. 코드는 Write·Edit 도구로 쓰고 `cargo`, `npm`, `node`는 PowerShell 도구로 부른다(PowerShell 호출마다 `$env:Path += ";$env:USERPROFILE\.cargo\bin"`). PowerShell 도구는 `Remove-Item`과 `robocopy`를 한 명령에 섞으면 막으므로 따로 부른다.
- 테스트는 `node --test "tests/*.test.ts"`로 돈다(Node의 타입 제거로 `.ts`를 그대로 읽는다). `tests/`는 `tsc`의 범위 밖이다(`@types/node`가 없다). 새 패키지를 내려받지 않는다.
- 확인은 사용자의 `%APPDATA%\xGetSongs`와 `~\Music\xGetSongs`를 건드리지 않는다. 사이드카의 앱 데이터는 임시 폴더이고 yt-dlp는 앱의 것을 그 임시 폴더로 **복사**해 쓴다. 실제 다운로드는 사용자가 이 확인에 쓰라고 준 영상 한 곡과 재생목록(취소만 한다)뿐이다.
- Gradle을 부를 일이 있으면 `--no-daemon`이다(이 계획은 이미 만들어진 사이드카 이미지 `server/build/sidecar-image`를 쓴다).

## Review Focus

계획의 테스트가 직접 다루지 않아 사용자에게 먼저 문제가 될 것, 가능성이 높은 순서:

1. **한글 입력 중에 글자가 깨진다.** 진행률이 초당 수십 번 갱신되는 동안 입력란(주소, 출력 폴더, 앨범명, 순위)에 한글을 조합하고 있으면, 화면이 그 입력란의 값을 다시 써서 조합 중인 글자가 사라지거나 끊기면 안 된다. 화면은 IME가 조합 중인 입력란에 값을 쓰지 않는다(`composing`). 이것은 자동으로 시험할 수 없으므로 코드를 읽고(Task 7) 사용자가 실제 한글로 직접 확인한다(Task 9의 안내).
2. **미리보기의 파일 이름이 서버가 실제로 쓰는 이름과 다르다.** 공백으로 치는 글자(JS의 `trim()`은 U+FEFF를 지우고 U+001C~001F를 못 지운다), 서로게이트 쌍, Windows 장치 이름(`CON`, `NUL.x`), 긴 이름의 자르기. → Task 1의 `text.test.ts`, Task 3의 대조 도구(서버의 Kotlin과 무작위 입력으로 비교)
3. **진행률 갱신이 목록을 끊는다.** 한 곡이 끝날 때까지 이벤트가 초당 수십 번 오는데 매번 목록을 다시 만들면 스크롤과 포커스를 잃고 느려진다. 화면은 행이 바뀐 것만 고친다(`patchRow`). → Task 7의 브라우저 시나리오 B(8곡 진행 중 막대 8개)와 실제 셸의 점검
4. **작업 중에 화면을 새로 읽는다.** F5와 Ctrl+R은 막지만 우클릭 메뉴의 새로고침은 남는다. 이때 돌고 있는 작업은 서버에서 계속되고 화면은 처음 상태로 돌아가며 이벤트를 다시 받을 수 없다. 이 한계는 고치지 않고 보고서에 적는다(해결은 서버 쪽 재접속이 필요하다).
5. **실제 서버가 거절하거나 실패한다.** 잘못된 주소(화면이 서버에 묻기 전에 거절), 서버가 주는 오류 문구, 도구가 없는 상태, 출력 폴더 없음, 취소. → Task 4~5의 원본 시나리오(`invalidInputIsRejectedBeforeAnyRequest`, `startingWithoutAnOutputFolderIsRefused`, `aJobThatCannotStartReturnsToThePreview` 등), Task 7의 브라우저 시나리오 C·D, Task 8의 실제 셸 확인(2단계의 오류 문구 확인도 그대로 돈다)

## 이 단계의 관문

| 번호 | 조건 | 확인 |
|---|---|---|
| S1 | 옮긴 네 스위트의 원본 테스트가 같은 이름으로 모두 있고 통과한다: `FilenameFormatterTest` 39, `InputClassifierTest` 15, `LabelsTest` 35, `AppStateHolderTest` 95(포트 쪽은 추가 테스트가 더 있다). 원본에 테스트가 더해지면 점검 테스트가 실패한다 | Task 1·2·4·5 |
| S2 | 서버가 쓰는 Kotlin 구현과 TS 포트가 무작위 입력 80,000건(`format`, `folderName`, `destinationFolder`, `classify` 각 20,000)에서 같은 답을 낸다. 포트를 일부러 틀리게 하면 이 도구가 차이를 잡는다 | Task 3 |
| S3 | 데모 서버 위의 화면이 브라우저 시나리오 A~D에서 모두 `pass: true`다(조회·옵션·앨범명, 시작·취소·재실행·재시도·새로 시작, 영상 한 곡과 순위, 잘못된 주소, 설치 확인창) | Task 7 |
| S4 | 실제 셸에서 실제 사이드카와 실제 yt-dlp로 화면이 동작한다: 도구 표시, 기본 폴더 `<홈>\Music\xGetSongs`, 영상 한 곡을 조회해 순위 42를 정하고 임시 폴더에 받아 완료(`042 ….m4a`가 100KB 넘게 하나), 재생목록(101곡)을 조회해 시작하고 진행이 오면 취소해 `취소됨` 요약과 움직이는 행 0개로 끝남. 사용자의 `~\Music\xGetSongs` 파일 수는 그대로이고 남는 프로세스가 없다 | Task 8 |
| S5 | 앞 단계의 관문이 유지된다: 브리지 점검 12개 항목이 모두 기대값, 창 닫기 3초 이내와 마커 `reason=user`, 시작 직후 닫기와 강제 종료 | Task 8 |
| S6 | 마지막에 전체가 통과한다: `node --test` 전체, `tsc --noEmit`, `npm run build`, `sidecar-host`의 `cargo test`, 셸의 `cargo clippy` | Task 9 |
| 악화 한도 | 창이 뜬 뒤 사이드카가 준비될 때까지(`waitedMs`) 10초 이하(설계 §5.1과 같다) | Task 8의 점검 기록 |

## File Structure

| 파일 | 책임 |
|---|---|
| `shell/src/logic/text.ts` (새) | Kotlin의 공백 판정과 `trim`(JS의 `trim()`과 다르다) |
| `shell/src/logic/filename.ts` (새) | `FilenameFormatter`: `format`, `folderName`, `destinationFolder`, `sanitize` |
| `shell/src/logic/input.ts` (새) | `InputClassifier`: `classify`, `canonicalUrl`, 거절 문구 |
| `shell/src/state/types.ts` (새) | `UiState`, `ItemRow`, `ItemStatus`, `Phase`, `UserSettings`, `SettingsStore`, `XgsApi` |
| `shell/src/state/labels.ts` (새) | 화면의 문구와 `destinationLabel`, `albumNameText`, 순위 입력 |
| `shell/src/state/holder.ts` (새) | `AppStateHolder`: 모든 화면 논리. 타이머와 시계를 주입받는다 |
| `shell/src/demo.ts` (새) | `?demo`용 가짜 서버(`XgsApi` 구현) |
| `shell/src/main.ts` (교체) | 화면: holder의 상태를 그리고 입력을 holder에 알린다 |
| `shell/index.html`, `shell/src/app.css` (교체, `prototype.css`에서 옮김) | 화면의 뼈대와 스타일 |
| `shell/src/prototype/`, `shell/tests/fake.test.ts` (삭제) | 프로토타입의 가짜 데이터와 그 테스트 |
| `shell/tests/{text,filename,input,labels,holder,demo,kotlin-coverage}.test.ts`, `shell/tests/support/{fixtures,fakes}.ts` (새, `labels.test.ts`는 교체) | 원본 시나리오의 이식과 포트가 더한 테스트 |
| `shell/tools/differential/{Oracle.java,fuzz.mts}` (새) | 서버의 Kotlin 구현과 TS 포트를 무작위 입력으로 대조하는 개발자 도구 |
| `shell/src/check-screen.ts`, `shell/src/check.ts` (새·수정), `shell/src-tauri/src/lib.rs` (`check_env` 추가) | 개발자용 점검: 실제 화면을 실제 사이드카로 구동 |
| `docs/superpowers/specs/2026-10-11-tauri-frontend-port-report.md` (새) | 종단 확인 결과 |

---

### Task 1: `FilenameFormatter`와 Kotlin의 공백 규칙

**Files:**
- Create: `shell/src/logic/text.ts`, `shell/src/logic/filename.ts`, `shell/tests/filename.test.ts`, `shell/tests/text.test.ts`

**Interfaces:**
- Consumes: 없다(순수 함수).
- Produces (Task 2~7이 쓴다):
  - `text.ts`: `isWhitespace(character)`, `trim`, `trimStart`, `trimEnd`, `trimEndChars(text, characters)`, `trimStartChars`, `isNullOrBlank`, `isHighSurrogate`, `isLowSurrogate`
  - `filename.ts`: `MIN_RANK=1`, `MAX_RANK=999`, `MAX_BASE_LENGTH=180`, `MAX_ARTIST_LENGTH=80`, `MAX_FOLDER_LENGTH=80`, `UNTITLED_PLAYLIST`, `format(rank, artist, title, includeRank=true): string`(범위 밖 순위는 `RangeError`), `folderName(title)`, `destinationFolder(isPlaylist, playlistTitle, typed): string | null`, `sanitize(text)`

원본의 테스트 39개는 Kotlin 테스트와 같은 이름(`formatsTheDocumentedExample` 등)이다. `text.test.ts`의 5개는 원본에 없고, 이 포트가 Kotlin과 같은 공백 규칙을 쓰는지를 지킨다(서버의 파일 이름과 미리보기의 이름이 달라지는 가장 흔한 길이다).

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`shell/tests/text.test.ts`:

```typescript
// Kotlin's white space and trimming, which the file names of the preview depend on. Not in the Kotlin files: this is what the port
// needs to be sure of. JavaScript's own `trim()` differs from Kotlin's at U+001C..U+001F and at U+FEFF.

import assert from "node:assert/strict";
import test from "node:test";
import { isNullOrBlank, isWhitespace, trim, trimEnd, trimEndChars, trimStart, trimStartChars } from "../src/logic/text.ts";

/**
 * Every UTF-16 unit for which Kotlin's `Char.isWhitespace()` is true on the JVM (`Character.isWhitespace(c) ||
 * Character.isSpaceChar(c)`), as listed by a JVM (JBR 21.0.9) on 2026-10-11 for the whole range U+0000..U+FFFF.
 */
const KOTLIN_WHITESPACE = [
  0x9, 0xa, 0xb, 0xc, 0xd, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0xa0, 0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007,
  0x2008, 0x2009, 0x200a, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000,
];

test("exactlyTheUnitsKotlinCallsWhiteSpaceAreWhiteSpace", () => {
  const found: number[] = [];
  for (let code = 0; code <= 0xffff; code += 1) if (isWhitespace(String.fromCharCode(code))) found.push(code);

  assert.deepEqual(found, KOTLIN_WHITESPACE);
});

test("aByteOrderMarkIsNotWhiteSpaceAndSurvivesTrimmingAsInKotlin", () => {
  assert.equal(trim("\uFEFFA\uFEFF"), "\uFEFFA\uFEFF");
  assert.equal("\uFEFFA\uFEFF".trim(), "A", "JavaScript's own trim() would have removed them");
});

test("theSeparatorsOfTheControlRangeAreWhiteSpaceAsInKotlin", () => {
  assert.equal(trim("\u001CA\u001F"), "A");
  assert.equal("\u001CA\u001F".trim(), "\u001CA\u001F", "JavaScript's own trim() keeps them");
});

test("trimmingWorksFromEachEndAndForSpecificCharacters", () => {
  assert.equal(trimStart(" \u00A0\u3000A "), "A ");
  assert.equal(trimEnd(" A\u2003 "), " A");
  assert.equal(trimEndChars("Mix. . ", ". "), "Mix");
  assert.equal(trimStartChars("  Mix", " "), "Mix");
  assert.equal(trimEndChars("C:\\Music\\/", "\\/"), "C:\\Music");
  assert.equal(trim(""), "");
  assert.equal(trim("   "), "");
});

test("blankMeansNothingButWhiteSpace", () => {
  assert.equal(isNullOrBlank(null), true);
  assert.equal(isNullOrBlank(undefined), true);
  assert.equal(isNullOrBlank(""), true);
  assert.equal(isNullOrBlank(" \t\n\u3000"), true);
  assert.equal(isNullOrBlank("\uFEFF"), false);
  assert.equal(isNullOrBlank("a"), false);
});
```

`shell/tests/filename.test.ts`:

```typescript
// The cases of shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt, one test per Kotlin test
// and under the same name, so that the port and the original are held to the same table.

import assert from "node:assert/strict";
import test from "node:test";
import {
  MAX_ARTIST_LENGTH,
  MAX_BASE_LENGTH,
  MAX_FOLDER_LENGTH,
  UNTITLED_PLAYLIST,
  destinationFolder,
  folderName,
  format,
} from "../src/logic/filename.ts";

const DEVICES = ["CON", "PRN", "AUX", "NUL", ...Array.from({ length: 9 }, (_, index) => [`COM${index + 1}`, `LPT${index + 1}`]).flat()];

/** No lone surrogate in [text]: every high one is followed by a low one and every low one follows a high one. */
function assertNoLoneSurrogate(text: string) {
  for (let index = 0; index < text.length; index += 1) {
    const code = text.charCodeAt(index);
    const next = text.charCodeAt(index + 1);
    const previous = text.charCodeAt(index - 1);
    if (code >= 0xd800 && code <= 0xdbff) assert.ok(next >= 0xdc00 && next <= 0xdfff, `lone high surrogate at ${index}`);
    if (code >= 0xdc00 && code <= 0xdfff) assert.ok(previous >= 0xd800 && previous <= 0xdbff, `lone low surrogate at ${index}`);
  }
}

const withoutExtension = (name: string) => name.replace(/\.m4a$/, "");

test("formatsTheDocumentedExample", () => {
  assert.equal(format(1, "소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)"), "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
});

test("padsRankToThreeDigits", () => {
  assert.ok(format(7, "A", "B").startsWith("007 "));
  assert.ok(format(42, "A", "B").startsWith("042 "));
  assert.ok(format(999, "A", "B").startsWith("999 "));
});

test("rejectsRankOutsideOneTo999", () => {
  assert.throws(() => format(0, "A", "B"), RangeError);
  assert.throws(() => format(1000, "A", "B"), RangeError);
});

test("replacesForbiddenCharactersWithFullWidthOnes", () => {
  assert.equal(
    format(5, "태연 (TAEYEON)", "만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"),
    "005 태연 (TAEYEON) - 만찬가 (晩餐歌 ／ BANSANKA) ： J-POP REMAKE Vol.1.m4a",
  );
  assert.equal(format(1, "A", '\\/:*?"<>|'), "001 A - ＼／：＊？＂＜＞｜.m4a");
});

test("dropsControlCharactersAndTrailingDotsAndSpaces", () => {
  assert.equal(format(1, "A", "Mr.\t\n. "), "001 A - Mr.m4a");
  assert.equal(format(1, "A", "B.C"), "001 A - B.C.m4a");
});

test("emptyTitleGetsAPlaceholder", () => {
  assert.equal(format(1, "A", "  "), "001 A - untitled.m4a");
});

test("longTitleIsTruncatedWithEllipsisKeepingRankAndArtist", () => {
  const base = withoutExtension(format(12, "ARTIST", "가".repeat(500)));
  assert.equal(base.length, MAX_BASE_LENGTH);
  assert.ok(base.startsWith("012 ARTIST - 가"));
  assert.ok(base.endsWith("…"));
});

test("veryLongArtistIsTruncatedToo", () => {
  const base = withoutExtension(format(1, "A".repeat(300), "Song"));
  assert.ok(base.length <= MAX_BASE_LENGTH);
  assert.ok(base.startsWith(`001 ${"A".repeat(10)}`));
  assert.ok(base.endsWith(" - Song"));
  assert.ok(base.includes("…"));
});

test("truncationNeverSplitsASurrogatePair", () => {
  const base = withoutExtension(format(1, "A", "😀".repeat(200)));
  assert.ok(base.length <= MAX_BASE_LENGTH);
  assertNoLoneSurrogate(base);
});

// ---- without the rank ----

test("theRankIsIncludedUnlessTheCallerLeavesItOut", () => {
  assert.equal(format(1, "A", "B"), "001 A - B.m4a");
  assert.equal(format(1, "A", "B", true), "001 A - B.m4a");
  assert.equal(format(42, "소연", "노래"), format(42, "소연", "노래", true));
});

test("withoutTheRankTheNameIsArtistDashTitle", () => {
  const name = format(1, "A", "B", false);

  assert.equal(name, "A - B.m4a");
  assert.ok(!name.startsWith(" "), "no leading space where the rank would have been");
});

test("withoutTheRankTheDocumentedExampleLosesOnlyThePrefix", () => {
  assert.equal(format(1, "소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)", false), "소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
});

test("withoutTheRankTheNameDoesNotDependOnTheRank", () => {
  const names = [1, 7, 999].map((rank) => format(rank, "A", "B", false));

  assert.deepEqual([...new Set(names)], ["A - B.m4a"]);
});

test("withoutTheRankTheRankIsStillValidated", () => {
  assert.throws(() => format(0, "A", "B", false), RangeError);
  assert.throws(() => format(1000, "A", "B", false), RangeError);
});

test("withoutTheRankForbiddenCharactersAreStillReplaced", () => {
  assert.equal(format(1, "A", '\\/:*?"<>|', false), "A - ＼／：＊？＂＜＞｜.m4a");
  assert.equal(format(1, "AC/DC", "Who Made Who?", false), "AC／DC - Who Made Who？.m4a");
});

test("withoutTheRankControlCharactersAndTrailingDotsAndSpacesAreStillDropped", () => {
  assert.equal(format(1, "A", "Mr.\t\n. ", false), "A - Mr.m4a");
  assert.equal(format(1, "A", "B.C", false), "A - B.C.m4a");
});

test("withoutTheRankAnEmptyTitleStillGetsAPlaceholder", () => {
  assert.equal(format(1, "A", "  ", false), "A - untitled.m4a");
  assert.equal(format(1, "A", "", false), "A - untitled.m4a");
});

test("withoutTheRankALongTitleFillsTheWholeBaseBudget", () => {
  const base = withoutExtension(format(12, "ARTIST", "가".repeat(500), false));

  assert.equal(base.length, MAX_BASE_LENGTH);
  assert.ok(base.startsWith("ARTIST - 가"), base);
  assert.ok(base.endsWith("…"));
  // 180 - "ARTIST - ".length = 171 title units: 170 characters and the ellipsis.
  assert.equal(base, "ARTIST - " + "가".repeat(170) + "…");
});

test("withoutTheRankAnArtistOfEightyCharactersIsKeptAndTheTitleGetsTheRest", () => {
  const artist = "A".repeat(MAX_ARTIST_LENGTH);

  const base = withoutExtension(format(1, artist, "T".repeat(500), false));

  assert.equal(base.length, MAX_BASE_LENGTH);
  assert.ok(base.startsWith(`${artist} - T`), base);
  assert.equal(base, `${artist} - ` + "T".repeat(96) + "…");
});

test("withoutTheRankAnArtistOverEightyCharactersIsCutWithAnEllipsis", () => {
  const expected = "A".repeat(MAX_ARTIST_LENGTH - 1) + "… - Song.m4a";

  assert.equal(format(1, "A".repeat(300), "Song", false), expected);
  assert.equal(format(1, "A".repeat(81), "Song", false), expected);
});

test("withoutTheRankTruncationNeverSplitsASurrogatePair", () => {
  const base = withoutExtension(format(1, "A", "😀".repeat(200), false));

  assert.ok(base.length <= MAX_BASE_LENGTH);
  assert.ok(base.startsWith("A - 😀"), base);
  assertNoLoneSurrogate(base);
});

test("withoutTheRankADeviceNameAsTheArtistGetsAnUnderscoreAfterTheStem", () => {
  assert.equal(format(1, "NUL.x", "Song", false), "NUL_.x - Song.m4a");
  assert.equal(format(1, "Con.", "Song", false), "Con_. - Song.m4a");
  assert.equal(format(1, "com1.x", "Song", false), "com1_.x - Song.m4a");
  assert.equal(format(1, "LPT9.x", "Song", false), "LPT9_.x - Song.m4a");
  assert.equal(format(1, "aux.tar.gz", "Song", false), "aux_.tar.gz - Song.m4a");
  assert.equal(format(1, "CON .x", "Song", false), "CON_ .x - Song.m4a");
});

test("withoutTheRankEveryDeviceNameAsTheArtistIsCaught", () => {
  for (const device of DEVICES) {
    assert.equal(format(1, `${device}.x`, "B", false), `${device}_.x - B.m4a`, device);
  }
});

test("withoutTheRankNamesThatMerelyResembleDeviceNamesStayUnchanged", () => {
  assert.equal(format(1, "Console", "Song", false), "Console - Song.m4a");
  assert.equal(format(1, "Con", "Song", false), "Con - Song.m4a");
  assert.equal(format(1, "LPT9", "Song", false), "LPT9 - Song.m4a");
  assert.equal(format(1, "NULL", "Song", false), "NULL - Song.m4a");
  assert.equal(format(1, "CON TEST", "Song", false), "CON TEST - Song.m4a");
  assert.equal(format(1, "COM0", "Song", false), "COM0 - Song.m4a");
  assert.equal(format(1, "my.con", "Song", false), "my.con - Song.m4a");
});

test("withTheRankADeviceNameAsTheArtistIsLeftAlone", () => {
  assert.equal(format(1, "NUL.x", "Song"), "001 NUL.x - Song.m4a");
  assert.equal(format(1, "Con", "Song", true), "001 Con - Song.m4a");
  assert.equal(format(42, "LPT9", "Song", true), "042 LPT9 - Song.m4a");
});

test("theUnderscoreForADeviceNameArtistStillKeepsTheNameWithinTheBudget", () => {
  const artist = "NUL." + "A".repeat(300);

  const base = withoutExtension(format(1, artist, "T".repeat(500), false));

  assert.equal(base.length, MAX_BASE_LENGTH);
  assert.ok(base.startsWith("NUL_.AAA"), base);
  assert.ok(base.endsWith("…"));
  // The artist is cut to 80 units first, the `_` makes it 81, and the title gets 180 - 81 - " - ".length = 96.
  assert.equal(base, "NUL_." + "A".repeat(75) + "… - " + "T".repeat(95) + "…");
});

test("missingOrBlankPlaylistTitleGetsTheUntitledFolderName", () => {
  assert.equal(folderName(null), "재생목록");
  assert.equal(folderName(""), "재생목록");
  assert.equal(folderName("   "), "재생목록");
  assert.equal(folderName("\t\n"), "재생목록");
  assert.equal(folderName(null), UNTITLED_PLAYLIST);
});

test("ordinaryAndKoreanPlaylistTitlesStayUnchanged", () => {
  assert.equal(folderName("Melon Daily Top 100"), "Melon Daily Top 100");
  assert.equal(folderName("멜론 일간 차트 TOP 100"), "멜론 일간 차트 TOP 100");
});

test("folderNameReplacesForbiddenCharactersWithFullWidthOnes", () => {
  assert.equal(folderName("Best: Hits? <2024>"), "Best： Hits？ ＜2024＞");
  assert.equal(folderName('\\/:*?"<>|'), "＼／：＊？＂＜＞｜");
});

test("folderNameDropsTrailingDotsAndSpaces", () => {
  assert.equal(folderName("Mix... "), "Mix");
  assert.equal(folderName("Mix. . "), "Mix");
  assert.equal(folderName("Mix.Vol.1."), "Mix.Vol.1");
});

test("folderNameThatIsOnlyDotsOrSpacesGetsTheUntitledFolderName", () => {
  assert.equal(folderName("..."), "재생목록");
  assert.equal(folderName(" . . "), "재생목록");
});

test("folderNameDropsControlCharacters", () => {
  assert.equal(folderName("My\u0000Mix\u001F"), "MyMix");
  assert.equal(folderName("\tMy \u007FMix\n"), "My Mix");
  assert.equal(folderName("\u0001\u0002"), "재생목록");
});

test("folderNameIsCutToEightyUnitsWithAnEllipsis", () => {
  const name = folderName("a".repeat(81));
  assert.equal(name.length, MAX_FOLDER_LENGTH);
  assert.equal(name, "a".repeat(79) + "…");
  assert.equal(folderName("a".repeat(80)), "a".repeat(80));
});

test("folderNameTruncationNeverEndsInALoneHighSurrogate", () => {
  const name = folderName("🎵".repeat(100));
  assert.ok(name.length <= MAX_FOLDER_LENGTH);
  assert.ok(name.endsWith("…"));
  assertNoLoneSurrogate(name);
});

test("reservedWindowsDeviceNamesGetAnUnderscoreAfterTheStem", () => {
  assert.equal(folderName("CON"), "CON_");
  assert.equal(folderName("nul"), "nul_");
  assert.equal(folderName("Com1"), "Com1_");
  assert.equal(folderName("LPT9"), "LPT9_");
  assert.equal(folderName("con.txt"), "con_.txt");
  assert.equal(folderName("aux.tar.gz"), "aux_.tar.gz");
  assert.equal(folderName("CON .txt"), "CON_ .txt");
  for (const device of DEVICES) {
    assert.equal(folderName(device), `${device}_`, device);
  }
});

test("namesThatMerelyResembleReservedOnesStayUnchanged", () => {
  assert.equal(folderName("Console"), "Console");
  assert.equal(folderName("CON TEST"), "CON TEST");
  assert.equal(folderName("NULL"), "NULL");
  assert.equal(folderName("COM0"), "COM0");
  assert.equal(folderName("COM10"), "COM10");
  assert.equal(folderName("my.con"), "my.con");
});

test("aPlaylistWithoutATypedFolderNameIsSavedInAFolderNamedAfterIt", () => {
  assert.equal(destinationFolder(true, "Best: Of?", null), "Best： Of？");
  assert.equal(destinationFolder(true, "Mix", ""), "Mix");
  assert.equal(destinationFolder(true, "Mix", "  \t"), "Mix");
  assert.equal(destinationFolder(true, null, null), "재생목록");
});

test("aSingleVideoWithoutATypedFolderNameHasNoFolder", () => {
  assert.equal(destinationFolder(false, "Mix", null), null);
  assert.equal(destinationFolder(false, null, "   "), null);
});

test("aTypedFolderNameWinsForAPlaylistAndAVideoAndIsMadeSafeLikeAnyFolderName", () => {
  assert.equal(destinationFolder(true, "Mix", "My Songs"), "My Songs");
  assert.equal(destinationFolder(false, "Mix", "  My Songs "), "My Songs");
  assert.equal(destinationFolder(false, null, "a\\b/c: d"), "a＼b／c： d");
  assert.equal(destinationFolder(false, null, ".."), "재생목록");
  assert.equal(destinationFolder(true, "Mix", "CON"), "CON_");
});
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/text.test.ts tests/filename.test.ts 2>&1 | Select-String "ERR_MODULE_NOT_FOUND|^ℹ (tests|pass|fail)"
```

Expected: 두 파일 모두 `ERR_MODULE_NOT_FOUND`(`../src/logic/text.ts`, `../src/logic/filename.ts`)로 실패.

- [ ] **Step 3: 구현한다**

`shell/src/logic/text.ts`:

```typescript
// Kotlin's idea of white space and of trimming, so that the ports of `shared` give exactly the answers the server gives.
// JavaScript's `trim()` differs at the edges (it also trims U+FEFF, and not U+001C..U+001F), and a file name shown in the
// preview must be the file name the server writes.

/** Kotlin `Char.isWhitespace()` on the JVM: Java's white space plus every Unicode space, line and paragraph separator. */
export function isWhitespace(character: string): boolean {
  const code = character.charCodeAt(0);
  if ((code >= 0x09 && code <= 0x0d) || (code >= 0x1c && code <= 0x20)) return true;
  return /^[\p{Zs}\p{Zl}\p{Zp}]$/u.test(character);
}

/** Kotlin `String.trim()`. */
export function trim(text: string): string {
  return trimEnd(trimStart(text));
}

/** Kotlin `String.trimStart()` (white space). */
export function trimStart(text: string): string {
  let start = 0;
  while (start < text.length && isWhitespace(text[start])) start += 1;
  return text.slice(start);
}

/** Kotlin `String.trimEnd()` (white space). */
export function trimEnd(text: string): string {
  let end = text.length;
  while (end > 0 && isWhitespace(text[end - 1])) end -= 1;
  return text.slice(0, end);
}

/** Kotlin `String.trimEnd(vararg chars)`. */
export function trimEndChars(text: string, characters: string): string {
  let end = text.length;
  while (end > 0 && characters.includes(text[end - 1])) end -= 1;
  return text.slice(0, end);
}

/** Kotlin `String.trimStart(vararg chars)`. */
export function trimStartChars(text: string, characters: string): string {
  let start = 0;
  while (start < text.length && characters.includes(text[start])) start += 1;
  return text.slice(start);
}

/** Kotlin `CharSequence.isNullOrBlank()`. */
export function isNullOrBlank(text: string | null | undefined): boolean {
  return text === null || text === undefined || trim(text) === "";
}

export function isHighSurrogate(character: string): boolean {
  const code = character.charCodeAt(0);
  return code >= 0xd800 && code <= 0xdbff;
}

export function isLowSurrogate(character: string): boolean {
  const code = character.charCodeAt(0);
  return code >= 0xdc00 && code <= 0xdfff;
}
```

`shell/src/logic/filename.ts`:

```typescript
// Ported from shared/src/commonMain/kotlin/com/xgetsongs/shared/filename/FilenameFormatter.kt. The preview shows the names
// this builds, and the server builds the real ones with the Kotlin original: tests/filename.test.ts holds the cases of
// FilenameFormatterTest.kt so that the two cannot drift apart unnoticed.

import { isHighSurrogate, isNullOrBlank, trim, trimEnd, trimEndChars, trimStartChars } from "./text.ts";

export const MIN_RANK = 1;
export const MAX_RANK = 999;

/** Limit for the file name without extension. */
export const MAX_BASE_LENGTH = 180;
export const MAX_ARTIST_LENGTH = 80;

/** Limit for a playlist folder name, before a reserved-name `_` is added. */
export const MAX_FOLDER_LENGTH = 80;

/** Folder name used when a playlist has no usable title. */
export const UNTITLED_PLAYLIST = "재생목록";

const ELLIPSIS = "…";
const EXTENSION = ".m4a";
const EMPTY_TITLE_PLACEHOLDER = "untitled";

const FULLWIDTH: Readonly<Record<string, string>> = {
  "\\": "＼",
  "/": "／",
  ":": "：",
  "*": "＊",
  "?": "？",
  '"': "＂",
  "<": "＜",
  ">": "＞",
  "|": "｜",
};

/** Windows device names that cannot be used as a file or folder name, with or without an extension. */
const RESERVED_DEVICE_NAMES: ReadonlySet<string> = new Set([
  "CON",
  "PRN",
  "AUX",
  "NUL",
  ...Array.from({ length: 9 }, (_, index) => [`COM${index + 1}`, `LPT${index + 1}`]).flat(),
]);

/**
 * Builds `{rank 3 digits} {artist} - {title}.m4a`, or `{artist} - {title}.m4a` when [includeRank] is false. The length
 * limits are the same either way (MAX_BASE_LENGTH for the whole name, so the title gets what the prefix leaves). [rank]
 * must be in MIN_RANK..MAX_RANK even when it is not printed. Without the rank the artist starts the name, so an artist
 * whose text before the first `.` is a reserved Windows device name gets a `_` after it (`NUL.x` becomes `NUL_.x`).
 */
export function format(rank: number, artist: string, title: string, includeRank = true): string {
  if (!Number.isInteger(rank) || rank < MIN_RANK || rank > MAX_RANK) {
    throw new RangeError(`rank must be in ${MIN_RANK}..${MAX_RANK} but was ${rank}`);
  }
  const cutArtist = truncate(sanitize(artist), MAX_ARTIST_LENGTH);
  const cleanArtist = includeRank || !cutArtist.includes(".") ? cutArtist : escapeDeviceName(cutArtist);
  const rankPrefix = includeRank ? `${String(rank).padStart(3, "0")} ` : "";
  const prefix = `${rankPrefix}${cleanArtist} - `;
  const sanitizedTitle = sanitize(title);
  const cleanTitle = sanitizedTitle === "" ? EMPTY_TITLE_PLACEHOLDER : sanitizedTitle;
  const titleBudget = MAX_BASE_LENGTH - prefix.length;
  const base = trimEndChars(prefix + truncate(cleanTitle, titleBudget), ". ");
  return base + EXTENSION;
}

/**
 * The folder name for a playlist titled [title]: sanitized, cut to MAX_FOLDER_LENGTH, without trailing dots or spaces, and
 * with a `_` after a reserved Windows device name (`CON` becomes `CON_`, `con.txt` becomes `con_.txt`). Falls back to
 * UNTITLED_PLAYLIST.
 */
export function folderName(title: string | null | undefined): string {
  if (isNullOrBlank(title)) return UNTITLED_PLAYLIST;
  const name = trimStartChars(trimEndChars(truncate(sanitize(title as string), MAX_FOLDER_LENGTH), ". "), " ");
  if (name === "") return UNTITLED_PLAYLIST;
  return escapeDeviceName(name);
}

/**
 * The folder inside the output folder that a job saves into, or null for the output folder itself. A [typed] name (the
 * album name the user typed) that is not blank wins, for a playlist and for a single video alike; otherwise a playlist
 * gets a folder named after [playlistTitle] and a single video gets none. The name is made safe by [folderName] either way.
 */
export function destinationFolder(
  isPlaylist: boolean,
  playlistTitle: string | null | undefined,
  typed: string | null | undefined,
): string | null {
  if (!isNullOrBlank(typed)) return folderName(typed);
  if (isPlaylist) return folderName(playlistTitle);
  return null;
}

/**
 * Windows treats the text before the first `.` of a name (trailing spaces ignored) as a device when it is one of the
 * reserved device names, whatever the case. Puts a `_` right after that stem then; other names are returned as they are.
 */
function escapeDeviceName(name: string): string {
  const dot = name.indexOf(".");
  const stem = trimEndChars(dot < 0 ? name : name.slice(0, dot), " ");
  return RESERVED_DEVICE_NAMES.has(stem.toUpperCase()) ? stem + "_" + name.slice(stem.length) : name;
}

/** Replaces forbidden characters with full-width look-alikes and drops control characters. */
export function sanitize(text: string): string {
  let result = "";
  for (let index = 0; index < text.length; index += 1) {
    const character = text[index];
    const code = character.charCodeAt(0);
    if (code < 0x20 || code === 0x7f) continue;
    result += FULLWIDTH[character] ?? character;
  }
  return trim(result);
}

/** Cuts [text] to at most [max] UTF-16 units, ending with an ellipsis, never splitting a surrogate pair. */
function truncate(text: string, max: number): string {
  if (text.length <= max) return text;
  let end = max - ELLIPSIS.length;
  if (end > 0 && isHighSurrogate(text[end - 1])) end -= 1;
  return trimEnd(text.slice(0, Math.max(end, 0))) + ELLIPSIS;
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/text.test.ts tests/filename.test.ts 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"
```

Expected: `ℹ tests 44`, `ℹ pass 44`, `ℹ fail 0`, `tsc exit 0`.

- [ ] **Step 5: Commit**

```bash
git add shell/src/logic shell/tests/text.test.ts shell/tests/filename.test.ts
git commit -m "feat(shell): FilenameFormatter를 TypeScript로 옮기고 원본 테스트 39개와 Kotlin 공백 규칙 테스트를 더한다"
```

---

### Task 2: `InputClassifier`

**Files:**
- Create: `shell/src/logic/input.ts`, `shell/tests/input.test.ts`

**Interfaces:**
- Consumes: Task 1의 `trim`.
- Produces: `ParsedInput`(`{kind:"playlist", id, alsoVideoId} | {kind:"video", id}`), `REJECT_MESSAGES`(`EMPTY`, `UNSUPPORTED_HOST`, `MIX_PLAYLIST`, `UNRECOGNIZED`의 한국어 문구), `RejectReason`, `ClassifyResult`(`{ok:true, input} | {ok:false, reason}`), `classify(raw)`, `canonicalUrl(input)`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`shell/tests/input.test.ts`(원본 15개와, 거절 문구가 서버의 문구와 같음을 지키는 1개):

```typescript
// The cases of shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt, one test per Kotlin test and
// under the same name.

import assert from "node:assert/strict";
import test from "node:test";
import { REJECT_MESSAGES, canonicalUrl, classify, type ClassifyResult, type ParsedInput, type RejectReason } from "../src/logic/input.ts";

const playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV";
const videoId = "dQw4w9WgXcQ";

const ok = (input: ParsedInput): ClassifyResult => ({ ok: true, input });
const rejected = (reason: RejectReason): ClassifyResult => ({ ok: false, reason });
const playlist = (id: string, alsoVideoId: string | null = null): ParsedInput => ({ kind: "playlist", id, alsoVideoId });
const video = (id: string): ParsedInput => ({ kind: "video", id });

/** The input of a result that has to be Ok. */
function accepted(result: ClassifyResult): ParsedInput {
  assert.ok(result.ok, `rejected: ${JSON.stringify(result)}`);
  return result.input;
}

test("barePlaylistId", () => {
  assert.deepEqual(classify(playlistId), ok(playlist(playlistId)));
});

test("bareVideoId", () => {
  assert.deepEqual(classify(videoId), ok(video(videoId)));
});

test("elevenCharIdStartingWithPlIsAVideo", () => {
  assert.deepEqual(classify("PLxxxxxxxxx"), ok(video("PLxxxxxxxxx")));
});

test("playlistUrl", () => {
  assert.deepEqual(classify(`https://www.youtube.com/playlist?list=${playlistId}`), ok(playlist(playlistId)));
});

test("watchUrl", () => {
  assert.deepEqual(classify(`https://www.youtube.com/watch?v=${videoId}`), ok(video(videoId)));
});

test("shortUrlsAndShorts", () => {
  assert.deepEqual(classify(`https://youtu.be/${videoId}?si=abc`), ok(video(videoId)));
  assert.deepEqual(classify(`https://www.youtube.com/shorts/${videoId}`), ok(video(videoId)));
});

test("urlWithoutScheme", () => {
  assert.deepEqual(classify(`music.youtube.com/watch?v=${videoId}`), ok(video(videoId)));
});

test("watchUrlWithListIsPlaylistAndRemembersVideo", () => {
  assert.deepEqual(classify(`https://www.youtube.com/watch?v=${videoId}&list=${playlistId}`), ok(playlist(playlistId, videoId)));
});

test("mixListWithVideoFallsBackToVideo", () => {
  assert.deepEqual(classify(`https://www.youtube.com/watch?v=${videoId}&list=RD${videoId}`), ok(video(videoId)));
});

test("mixListAloneIsRejected", () => {
  assert.deepEqual(classify("https://www.youtube.com/playlist?list=RDCLAK5uy_abcdefgh"), rejected("MIX_PLAYLIST"));
});

test("blankIsRejected", () => {
  assert.deepEqual(classify("   "), rejected("EMPTY"));
});

test("foreignHostsAreRejected", () => {
  assert.deepEqual(classify(`https://evil.com/watch?v=${videoId}`), rejected("UNSUPPORTED_HOST"));
  assert.deepEqual(classify(`https://www.youtube.com.evil.com/watch?v=${videoId}`), rejected("UNSUPPORTED_HOST"));
  assert.deepEqual(classify(`https://youtube.com@evil.com/watch?v=${videoId}`), rejected("UNSUPPORTED_HOST"));
});

test("garbageIsRejected", () => {
  assert.deepEqual(classify("PLshort"), rejected("UNRECOGNIZED"));
  assert.deepEqual(classify("hello world"), rejected("UNRECOGNIZED"));
  assert.deepEqual(classify("PLabc; rm -rf /"), rejected("UNSUPPORTED_HOST"));
});

test("canonicalUrlIsRebuiltFromIdOnly", () => {
  const input = accepted(classify(`https://www.youtube.com/watch?v=${videoId}&list=PLx%20--exec&t=10s`));
  assert.equal(canonicalUrl(input), `https://www.youtube.com/watch?v=${videoId}`);
});

test("playlistCanonicalUrl", () => {
  assert.equal(canonicalUrl(accepted(classify(playlistId))), `https://www.youtube.com/playlist?list=${playlistId}`);
});

// Not in the Kotlin file: the messages the screen shows are the ones the server says for the same rejection.
test("theRejectMessagesAreTheOnesTheServerSays", () => {
  assert.deepEqual(REJECT_MESSAGES, {
    EMPTY: "재생목록 ID 또는 영상 주소를 입력하세요.",
    UNSUPPORTED_HOST: "YouTube 주소만 사용할 수 있습니다.",
    MIX_PLAYLIST: "자동 생성 믹스 목록은 지원하지 않습니다.",
    UNRECOGNIZED: "재생목록 ID 또는 영상 주소로 인식할 수 없습니다.",
  });
});
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/input.test.ts 2>&1 | Select-String "ERR_MODULE_NOT_FOUND|^ℹ (tests|pass|fail)"
```

Expected: `ERR_MODULE_NOT_FOUND`(`../src/logic/input.ts`).

- [ ] **Step 3: 구현한다**

`shell/src/logic/input.ts`:

```typescript
// Ported from shared/src/commonMain/kotlin/com/xgetsongs/shared/input/InputClassifier.kt. The screen checks what the user typed
// before it asks the server anything, and the server runs the Kotlin original on what it receives: tests/input.test.ts holds
// the cases of InputClassifierTest.kt.

import { trim } from "./text.ts";

/** A user input that has been validated and reduced to YouTube IDs. */
export type ParsedInput =
  | { kind: "playlist"; id: string; alsoVideoId: string | null }
  | { kind: "video"; id: string };

export const REJECT_MESSAGES = {
  EMPTY: "재생목록 ID 또는 영상 주소를 입력하세요.",
  UNSUPPORTED_HOST: "YouTube 주소만 사용할 수 있습니다.",
  MIX_PLAYLIST: "자동 생성 믹스 목록은 지원하지 않습니다.",
  UNRECOGNIZED: "재생목록 ID 또는 영상 주소로 인식할 수 없습니다.",
} as const;

export type RejectReason = keyof typeof REJECT_MESSAGES;

export type ClassifyResult = { ok: true; input: ParsedInput } | { ok: false; reason: RejectReason };

/** Canonical URL rebuilt from the ID only. The raw user text is never passed on. */
export function canonicalUrl(input: ParsedInput): string {
  return input.kind === "playlist"
    ? `https://www.youtube.com/playlist?list=${input.id}`
    : `https://www.youtube.com/watch?v=${input.id}`;
}

const PLAYLIST_PREFIXES = ["PL", "UU", "LL", "FL", "OL"];
const MIX_PREFIX = "RD";
const MIN_PLAYLIST_ID_LENGTH = 12;
const VIDEO_ID_LENGTH = 11;
const ALLOWED_HOSTS: ReadonlySet<string> = new Set([
  "youtube.com",
  "www.youtube.com",
  "m.youtube.com",
  "music.youtube.com",
  "youtu.be",
]);
const VIDEO_PATH_PREFIXES = ["shorts", "embed", "live"];

export function classify(raw: string): ClassifyResult {
  const text = trim(raw);
  if (text === "") return rejected("EMPTY");
  if (isIdChars(text)) return classifyBareId(text);
  if (!text.includes(".") && !text.includes("/")) return rejected("UNRECOGNIZED");
  return classifyUrl(text);
}

// An ID is made of ASCII letters and digits, `_` and `-` only.
function isIdChars(text: string): boolean {
  return /^[A-Za-z0-9_-]+$/.test(text);
}

function isPlaylistId(text: string): boolean {
  return text.length >= MIN_PLAYLIST_ID_LENGTH && isIdChars(text) && PLAYLIST_PREFIXES.some((prefix) => text.startsWith(prefix));
}

function isMixId(text: string): boolean {
  return text.length >= MIN_PLAYLIST_ID_LENGTH && isIdChars(text) && text.startsWith(MIX_PREFIX);
}

function isVideoId(text: string): boolean {
  return text.length === VIDEO_ID_LENGTH && isIdChars(text);
}

function classifyBareId(text: string): ClassifyResult {
  if (isPlaylistId(text)) return ok({ kind: "playlist", id: text, alsoVideoId: null });
  if (isMixId(text)) return rejected("MIX_PLAYLIST");
  if (isVideoId(text)) return ok({ kind: "video", id: text });
  return rejected("UNRECOGNIZED");
}

function classifyUrl(text: string): ClassifyResult {
  const scheme = text.indexOf("://");
  const afterScheme = scheme < 0 ? text : text.slice(scheme + 3);
  const hostEnd = [...afterScheme].findIndex((character) => character === "/" || character === "?" || character === "#");
  const host = (hostEnd < 0 ? afterScheme : afterScheme.slice(0, hostEnd)).toLowerCase();
  if (!ALLOWED_HOSTS.has(host)) return rejected("UNSUPPORTED_HOST");

  const rest = hostEnd < 0 ? "" : afterScheme.slice(hostEnd);
  const beforeFragment = rest.split("#")[0];
  const questionMark = beforeFragment.indexOf("?");
  const path = questionMark < 0 ? beforeFragment : beforeFragment.slice(0, questionMark);
  const query = questionMark < 0 ? "" : beforeFragment.slice(questionMark + 1);
  const params = new Map<string, string>();
  for (const pair of query.split("&")) {
    const equals = pair.indexOf("=");
    if (equals >= 0) params.set(pair.slice(0, equals), pair.slice(equals + 1));
  }

  const segments = path.split("/").filter((segment) => segment !== "");
  let videoCandidate: string | undefined;
  if (host === "youtu.be") videoCandidate = segments[0];
  else if (segments.length > 0 && VIDEO_PATH_PREFIXES.includes(segments[0])) videoCandidate = segments[1];
  else videoCandidate = params.get("v");
  const videoId = videoCandidate !== undefined && isVideoId(videoCandidate) ? videoCandidate : null;
  const listId = params.get("list");

  if (listId !== undefined && isPlaylistId(listId)) return ok({ kind: "playlist", id: listId, alsoVideoId: videoId });
  if (listId !== undefined && isMixId(listId)) {
    return videoId !== null ? ok({ kind: "video", id: videoId }) : rejected("MIX_PLAYLIST");
  }
  if (videoId !== null) return ok({ kind: "video", id: videoId });
  return rejected("UNRECOGNIZED");
}

function ok(input: ParsedInput): ClassifyResult {
  return { ok: true, input };
}

function rejected(reason: RejectReason): ClassifyResult {
  return { ok: false, reason };
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/input.test.ts 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"
```

Expected: `ℹ tests 16`, `ℹ pass 16`, `ℹ fail 0`, `tsc exit 0`.

- [ ] **Step 5: Commit**

```bash
git add shell/src/logic/input.ts shell/tests/input.test.ts
git commit -m "feat(shell): InputClassifier를 TypeScript로 옮기고 원본 테스트를 같은 이름으로 더한다"
```

---

### Task 3: 서버의 Kotlin 구현과 포트를 무작위 입력으로 대조하는 도구

**Files:**
- Create: `shell/tools/differential/Oracle.java`, `shell/tools/differential/fuzz.mts`

**Interfaces:**
- Consumes: Task 1~2의 `format`, `folderName`, `destinationFolder`, `classify`; 1단계의 사이드카 이미지 `server/build/sidecar-image`(`lib\shared-jvm.jar`, `lib\kotlin-stdlib-*.jar`, `runtime\bin\java.exe`)
- Produces: `node tools/differential/fuzz.mts [종류당 건수] [시드]` — 같은 입력을 서버의 Kotlin(`Oracle.java`가 jar의 `FilenameFormatter`, `InputClassifier`를 부른다)과 TS 포트에 넣어 답을 비교한다. 불일치가 있으면 처음 12건을 보여 주고 종료 코드 1

이 도구는 TDD의 테스트가 아니라 검증 도구다. 이 계획을 쓰며 Kotlin의 `Char.isWhitespace()`와 포트의 집합이 같음(JVM이 U+0000~FFFF에서 28개를 꼽았고 포트가 같은 28개), 그리고 80,000건에서 불일치 0건임을 확인했다.

- [ ] **Step 1: 사이드카 이미지가 있는지 확인한다**

```powershell
Test-Path C:\Projects\xGetSongs\server\build\sidecar-image\lib\shared-jvm.jar
```

Expected: `True`. `False`이면 `.\gradlew.bat :server:sidecarImage --no-daemon`으로 만든다.

- [ ] **Step 2: 도구를 쓴다**

`shell/tools/differential/Oracle.java`:

```java
import com.xgetsongs.shared.filename.FilenameFormatter;
import com.xgetsongs.shared.input.ClassifyResult;
import com.xgetsongs.shared.input.InputClassifier;
import com.xgetsongs.shared.input.ParsedInput;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

/**
 * The Kotlin originals (shared-jvm.jar) as an oracle for the TypeScript ports. Reads one case per line from stdin, tab-separated;
 * a string is the hex of its UTF-16 units (4 digits each), "." for the empty string and "-" for null. Writes one answer per line.
 *
 *   F rank includeRank(0|1) artist title        -> OK <hex> | ERR
 *   D title                                      -> OK <hex>
 *   G isPlaylist(0|1) playlistTitle typed        -> OK <hex> | NULL
 *   C raw                                        -> P <id> <alsoVideoId> | V <id> | REJECT <REASON>
 */
public class Oracle {
    static String dec(String hex) {
        if (hex.equals("-")) return null;
        if (hex.equals(".")) return "";
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < hex.length(); i += 4) out.append((char) Integer.parseInt(hex.substring(i, i + 4), 16));
        return out.toString();
    }

    static String enc(String text) {
        if (text == null) return "-";
        if (text.isEmpty()) return ".";
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < text.length(); i++) out.append(String.format("%04x", (int) text.charAt(i)));
        return out.toString();
    }

    public static void main(String[] args) throws Exception {
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.US_ASCII));
        StringBuilder out = new StringBuilder();
        for (String line = in.readLine(); line != null; line = in.readLine()) {
            String[] part = line.split("\t", -1);
            switch (part[0]) {
                case "F":
                    try {
                        String name = FilenameFormatter.INSTANCE.format(Integer.parseInt(part[1]), dec(part[3]), dec(part[4]), part[2].equals("1"));
                        out.append("OK ").append(enc(name));
                    } catch (IllegalArgumentException e) {
                        out.append("ERR");
                    }
                    break;
                case "D":
                    out.append("OK ").append(enc(FilenameFormatter.INSTANCE.folderName(dec(part[1]))));
                    break;
                case "G": {
                    String folder = FilenameFormatter.INSTANCE.destinationFolder(part[1].equals("1"), dec(part[2]), dec(part[3]));
                    out.append(folder == null ? "NULL" : "OK " + enc(folder));
                    break;
                }
                case "C": {
                    ClassifyResult result = InputClassifier.INSTANCE.classify(dec(part[1]));
                    if (result instanceof ClassifyResult.Ok ok) {
                        ParsedInput input = ok.getInput();
                        if (input instanceof ParsedInput.Playlist p) out.append("P ").append(enc(p.getId())).append(' ').append(enc(p.getAlsoVideoId()));
                        else out.append("V ").append(enc(((ParsedInput.Video) input).getId()));
                    } else {
                        out.append("REJECT ").append(((ClassifyResult.Rejected) result).getReason().name());
                    }
                    break;
                }
                default:
                    out.append("?");
            }
            out.append('\n');
        }
        System.out.print(out);
    }
}
```

`shell/tools/differential/fuzz.mts`:

```typescript
// Differential test: the same random cases go through the Kotlin originals (Oracle.java, on the jars of the sidecar image that
// `gradlew :server:sidecarImage` makes) and the TypeScript ports (src/logic); the answers must be identical. The Kotlin code is what
// the server runs, so this is what keeps the names of the preview and the checks of the screen the same as the server's.
//
//   node tools/differential/fuzz.mts [cases-per-kind] [seed]       (from the shell folder; XGS_SIDECAR_DIR names another image)
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { destinationFolder, folderName, format } from "../../src/logic/filename.ts";
import { classify } from "../../src/logic/input.ts";

const perKind = Number(process.argv[2] ?? 10000);
let seed = Number(process.argv[3] ?? 20261011) >>> 0;

function random(): number {
  // mulberry32
  seed = (seed + 0x6d2b79f5) >>> 0;
  let t = seed;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const int = (low: number, high: number) => low + Math.floor(random() * (high - low + 1));
const pick = <T>(items: readonly T[]): T => items[int(0, items.length - 1)];
const chance = (probability: number) => random() < probability;

const ASCII = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const PUNCT = " .-_()[]{}'&,;!@#$%^+=~`";
const FORBIDDEN = '\\/:*?"<>|';
const CONTROL = ["\u0000", "\u0001", "\t", "\n", "\r", "\u001b", "\u001c", "\u001f", "\u007f"];
const SPACES = ["\u00a0", "\u3000", "\u2003", "\u2028", "\ufeff", "\u200b", "\u180e", "\u0085", "\u2007", "\u202f"];
const KOREAN = "가나다라마바사아자차카타파하소연퇴사할게여기안";
const EMOJI = ["😀", "🎵", "🇰🇷", "👨‍👩‍👧", "𝒜"];
const LONE = ["\ud83d", "\ude00", "\udbff"];
const DEVICES = ["CON", "PRN", "AUX", "NUL", "COM1", "LPT9", "con", "Nul", "com0", "LPT10", "COM9", "lpt1"];

function token(): string {
  const roll = random();
  if (roll < 0.3) return ASCII[int(0, ASCII.length - 1)];
  if (roll < 0.42) return PUNCT[int(0, PUNCT.length - 1)];
  if (roll < 0.52) return FORBIDDEN[int(0, FORBIDDEN.length - 1)];
  if (roll < 0.58) return pick(CONTROL);
  if (roll < 0.66) return pick(SPACES);
  if (roll < 0.82) return KOREAN[int(0, KOREAN.length - 1)];
  if (roll < 0.9) return pick(EMOJI);
  if (roll < 0.93) return pick(LONE);
  return pick(DEVICES) + pick(["", ".", " .", ". ", ".x", "..", " "]);
}

function text(): string {
  const shape = random();
  if (shape < 0.08) return "";
  if (shape < 0.18) return pick(DEVICES) + pick(["", ".", ".x", " .txt", ". ", ".tar.gz"]);
  if (shape < 0.3) {
    // long runs that sit near the truncation limits (80 for an artist or a folder, 180 for a whole name)
    const unit = pick(["a", "가", "😀", "A.", "🎵", "ab"]);
    return unit.repeat(int(60, 220)) + (chance(0.5) ? token() : "");
  }
  const length = shape < 0.7 ? int(1, 12) : int(12, 120);
  let result = "";
  for (let index = 0; index < length; index += 1) result += token();
  return result;
}

const hex = (value: string | null): string => {
  if (value === null) return "-";
  if (value === "") return ".";
  let out = "";
  for (let index = 0; index < value.length; index += 1) out += value.charCodeAt(index).toString(16).padStart(4, "0");
  return out;
};

const nullable = (): string | null => (chance(0.15) ? null : text());

// ---- URL-like input for the classifier ----
const ID_CHARS = ASCII + "_-";
const idOf = (length: number) => Array.from({ length }, () => ID_CHARS[int(0, ID_CHARS.length - 1)]).join("");
const videoId = () => (chance(0.85) ? idOf(11) : idOf(int(0, 14)) + (chance(0.2) ? "é" : ""));
const playlistId = () => pick(["PL", "UU", "LL", "FL", "OL", "RD", "PL", "XX"]) + idOf(int(8, 36));

function classifierInput(): string {
  const shape = random();
  if (shape < 0.12) return pick(["", " ", "\t\n", "hello world", "PLshort", "PLabc; rm -rf /", "youtu.be", "a.b", "/", "."]);
  if (shape < 0.3) return (chance(0.5) ? playlistId() : videoId()) + (chance(0.1) ? " " : "");
  const scheme = pick(["https://", "http://", "", "HTTPS://", "ftp://", "//"]);
  const host = pick([
    "www.youtube.com", "youtube.com", "m.youtube.com", "music.youtube.com", "youtu.be", "WWW.YOUTUBE.COM", "www.youtube.com.evil.com",
    "evil.com", "youtube.com@evil.com", "www.youtube.com:443", "user@youtu.be", "youtube.co", "",
  ]);
  const path = pick(["/watch", "/playlist", `/shorts/${videoId()}`, `/embed/${videoId()}`, `/live/${videoId()}`, "/", "", `/${videoId()}`, "/watch/", "//watch", "/shorts"]);
  const params: string[] = [];
  if (chance(0.7)) params.push(`v=${videoId()}`);
  if (chance(0.6)) params.push(`list=${playlistId()}`);
  for (const extra of ["t=10s", "index=3", "si=abc", "&", "=", "v", "list=", "v=", "x=y=z"]) if (chance(0.15)) params.push(extra);
  for (let index = params.length - 1; index > 0; index -= 1) {
    const other = int(0, index);
    [params[index], params[other]] = [params[other], params[index]];
  }
  const query = params.length > 0 ? "?" + params.join("&") : "";
  const fragment = chance(0.1) ? "#frag" : "";
  const padding = chance(0.1) ? " " : "";
  return padding + scheme + host + path + query + fragment + padding;
}

// ---- the cases ----
interface Case {
  line: string;
  expected: string;
}
const cases: Case[] = [];

for (let index = 0; index < perKind; index += 1) {
  const rank = chance(0.03) ? pick([0, 1000, -5, 5000]) : int(1, 999);
  const includeRank = chance(0.5);
  const artist = text();
  const title = text();
  let expected: string;
  try {
    expected = "OK " + hex(format(rank, artist, title, includeRank));
  } catch {
    expected = "ERR";
  }
  cases.push({ line: `F\t${rank}\t${includeRank ? 1 : 0}\t${hex(artist)}\t${hex(title)}`, expected });
}
for (let index = 0; index < perKind; index += 1) {
  const title = nullable();
  cases.push({ line: `D\t${hex(title)}`, expected: "OK " + hex(folderName(title)) });
}
for (let index = 0; index < perKind; index += 1) {
  const isPlaylist = chance(0.5);
  const title = nullable();
  const typed = nullable();
  const folder = destinationFolder(isPlaylist, title, typed);
  cases.push({ line: `G\t${isPlaylist ? 1 : 0}\t${hex(title)}\t${hex(typed)}`, expected: folder === null ? "NULL" : "OK " + hex(folder) });
}
for (let index = 0; index < perKind; index += 1) {
  const raw = classifierInput();
  const result = classify(raw);
  let expected: string;
  if (!result.ok) expected = `REJECT ${result.reason}`;
  else if (result.input.kind === "video") expected = `V ${hex(result.input.id)}`;
  else expected = `P ${hex(result.input.id)} ${hex(result.input.alsoVideoId)}`;
  cases.push({ line: `C\t${hex(raw)}`, expected });
}

const image = process.env.XGS_SIDECAR_DIR ?? fileURLToPath(new URL("../../../server/build/sidecar-image", import.meta.url));
const run = spawnSync(`${image}/runtime/bin/java.exe`, ["-cp", `${image}/lib/*`, fileURLToPath(new URL("Oracle.java", import.meta.url))], {
  input: cases.map((entry) => entry.line).join("\n") + "\n",
  encoding: "ascii",
  maxBuffer: 1 << 28,
});
if (run.status !== 0) throw new Error(`the oracle failed: ${run.stderr}`);
const answers = run.stdout.split("\n");
const decode = (hexText: string) => {
  if (hexText === "-") return "(null)";
  if (hexText === ".") return "(empty)";
  let out = "";
  for (let index = 0; index < hexText.length; index += 4) out += String.fromCharCode(parseInt(hexText.slice(index, index + 4), 16));
  return JSON.stringify(out);
};

let mismatches = 0;
const kinds: Record<string, { cases: number; mismatches: number }> = {};
cases.forEach((entry, index) => {
  const kind = entry.line[0];
  kinds[kind] ??= { cases: 0, mismatches: 0 };
  kinds[kind].cases += 1;
  if (answers[index] !== entry.expected) {
    mismatches += 1;
    kinds[kind].mismatches += 1;
    if (mismatches <= 12) {
      console.log(`MISMATCH #${index} [${kind}] input: ${entry.line.split("\t").slice(1).map(decode).join(" , ")}`);
      console.log(`   kotlin: ${answers[index]?.startsWith("OK ") ? "OK " + decode(answers[index].slice(3)) : answers[index]}`);
      console.log(`   port:   ${entry.expected.startsWith("OK ") ? "OK " + decode(entry.expected.slice(3)) : entry.expected}`);
    }
  }
});
console.log(`${cases.length} cases (${perKind} per kind), ${mismatches} mismatches`, JSON.stringify(kinds));
if (mismatches > 0) process.exitCode = 1;
```

- [ ] **Step 3: 돌린다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node tools/differential/fuzz.mts 20000 2>&1 | Select-Object -Last 3; "exit $LASTEXITCODE"
```

Expected: `80000 cases (20000 per kind), 0 mismatches {...}`, `exit 0`. (JVM을 띄우므로 10초쯤 걸린다.)

- [ ] **Step 4: 도구가 차이를 잡는지 확인한다**

`shell/src/logic/text.ts`의 `trim`을 일부러 JS의 것으로 바꾼다:

```typescript
export function trim(text: string): string {
  return text.trim();
}
```

```powershell
Set-Location C:\Projects\xGetSongs\shell; node tools/differential/fuzz.mts 3000 2>&1 | Select-Object -Last 1; "exit $LASTEXITCODE"
```

Expected: `... mismatches` 수가 0이 아니고(이 계획을 쓰며 117건) `exit 1`. 확인한 뒤 **원래 `trim`으로 되돌리고** Step 3을 다시 돌려 0건을 본다.

- [ ] **Step 5: Commit**

```bash
git add shell/tools
git commit -m "test(shell): 서버의 Kotlin 구현과 TS 포트를 같은 무작위 입력으로 대조하는 도구를 더한다"
```

---

### Task 4: `UiState`와 `Labels`

**Files:**
- Create: `shell/src/state/types.ts`, `shell/src/state/labels.ts`, `shell/tests/support/fixtures.ts`
- Replace: `shell/tests/labels.test.ts`(프로토타입 것을 원본 `LabelsTest`의 35개로)

**Interfaces:**
- Consumes: Task 1의 `destinationFolder`, `MIN_RANK`, `MAX_RANK`, `trim`, `trimEndChars`; 2단계의 `bridge/types.ts`
- Produces (Task 5~7이 쓴다):
  - `types.ts`: `ItemStatus`, `ItemRow`, `Phase`, `UiState`(`elapsedMs`는 밀리초), `initialUiState(overrides)`, `failedRanks`, `canResolve`, `UserSettings`, `userSettings(overrides)`, `SettingsStore`, `noSettingsStore`, `XgsApi`(2단계 `Bridge`가 만족한다)
  - `labels.ts`: `UPDATE_YT_DLP_LABEL`, `FORMAT_FAILURE_HELP`, `statusLabel`, `summaryText(state)`, `elapsedLabel(ms)`, `destinationPath`, `destinationLabel`, `albumNameText`, `rankLabel`, `rankInputText`(ASCII 숫자만), `rankFromInput`
  - `tests/support/fixtures.ts`: `item`, `unavailableItem`, `playlist`, `video`, `resolvedState`, `LYRICS_OUTCOMES`

`rankInputText`는 원본(Kotlin `Char.isDigit`)과 달리 ASCII 숫자만 남긴다(순위 칸에 다른 문자의 숫자를 받지 않는다. 원본은 그런 숫자를 받고도 `toIntOrNull`이 읽는다). 이 한 가지만 일부러 다르다.

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`shell/tests/support/fixtures.ts`:

```typescript
// The server answers the tests build on: the items and the resolve responses of FakeApi.kt, and a UiState around a response.

import type { InputKind, LyricsOutcome, ResolveResponse, ResolvedItem } from "../../src/bridge/types.ts";
import { initialUiState, type UiState } from "../../src/state/types.ts";

export const LYRICS_OUTCOMES: LyricsOutcome[] = ["DESCRIPTION", "ONLINE", "NOT_FOUND", "SEARCH_OFF"];

const pad = (value: number, length: number) => String(value).padStart(length, "0");

/** An available item: artist `A<rank>` and track `T<rank>` unless given. */
export function item(rank: number, artist = `A${rank}`, track = `T${rank}`): ResolvedItem {
  return {
    rank,
    videoId: `vid${pad(rank, 8)}`,
    title: `${artist} - ${track}`,
    channel: null,
    artist,
    track,
    lowConfidence: false,
    available: true,
    unavailableReason: null,
    expectedFileName: `${pad(rank, 3)} ${artist} - ${track}.m4a`,
  };
}

/** An item the server cannot download (a private video). */
export function unavailableItem(rank = 2, reason = "비공개 영상"): ResolvedItem {
  return {
    rank,
    videoId: `vid${pad(rank, 8)}`,
    title: "[Private video]",
    channel: null,
    artist: "",
    track: "",
    lowConfidence: false,
    available: false,
    unavailableReason: reason,
    expectedFileName: null,
  };
}

/** A playlist of three: two available (the third with a guessed artist) and a private one between them. */
export function playlist(alsoVideoId: string | null = null): ResolveResponse {
  return {
    resolveId: "resolve-1",
    kind: "PLAYLIST",
    playlistTitle: "Sample",
    items: [item(1), unavailableItem(2), { ...item(3), lowConfidence: true }],
    truncated: false,
    alsoVideoId,
  };
}

/** A single video, by `IU` called `Love`. */
export function video(): ResolveResponse {
  return {
    resolveId: "resolve-2",
    kind: "VIDEO",
    playlistTitle: null,
    items: [item(1, "IU", "Love")],
    truncated: false,
    alsoVideoId: null,
  };
}

/** A state with a response resolved into it, as the labels tests need it (no rows). */
export function resolvedState(
  kind: InputKind,
  playlistTitle: string | null = null,
  outputDir = "D:\\Music",
  albumName: string | null = null,
): UiState {
  return initialUiState({
    outputDir,
    albumName,
    resolved: { resolveId: "r1", kind, playlistTitle, items: [], truncated: false, alsoVideoId: null },
  });
}
```

`shell/tests/labels.test.ts`(기존 파일을 아래로 통째로 바꾼다. 원본 35개):

```typescript
// The cases of app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt, one test per Kotlin test and under the same
// name. (Kotlin's `Duration` is milliseconds here.)

import assert from "node:assert/strict";
import test from "node:test";
import {
  FORMAT_FAILURE_HELP,
  UPDATE_YT_DLP_LABEL,
  albumNameText,
  destinationLabel,
  destinationPath,
  elapsedLabel,
  rankFromInput,
  rankInputText,
  rankLabel,
  statusLabel,
  summaryText,
} from "../src/state/labels.ts";
import type { LyricsOutcome } from "../src/bridge/types.ts";
import { initialUiState, type ItemStatus } from "../src/state/types.ts";
import { LYRICS_OUTCOMES, resolvedState } from "./support/fixtures.ts";

const done = (lyrics: LyricsOutcome | null = null): ItemStatus => ({ kind: "done", lyrics });
const seconds = (value: number) => value * 1000;

test("statusLabels", () => {
  assert.equal(statusLabel({ kind: "ready" }), "준비됨");
  assert.equal(statusLabel({ kind: "waiting" }), "대기 중");
  assert.equal(statusLabel({ kind: "downloading", percent: null }), "다운로드 중…");
  assert.equal(statusLabel({ kind: "downloading", percent: 42.9 }), "다운로드 42%");
  assert.equal(statusLabel({ kind: "finishing" }), "마무리 중…");
  assert.equal(statusLabel(done()), "완료");
  assert.equal(statusLabel({ kind: "skipped", reason: "비공개 영상" }), "건너뜀: 비공개 영상");
  assert.equal(statusLabel({ kind: "failed", message: "boom" }), "실패: boom");
});

test("theFormatFailureHelpNamesTheFailureTheUpdateButtonByItsRealLabelAndTheJsRuntimes", () => {
  assert.ok(FORMAT_FAILURE_HELP.includes("“m4a 오디오 형식이 없습니다.”"));
  assert.ok(FORMAT_FAILURE_HELP.includes(`앱 위쪽의 ${UPDATE_YT_DLP_LABEL} 버튼`));
  assert.ok(FORMAT_FAILURE_HELP.includes("JavaScript 런타임(Node.js, Deno)"));
  assert.ok(FORMAT_FAILURE_HELP.includes("Node.js 22+ 또는 Deno 2.3+"));
});

test("aFinishedRowSaysWhereItsLyricsCameFromOrWhyItHasNone", () => {
  assert.equal(statusLabel(done(null)), "완료");
  assert.equal(statusLabel(done("DESCRIPTION")), "완료 · 가사 ✓ 설명란");
  assert.equal(statusLabel(done("ONLINE")), "완료 · 가사 ✓ 인터넷");
  assert.equal(statusLabel(done("NOT_FOUND")), "완료 · 가사 없음");
  assert.equal(statusLabel(done("SEARCH_OFF")), "완료 · 가사 없음 (검색 끔)");
});

test("aFinishedRowWithoutAKnownOutcomeIsTheSameAsTheDefault", () => {
  assert.deepEqual(done(null), done());
  assert.equal(statusLabel(done()), statusLabel(done(null)));
});

test("everyLyricsOutcomeHasItsOwnLabelStartingWithDone", () => {
  const labels = LYRICS_OUTCOMES.map((outcome) => statusLabel(done(outcome)));

  assert.equal(new Set(labels).size, labels.length, `no two outcomes may look alike: ${labels}`);
  assert.ok(labels.every((label) => label.startsWith("완료 · 가사 ")), String(labels));
});

test("theLongestFinishedLabelStillFitsOnOneLineOfTheStatusCell", () => {
  // The Compose status cell was 220.dp wide with 12.sp text, so a label of at most 17 characters never wrapped. The web
  // list keeps the same bound on the text (the cell of the web list is as wide).
  const longest = Math.max(...[...LYRICS_OUTCOMES, null].map((outcome) => statusLabel(done(outcome)).length));

  assert.ok(longest * 12 <= 220, `the longest finished label has ${longest} characters`);
});

test("summaryTextByJobStatus", () => {
  const summary = { succeeded: 2, skipped: 1, failed: 3 };
  assert.equal(summaryText(initialUiState({ jobStatus: "COMPLETED", summary })), "완료 — 성공 2 · 건너뜀 1 · 실패 3");
  assert.equal(summaryText(initialUiState({ jobStatus: "CANCELLED", summary })), "취소됨 — 성공 2 · 건너뜀 1 · 실패 3");
  assert.equal(summaryText(initialUiState({ jobStatus: "FAILED", summary })), "중단됨 — 성공 2 · 건너뜀 1 · 실패 3");
});

test("summaryTextAddsHowLongTheJobTookWhenItIsKnown", () => {
  const summary = { succeeded: 2, skipped: 1, failed: 3 };

  assert.equal(
    summaryText(initialUiState({ jobStatus: "COMPLETED", summary, elapsedMs: seconds(185) })),
    "완료 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 3분 05초",
  );
  assert.equal(
    summaryText(initialUiState({ jobStatus: "CANCELLED", summary, elapsedMs: seconds(42) })),
    "취소됨 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 42초",
  );
  assert.equal(
    summaryText(initialUiState({ jobStatus: "FAILED", summary, elapsedMs: 400 })),
    "중단됨 — 성공 2 · 건너뜀 1 · 실패 3 · 소요 0초",
  );
});

test("elapsedLabelShowsWholeSecondsMinutesAndHours", () => {
  assert.equal(elapsedLabel(0), "0초");
  assert.equal(elapsedLabel(seconds(59.9)), "59초");
  assert.equal(elapsedLabel(seconds(60)), "1분 00초");
  assert.equal(elapsedLabel(seconds(185)), "3분 05초");
  assert.equal(elapsedLabel(seconds(3599)), "59분 59초");
  assert.equal(elapsedLabel(seconds(3600)), "1시간 00분 00초");
  assert.equal(elapsedLabel(seconds(7384)), "2시간 03분 04초");
  assert.equal(elapsedLabel(seconds(-5)), "0초", "a clock that went backwards says nothing negative");
});

test("noSummaryUntilAJobEnds", () => {
  assert.equal(summaryText(initialUiState()), null);
  assert.equal(summaryText(initialUiState({ summary: { succeeded: 1, skipped: 0, failed: 0 }, jobStatus: null })), null);
});

// ---- the text the album name field shows, which is also the name of the folder ----

test("anUntouchedFieldShowsTheOriginalPlaylistTitle", () => {
  assert.equal(albumNameText(resolvedState("PLAYLIST", "Best: Of?")), "Best: Of?", "the original title, which is what the files get");
  assert.equal(albumNameText(resolvedState("PLAYLIST", null)), "");
});

test("anUntouchedFieldOfAVideoAndBeforeAnythingIsResolvedIsEmpty", () => {
  assert.equal(albumNameText(resolvedState("VIDEO", "Sample")), "");
  assert.equal(albumNameText(initialUiState()), "");
});

test("aTouchedFieldShowsExactlyWhatWasTypedEvenWhenItIsBlank", () => {
  assert.equal(albumNameText(resolvedState("PLAYLIST", "Sample", "D:\\Music", "내 앨범 ")), "내 앨범 ");
  assert.equal(albumNameText(resolvedState("PLAYLIST", "Sample", "D:\\Music", "")), "", "cleared by the user: the field stays empty");
});

test("aClearedAlbumFieldMeansTheDefaultFolderInTheDestination", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:\\Music", "")), "D:\\Music\\Sample");
});

test("aTypedAlbumNameReplacesThePlaylistFolderInTheDestination", () => {
  const state = resolvedState("PLAYLIST", "Sample", "D:\\Music", "내 앨범");

  assert.equal(destinationPath(state), "D:\\Music\\내 앨범");
  assert.equal(destinationLabel(state), "저장 위치: D:\\Music\\내 앨범");
});

test("aTypedAlbumNameGivesASingleVideoAFolderInTheDestination", () => {
  assert.equal(destinationPath(resolvedState("VIDEO", null, "D:\\Music", "내 앨범")), "D:\\Music\\내 앨범");
});

test("aTypedAlbumNameIsShownAsAFolderTheWayTheServerWillMakeIt", () => {
  assert.equal(destinationPath(resolvedState("VIDEO", null, "D:\\Music", " a\\b/c ")), "D:\\Music\\a＼b／c");
});

test("aBlankTypedAlbumNameLeavesTheDestinationAsItWas", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:\\Music", "  ")), "D:\\Music\\Sample");
  assert.equal(destinationPath(resolvedState("VIDEO", null, "D:\\Music", "  ")), "D:\\Music");
});

test("destinationIsNullBeforeAnythingIsResolved", () => {
  assert.equal(destinationLabel(initialUiState({ outputDir: "D:\\Music" })), null);
});

test("destinationIsNullWhileTheOutputFolderIsBlank", () => {
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample", "")), null);
  assert.equal(destinationLabel(resolvedState("VIDEO", null, "   ")), null);
});

test("aVideoIsSavedInTheOutputFolderItself", () => {
  assert.equal(destinationLabel(resolvedState("VIDEO")), "저장 위치: D:\\Music");
  // a title on a video is ignored: only the kind decides whether there is a sub-folder
  assert.equal(destinationLabel(resolvedState("VIDEO", "Sample")), "저장 위치: D:\\Music");
});

test("aPlaylistIsSavedInAFolderNamedAfterIt", () => {
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample")), "저장 위치: D:\\Music\\Sample");
});

test("trailingSlashesOfTheOutputFolderAreDroppedBeforeTheFolderName", () => {
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample", "D:\\Music\\")), "저장 위치: D:\\Music\\Sample");
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample", "D:/Music/")), "저장 위치: D:/Music\\Sample");
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Sample", "D:\\")), "저장 위치: D:\\Sample");
});

test("theFolderNameIsSanitizedLikeTheServerDoesIt", () => {
  assert.equal(destinationLabel(resolvedState("PLAYLIST", "Best: Of?")), "저장 위치: D:\\Music\\Best： Of？"); // full-width colon and question mark
  assert.equal(destinationLabel(resolvedState("PLAYLIST", null)), "저장 위치: D:\\Music\\재생목록");
});

// ---- destinationPath: the path part of the destination label ----

test("destinationPathIsNullBeforeAnythingIsResolved", () => {
  assert.equal(destinationPath(initialUiState({ outputDir: "D:\\Music" })), null);
});

test("destinationPathIsNullWhileTheOutputFolderIsBlank", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "")), null);
  assert.equal(destinationPath(resolvedState("VIDEO", null, "   ")), null);
});

test("theDestinationPathOfAVideoIsTheOutputFolderItself", () => {
  assert.equal(destinationPath(resolvedState("VIDEO")), "D:\\Music");
  assert.equal(destinationPath(resolvedState("VIDEO", "Sample")), "D:\\Music");
});

test("theDestinationPathOfAPlaylistIsAFolderNamedAfterIt", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample")), "D:\\Music\\Sample");
});

test("theDestinationPathDropsTrailingSeparatorsOfTheOutputFolder", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:\\Music\\")), "D:\\Music\\Sample");
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:/Music/")), "D:/Music\\Sample");
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Sample", "D:\\")), "D:\\Sample");
});

test("theDestinationPathUsesTheSanitizedFolderName", () => {
  assert.equal(destinationPath(resolvedState("PLAYLIST", "Best: Of?")), "D:\\Music\\Best： Of？");
  assert.equal(destinationPath(resolvedState("PLAYLIST", null)), "D:\\Music\\재생목록");
});

test("theDestinationLabelIsThePathWithItsCaptionAndNullWhenThereIsNoPath", () => {
  const states = [
    initialUiState({ outputDir: "D:\\Music" }),
    resolvedState("PLAYLIST", "Sample", ""),
    resolvedState("VIDEO"),
    resolvedState("PLAYLIST", "Sample", "D:\\Music\\"),
    resolvedState("PLAYLIST", null),
  ];
  for (const state of states) {
    const path = destinationPath(state);
    assert.equal(destinationLabel(state), path === null ? null : `저장 위치: ${path}`);
  }
  assert.equal(destinationLabel(resolvedState("PLAYLIST", null)), "저장 위치: D:\\Music\\재생목록");
});

test("rankIsZeroPadded", () => {
  assert.equal(rankLabel(7), "007");
  assert.equal(rankLabel(999), "999");
});

test("rankInputKeepsOnlyUpToThreeDigits", () => {
  assert.equal(rankInputText("12a3"), "123");
  assert.equal(rankInputText("5000"), "500");
  assert.equal(rankInputText(""), "");
  assert.equal(rankInputText("abc"), "");
});

test("rankFromInputIsNullForEmptyText", () => {
  assert.equal(rankFromInput(""), null);
});

test("rankFromInputClampsToTheValidRange", () => {
  assert.equal(rankFromInput("0"), 1);
  assert.equal(rankFromInput("7"), 7);
  assert.equal(rankFromInput("999"), 999);
  assert.equal(rankFromInput("000"), 1);
});
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/labels.test.ts 2>&1 | Select-String "ERR_MODULE_NOT_FOUND|^ℹ (tests|pass|fail)"
```

Expected: `ERR_MODULE_NOT_FOUND`(`../src/state/labels.ts` 또는 `types.ts`).

- [ ] **Step 3: 구현한다**

`shell/src/state/types.ts`:

```typescript
// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/UiState.kt and app/.../settings/*.kt.

import type {
  ActionResult,
  JobCreated,
  JobEvent,
  JobRequest,
  JobStatus,
  JobSummary,
  LyricsOutcome,
  ResolveResponse,
  ResolvedItem,
  ToolsStatus,
} from "../bridge/types.ts";

export type ItemStatus =
  /** Shown in the preview before a download starts. */
  | { kind: "ready" }
  /** Part of a running job but not started yet. */
  | { kind: "waiting" }
  | { kind: "downloading"; percent: number | null }
  | { kind: "finishing" }
  /** `lyrics` is what the finished file got as lyrics (or why it got none); null when the server did not say. */
  | { kind: "done"; lyrics: LyricsOutcome | null }
  | { kind: "skipped"; reason: string }
  | { kind: "failed"; message: string };

export interface ItemRow {
  item: ResolvedItem;
  /** The file name shown to the user: the preview's guess, replaced by the real one once known. */
  fileName: string | null;
  status: ItemStatus;
}

export type Phase = "IDLE" | "RESOLVING" | "PREVIEW" | "RUNNING" | "FINISHED";

export interface UiState {
  phase: Phase;
  input: string;
  /** `outputDir`, `overwrite`, `includeRank` and `searchLyricsOnline` are the options the app remembers between runs. */
  outputDir: string;
  overwrite: boolean;
  /** Whether file names start with the rank (`001 `). Sent with the job; the preview names follow it. */
  includeRank: boolean;
  /** Whether a song whose description has no lyrics is looked up on the internet. Sent with the job; the preview does not change. */
  searchLyricsOnline: boolean;
  /** Not remembered: it belongs to the video that is on screen. */
  singleRank: number;
  /**
   * The album name the user typed, as typed; it is also the name of the folder the files are saved in. Null means the user
   * has not touched the field: it shows the default (see `albumNameText`) and sends nothing. Once touched, the text stays as
   * it is, and blank means the default again. Not remembered, like `singleRank`; `reset` clears it.
   */
  albumName: string | null;
  resolved: ResolveResponse | null;
  rows: ItemRow[];
  error: string | null;
  jobStatus: JobStatus | null;
  summary: JobSummary | null;
  /** How long the job that `summary` belongs to took, from the press of the button to the job-done event; null while there is none. */
  elapsedMs: number | null;
  tools: ToolsStatus | null;
  toolBusy: boolean;
  toolMessage: string | null;
}

export function initialUiState(overrides: Partial<UiState> = {}): UiState {
  return {
    phase: "IDLE",
    input: "",
    outputDir: "",
    overwrite: false,
    includeRank: true,
    searchLyricsOnline: true,
    singleRank: 1,
    albumName: null,
    resolved: null,
    rows: [],
    error: null,
    jobStatus: null,
    summary: null,
    elapsedMs: null,
    tools: null,
    toolBusy: false,
    toolMessage: null,
    ...overrides,
  };
}

export function failedRanks(state: UiState): number[] {
  return state.rows.filter((row) => row.status.kind === "failed").map((row) => row.item.rank);
}

export function canResolve(state: UiState): boolean {
  return state.phase !== "RESOLVING" && state.phase !== "RUNNING";
}

/**
 * The options the app restores on the next start. The typed playlist or video address and the rank of a single video are
 * not part of it. `outputDir` is null (or blank) when the user never chose a folder: the app then uses its default.
 * `searchLyricsOnline` is on for a settings file written before the option existed.
 */
export interface UserSettings {
  outputDir: string | null;
  overwrite: boolean;
  includeRank: boolean;
  searchLyricsOnline: boolean;
}

export function userSettings(overrides: Partial<UserSettings> = {}): UserSettings {
  return { outputDir: null, overwrite: false, includeRank: true, searchLyricsOnline: true, ...overrides };
}

/** Where the remembered options live. */
export interface SettingsStore {
  /** The saved options, or the defaults when there are none or they cannot be read. Never throws. */
  load(): UserSettings;
  /** Stores `settings`. A store that cannot write does nothing. Never throws. */
  save(settings: UserSettings): void;
}

/** Remembers nothing: loads the defaults and drops what it is asked to save. */
export const noSettingsStore: SettingsStore = {
  load: () => userSettings(),
  save: () => {},
};

/** Everything the screen logic needs from the server; the `Bridge` of `bridge/bridge.ts` is one. */
export interface XgsApi {
  tools(): Promise<ToolsStatus>;
  installYtDlp(): Promise<ActionResult>;
  updateYtDlp(): Promise<ActionResult>;
  resolve(input: string): Promise<ResolveResponse>;
  startJob(request: JobRequest): Promise<JobCreated>;
  /** Calls `onEvent` for each event and resolves after `job-done`; rejects when the stream fails. */
  watchJob(jobId: string, onEvent: (event: JobEvent) => void): Promise<void>;
  cancelJob(jobId: string): Promise<void>;
}
```

`shell/src/state/labels.ts`:

```typescript
// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt: the texts of the screen.

import type { LyricsOutcome } from "../bridge/types.ts";
import { MAX_RANK, MIN_RANK, destinationFolder } from "../logic/filename.ts";
import { trim, trimEndChars } from "../logic/text.ts";
import type { ItemStatus, UiState } from "./types.ts";

/** The label of the button that updates yt-dlp; FORMAT_FAILURE_HELP points to it by this name. */
export const UPDATE_YT_DLP_LABEL = "yt-dlp 업데이트";

/**
 * The help shown next to the update button of the tools panel. "m4a 오디오 형식이 없습니다." (the engine's message) is also what
 * an outdated yt-dlp or a broken JavaScript runtime makes every song fail with, so the user is told to check those first.
 */
export const FORMAT_FAILURE_HELP =
  "재생목록의 모든 곡이 “m4a 오디오 형식이 없습니다.”로 실패하면 영상 문제가 아니라 yt-dlp가 낡았거나 " +
  `JavaScript 런타임(Node.js, Deno)에 문제가 있는 것일 수 있습니다. 앱 위쪽의 ${UPDATE_YT_DLP_LABEL} 버튼으로 ` +
  "최신 버전으로 바꾸고 Node.js 22+ 또는 Deno 2.3+가 설치돼 있는지 확인한 뒤 다시 받으세요.";

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

/** A finished row says where its lyrics came from or why it has none. Plain `완료` when the server did not report an outcome. */
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

/** The one-line result shown after a job ends, with how long it took when that is known; null while there is nothing to report. */
export function summaryText(state: UiState): string | null {
  const summary = state.summary;
  if (summary === null) return null;
  const took = state.elapsedMs === null ? "" : ` · 소요 ${elapsedLabel(state.elapsedMs)}`;
  const counts = `성공 ${summary.succeeded} · 건너뜀 ${summary.skipped} · 실패 ${summary.failed}${took}`;
  switch (state.jobStatus) {
    case "COMPLETED":
      return `완료 — ${counts}`;
    case "CANCELLED":
      return `취소됨 — ${counts}`;
    case "FAILED":
      return `중단됨 — ${counts}`;
    case null:
      return null;
  }
}

/** `elapsedMs` in whole seconds as `42초`, `3분 05초` or `1시간 02분 05초`; never negative. */
export function elapsedLabel(elapsedMs: number): string {
  const total = Math.max(Math.trunc(elapsedMs / 1000), 0);
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const seconds = total % 60;
  const two = (value: number) => String(value).padStart(2, "0");
  if (hours > 0) return `${hours}시간 ${two(minutes)}분 ${two(seconds)}초`;
  if (minutes > 0) return `${minutes}분 ${two(seconds)}초`;
  return `${seconds}초`;
}

/**
 * Where the files will be saved: a folder named after the album name the user typed inside the output folder, else a folder
 * named after the playlist for a playlist, else the output folder itself for a video (the same names the server uses). Null
 * until something is resolved and an output folder is typed.
 */
export function destinationPath(state: UiState): string | null {
  const resolved = state.resolved;
  if (resolved === null) return null;
  if (trim(state.outputDir) === "") return null;
  const folder = destinationFolder(resolved.kind === "PLAYLIST", resolved.playlistTitle, state.albumName);
  if (folder === null) return state.outputDir;
  return trimEndChars(state.outputDir, "\\/") + "\\" + folder;
}

/**
 * What the album name field shows: the text the user typed, else the title of the playlist, which the files get as their
 * album when the field is left alone (a single video keeps the album of its own then), else nothing. The folder is named
 * after the same text (see `destinationPath`).
 */
export function albumNameText(state: UiState): string {
  if (state.albumName !== null) return state.albumName;
  const resolved = state.resolved;
  return resolved !== null && resolved.kind === "PLAYLIST" ? (resolved.playlistTitle ?? "") : "";
}

/** The line shown above the options: `destinationPath` with its caption; null when there is no destination yet. */
export function destinationLabel(state: UiState): string | null {
  const path = destinationPath(state);
  return path === null ? null : `저장 위치: ${path}`;
}

/** The zero-padded rank shown in the list, e.g. `007`. */
export function rankLabel(rank: number): string {
  return String(rank).padStart(3, "0");
}

/** What the rank text field may hold while the user types: digits only, at most three of them. (ASCII digits only.) */
export function rankInputText(raw: string): string {
  return raw.replace(/[^0-9]/g, "").slice(0, 3);
}

/** The rank typed so far, kept within the valid range; null while the field is empty. */
export function rankFromInput(text: string): number | null {
  if (!/^[0-9]+$/.test(text)) return null;
  return Math.min(Math.max(Number.parseInt(text, 10), MIN_RANK), MAX_RANK);
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/labels.test.ts 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"
```

Expected: `ℹ tests 35`, `ℹ pass 35`, `ℹ fail 0`, `tsc exit 0`.

- [ ] **Step 5: Commit**

```bash
git add shell/src/state shell/tests/labels.test.ts shell/tests/support/fixtures.ts
git commit -m "feat(shell): UiState와 Labels를 옮기고 원본 테스트 35개를 더한다"
```

---

### Task 5: `AppStateHolder`

**Files:**
- Create: `shell/src/state/holder.ts`, `shell/tests/support/fakes.ts`, `shell/tests/holder.test.ts`, `shell/tests/kotlin-coverage.test.ts`

**Interfaces:**
- Consumes: Task 1의 `format`, `MIN_RANK`, `MAX_RANK`, `isNullOrBlank`; Task 2의 `classify`, `canonicalUrl`, `REJECT_MESSAGES`; Task 4의 `types.ts`; 2단계의 `JobEvent`, `JobRequest`, `JobOptions`
- Produces (Task 6~7이 쓴다): `AppStateHolder`(생성자 `{api, defaultOutputDir, settings?, now?, scheduler?}`), `getState()`, `subscribe(listener)`, `onInput`, `onOutputDir`, `onOverwrite`, `onIncludeRank`, `onSearchLyricsOnline`, `onAlbumName`, `onSingleRank`, `dismissError`, `flushSettings`, `reset`, `refreshTools`, `installYtDlp`, `updateYtDlp`, `resolve`, `switchToVideoOnly`, `startDownload`, `retryFailed`, `cancel`; `Scheduler`, `failureMessage(error)`
- 테스트 도구: `FakeApi`(`send`, `closeEvents`, `resolveGate`, …), `FakeSettingsStore`, `FakeTimers`(`advance`), `Deferred`, `flush`, `apiError`

원본의 코루틴 테스트는 `runCurrent()`를 `await flush()`로, `advanceTimeBy(n)`을 `timers.advance(n)`으로, 시간 원천을 `clock.ms`로 바꿨다. 원본 95개는 같은 이름이고 포트가 3개를 더한다(구독, `reset` 뒤의 옛 작업 이벤트 무시, 서버의 문구가 아닌 실패의 문구). `kotlin-coverage.test.ts`는 네 스위트의 원본 파일을 읽어 `@Test fun 이름`이 포트에 같은 이름으로 있는지 본다(`app/`이 사라지면 그 스위트는 건너뛴다).

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`shell/tests/support/fakes.ts`:

```typescript
// The stand-ins the holder tests need: FakeApi.kt, FakeSettingsStore.kt, and a timer that only moves when the test says so.

import type {
  ActionResult,
  JobCreated,
  JobEvent,
  JobRequest,
  ResolveResponse,
  ToolInfo,
  ToolsStatus,
} from "../../src/bridge/types.ts";
import type { Scheduler } from "../../src/state/holder.ts";
import { userSettings, type SettingsStore, type UserSettings, type XgsApi } from "../../src/state/types.ts";
import { playlist } from "./fixtures.ts";

/** A failure the way the Rust bridge reports one: a plain object with a message fit for the user (Kotlin's `ApiError`). */
export function apiError(message: string): { message: string } {
  return { message };
}

export class Deferred<T = void> {
  readonly promise: Promise<T>;
  resolve!: (value: T) => void;
  reject!: (reason: unknown) => void;

  constructor() {
    this.promise = new Promise<T>((resolve, reject) => {
      this.resolve = resolve;
      this.reject = reject;
    });
  }
}

/** Lets every promise that can settle settle: the equivalent of the coroutine tests' `runCurrent()`. */
export function flush(): Promise<void> {
  return new Promise((resolve) => setImmediate(resolve));
}

const found = (version: string): ToolInfo => ({ found: true, version, path: null });

/** What the "server" sends for the running job: buffered until the screen listens, ended by `closeEvents`. */
class EventQueue {
  private readonly items: JobEvent[] = [];
  private closed = false;
  private wake: (() => void) | null = null;

  push(event: JobEvent): void {
    this.items.push(event);
    this.wake?.();
  }

  close(): void {
    this.closed = true;
    this.wake?.();
  }

  /** Delivers the events in order, and returns when the queue is closed and empty. */
  async drain(onEvent: (event: JobEvent) => void): Promise<void> {
    for (;;) {
      const next = this.items.shift();
      if (next !== undefined) {
        onEvent(next);
        continue;
      }
      if (this.closed) return;
      await new Promise<void>((resolve) => {
        this.wake = () => {
          this.wake = null;
          resolve();
        };
      });
    }
  }
}

export class FakeApi implements XgsApi {
  toolsStatus: ToolsStatus = { ytDlp: found("1"), ffmpeg: found("1"), jsRuntime: found("24") };
  resolveResponse: ResolveResponse = playlist();
  resolveError: unknown = null;
  /** When set, `resolve` waits for it before it answers (or fails), so a test can change things while a lookup is held. */
  resolveGate: Deferred | null = null;
  startError: unknown = null;
  toolsError: unknown = null;
  installMessage = "installed";

  readonly resolveInputs: string[] = [];
  readonly jobRequests: JobRequest[] = [];
  readonly cancelled: string[] = [];
  installCalls = 0;

  private events = new EventQueue();

  /** Sends `event` to the screen as the running job's next event. */
  send(...events: JobEvent[]): void {
    for (const event of events) this.events.push(event);
  }

  /** Ends the running job's event stream. */
  closeEvents(): void {
    this.events.close();
  }

  async tools(): Promise<ToolsStatus> {
    if (this.toolsError !== null) throw this.toolsError;
    return this.toolsStatus;
  }

  async installYtDlp(): Promise<ActionResult> {
    if (this.toolsError !== null) throw this.toolsError;
    this.installCalls += 1;
    return { message: this.installMessage };
  }

  async updateYtDlp(): Promise<ActionResult> {
    return { message: "updated" };
  }

  async resolve(input: string): Promise<ResolveResponse> {
    this.resolveInputs.push(input);
    if (this.resolveGate !== null) await this.resolveGate.promise;
    if (this.resolveError !== null) throw this.resolveError;
    return this.resolveResponse;
  }

  async startJob(request: JobRequest): Promise<JobCreated> {
    if (this.startError !== null) throw this.startError;
    this.jobRequests.push(request);
    this.events = new EventQueue();
    return { jobId: `job-${this.jobRequests.length}` };
  }

  watchJob(_jobId: string, onEvent: (event: JobEvent) => void): Promise<void> {
    return this.events.drain(onEvent);
  }

  async cancelJob(jobId: string): Promise<void> {
    this.cancelled.push(jobId);
  }
}

/** Hands out `stored` on `load` and records every `save`, in order. */
export class FakeSettingsStore implements SettingsStore {
  stored: UserSettings;
  readonly saved: UserSettings[] = [];
  loadCalls = 0;

  constructor(stored: UserSettings = userSettings()) {
    this.stored = stored;
  }

  load(): UserSettings {
    this.loadCalls += 1;
    return this.stored;
  }

  save(settings: UserSettings): void {
    this.saved.push(settings);
  }
}

/** Timers on a clock that moves only by `advance`: `setTimeout` callbacks run when the time they wait for has passed. */
export class FakeTimers implements Scheduler {
  private time = 0;
  private nextId = 1;
  private readonly tasks = new Map<number, { at: number; callback: () => void }>();

  setTimeout(callback: () => void, ms: number): number {
    const id = this.nextId++;
    this.tasks.set(id, { at: this.time + ms, callback });
    return id;
  }

  clearTimeout(handle: unknown): void {
    this.tasks.delete(handle as number);
  }

  /** Moves the clock forward by `ms`, running (in order) every callback that falls due on the way, the one at the end included. */
  advance(ms: number): void {
    const target = this.time + ms;
    for (;;) {
      let nextId: number | null = null;
      for (const [id, task] of this.tasks) {
        if (task.at <= target && (nextId === null || task.at < (this.tasks.get(nextId) as { at: number }).at)) nextId = id;
      }
      if (nextId === null) break;
      const task = this.tasks.get(nextId) as { at: number; callback: () => void };
      this.tasks.delete(nextId);
      this.time = task.at;
      task.callback();
    }
    this.time = target;
  }

  /** How many callbacks are waiting. */
  get pending(): number {
    return this.tasks.size;
  }
}
```

`shell/tests/holder.test.ts`(원본 95개와 포트의 3개):

```typescript
// The scenarios of app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt, one test per Kotlin test and under
// the same name (tests/kotlin-coverage.test.ts checks that none is missing). Kotlin's `runCurrent()` is `await flush()`,
// `advanceTimeBy` is `timers.advance`, and the test time source is `clock.ms`.

import assert from "node:assert/strict";
import test from "node:test";
import type { JobEvent, JobOptions, JobRequest, JobStatus, LyricsOutcome, ResolvedItem, Stage } from "../src/bridge/types.ts";
import { AppStateHolder } from "../src/state/holder.ts";
import { albumNameText, statusLabel, summaryText } from "../src/state/labels.ts";
import { failedRanks, noSettingsStore, userSettings, type ItemRow, type ItemStatus, type SettingsStore, type UserSettings } from "../src/state/types.ts";
import { REJECT_MESSAGES } from "../src/logic/input.ts";
import { Deferred, FakeApi, FakeSettingsStore, FakeTimers, apiError, flush } from "./support/fakes.ts";
import { LYRICS_OUTCOMES, item, playlist, unavailableItem, video } from "./support/fixtures.ts";

const playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV";

// ---- the rig ---------------------------------------------------------------------------

interface Rig {
  api: FakeApi;
  holder: AppStateHolder;
  timers: FakeTimers;
  /** The time source of the holder: the test moves it. */
  clock: { ms: number };
}

function rig({ api = new FakeApi(), store = noSettingsStore }: { api?: FakeApi; store?: SettingsStore } = {}): Rig {
  const timers = new FakeTimers();
  const clock = { ms: 0 };
  const holder = new AppStateHolder({
    api,
    defaultOutputDir: "C:/Music/xGetSongs",
    settings: store,
    now: () => clock.ms,
    scheduler: timers,
  });
  return { api, holder, timers, clock };
}

/** A rig whose holder has resolved the sample playlist (or whatever the api answers). */
async function resolved(options: { api?: FakeApi; store?: SettingsStore } = {}): Promise<Rig> {
  const made = rig(options);
  made.holder.onInput(playlistId);
  made.holder.resolve();
  await flush();
  return made;
}

/** Resolved, started, and ended with rank 1 done and rank 3 failed ("boom"). */
async function finishedWithOneFailure(): Promise<Rig> {
  const made = await resolved();
  made.holder.startDownload();
  await flush();
  made.api.send(itemDone(1, "001 A1 - T1.m4a"), itemFailed(3, "boom"), jobDone("COMPLETED", 1, 0, 1));
  made.api.closeEvents();
  await flush();
  return made;
}

const state = (holder: AppStateHolder) => holder.getState();

function row(holder: AppStateHolder, rank: number): ItemRow {
  const found = holder.getState().rows.find((candidate) => candidate.item.rank === rank);
  assert.ok(found, `no row of rank ${rank}`);
  return found;
}

const optionsOf = (request: JobRequest): JobOptions => request.options as JobOptions;
const lastRequest = (api: FakeApi): JobRequest => api.jobRequests[api.jobRequests.length - 1];

// ---- the events a job sends ----------------------------------------------------------------

const itemStarted = (rank: number, videoId: string, fileName: string): JobEvent => ({ type: "item-started", rank, videoId, fileName });
const progress = (rank: number, stage: Stage, percent: number | null = null): JobEvent => ({ type: "progress", rank, stage, percent });
const itemDone = (rank: number, fileName: string, lyrics: LyricsOutcome | null = null): JobEvent => ({ type: "item-done", rank, fileName, lyrics });
const itemSkipped = (rank: number, reason: string): JobEvent => ({ type: "item-skipped", rank, reason });
const itemFailed = (rank: number, message: string): JobEvent => ({ type: "item-failed", rank, message });
const jobDone = (status: JobStatus, succeeded: number, skipped: number, failed: number): JobEvent => ({
  type: "job-done",
  status,
  summary: { succeeded, skipped, failed },
});

// ---- the statuses a row can have -----------------------------------------------------------

const ready: ItemStatus = { kind: "ready" };
const waiting: ItemStatus = { kind: "waiting" };
const finishing: ItemStatus = { kind: "finishing" };
const downloading = (percent: number | null): ItemStatus => ({ kind: "downloading", percent });
const done = (lyrics: LyricsOutcome | null = null): ItemStatus => ({ kind: "done", lyrics });
const skipped = (reason: string): ItemStatus => ({ kind: "skipped", reason });
const failed = (message: string): ItemStatus => ({ kind: "failed", message });

// ---- resolving ------------------------------------------------------------------------

test("startsIdleWithTheDefaultOutputFolder", () => {
  const { holder } = rig();

  assert.equal(state(holder).phase, "IDLE");
  assert.equal(state(holder).outputDir, "C:/Music/xGetSongs");
});

test("resolveShowsAPreviewOfEveryItem", async () => {
  const { api, holder } = await resolved();

  assert.equal(state(holder).phase, "PREVIEW");
  assert.deepEqual(api.resolveInputs, [playlistId]);
  assert.deepEqual(state(holder).rows.map((r) => r.item.rank), [1, 2, 3]);
  assert.deepEqual(row(holder, 1).status, ready);
  assert.deepEqual(row(holder, 2).status, skipped("비공개 영상"));
  assert.equal(row(holder, 1).fileName, "001 A1 - T1.m4a");
});

test("invalidInputIsRejectedBeforeAnyRequest", async () => {
  const { api, holder } = rig();
  holder.onInput("https://evil.com/x");

  holder.resolve();
  await flush();

  assert.equal(state(holder).error, REJECT_MESSAGES.UNSUPPORTED_HOST);
  assert.equal(api.resolveInputs.length, 0);
  assert.equal(state(holder).phase, "IDLE");
});

test("serverErrorsAreShownAndTheScreenStaysUsable", async () => {
  const api = new FakeApi();
  api.resolveError = apiError("비공개 재생목록");
  const { holder } = await resolved({ api });

  assert.equal(state(holder).error, "비공개 재생목록");
  assert.equal(state(holder).phase, "IDLE");
});

test("switchingToTheVideoOnlyResolvesTheVideoUrl", async () => {
  const api = new FakeApi();
  api.resolveResponse = playlist("dQw4w9WgXcQ");
  const { holder } = await resolved({ api });

  holder.switchToVideoOnly();
  await flush();

  assert.equal(api.resolveInputs[api.resolveInputs.length - 1], "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
});

// ---- single video rank ----------------------------------------------------------------

test("changingTheRankRewritesTheSingleVideoFileName", async () => {
  const api = new FakeApi();
  api.resolveResponse = video();
  const { holder } = await resolved({ api });

  holder.onSingleRank(42);

  assert.equal(row(holder, 42).fileName, "042 IU - Love.m4a");
  assert.equal(state(holder).singleRank, 42);
});

test("rankIsClampedToOneThrough999", () => {
  const { holder } = rig();

  holder.onSingleRank(0);
  assert.equal(state(holder).singleRank, 1);
  holder.onSingleRank(5000);
  assert.equal(state(holder).singleRank, 999);
});

// ---- rank in the file name ------------------------------------------------------------

test("theRankIsPartOfTheFileNameUnlessTheUserTurnsItOff", () => {
  const { holder } = rig();

  assert.equal(state(holder).includeRank, true);
  holder.onIncludeRank(false);
  assert.equal(state(holder).includeRank, false);
  holder.onIncludeRank(true);
  assert.equal(state(holder).includeRank, true);
});

test("turningTheRankOffRewritesTheFileNamesOfThePreview", async () => {
  const { holder } = await resolved();

  holder.onIncludeRank(false);

  assert.equal(state(holder).phase, "PREVIEW");
  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "A3 - T3.m4a");
  assert.deepEqual(state(holder).rows.map((r) => r.item.rank), [1, 2, 3], "the position column stays");
  assert.deepEqual(row(holder, 1).status, ready);
});

test("turningTheRankBackOnRestoresTheRankedFileNames", async () => {
  const { holder } = await resolved();

  holder.onIncludeRank(false);
  holder.onIncludeRank(true);

  assert.equal(row(holder, 1).fileName, "001 A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "003 A3 - T3.m4a");
});

test("unavailableRowsKeepTheirTextWhenTheOptionChanges", async () => {
  const unavailable: ResolvedItem = { ...unavailableItem(2, "비공개 영상"), expectedFileName: "kept as it was.m4a" };
  const api = new FakeApi();
  api.resolveResponse = { ...playlist(), items: [item(1), unavailable] };
  const { holder } = await resolved({ api });

  holder.onIncludeRank(false);

  assert.equal(row(holder, 2).fileName, "kept as it was.m4a");
  assert.deepEqual(row(holder, 2).status, skipped("비공개 영상"));
  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
});

test("resolvingWithTheRankOffShowsFileNamesWithoutTheRank", async () => {
  const { holder } = rig();
  holder.onIncludeRank(false);
  holder.onInput(playlistId);

  holder.resolve();
  await flush();

  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "A3 - T3.m4a");
  assert.equal(row(holder, 2).fileName, null);
});

test("aVideoResolvedWithTheRankOffShowsAFileNameWithoutTheRank", async () => {
  const api = new FakeApi();
  api.resolveResponse = video();
  const { holder } = rig({ api });
  holder.onIncludeRank(false);
  holder.onSingleRank(42);
  holder.onInput("https://www.youtube.com/watch?v=dQw4w9WgXcQ");

  holder.resolve();
  await flush();

  assert.equal(row(holder, 42).fileName, "IU - Love.m4a");
});

test("changingTheSingleVideoRankKeepsTheOption", async () => {
  const api = new FakeApi();
  api.resolveResponse = video();
  const { holder } = await resolved({ api });
  holder.onIncludeRank(false);

  holder.onSingleRank(42);

  assert.equal(row(holder, 42).fileName, "IU - Love.m4a");
  assert.equal(state(holder).singleRank, 42);
  holder.onIncludeRank(true);
  assert.equal(row(holder, 42).fileName, "042 IU - Love.m4a", "the rank typed by the user is what comes back");
});

test("theOptionChosenWhileResolvingAppliesToTheResult", async () => {
  const { api, holder } = rig();
  holder.onInput(playlistId);
  holder.resolve();
  holder.onIncludeRank(false); // the lookup has not finished yet: there is nothing to rewrite
  await flush();

  assert.equal(state(holder).phase, "PREVIEW");
  assert.deepEqual(api.resolveInputs, [playlistId]);
  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
});

test("startingSendsTheIncludeRankOption", async () => {
  const { api, holder } = await resolved();

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).includeRank, true, "on by default");
});

test("startingWithTheRankOffSendsIt", async () => {
  const { api, holder } = await resolved();
  holder.onIncludeRank(false);

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).includeRank, false);
});

test("aRetrySendsTheOptionToo", async () => {
  const { api, holder } = await finishedWithOneFailure();
  holder.onIncludeRank(false);

  holder.retryFailed();
  await flush();

  assert.deepEqual(lastRequest(api).ranks, [3]);
  assert.equal(optionsOf(lastRequest(api)).includeRank, false);
});

test("turningTheRankOffWhileAJobRunsLeavesTheRowsOfThatJobAlone", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemStarted(1, "vid00000001", "001 Real - Name.m4a"));
  await flush();

  holder.onIncludeRank(false);

  assert.equal(state(holder).phase, "RUNNING");
  assert.equal(state(holder).includeRank, false);
  assert.equal(row(holder, 1).fileName, "001 Real - Name.m4a");
  assert.equal(row(holder, 3).fileName, "003 A3 - T3.m4a");
  assert.deepEqual(row(holder, 1).status, downloading(null));
});

test("turningTheRankOffAfterAJobFinishedLeavesTheRealFileNamesAlone", async () => {
  const { holder } = await finishedWithOneFailure();

  holder.onIncludeRank(false);

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(row(holder, 1).fileName, "001 A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "003 A3 - T3.m4a");
  assert.equal(state(holder).includeRank, false);
});

test("resetKeepsTheIncludeRankOption", async () => {
  const { holder } = await resolved();
  holder.onIncludeRank(false);

  holder.reset();

  assert.equal(state(holder).phase, "IDLE");
  assert.equal(state(holder).includeRank, false);
});

// ---- lyrics search on the internet ----------------------------------------------------

test("theLyricsSearchIsOnUntilTheUserTurnsItOff", () => {
  const { holder } = rig();

  assert.equal(state(holder).searchLyricsOnline, true);
  holder.onSearchLyricsOnline(false);
  assert.equal(state(holder).searchLyricsOnline, false);
  holder.onSearchLyricsOnline(true);
  assert.equal(state(holder).searchLyricsOnline, true);
});

test("startingSendsTheSearchLyricsOnlineOption", async () => {
  const { api, holder } = await resolved();

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).searchLyricsOnline, true, "on by default");
});

test("startingWithTheLyricsSearchOffSendsIt", async () => {
  const { api, holder } = await resolved();
  holder.onSearchLyricsOnline(false);

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).searchLyricsOnline, false);
  assert.equal(optionsOf(api.jobRequests[0]).includeRank, true, "the other options are not touched");
});

test("aRetrySendsTheLyricsOptionToo", async () => {
  const { api, holder } = await finishedWithOneFailure();
  holder.onSearchLyricsOnline(false);

  holder.retryFailed();
  await flush();

  assert.deepEqual(lastRequest(api).ranks, [3]);
  assert.equal(optionsOf(lastRequest(api)).searchLyricsOnline, false);
});

// ---- how long a job took ---------------------------------------------------------------

test("aFinishedJobKnowsHowLongItTookFromTheStartToTheEnd", async () => {
  const { api, holder, clock } = await resolved();
  assert.equal(state(holder).elapsedMs, null);

  clock.ms += 5_000; // thinking time before the button is pressed does not count
  holder.startDownload();
  await flush();
  clock.ms += 83_000;
  api.send(jobDone("COMPLETED", 1, 0, 0));
  await flush();

  assert.equal(state(holder).elapsedMs, 83_000);
  assert.equal(summaryText(state(holder)), "완료 — 성공 1 · 건너뜀 0 · 실패 0 · 소요 1분 23초");
});

test("aCancelledJobKnowsHowLongItRan", async () => {
  const { api, holder, clock } = await resolved();
  holder.startDownload();
  await flush();

  clock.ms += 9_000;
  api.send(jobDone("CANCELLED", 0, 0, 0));
  await flush();

  assert.equal(state(holder).elapsedMs, 9_000);
});

test("aNewJobStartsTheClockAgainAndClearsTheOldTime", async () => {
  const { api, holder, clock } = await finishedWithOneFailure();
  assert.equal(state(holder).elapsedMs, 0, "the helper's job ended at once");

  holder.retryFailed();
  await flush();
  assert.equal(state(holder).elapsedMs, null, "no time is shown while the job runs");
  clock.ms += 12_000;
  api.send(jobDone("COMPLETED", 1, 0, 0));
  await flush();

  assert.equal(state(holder).elapsedMs, 12_000, "only the retry is timed");
});

test("noTimeIsShownWhenTheConnectionBreaksBeforeTheJobEnds", async () => {
  const { api, holder, clock } = await resolved();
  holder.startDownload();
  await flush();

  clock.ms += 30_000;
  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).elapsedMs, null);
});

test("resetClearsTheTime", async () => {
  const { api, holder, clock } = await resolved();
  holder.startDownload();
  await flush();
  clock.ms += 3_000;
  api.send(jobDone("COMPLETED", 1, 0, 0));
  await flush();

  holder.reset();

  assert.equal(state(holder).elapsedMs, null);
});

// ---- the album name typed by the user, which is also the folder name ------------------------

test("startingWithoutATypedAlbumNameSendsNone", async () => {
  const { api, holder } = await resolved();
  holder.onAlbumName("   ");

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).albumName, null, "blank is the same as nothing typed");
});

test("startingSendsTheTypedAlbumName", async () => {
  const { api, holder } = await resolved();
  holder.onAlbumName("내 앨범");

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).albumName, "내 앨범");
});

test("aRetrySendsTheTypedAlbumNameToo", async () => {
  const { api, holder } = await finishedWithOneFailure();
  holder.onAlbumName("내 앨범");

  holder.retryFailed();
  await flush();

  assert.equal(optionsOf(lastRequest(api)).albumName, "내 앨범");
});

test("theTypedAlbumNameLeavesThePreviewAndTheFileNamesAlone", async () => {
  const { holder } = await resolved();
  const before = state(holder).rows;

  holder.onAlbumName("내 앨범");

  assert.deepEqual(state(holder).rows, before);
});

test("resetClearsTheTypedAlbumName", async () => {
  const { holder } = await resolved();
  holder.onAlbumName("내 앨범");

  holder.reset();

  assert.equal(state(holder).albumName, null);
});

test("anAlbumNameTheUserDidNotTouchIsNotSentEvenThoughTheFieldShowsTheDefault", async () => {
  const { api, holder } = await resolved();
  assert.equal(albumNameText(state(holder)), "Sample", "the field shows the playlist title");

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).albumName, null, "the server uses the playlist title itself");
});

test("aTouchedFieldSendsItsTextEvenWhenItIsTheDefault", async () => {
  const { api, holder } = await resolved();
  holder.onAlbumName("Sample");

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).albumName, "Sample", "the user asked for the title on every file");
});

test("aTouchedFieldKeepsItsTextWhenSomethingIsResolvedAgain", async () => {
  const api = new FakeApi();
  const { holder } = await resolved({ api });
  holder.onAlbumName("내 앨범");

  api.resolveResponse = video();
  holder.resolve();
  await flush();

  assert.equal(albumNameText(state(holder)), "내 앨범", "typed text stays");
});

test("anUntouchedFieldFollowsTheNewDefaultWhenSomethingIsResolvedAgain", async () => {
  const api = new FakeApi();
  const { holder } = await resolved({ api });
  assert.equal(albumNameText(state(holder)), "Sample");

  api.resolveResponse = video();
  holder.resolve();
  await flush();

  assert.equal(albumNameText(state(holder)), "", "a video has no title to show");
});

test("theTypedAlbumNameIsNeverRemembered", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = rig({ store });

  holder.onAlbumName("내 앨범");
  timers.advance(5_000);
  await flush();
  holder.flushSettings();

  assert.deepEqual(store.saved, []);
});

test("theLyricsOptionLeavesThePreviewAndTheFileNamesAlone", async () => {
  const { holder } = await resolved();
  const before = state(holder).rows;

  holder.onSearchLyricsOnline(false);

  assert.equal(state(holder).phase, "PREVIEW");
  assert.deepEqual(state(holder).rows, before);
});

test("resetKeepsTheSearchLyricsOnlineOption", async () => {
  const { holder } = await resolved();
  holder.onSearchLyricsOnline(false);

  holder.reset();

  assert.equal(state(holder).phase, "IDLE");
  assert.equal(state(holder).searchLyricsOnline, false);
});

// ---- remembered options ---------------------------------------------------------------

const holderWith = (store: FakeSettingsStore) => rig({ store });

/** What the holder saves when only the folder differs from the defaults of a fresh FakeSettingsStore. */
const defaultsWith = (overrides: Partial<UserSettings> = {}): UserSettings =>
  userSettings({ outputDir: "C:/Music/xGetSongs", overwrite: false, includeRank: true, searchLyricsOnline: true, ...overrides });

async function assertSavedOnlyAfterTheQuietPeriod(timers: FakeTimers, store: FakeSettingsStore, expected: UserSettings): Promise<void> {
  timers.advance(399);
  await flush();
  assert.deepEqual(store.saved, [], "nothing is saved before 400 ms have passed");
  timers.advance(1);
  await flush();
  assert.deepEqual(store.saved, [expected]);
}

test("theOptionsStartFromTheStoredSettings", () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: "D:/음악", overwrite: true, includeRank: false, searchLyricsOnline: false }));

  const { holder } = holderWith(store);

  const current = state(holder);
  assert.equal(current.outputDir, "D:/음악");
  assert.equal(current.overwrite, true);
  assert.equal(current.includeRank, false);
  assert.equal(current.searchLyricsOnline, false);
  assert.equal(current.phase, "IDLE");
  assert.equal(store.loadCalls, 1);
});

test("aStoredFileFromBeforeTheLyricsOptionStartsWithTheSearchOn", () => {
  // What an old settings file loads as: every key it has, and the default for the one it lacks.
  const { holder } = holderWith(new FakeSettingsStore(userSettings({ outputDir: "D:/Songs", overwrite: true })));

  assert.equal(state(holder).searchLyricsOnline, true);
});

test("changingTheLyricsSearchSavesTheValuesAfterAQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onSearchLyricsOnline(false);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ searchLyricsOnline: false }));
});

test("theLyricsOptionIsSavedWhileAJobRunsToo", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = await resolved({ store });
  holder.startDownload();
  await flush();
  assert.equal(state(holder).phase, "RUNNING");

  holder.onSearchLyricsOnline(false);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ searchLyricsOnline: false }));
});

test("theLyricsOptionIsRestoredByTheNextHolderFromWhatWasSaved", () => {
  const store = new FakeSettingsStore();
  const first = holderWith(store).holder;
  first.onSearchLyricsOnline(false);
  first.flushSettings();
  assert.deepEqual(store.saved, [defaultsWith({ searchLyricsOnline: false })]);
  store.stored = store.saved[0]; // what the file holds after the first run

  const second = holderWith(store).holder;

  assert.equal(state(second).searchLyricsOnline, false);
});

test("settingTheLyricsOptionToTheValueItAlreadyHasSavesNothing", async () => {
  const store = new FakeSettingsStore(userSettings({ searchLyricsOnline: false }));
  const { holder, timers } = holderWith(store);

  holder.onSearchLyricsOnline(false);
  timers.advance(5_000);
  await flush();
  holder.flushSettings();

  assert.deepEqual(store.saved, []);
});

test("aMissingOrBlankStoredFolderFallsBackToTheDefaultFolder", () => {
  for (const stored of [null, "", "   "]) {
    const { holder } = holderWith(new FakeSettingsStore(userSettings({ outputDir: stored, overwrite: true })));

    assert.equal(state(holder).outputDir, "C:/Music/xGetSongs", `stored folder: ${stored}`);
    assert.equal(state(holder).overwrite, true, "the other options are still taken from the store");
  }
});

test("changingTheOutputFolderSavesTheValuesAfterAQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOutputDir("D:/Songs");

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ outputDir: "D:/Songs" }));
});

test("changingOverwriteSavesTheValuesAfterAQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOverwrite(true);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ overwrite: true }));
});

test("changingTheRankOptionSavesTheValuesAfterAQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onIncludeRank(false);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ includeRank: false }));
});

test("theRankOptionIsSavedWhileAJobRunsToo", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = await resolved({ store });
  holder.startDownload();
  await flush();
  assert.equal(state(holder).phase, "RUNNING");

  holder.onIncludeRank(false);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ includeRank: false }));
});

test("quickChangesAreSavedOnceWithTheLastValues", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOutputDir("D:/S");
  timers.advance(300);
  holder.onOutputDir("D:/Songs");
  timers.advance(300);
  holder.onIncludeRank(false);
  timers.advance(399);
  await flush();
  assert.deepEqual(store.saved, [], "every change starts the quiet period again");
  timers.advance(1);
  await flush();

  assert.deepEqual(store.saved, [defaultsWith({ outputDir: "D:/Songs", includeRank: false })]);
  timers.advance(5_000);
  await flush();
  assert.equal(store.saved.length, 1, "no second save follows");
});

test("settingAValueThatIsAlreadySetSavesNothing", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOverwrite(false);
  holder.onIncludeRank(true);
  holder.onSearchLyricsOnline(true);
  holder.onOutputDir("C:/Music/xGetSongs");
  timers.advance(5_000);
  await flush();

  assert.deepEqual(store.saved, []);
});

test("repeatingAValueDoesNotRestartTheQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOverwrite(true);
  timers.advance(300);
  holder.onOverwrite(true);
  timers.advance(99);
  await flush();
  assert.deepEqual(store.saved, []);
  timers.advance(1);
  await flush();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true })]);
});

test("flushSettingsSavesRightAwayAndCancelsThePendingSave", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onOverwrite(true);
  timers.advance(100);
  await flush();
  assert.deepEqual(store.saved, []);

  holder.flushSettings();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true })], "saved at once, without waiting");
  timers.advance(5_000);
  await flush();
  assert.equal(store.saved.length, 1, "the pending save was cancelled, so there is no second one");
});

test("flushSettingsAfterNoChangeSavesNothing", () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: "D:/Songs", includeRank: false }));
  const { holder } = holderWith(store);

  holder.flushSettings();

  assert.deepEqual(store.saved, []);
});

test("flushSettingsLeavesAFileAloneThatWasLoadedAsTheDefaultsOrNormalised", () => {
  // A missing, corrupt or hand-edited file loads as the defaults (a blank folder): closing the app without touching an
  // option must leave such a file alone.
  for (const stored of [userSettings(), userSettings({ outputDir: "  " })]) {
    const store = new FakeSettingsStore(stored);
    const { holder } = holderWith(store);

    holder.flushSettings();

    assert.deepEqual(store.saved, [], `stored: ${JSON.stringify(stored)}`);
  }
});

test("flushSettingsAfterChangesSavesTheCurrentValues", () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: "D:/Songs" }));
  const { holder } = holderWith(store);
  holder.onOverwrite(true);
  holder.onIncludeRank(false);

  holder.flushSettings();

  assert.deepEqual(store.saved, [defaultsWith({ outputDir: "D:/Songs", overwrite: true, includeRank: false })]);
});

test("flushSettingsAfterADebouncedSaveThatAlreadyWroteTheValuesSavesNothing", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onOverwrite(true);
  timers.advance(400);
  await flush();
  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true })]);

  holder.flushSettings();

  assert.equal(store.saved.length, 1, "the values are already on disk");
});

test("flushSettingsSavesAgainWhenTheValuesChangedAfterTheLastSave", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onOverwrite(true);
  timers.advance(400);
  await flush();
  holder.onIncludeRank(false);

  holder.flushSettings();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true }), defaultsWith({ overwrite: true, includeRank: false })]);
});

test("changingAValueAndBackBeforeTheQuietPeriodEndsSavesNothing", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOverwrite(true);
  timers.advance(100);
  holder.onOverwrite(false);
  timers.advance(5_000);
  await flush();
  holder.flushSettings();

  assert.deepEqual(store.saved, [], "the values are the ones that are already stored");
});

test("changingBackAfterTheValuesWereSavedSavesTheOriginalAgain", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onOverwrite(true);
  timers.advance(400);
  await flush();

  holder.onOverwrite(false);
  timers.advance(400);
  await flush();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true }), defaultsWith({ overwrite: false })]);
});

test("nothingIsSavedAtStartup", async () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: " " }));

  const { timers } = holderWith(store);
  timers.advance(5_000);
  await flush();

  assert.deepEqual(store.saved, []);
});

test("resetKeepsTheOptionsAndSavesNothing", async () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: "D:/Songs", overwrite: true, includeRank: false, searchLyricsOnline: false }));
  const { holder, timers } = holderWith(store);
  holder.onInput(playlistId);
  holder.resolve();
  await flush();

  holder.reset();
  timers.advance(5_000);
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "IDLE");
  assert.equal(current.outputDir, "D:/Songs");
  assert.equal(current.overwrite, true);
  assert.equal(current.includeRank, false);
  assert.equal(current.searchLyricsOnline, false);
  assert.deepEqual(store.saved, []);
});

test("theTypedInputAndTheSingleVideoRankAreNeverRemembered", async () => {
  // The stored form has room for exactly the four options, so neither the address nor the rank can be written.
  assert.deepEqual(Object.keys(userSettings()).sort(), ["includeRank", "outputDir", "overwrite", "searchLyricsOnline"]);

  // Typing or choosing a rank saves nothing by itself: one change of an option later, there is exactly one save.
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onInput(playlistId);
  holder.onSingleRank(42);
  holder.onOverwrite(true);
  timers.advance(400);
  await flush();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true })]);
});

// ---- downloading ----------------------------------------------------------------------

test("startingSendsTheOptionsAndMarksItemsWaiting", async () => {
  const { api, holder } = await resolved();
  holder.onOverwrite(true);
  holder.onOutputDir("D:/Songs");

  holder.startDownload();
  await flush();

  const request = api.jobRequests[0];
  assert.equal(api.jobRequests.length, 1);
  assert.equal(request.resolveId, "resolve-1");
  assert.equal(optionsOf(request).outputDir, "D:/Songs");
  assert.equal(optionsOf(request).overwrite, true);
  assert.equal(request.ranks, null);
  assert.equal(state(holder).phase, "RUNNING");
  assert.deepEqual(row(holder, 1).status, waiting);
  assert.deepEqual(row(holder, 2).status, skipped("비공개 영상"));
});

test("jobEventsDriveTheRowsAndTheSummary", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  api.send(itemStarted(1, "vid00000001", "001 Real - Name.m4a"));
  await flush();
  assert.deepEqual(row(holder, 1).status, downloading(null));
  assert.equal(row(holder, 1).fileName, "001 Real - Name.m4a");

  api.send(progress(1, "DOWNLOADING", 40));
  await flush();
  assert.deepEqual(row(holder, 1).status, downloading(40));

  api.send(progress(1, "FINISHING"));
  await flush();
  assert.deepEqual(row(holder, 1).status, finishing);

  api.send(itemDone(1, "001 Real - Name.m4a"), itemFailed(3, "boom"), jobDone("COMPLETED", 1, 0, 1));
  api.closeEvents();
  await flush();

  const current = state(holder);
  assert.deepEqual(row(holder, 1).status, done(null), "an event without an outcome leaves it not known");
  assert.deepEqual(row(holder, 3).status, failed("boom"));
  assert.equal(current.phase, "FINISHED");
  assert.deepEqual(current.summary, { succeeded: 1, skipped: 0, failed: 1 });
  assert.equal(current.jobStatus, "COMPLETED");
  assert.deepEqual(failedRanks(current), [3]);
});

// ---- the lyrics outcome of a finished row ----

test("aFinishedRowShowsTheLyricsOutcomeOfItsEventAndTheFinalFileName", async () => {
  for (const outcome of [...LYRICS_OUTCOMES, null]) {
    const { api, holder } = await resolved();
    holder.startDownload();
    await flush();
    api.send(itemStarted(1, "vid00000001", "001 Preview - Name.m4a"));

    api.send(itemDone(1, "001 Real - Name.m4a", outcome));
    await flush();

    assert.deepEqual(row(holder, 1).status, done(outcome), String(outcome));
    assert.equal(row(holder, 1).fileName, "001 Real - Name.m4a", String(outcome));
  }
});

test("everyFinishedRowKeepsItsOwnOutcome", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), itemDone(3, "003 A3 - T3.m4a", "SEARCH_OFF"));
  await flush();

  assert.equal(statusLabel(row(holder, 1).status), "완료 · 가사 ✓ 인터넷");
  assert.equal(statusLabel(row(holder, 3).status), "완료 · 가사 없음 (검색 끔)");
  assert.deepEqual(row(holder, 2).status, skipped("비공개 영상"));
});

test("aNewRunReplacesTheOutcomeOfTheRowsItRuns", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();
  assert.deepEqual(row(holder, 1).status, done("ONLINE"));

  holder.startDownload();
  await flush();
  assert.deepEqual(row(holder, 1).status, waiting, "the old outcome is gone as soon as the new run starts");

  api.send(itemDone(1, "001 A1 - T1.m4a", "NOT_FOUND"));
  await flush();

  assert.deepEqual(row(holder, 1).status, done("NOT_FOUND"));
});

test("aRetryReplacesTheFailureWithTheOutcomeAndLeavesTheOtherOutcomesAlone", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "DESCRIPTION"), itemFailed(3, "boom"), jobDone("COMPLETED", 1, 0, 1));
  api.closeEvents();
  await flush();
  holder.retryFailed();
  await flush();

  api.send(itemDone(3, "003 A3 - T3.m4a", "NOT_FOUND"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();

  assert.deepEqual(row(holder, 1).status, done("DESCRIPTION"));
  assert.deepEqual(row(holder, 3).status, done("NOT_FOUND"));
  assert.deepEqual(failedRanks(state(holder)), []);
});

test("anEndedOrBrokenJobKeepsTheOutcomeOfTheRowsThatFinished", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), itemStarted(3, "vid00000003", "003 A3 - T3.m4a"));
  await flush();

  api.closeEvents(); // no JobDone: the connection broke while row 3 was in flight
  await flush();

  assert.deepEqual(row(holder, 1).status, done("ONLINE"));
  assert.deepEqual(row(holder, 3).status, ready);
  assert.equal(state(holder).error, "서버와의 연결이 끊어졌습니다.");
});

test("aCancelledJobKeepsTheOutcomeOfTheRowsThatFinished", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "SEARCH_OFF"), jobDone("CANCELLED", 1, 0, 0));
  api.closeEvents();
  await flush();

  assert.deepEqual(row(holder, 1).status, done("SEARCH_OFF"));
  assert.deepEqual(row(holder, 3).status, ready);
});

test("resetClearsTheFinishedRowsWithTheirOutcomes", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();
  assert.deepEqual(row(holder, 1).status, done("ONLINE"));

  holder.reset();

  assert.equal(state(holder).rows.length, 0);
  assert.equal(state(holder).phase, "IDLE");
  assert.equal(state(holder).summary, null);
});

test("skippedItemsShowTheirReason", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  api.send(itemSkipped(1, "이미 존재"));
  await flush();

  assert.deepEqual(row(holder, 1).status, skipped("이미 존재"));
});

test("retryRunsOnlyTheFailedRanksAndKeepsTheOtherResults", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "DESCRIPTION"), itemFailed(3, "boom"), jobDone("COMPLETED", 1, 0, 1));
  api.closeEvents();
  await flush();

  holder.retryFailed();
  await flush();

  assert.deepEqual(lastRequest(api).ranks, [3]);
  assert.equal(state(holder).phase, "RUNNING");
  assert.deepEqual(row(holder, 1).status, done("DESCRIPTION"));
  assert.deepEqual(row(holder, 3).status, waiting);
});

test("retryOfASingleVideoRerunsTheWholeVideo", async () => {
  const api = new FakeApi();
  api.resolveResponse = video();
  const { holder } = await resolved({ api });
  holder.onSingleRank(7);
  holder.startDownload();
  await flush();
  api.send(itemFailed(7, "boom"), jobDone("COMPLETED", 0, 0, 1));
  api.closeEvents();
  await flush();

  holder.retryFailed();
  await flush();

  assert.equal(lastRequest(api).ranks, null);
  assert.equal(optionsOf(lastRequest(api)).singleRank, 7);
});

test("cancelAsksTheServerToStopTheJob", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  holder.cancel();
  await flush();
  assert.deepEqual(api.cancelled, ["job-1"]);

  api.send(jobDone("CANCELLED", 0, 0, 0));
  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).jobStatus, "CANCELLED");
  assert.deepEqual(row(holder, 1).status, ready);
});

test("aStreamThatEndsWithoutJobDoneIsReportedAsALostConnection", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).error, "서버와의 연결이 끊어졌습니다.");
});

test("aJobThatCannotStartReturnsToThePreview", async () => {
  const { api, holder } = await resolved();
  api.startError = apiError("출력 폴더를 만들 수 없습니다");

  holder.startDownload();
  await flush();

  assert.equal(state(holder).phase, "PREVIEW");
  assert.equal(state(holder).error, "출력 폴더를 만들 수 없습니다");
  assert.deepEqual(row(holder, 1).status, ready);
});

test("startingWithoutAnOutputFolderIsRefused", async () => {
  const { api, holder } = await resolved();
  holder.onOutputDir("  ");

  holder.startDownload();
  await flush();

  assert.equal(api.jobRequests.length, 0);
  assert.equal(state(holder).error, "출력 폴더를 지정하세요.");
});

test("resetReturnsToAnEmptyScreenButKeepsSettings", async () => {
  const { holder } = await resolved();
  holder.onOutputDir("D:/Songs");

  holder.reset();

  const current = state(holder);
  assert.equal(current.phase, "IDLE");
  assert.equal(current.rows.length, 0);
  assert.equal(current.outputDir, "D:/Songs");
});

// ---- tools ----------------------------------------------------------------------------

test("refreshToolsLoadsTheStatus", async () => {
  const { api, holder } = rig();

  holder.refreshTools();
  await flush();

  assert.deepEqual(state(holder).tools, api.toolsStatus);
});

test("installingYtDlpReportsTheResultAndRefreshesTheStatus", async () => {
  const { api, holder } = rig();

  holder.installYtDlp();
  await flush();

  assert.equal(api.installCalls, 1);
  assert.equal(state(holder).toolMessage, "installed");
  assert.equal(state(holder).toolBusy, false);
  assert.deepEqual(state(holder).tools, api.toolsStatus);
});

// ---- job endings and failures ---------------------------------------------------------

test("rowsInFlightGoBackToReadyWhenACancelledJobEnds", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemStarted(1, "vid00000001", "001 A1 - T1.m4a"), progress(1, "DOWNLOADING", 40));
  await flush();
  assert.deepEqual(row(holder, 1).status, downloading(40));

  api.send(jobDone("CANCELLED", 0, 0, 0));
  api.closeEvents();
  await flush();

  assert.deepEqual(row(holder, 1).status, ready);
  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).jobStatus, "CANCELLED");
});

test("rowsInFlightGoBackToReadyWhenTheStreamBreaks", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemStarted(1, "vid00000001", "001 A1 - T1.m4a"), progress(1, "FINISHING"));
  await flush();
  assert.deepEqual(row(holder, 1).status, finishing);

  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).error, "서버와의 연결이 끊어졌습니다.");
  assert.deepEqual(row(holder, 1).status, ready);
});

test("aRawExceptionDuringResolveDoesNotLeaveTheScreenResolving", async () => {
  const api = new FakeApi();
  api.resolveError = new Error("boom");
  const { holder } = await resolved({ api });

  assert.equal(state(holder).phase, "IDLE");
  assert.ok((state(holder).error ?? "").includes("boom"));
});

test("aRawExceptionFromToolActionsClearsTheBusyFlag", async () => {
  const api = new FakeApi();
  api.toolsError = new Error("boom");
  const { holder } = rig({ api });

  holder.installYtDlp();
  await flush();

  assert.equal(state(holder).toolBusy, false);
  assert.ok((state(holder).toolMessage ?? "").includes("boom"));

  holder.refreshTools();
  await flush();

  assert.ok((state(holder).error ?? "").includes("boom"));
});

test("aFailedStartFromFinishedKeepsThePreviousResults", async () => {
  const { api, holder } = await finishedWithOneFailure();
  api.startError = apiError("출력 폴더를 만들 수 없습니다");

  holder.retryFailed();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "FINISHED");
  assert.deepEqual(row(holder, 3).status, failed("boom"));
  assert.deepEqual(row(holder, 1).status, done(null));
  assert.deepEqual(current.summary, { succeeded: 1, skipped: 0, failed: 1 });
  assert.equal(current.jobStatus, "COMPLETED");
  assert.deepEqual(failedRanks(current), [3]);
  assert.equal(current.error, "출력 폴더를 만들 수 없습니다");
});

test("aRawExceptionWhileStartingARetryRestoresThePreviousScreen", async () => {
  const { api, holder } = await finishedWithOneFailure();
  api.startError = new Error("boom");

  holder.retryFailed();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "FINISHED");
  assert.deepEqual(failedRanks(current), [3]);
  assert.ok((current.error ?? "").includes("boom"));
});

test("aFailedSecondLookupKeepsThePreviewUsable", async () => {
  const { api, holder } = await resolved();
  const rowsBefore = state(holder).rows;
  api.resolveError = apiError("x");

  holder.onInput(playlistId);
  holder.resolve();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "PREVIEW");
  assert.deepEqual(current.rows.map((r) => r.item.rank), rowsBefore.map((r) => r.item.rank));
  assert.deepEqual(current.rows.map((r) => r.status), rowsBefore.map((r) => r.status));
  assert.equal(current.error, "x");
});

test("aFailedSecondLookupShowsTheOldRowsWithTheOptionAsItIsNow", async () => {
  const { api, holder } = await resolved();
  assert.equal(row(holder, 1).fileName, "001 A1 - T1.m4a");
  api.resolveGate = new Deferred();
  api.resolveError = apiError("x");
  holder.onInput(playlistId);
  holder.resolve();
  await flush();
  assert.equal(state(holder).phase, "RESOLVING");

  holder.onIncludeRank(false); // while the lookup is held: nothing is rewritten yet
  api.resolveGate.resolve();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "PREVIEW");
  assert.equal(current.error, "x");
  assert.equal(current.includeRank, false);
  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "A3 - T3.m4a");
  assert.deepEqual(current.rows.map((r) => r.item.rank), [1, 2, 3]);
  assert.deepEqual(row(holder, 1).status, ready);
});

test("aFailedLookupAfterAFinishedJobKeepsTheResults", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();
  api.resolveError = apiError("x");

  holder.resolve();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "FINISHED");
  assert.deepEqual(current.summary, { succeeded: 1, skipped: 0, failed: 0 });
  assert.deepEqual(row(holder, 1).status, done("ONLINE"));
  assert.equal(current.error, "x");
});

// ---- not in the Kotlin file: what the port adds ----------------------------------------------

test("subscribersAreToldOfEveryChangeUntilTheyStop", () => {
  const { holder } = rig();
  const seen: string[] = [];
  const stop = holder.subscribe((current) => seen.push(current.input));

  holder.onInput("a");
  holder.onInput("ab");
  stop();
  holder.onInput("abc");

  assert.deepEqual(seen, ["a", "ab"]);
});

test("whatAJobReportsAfterTheScreenWasResetIsIgnored", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  holder.reset();

  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "IDLE");
  assert.deepEqual(state(holder).rows, []);
  assert.equal(state(holder).summary, null);
  assert.equal(state(holder).error, null);
});

test("aFailureThatIsNotTheServersMessageSaysThatCommunicationFailed", async () => {
  const api = new FakeApi();
  api.resolveError = "invoke is not defined"; // a rejection that is only text
  const { holder } = await resolved({ api });

  assert.equal(state(holder).error, "서버와 통신 중 오류가 발생했습니다: invoke is not defined");
});
```

`shell/tests/kotlin-coverage.test.ts`:

```typescript
// Guards the port: every test of the four Kotlin suites that these files were ported from has a test of the same name here.
// The Kotlin files are the specification; when one of them gains a test, this fails until the port gains it too.
//
// It looks the Kotlin files up in the repository. When `app/` is gone (stage 6 of the migration deletes it) the app's suites are
// skipped; `shared/` stays, so the two suites of `shared` keep being compared.

import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import test from "node:test";

const repository = new URL("../../", import.meta.url);

const PORTS: ReadonlyArray<readonly [kotlin: string, port: string]> = [
  ["shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt", "filename.test.ts"],
  ["shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt", "input.test.ts"],
  ["app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt", "labels.test.ts"],
  ["app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt", "holder.test.ts"],
];

for (const [kotlin, port] of PORTS) {
  const kotlinPath = new URL(kotlin, repository);
  test(`every test of ${kotlin.split("/").pop()} is ported to ${port}`, { skip: !existsSync(kotlinPath) && "the Kotlin file is not in this checkout" }, () => {
    const names = [...readFileSync(kotlinPath, "utf8").matchAll(/@Test\s+fun\s+(\w+)\(/g)].map((match) => match[1]);
    const ported = new Set([...readFileSync(new URL(port, import.meta.url), "utf8").matchAll(/^test\("(\w+)"/gm)].map((match) => match[1]));

    assert.ok(names.length > 0, "the Kotlin file has tests");
    assert.deepEqual(
      names.filter((name) => !ported.has(name)),
      [],
      `ported ${names.filter((name) => ported.has(name)).length} of ${names.length}`,
    );
  });
}
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/holder.test.ts 2>&1 | Select-String "ERR_MODULE_NOT_FOUND|^ℹ (tests|pass|fail)"; node --test tests/kotlin-coverage.test.ts 2>&1 | Select-String "✔|✖|^ℹ (tests|pass|fail)"
```

Expected: `holder.test.ts`는 `ERR_MODULE_NOT_FOUND`(`../src/state/holder.ts`). 점검 테스트는 4개 모두 통과한다(테스트 이름은 이미 다 있다).

- [ ] **Step 3: 구현한다**

`shell/src/state/holder.ts`:

```typescript
// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/AppStateHolder.kt: all the screen logic. The DOM code only
// renders `getState()` and calls these functions. Coroutines became promises; the 400 ms wait before the options are saved and
// the clock behind the elapsed time are given in, so that the tests run in no time at all.

import type { JobEvent, JobOptions, JobRequest, ResolvedItem } from "../bridge/types.ts";
import { MAX_RANK, MIN_RANK, format } from "../logic/filename.ts";
import { canonicalUrl, classify, REJECT_MESSAGES } from "../logic/input.ts";
import { isNullOrBlank } from "../logic/text.ts";
import {
  canResolve,
  failedRanks,
  initialUiState,
  noSettingsStore,
  type ItemRow,
  type SettingsStore,
  type UiState,
  type UserSettings,
  type XgsApi,
} from "./types.ts";

/** What the holder needs of the timers: `window.setTimeout` and `clearTimeout`, or a fake one in the tests. */
export interface Scheduler {
  setTimeout(callback: () => void, ms: number): unknown;
  clearTimeout(handle: unknown): void;
}

const realScheduler: Scheduler = {
  setTimeout: (callback, ms) => globalThis.setTimeout(callback, ms),
  clearTimeout: (handle) => globalThis.clearTimeout(handle as ReturnType<typeof globalThis.setTimeout>),
};

export interface HolderOptions {
  api: XgsApi;
  defaultOutputDir: string;
  settings?: SettingsStore;
  /** Milliseconds from any fixed point; the time a job took is measured with it. */
  now?: () => number;
  scheduler?: Scheduler;
}

/** How long the options must stay unchanged before they are written. */
const SAVE_DELAY_MS = 400;

const LOST_CONNECTION = "서버와의 연결이 끊어졌습니다.";

export class AppStateHolder {
  private readonly api: XgsApi;
  private readonly settings: SettingsStore;
  private readonly now: () => number;
  private readonly scheduler: Scheduler;
  private readonly listeners = new Set<(state: UiState) => void>();

  private state: UiState;
  private jobId: string | null = null;
  /** When the job that runs now was started; the time shown at its end is measured from it. */
  private jobStart: number | null = null;
  /** Bumped by `reset`: whatever a job that ran before it reports afterwards is ignored. */
  private epoch = 0;
  private saveHandle: unknown = null;

  /**
   * The options as they are known to be stored: what the screen started with (what the settings hold, with a blank folder
   * replaced by the default), then whatever was saved last. Nothing is written while the screen still shows exactly these, so
   * a file that could not be read is not replaced by defaults and the default folder does not end up in the file unless the
   * user changes something.
   */
  private lastKnown: UserSettings;

  constructor({ api, defaultOutputDir, settings = noSettingsStore, now = () => Date.now(), scheduler = realScheduler }: HolderOptions) {
    this.api = api;
    this.settings = settings;
    this.now = now;
    this.scheduler = scheduler;
    const stored = settings.load();
    this.state = initialUiState({
      outputDir: !isNullOrBlank(stored.outputDir) ? (stored.outputDir as string) : defaultOutputDir,
      overwrite: stored.overwrite,
      includeRank: stored.includeRank,
      searchLyricsOnline: stored.searchLyricsOnline,
    });
    this.lastKnown = optionsOf(this.state);
  }

  getState(): UiState {
    return this.state;
  }

  /** Calls `listener` after every change of the state; returns the function that stops it. */
  subscribe(listener: (state: UiState) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private update(change: (state: UiState) => UiState): void {
    const next = change(this.state);
    if (next === this.state) return;
    this.state = next;
    for (const listener of [...this.listeners]) listener(next);
  }

  // ---- simple setters -----------------------------------------------------------------

  onInput(text: string): void {
    this.update((state) => ({ ...state, input: text }));
  }

  onOutputDir(dir: string): void {
    this.changeOptions((state) => ({ ...state, outputDir: dir }));
  }

  onOverwrite(value: boolean): void {
    this.changeOptions((state) => ({ ...state, overwrite: value }));
  }

  /**
   * Turning the rank in the file name on or off rewrites the names shown in the preview. Rows of a running or finished job
   * are left alone: they show the names the server really used, and the rows of a job that ended before it started them
   * (cancelled or aborted) keep the preview names they had.
   */
  onIncludeRank(value: boolean): void {
    this.changeOptions((state) =>
      state.phase !== "PREVIEW"
        ? { ...state, includeRank: value }
        : { ...state, includeRank: value, rows: state.rows.map((row) => withFileName(row, value)) },
    );
  }

  /** Whether a song whose description has no lyrics is looked up on the internet. It only matters when a job starts. */
  onSearchLyricsOnline(value: boolean): void {
    this.changeOptions((state) => ({ ...state, searchLyricsOnline: value }));
  }

  /** The album name for the tags of the next job, which is also the name of the folder it saves into; blank keeps the default. Not remembered. */
  onAlbumName(text: string): void {
    this.update((state) => ({ ...state, albumName: text }));
  }

  /** Changing the rank of a single video rewrites its file name in the preview. */
  onSingleRank(value: number): void {
    this.update((state) => {
      const rank = Math.min(Math.max(value, MIN_RANK), MAX_RANK);
      if (state.resolved?.kind !== "VIDEO") return { ...state, singleRank: rank };
      return { ...state, singleRank: rank, rows: state.rows.map((row) => withRank(row, rank, state.includeRank)) };
    });
  }

  dismissError(): void {
    this.update((state) => ({ ...state, error: null }));
  }

  // ---- remembered options -----------------------------------------------------------------

  /** Applies `change` and, if it altered one of the remembered options, schedules a save. */
  private changeOptions(change: (state: UiState) => UiState): void {
    const old = this.state;
    const next = change(old);
    const altered = !sameOptions(optionsOf(next), optionsOf(old));
    this.update(() => next);
    if (altered) this.scheduleSave();
  }

  /** Each change restarts the wait, so typing a folder name produces one save, with the finished text. */
  private scheduleSave(): void {
    if (this.saveHandle !== null) this.scheduler.clearTimeout(this.saveHandle);
    this.saveHandle = this.scheduler.setTimeout(() => {
      this.saveHandle = null;
      this.saveIfChanged();
    }, SAVE_DELAY_MS);
  }

  /**
   * Saves the options as they are now, at once, and drops a save that is still waiting. Call it when the window closes,
   * because a change made in the last moments would otherwise be lost. Writes nothing when the options are the ones already
   * stored (see `lastKnown`).
   */
  flushSettings(): void {
    if (this.saveHandle !== null) this.scheduler.clearTimeout(this.saveHandle);
    this.saveHandle = null;
    this.saveIfChanged();
  }

  private saveIfChanged(): void {
    const current = optionsOf(this.state);
    if (sameOptions(current, this.lastKnown)) return;
    this.settings.save(current);
    this.lastKnown = current;
  }

  reset(): void {
    this.epoch += 1;
    this.jobId = null;
    this.update((state) =>
      initialUiState({
        outputDir: state.outputDir,
        overwrite: state.overwrite,
        includeRank: state.includeRank,
        searchLyricsOnline: state.searchLyricsOnline,
        tools: state.tools,
      }),
    );
  }

  // ---- tools ----------------------------------------------------------------------------

  refreshTools(): void {
    void (async () => {
      try {
        const tools = await this.api.tools();
        this.update((state) => ({ ...state, tools }));
      } catch (error) {
        this.update((state) => ({ ...state, error: failureMessage(error) }));
      }
    })();
  }

  installYtDlp(): void {
    this.runToolAction(async () => (await this.api.installYtDlp()).message);
  }

  updateYtDlp(): void {
    this.runToolAction(async () => (await this.api.updateYtDlp()).message);
  }

  private runToolAction(action: () => Promise<string>): void {
    this.update((state) => ({ ...state, toolBusy: true, toolMessage: null }));
    void (async () => {
      try {
        const message = await action();
        const tools = await this.api.tools();
        this.update((state) => ({ ...state, toolBusy: false, toolMessage: message, tools }));
      } catch (error) {
        this.update((state) => ({ ...state, toolBusy: false, toolMessage: failureMessage(error) }));
      }
    })();
  }

  // ---- resolving ------------------------------------------------------------------------

  resolve(): void {
    const current = this.state;
    if (!canResolve(current)) return;
    const result = classify(current.input);
    if (!result.ok) {
      this.update((state) => ({ ...state, error: REJECT_MESSAGES[result.reason] }));
      return;
    }
    this.doResolve(current.input);
  }

  /** For a watch URL that also carried a playlist: look up just that video instead. */
  switchToVideoOnly(): void {
    const videoId = this.state.resolved?.alsoVideoId;
    if (!videoId) return;
    const url = canonicalUrl({ kind: "video", id: videoId });
    this.update((state) => ({ ...state, input: url }));
    this.doResolve(url);
  }

  private doResolve(input: string): void {
    const before = this.state;
    this.update((state) => ({ ...state, phase: "RESOLVING", error: null }));
    void (async () => {
      try {
        const response = await this.api.resolve(input);
        this.update((state) => ({
          ...state,
          phase: "PREVIEW",
          resolved: response,
          rows: response.items.map((item) => previewRow(item, state.singleRank, response.kind, state.includeRank)),
          summary: null,
          jobStatus: null,
        }));
      } catch (error) {
        // A failed lookup must not strip the buttons from a preview that is still on screen. The user may have changed the
        // rank option while the lookup ran (the rows were not rewritten then), so a restored preview shows names that
        // follow the option as it is now.
        const phase = before.resolved !== null ? before.phase : "IDLE";
        this.update((state) => ({
          ...state,
          phase,
          rows: phase === "PREVIEW" ? state.rows.map((row) => withFileName(row, state.includeRank)) : state.rows,
          error: failureMessage(error),
        }));
      }
    })();
  }

  // ---- downloading ----------------------------------------------------------------------

  startDownload(): void {
    const state = this.state;
    const resolved = state.resolved;
    if (resolved === null) return;
    if (state.phase !== "PREVIEW" && state.phase !== "FINISHED") return;
    if (isNullOrBlank(state.outputDir)) {
      this.update((current) => ({ ...current, error: "출력 폴더를 지정하세요." }));
      return;
    }
    this.update((current) => ({
      ...current,
      phase: "RUNNING",
      error: null,
      summary: null,
      jobStatus: null,
      elapsedMs: null,
      rows: current.rows.map((row) => (row.item.available ? { ...row, status: { kind: "waiting" } } : row)),
    }));
    this.runJob({ resolveId: resolved.resolveId, options: jobOptions(state), ranks: null }, state);
  }

  /** Runs a new job for just the items that failed last time. */
  retryFailed(): void {
    const state = this.state;
    const resolved = state.resolved;
    if (resolved === null) return;
    const ranks = failedRanks(state);
    if (ranks.length === 0 || state.phase !== "FINISHED") return;
    this.update((current) => ({
      ...current,
      phase: "RUNNING",
      error: null,
      summary: null,
      jobStatus: null,
      elapsedMs: null,
      rows: current.rows.map((row) => (ranks.includes(row.item.rank) ? { ...row, status: { kind: "waiting" } } : row)),
    }));
    // A single video is always re-run as a whole; its rank is chosen by the user, not by the list.
    const retryRanks = resolved.kind !== "VIDEO" ? ranks : null;
    this.runJob({ resolveId: resolved.resolveId, options: jobOptions(state), ranks: retryRanks }, state);
  }

  cancel(): void {
    const id = this.jobId;
    if (id === null) return;
    void (async () => {
      try {
        await this.api.cancelJob(id);
      } catch (error) {
        this.update((state) => ({ ...state, error: failureMessage(error) }));
      }
    })();
  }

  /**
   * `before` is the screen as it was when the user pressed the button. It is restored when the job cannot even be started, so
   * a failed retry does not wipe the results of the previous run.
   */
  private runJob(request: JobRequest, before: UiState): void {
    this.jobStart = this.now();
    const epoch = this.epoch;
    void (async () => {
      let started = false;
      try {
        const created = await this.api.startJob(request);
        started = true;
        if (epoch !== this.epoch) return;
        this.jobId = created.jobId;
        await this.api.watchJob(created.jobId, (event) => {
          if (epoch === this.epoch) this.update((state) => this.apply(state, event));
        });
        if (epoch !== this.epoch) return;
        this.update((state) =>
          state.phase === "RUNNING"
            ? { ...state, phase: "FINISHED", error: LOST_CONNECTION, rows: state.rows.map(resetTransient) }
            : state,
        );
      } catch (error) {
        if (epoch !== this.epoch) return;
        this.update((state) =>
          started
            ? { ...state, phase: "FINISHED", error: failureMessage(error), rows: state.rows.map(resetTransient) }
            : {
                ...state,
                phase: before.phase,
                rows: before.rows,
                summary: before.summary,
                jobStatus: before.jobStatus,
                error: failureMessage(error),
              },
        );
      }
    })();
  }

  private apply(state: UiState, event: JobEvent): UiState {
    switch (event.type) {
      case "item-started":
        return updateRow(state, event.rank, (row) => ({ ...row, fileName: event.fileName, status: { kind: "downloading", percent: null } }));
      case "progress":
        return updateRow(state, event.rank, (row) => ({
          ...row,
          status: event.stage === "DOWNLOADING" ? { kind: "downloading", percent: event.percent ?? null } : { kind: "finishing" },
        }));
      case "item-done":
        return updateRow(state, event.rank, (row) => ({ ...row, fileName: event.fileName, status: { kind: "done", lyrics: event.lyrics ?? null } }));
      case "item-skipped":
        return updateRow(state, event.rank, (row) => ({ ...row, status: { kind: "skipped", reason: event.reason } }));
      case "item-failed":
        return updateRow(state, event.rank, (row) => ({ ...row, status: { kind: "failed", message: event.message } }));
      case "job-done":
        return {
          ...state,
          phase: "FINISHED",
          jobStatus: event.status,
          summary: event.summary,
          elapsedMs: this.jobStart === null ? null : this.now() - this.jobStart,
          rows: state.rows.map(resetTransient),
        };
      default:
        return state; // an event this version of the screen does not know
    }
  }
}

// ---- helpers --------------------------------------------------------------------------

/** The four options that are remembered; nothing else on the screen is. */
function optionsOf(state: UiState): UserSettings {
  return { outputDir: state.outputDir, overwrite: state.overwrite, includeRank: state.includeRank, searchLyricsOnline: state.searchLyricsOnline };
}

function sameOptions(a: UserSettings, b: UserSettings): boolean {
  return a.outputDir === b.outputDir && a.overwrite === b.overwrite && a.includeRank === b.includeRank && a.searchLyricsOnline === b.searchLyricsOnline;
}

function jobOptions(state: UiState): JobOptions {
  return {
    outputDir: state.outputDir,
    overwrite: state.overwrite,
    singleRank: state.singleRank,
    includeRank: state.includeRank,
    searchLyricsOnline: state.searchLyricsOnline,
    albumName: state.albumName !== null && !isNullOrBlank(state.albumName) ? state.albumName : null,
  };
}

/** The server's `expectedFileName` always carries the rank, so the shown name is built here to honour `includeRank`. */
function previewRow(item: ResolvedItem, singleRank: number, kind: "PLAYLIST" | "VIDEO", includeRank: boolean): ItemRow {
  const row: ItemRow = {
    item,
    fileName: item.expectedFileName,
    status: item.available ? { kind: "ready" } : { kind: "skipped", reason: item.unavailableReason ?? "" },
  };
  return kind === "VIDEO" ? withRank(row, singleRank, includeRank) : withFileName(row, includeRank);
}

function withRank(row: ItemRow, rank: number, includeRank: boolean): ItemRow {
  if (!row.item.available) return row;
  return withFileName({ ...row, item: { ...row.item, rank } }, includeRank);
}

function withFileName(row: ItemRow, includeRank: boolean): ItemRow {
  if (!row.item.available) return row;
  return { ...row, fileName: format(row.item.rank, row.item.artist, row.item.track, includeRank) };
}

/**
 * A cancelled or aborted job sends no final event for the items that were still waiting or in flight, so those rows go back
 * to ready. Finished, skipped and failed rows keep their result.
 */
function resetTransient(row: ItemRow): ItemRow {
  switch (row.status.kind) {
    case "waiting":
    case "downloading":
    case "finishing":
      return { ...row, status: { kind: "ready" } };
    default:
      return row;
  }
}

function updateRow(state: UiState, rank: number, change: (row: ItemRow) => ItemRow): UiState {
  return { ...state, rows: state.rows.map((row) => (row.item.rank === rank && row.item.available ? change(row) : row)) };
}

/**
 * What the user sees for a failure: the server's own message (a `BridgeError` from the Rust side is a plain object with a
 * `message`), or a generic one for anything else (an exception of the screen's own, a rejection that is just text).
 */
export function failureMessage(error: unknown): string {
  if (typeof error === "object" && error !== null && !(error instanceof Error) && typeof (error as { message?: unknown }).message === "string") {
    return (error as { message: string }).message;
  }
  const detail = error instanceof Error ? error.message : String(error);
  return `서버와 통신 중 오류가 발생했습니다: ${detail}`;
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/holder.test.ts 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; node --test tests/kotlin-coverage.test.ts 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"
```

Expected: `ℹ tests 98`, `ℹ pass 98`, `ℹ fail 0`; 점검 `ℹ tests 4`, `ℹ pass 4`; `tsc exit 0`.

- [ ] **Step 5: 점검 테스트가 정말 무는지 확인한다**

`shell/tests/holder.test.ts`에서 `test("resetClearsTheTime"`의 이름을 `test("resetClearsTheTimeX"`로 잠시 바꾸고 점검 테스트를 돌린다:

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/kotlin-coverage.test.ts 2>&1 | Select-String "✖|resetClearsTheTime"
```

Expected: `AppStateHolderTest.kt` 점검이 FAIL하고 `resetClearsTheTime`이 빠졌다고 말한다. 확인한 뒤 **이름을 되돌린다**.

- [ ] **Step 6: Commit**

```bash
git add shell/src/state/holder.ts shell/tests/support/fakes.ts shell/tests/holder.test.ts shell/tests/kotlin-coverage.test.ts
git commit -m "feat(shell): AppStateHolder를 옮기고 원본 시나리오 95개와 원본 누락 점검을 더한다"
```

---

### Task 6: 데모 서버

**Files:**
- Create: `shell/src/demo.ts`, `shell/tests/demo.test.ts`

**Interfaces:**
- Consumes: Task 1~2의 `format`, `classify`, `REJECT_MESSAGES`; Task 4의 `XgsApi`; 2단계의 `JobEvent`, `ResolveResponse`, `ResolvedItem`, `ToolsStatus`
- Produces (Task 7이 쓴다): `createDemoApi({tools?: "ok"|"broken", resolveDelayMs?, tickMs?, speed?, concurrency?}): XgsApi`, `DEMO_PLAYLIST_TITLE`. 100곡 재생목록(7번과 42번은 사용할 수 없음, 17의 배수는 가수명 추정), 진행이 움직이는 가짜 다운로드(동시 8곡, 13번과 58번은 처음에만 실패), 취소, 재시도

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`shell/tests/demo.test.ts`:

```typescript
// The demo server (`index.html?demo`) answers like the real one, so that the screen can be tried and looked at without it.

import assert from "node:assert/strict";
import test from "node:test";
import type { JobEvent } from "../src/bridge/types.ts";
import { createDemoApi } from "../src/demo.ts";

const fast = { resolveDelayMs: 1, tickMs: 1, speed: 400 };
const playlistUrl = "https://www.youtube.com/playlist?list=PLabcdefghijklmnop";

async function watch(api: ReturnType<typeof createDemoApi>, jobId: string): Promise<JobEvent[]> {
  const events: JobEvent[] = [];
  await api.watchJob(jobId, (event) => events.push(event));
  return events;
}

test("a resolved playlist has 100 rows, two of them not available", async () => {
  const resolved = await createDemoApi(fast).resolve(playlistUrl);

  assert.equal(resolved.kind, "PLAYLIST");
  assert.equal(resolved.items.length, 100);
  assert.deepEqual(resolved.items.filter((item) => !item.available).map((item) => item.rank), [7, 42]);
  assert.equal(resolved.items[0].expectedFileName, "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
});

test("a video address gives a single video and a watch address with a list remembers the video", async () => {
  const api = createDemoApi(fast);

  assert.equal((await api.resolve("dQw4w9WgXcQ")).kind, "VIDEO");
  const both = await api.resolve("https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PLabcdefghijklmnop");
  assert.equal(both.kind, "PLAYLIST");
  assert.equal(both.alsoVideoId, "dQw4w9WgXcQ");
});

test("input the server would refuse is refused with the server's message", async () => {
  await assert.rejects(createDemoApi(fast).resolve("https://evil.com/x"), { message: "YouTube 주소만 사용할 수 있습니다." });
});

test("a demo job runs every available song, fails ranks 13 and 58, and ends with job-done", async () => {
  const api = createDemoApi(fast);
  const resolved = await api.resolve(playlistUrl);

  const { jobId } = await api.startJob({ resolveId: resolved.resolveId, options: { includeRank: true } });
  const events = await watch(api, jobId);

  const last = events[events.length - 1];
  assert.equal(last.type, "job-done");
  assert.deepEqual(last.type === "job-done" && last.summary, { succeeded: 96, skipped: 0, failed: 2 });
  assert.deepEqual(events.filter((event) => event.type === "item-failed").map((event) => event.rank).sort((a, b) => a - b), [13, 58]);
  assert.equal(events.filter((event) => event.type === "item-started").length, 98);
});

test("a retry of the failed ranks succeeds", async () => {
  const api = createDemoApi(fast);
  const resolved = await api.resolve(playlistUrl);
  await watch(api, (await api.startJob({ resolveId: resolved.resolveId })).jobId);

  const retry = await watch(api, (await api.startJob({ resolveId: resolved.resolveId, ranks: [13, 58] })).jobId);

  const last = retry[retry.length - 1];
  assert.deepEqual(last.type === "job-done" && last.summary, { succeeded: 2, skipped: 0, failed: 0 });
});

test("a cancelled demo job ends with job-done CANCELLED", async () => {
  const api = createDemoApi({ ...fast, speed: 1 });
  const resolved = await api.resolve(playlistUrl);
  const { jobId } = await api.startJob({ resolveId: resolved.resolveId });

  const watching = watch(api, jobId);
  await api.cancelJob(jobId);
  const events = await watching;

  const last = events[events.length - 1];
  assert.equal(last.type === "job-done" && last.status, "CANCELLED");
});

test("an unknown job and an expired lookup say what the server says", async () => {
  const api = createDemoApi(fast);

  await assert.rejects(api.startJob({ resolveId: "nope" }), { message: "조회 결과가 만료되었습니다. 다시 조회하세요." });
  await assert.rejects(api.watchJob("nope", () => {}), { message: "작업을 찾을 수 없습니다." });
  await assert.rejects(api.cancelJob("nope"), { message: "작업을 찾을 수 없습니다." });
});
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/demo.test.ts 2>&1 | Select-String "ERR_MODULE_NOT_FOUND|^ℹ (tests|pass|fail)"
```

Expected: `ERR_MODULE_NOT_FOUND`(`../src/demo.ts`).

- [ ] **Step 3: 구현한다**

`shell/src/demo.ts`:

```typescript
// A server that is not there: the same calls as the real one (`XgsApi`), answered with a made-up playlist of 100 songs and a
// download that moves like the real one. `index.html?demo` runs the screen on it, so the whole screen can be tried (and
// looked at in a browser) without Tauri, the sidecar or the network.

import type { JobEvent, JobRequest, ResolveResponse, ResolvedItem, ToolsStatus } from "./bridge/types.ts";
import { format } from "./logic/filename.ts";
import { REJECT_MESSAGES, classify } from "./logic/input.ts";
import type { XgsApi } from "./state/types.ts";

export const DEMO_PLAYLIST_TITLE = "Melon Daily Top 100";

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

const LYRICS = ["DESCRIPTION", "ONLINE", "NOT_FOUND", "ONLINE"] as const;

/** Two unavailable videos (ranks 7 and 42) and every 17th song with an artist guessed from the channel name. */
function demoItem(rank: number): ResolvedItem {
  const pad = (value: number, length: number) => String(value).padStart(length, "0");
  if (rank === 7 || rank === 42) {
    return {
      rank,
      videoId: `demo${pad(rank, 7)}`,
      title: rank === 7 ? "[Private video]" : "[Deleted video]",
      channel: null,
      artist: "",
      track: "",
      lowConfidence: false,
      available: false,
      unavailableReason: rank === 7 ? "비공개 영상" : "삭제된 영상",
      expectedFileName: null,
    };
  }
  const [artist, track] = SAMPLE[(rank - 1) % SAMPLE.length];
  return {
    rank,
    videoId: `demo${pad(rank, 7)}`,
    title: `${artist} - ${track}`,
    channel: null,
    artist,
    track,
    lowConfidence: rank % 17 === 0,
    available: true,
    unavailableReason: null,
    expectedFileName: format(rank, artist, track, true),
  };
}

export interface DemoOptions {
  /** `"broken"` has no yt-dlp, ffmpeg or JavaScript runtime. */
  tools?: "ok" | "broken";
  /** How long a lookup takes. */
  resolveDelayMs?: number;
  /** How often a running job moves (default 100: ten times a second). */
  tickMs?: number;
  /** 1 makes a song take 5 to 25 seconds at the default tick, long enough to look at the screen; the tests use more. */
  speed?: number;
  /** How many songs run at once. */
  concurrency?: number;
}

interface DemoJob {
  items: ResolvedItem[];
  includeRank: boolean;
  cancelled: boolean;
}

const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

export function createDemoApi({ tools = "ok", resolveDelayMs = 600, tickMs = 100, speed = 1, concurrency = 8 }: DemoOptions = {}): XgsApi {
  let toolsStatus: ToolsStatus =
    tools === "ok"
      ? {
          ytDlp: { found: true, version: "2026.10.01", path: null },
          ffmpeg: { found: true, version: "8.1", path: null },
          jsRuntime: { found: true, version: "24.11.1", path: null },
        }
      : {
          ytDlp: { found: false, version: null, path: null },
          ffmpeg: { found: false, version: null, path: null },
          jsRuntime: { found: false, version: null, path: null },
        };
  const resolutions = new Map<string, ResolveResponse>();
  const jobs = new Map<string, DemoJob>();
  /** The songs that already failed once: a retry succeeds. */
  const failedBefore = new Set<number>();
  let resolveCount = 0;
  let jobCount = 0;

  return {
    async tools() {
      await sleep(200);
      return toolsStatus;
    },

    async installYtDlp() {
      await sleep(1500);
      toolsStatus = { ...toolsStatus, ytDlp: { found: true, version: "2026.10.01", path: null } };
      return { message: "yt-dlp 2026.10.01 을(를) 설치했습니다." };
    },

    async updateYtDlp() {
      await sleep(1500);
      return { message: "yt-dlp는 이미 최신 버전입니다." };
    },

    async resolve(input) {
      await sleep(resolveDelayMs);
      const result = classify(input);
      if (!result.ok) throw { message: REJECT_MESSAGES[result.reason] };
      resolveCount += 1;
      const resolveId = `demo-resolve-${resolveCount}`;
      const response: ResolveResponse =
        result.input.kind === "video"
          ? {
              resolveId,
              kind: "VIDEO",
              playlistTitle: null,
              items: [{ ...demoItem(3), videoId: result.input.id }],
              truncated: false,
              alsoVideoId: null,
            }
          : {
              resolveId,
              kind: "PLAYLIST",
              playlistTitle: DEMO_PLAYLIST_TITLE,
              items: Array.from({ length: 100 }, (_, index) => demoItem(index + 1)),
              truncated: false,
              alsoVideoId: result.input.alsoVideoId,
            };
      resolutions.set(resolveId, response);
      return response;
    },

    async startJob(request: JobRequest) {
      await sleep(50);
      const resolved = resolutions.get(request.resolveId);
      if (!resolved) throw { message: "조회 결과가 만료되었습니다. 다시 조회하세요." };
      const options = request.options ?? {};
      const isVideo = resolved.kind === "VIDEO";
      const items = resolved.items
        .filter((item) => item.available && (request.ranks == null || request.ranks.includes(item.rank)))
        .map((item) => (isVideo ? { ...item, rank: options.singleRank ?? 1 } : item));
      if (items.length === 0) throw { message: "다운로드할 항목이 없습니다." };
      jobCount += 1;
      const jobId = `demo-job-${jobCount}`;
      jobs.set(jobId, { items, includeRank: options.includeRank ?? true, cancelled: false });
      return { jobId };
    },

    watchJob(jobId, onEvent) {
      const job = jobs.get(jobId);
      if (!job) return Promise.reject({ message: "작업을 찾을 수 없습니다." });
      return runJob(job, onEvent);
    },

    async cancelJob(jobId) {
      const job = jobs.get(jobId);
      if (!job) throw { message: "작업을 찾을 수 없습니다." };
      job.cancelled = true;
    },
  };

  /** Runs the items `concurrency` at a time: each one's progress moves every tick, then a short "마무리 중", then its outcome. */
  function runJob(job: DemoJob, emit: (event: JobEvent) => void): Promise<void> {
    return new Promise<void>((resolve) => {
      const queue = [...job.items];
      const active = new Map<number, { item: ResolvedItem; fileName: string; percent: number; step: number; finishingTicks: number }>();
      const summary = { succeeded: 0, skipped: 0, failed: 0 };

      const end = (status: "COMPLETED" | "CANCELLED") => {
        clearInterval(timer);
        emit({ type: "job-done", status, summary });
        resolve();
      };

      const timer = setInterval(() => {
        if (job.cancelled) return end("CANCELLED");
        while (active.size < concurrency && queue.length > 0) {
          const item = queue.shift() as ResolvedItem;
          const fileName = format(item.rank, item.artist, item.track, job.includeRank);
          active.set(item.rank, { item, fileName, percent: 0, step: (0.4 + Math.random() * 1.6) * speed, finishingTicks: 0 });
          emit({ type: "item-started", rank: item.rank, videoId: item.videoId, fileName });
        }
        for (const [rank, work] of [...active]) {
          if (work.percent < 100) {
            work.percent = Math.min(100, work.percent + work.step);
            emit({ type: "progress", rank, stage: work.percent >= 100 ? "FINISHING" : "DOWNLOADING", percent: work.percent >= 100 ? null : work.percent });
            continue;
          }
          work.finishingTicks += 1;
          if (work.finishingTicks < 6) continue;
          active.delete(rank);
          const shouldFail = (rank === 13 || rank === 58) && !failedBefore.has(rank);
          if (shouldFail) {
            failedBefore.add(rank);
            summary.failed += 1;
            emit({ type: "item-failed", rank, message: rank === 13 ? "m4a 오디오 형식이 없습니다." : "태그를 쓰지 못했습니다. (지원하지 않는 m4a 구조)" });
          } else {
            summary.succeeded += 1;
            emit({ type: "item-done", rank, fileName: work.fileName, lyrics: LYRICS[rank % LYRICS.length] });
          }
        }
        if (active.size === 0 && queue.length === 0) end("COMPLETED");
      }, tickMs);
    });
  }
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/demo.test.ts 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"
```

Expected: `ℹ tests 7`, `ℹ pass 7`(2~3초), `tsc exit 0`.

- [ ] **Step 5: Commit**

```bash
git add shell/src/demo.ts shell/tests/demo.test.ts
git commit -m "feat(shell): 실제 서버 없이 화면을 돌려 볼 수 있는 데모 서버를 더한다"
```

---

### Task 7: 화면

**Files:**
- Replace: `shell/index.html`, `shell/src/main.ts`; `shell/src/prototype.css` → `shell/src/app.css`(이름을 바꾸고 내용을 교체)
- Delete: `shell/src/prototype/`, `shell/tests/fake.test.ts`

**Interfaces:**
- Consumes: Task 5의 `AppStateHolder`, Task 4의 `labels.ts`·`types.ts`, Task 6의 `createDemoApi`, 2단계의 `bridge/tauri.ts`의 `bridge`
- Produces: 화면. `index.html`은 실제 서버(브리지)를 쓰고, `index.html?demo`는 데모 서버를 쓴다(`?demo=broken`은 도구가 없는 상태, `&speed=30`은 가짜 다운로드를 30배 빠르게). 화면은 holder를 구독해 그리고, 목록은 행이 바뀐 것만 고치며, 조합 중인 입력란에 값을 쓰지 않고, F5·Ctrl+R을 막는다

- [ ] **Step 1: 프로토타입을 걷어내고 파일을 쓴다**

```powershell
Set-Location C:\Projects\xGetSongs; git mv shell/src/prototype.css shell/src/app.css; git rm -r -q shell/src/prototype shell/tests/fake.test.ts; git status --short
```

`shell/index.html`:

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
        <div class="row-flex info-line">
          <h2 id="info-title" class="ellipsis"></h2>
          <span id="truncated" class="bad small" hidden>999개를 넘어 앞 999개만 표시합니다.</span>
          <button id="video-only" type="button" hidden>이 영상만 받기</button>
        </div>

        <fieldset id="options" class="options">
          <div class="row-flex align-end">
            <label class="field grow">
              <span class="field-label">출력 폴더</span>
              <input id="output-dir" type="text" spellcheck="false" />
            </label>
            <!-- The folder chooser and "show in Explorer" come with stage 4 of the migration. -->
            <button id="pick-folder" type="button" hidden>폴더 선택</button>
          </div>
          <div id="destination-row" class="row-flex" hidden>
            <span id="destination" class="small grow ellipsis"></span>
            <button id="open-folder" type="button" class="small-button" hidden>탐색기에서 보기</button>
          </div>
          <label class="field">
            <span class="field-label">앨범명 (폴더명)</span>
            <input id="album" type="text" />
            <span class="hint">저장 폴더 이름과 앨범 태그가 모두 이 이름입니다. 비우면 재생목록 제목(영상 1개는 폴더 없이 영상 자체의 앨범)</span>
          </label>
          <div class="checks">
            <label class="check"><input id="opt-overwrite" type="checkbox" /> 기존 파일 덮어쓰기</label>
            <label class="check"><input id="opt-rank" type="checkbox" /> 파일명에 순번 포함</label>
            <label class="check"><input id="opt-lyrics" type="checkbox" /> 가사가 없으면 인터넷에서 검색</label>
          </div>
          <div class="row-flex align-end">
            <span class="concurrency-label">동시 다운로드</span>
            <span>코어 수의 70% (자동)</span>
            <label id="rank-field" class="field rank-field" hidden>
              <span class="field-label">순위 번호</span>
              <input id="single-rank" type="text" inputmode="numeric" autocomplete="off" />
            </label>
          </div>
        </fieldset>

        <ul id="list" class="list" aria-label="곡 목록"></ul>
        <div id="actions" class="row-flex actions"></div>
      </section>
    </div>

    <dialog id="install-dialog" aria-labelledby="install-title">
      <form id="install-form" method="dialog">
        <h3 id="install-title">yt-dlp 설치</h3>
        <p>GitHub(github.com/yt-dlp/yt-dlp)에서 yt-dlp.exe를 내려받아 이 앱 전용 폴더에 저장합니다. 계속할까요?</p>
        <div class="row-flex dialog-actions">
          <button value="cancel">취소</button>
          <button value="install" class="primary">내려받기</button>
        </div>
      </form>
    </dialog>
  </body>
</html>
```

`shell/src/app.css`(프로토타입의 스타일에서 상태 전환 막대를 빼고 대화상자와 작은 것들을 더했다):

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

.info-line {
  min-width: 0;
}

.info-line h2 {
  flex: 0 1 auto;
}

.rank-field {
  width: 120px;
  margin-left: 8px;
}

/* ---- the dialog that asks before yt-dlp is downloaded ---- */
dialog {
  max-width: 420px;
  padding: 16px;
  border: 1px solid var(--line);
  border-radius: 8px;
  background: Canvas;
  color: CanvasText;
}

dialog::backdrop {
  background: color-mix(in srgb, CanvasText 35%, transparent);
}

dialog h3 {
  margin: 0 0 8px;
  font-size: 16px;
  font-weight: 600;
}

.dialog-actions {
  justify-content: flex-end;
  margin-top: 16px;
}
```

`shell/src/main.ts`:

```typescript
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
}

void boot();
```

- [ ] **Step 2: 컴파일하고 시험하고 빌드한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; npx tsc --noEmit; "tsc exit $LASTEXITCODE"; node --test "tests/*.test.ts" 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; npm run build 2>&1 | Select-Object -Last 6
```

Expected: `tsc exit 0`, `ℹ tests 212`, `ℹ pass 212`, `ℹ fail 0`(2단계의 `bridge` 8 + `text` 5 + `filename` 39 + `input` 16 + `labels` 35 + `holder` 98 + `demo` 7 + 점검 4), vite 빌드 성공(`dist/index.html`, `dist/check.html`).

- [ ] **Step 3: 개발 서버를 띄운다**

개발 서버는 오래 도는 프로세스이므로 PowerShell 도구의 백그라운드로 띄우고 출력은 파일로 보낸다(출력을 `Select-Object -First`로 자르면 서버가 같이 끝난다):

```powershell
New-Item -ItemType Directory -Force "$env:TEMP\xgs-bridge-check" | Out-Null; Set-Location C:\Projects\xGetSongs\shell; npx vite *> "$env:TEMP\xgs-bridge-check\vite.log"
```

내장 브라우저 도구(`mcp__Claude_Browser__*`)로 `http://localhost:1420/?demo&speed=40`을 연다. 탐색이 거절되면 서버가 뜰 때까지 몇 초 뒤 다시 연다.

- [ ] **Step 4: 시나리오 A — 조회와 옵션**

`javascript_tool`로 아래를 실행한다(페이지를 연 직후):

```javascript
(async () => {
  // Run on http://localhost:1420/?demo&speed=40 right after it loaded. Looks a playlist up and changes the options.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const type = (element, value) => { element.value = value; element.dispatchEvent(new Event("input", { bubbles: true })); };
  const labels = () => [...document.querySelectorAll("#list .label")].map((element) => element.textContent);
  const buttons = () => [...document.querySelectorAll("#actions button")].map((element) => element.textContent);
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };

  await wait(500);
  expect("tools", $("#tools").innerText.split("\n").slice(0, 3).join(" | "), "yt-dlp 2026.10.01 ✓ | ffmpeg 8.1 ✓ | JS 런타임 24.11.1 ✓");
  expect("resolve disabled while the box is empty", $("#resolve").disabled, true);
  type($("#input"), "https://www.youtube.com/playlist?list=PLabcdefghijklmnop");
  expect("resolve enabled once there is text", $("#resolve").disabled, false);
  $("#resolve").click();
  expect("resolve disabled while looking up", $("#resolve").disabled, true);
  await wait(1200);

  expect("rows", labels().length, 100);
  expect("ready", labels().filter((label) => label === "준비됨").length, 98);
  expect("skipped", labels().filter((label) => label.startsWith("건너뜀")).length, 2);
  expect("buttons", buttons(), ["다운로드 시작", "새로 시작"]);
  expect("title", $("#info-title").textContent, "Melon Daily Top 100 · 100개");
  expect("video-only hidden", $("#video-only").hidden, true);
  expect("rank field hidden", $("#rank-field").hidden, true);
  expect("options", [$("#opt-overwrite").checked, $("#opt-rank").checked, $("#opt-lyrics").checked], [false, true, true]);
  expect("album shows the title", $("#album").value, "Melon Daily Top 100");
  expect("output folder", $("#output-dir").value, "C:\\Users\\사용자\\Music\\xGetSongs");
  expect("destination", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\xGetSongs\\Melon Daily Top 100");
  expect("first file", $("#list .file").textContent, "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
  expect("a guessed artist is warned about", document.querySelectorAll("#list .warn").length > 0, true);
  expect("options enabled", $("#options").disabled, false);

  $("#opt-rank").click();
  expect("rank off rewrites the names", $("#list .file").textContent, "소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
  $("#opt-rank").click();
  expect("rank on again", $("#list .file").textContent, "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");

  type($("#album"), "내 앨범");
  expect("a typed album names the folder", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\xGetSongs\\내 앨범");
  type($("#album"), "");
  expect("a cleared album is the default folder", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\xGetSongs\\Melon Daily Top 100");
  expect("a cleared album field stays empty", $("#album").value, "");

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

Expected: `{"pass":true,"failures":[]}`

- [ ] **Step 5: 시나리오 B — 시작, 취소, 다시, 실패 재시도, 새로 시작**

A 직후 같은 페이지에서(약 15초 걸린다):

```javascript
(async () => {
  // Run right after a-resolve.js (the playlist is resolved on the page). Starts, cancels, runs to the end, retries, starts over.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const labels = () => [...document.querySelectorAll("#list .label")].map((element) => element.textContent);
  const count = (test) => labels().filter(test).length;
  const buttons = () => [...document.querySelectorAll("#actions button")].map((element) => element.textContent);
  const click = (text) => { const found = [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === text); if (!found) throw new Error(`no button ${text} among ${buttons()}`); found.click(); };
  const until = async (test, ms = 90000) => { const started = Date.now(); while (!test()) { if (Date.now() - started > ms) throw new Error("timeout; buttons=" + buttons()); await wait(100); } };
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };
  const summary = () => ($("#actions .summary") || {}).textContent ?? null;

  click("다운로드 시작");
  expect("just started: buttons", buttons(), ["취소"]);
  expect("just started: everything waits", count((label) => label === "대기 중"), 98);
  expect("the options are locked while it runs", $("#options").disabled, true);
  await until(() => count((label) => label.startsWith("다운로드")) > 0);
  expect("eight run at once", count((label) => label.startsWith("다운로드")), 8);
  expect("each running song has a progress bar", document.querySelectorAll("#list progress:not([hidden])").length, 8);

  click("취소");
  await until(() => buttons().includes("다시 다운로드"));
  expect("cancelled: buttons", buttons(), ["다시 다운로드", "새로 시작"]);
  expect("cancelled: the summary says so", summary()?.startsWith("취소됨 — 성공 0 · 건너뜀 0 · 실패 0"), true);
  expect("cancelled: unfinished rows are ready again", count((label) => label === "준비됨"), 98);
  expect("cancelled: the options are free again", $("#options").disabled, false);

  click("다시 다운로드");
  await until(() => buttons().includes("실패 항목 재시도"));
  expect("finished: 96 done", count((label) => label.startsWith("완료")), 96);
  expect("finished: the two failures say why", labels().filter((label) => label.startsWith("실패")), ["실패: m4a 오디오 형식이 없습니다.", "실패: 태그를 쓰지 못했습니다. (지원하지 않는 m4a 구조)"]);
  expect("finished: the summary", summary()?.startsWith("완료 — 성공 96 · 건너뜀 0 · 실패 2 · 소요 "), true);
  expect("finished: buttons", buttons(), ["실패 항목 재시도", "다시 다운로드", "새로 시작"]);

  click("실패 항목 재시도");
  await until(() => !buttons().includes("실패 항목 재시도") && buttons().includes("새로 시작"));
  expect("retried: all 98 done", count((label) => label.startsWith("완료")), 98);
  expect("retried: only the retry is counted", summary()?.startsWith("완료 — 성공 2 · 건너뜀 0 · 실패 0"), true);

  click("새로 시작");
  expect("started over: nothing resolved", $("#resolved").hidden, true);
  expect("started over: the address box is empty", $("#input").value, "");
  expect("started over: no buttons", buttons(), []);

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

Expected: `{"pass":true,"failures":[]}`

- [ ] **Step 6: 시나리오 C — 영상 한 곡, 순위, 잘못된 주소**

B 직후(화면이 비어 있다):

```javascript
(async () => {
  // Run right after b-job.js (the screen is empty). A watch address with a list, "this video only", the rank of one video.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const type = (element, value) => { element.value = value; element.dispatchEvent(new Event("input", { bubbles: true })); };
  const labels = () => [...document.querySelectorAll("#list .label")].map((element) => element.textContent);
  const buttons = () => [...document.querySelectorAll("#actions button")].map((element) => element.textContent);
  const click = (text) => { const found = [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === text); if (!found) throw new Error(`no button ${text} among ${buttons()}`); found.click(); };
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };

  type($("#input"), "https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PLabcdefghijklmnop");
  $("#resolve").click();
  await wait(1200);
  expect("a watch address with a list is the playlist", $("#info-title").textContent, "Melon Daily Top 100 · 100개");
  expect("and offers the one video", $("#video-only").hidden, false);
  expect("no rank box for a playlist", $("#rank-field").hidden, true);

  $("#video-only").click();
  await wait(1200);
  expect("this video only: the address becomes the video's", $("#input").value, "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
  expect("this video only: one row", labels().length, 1);
  expect("this video only: the title", $("#info-title").textContent, "단일 영상");
  expect("this video only: the rank box shows 1", [$("#rank-field").hidden, $("#single-rank").value], [false, "1"]);
  expect("this video only: saved in the output folder itself", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\xGetSongs");
  expect("this video only: the shortcut is gone", $("#video-only").hidden, true);
  expect("this video only: the album field is empty", $("#album").value, "");

  type($("#single-rank"), "42x7");
  expect("the rank box keeps digits, three at most", $("#single-rank").value, "427");
  expect("and the name follows", $("#list .file").textContent, "427 아이오아이 (I.O.I) - 갑자기 (Suddenly).m4a");
  type($("#single-rank"), "");
  expect("an emptied rank box stays empty", $("#single-rank").value, "");
  expect("and the name stays", $("#list .file").textContent, "427 아이오아이 (I.O.I) - 갑자기 (Suddenly).m4a");
  type($("#single-rank"), "0");
  expect("rank 0 is read as 1, the box keeps the 0", [$("#single-rank").value, $("#list .file").textContent.slice(0, 3)], ["0", "001"]);
  type($("#single-rank"), "7");
  $("#opt-rank").click();
  expect("without the rank in the name", $("#list .file").textContent, "아이오아이 (I.O.I) - 갑자기 (Suddenly).m4a");
  $("#opt-rank").click();

  click("다운로드 시작");
  const started = Date.now();
  while (!buttons().includes("다시 다운로드") && Date.now() - started < 60000) await wait(100);
  expect("the video is done", labels(), ["완료 · 가사 ✓ 인터넷"]);
  expect("and says so", ($("#actions .summary") || {}).textContent?.startsWith("완료 — 성공 1 · 건너뜀 0 · 실패 0"), true);

  click("새로 시작");
  type($("#input"), "https://evil.com/x");
  $("#resolve").click();
  await wait(100);
  expect("an address the server would refuse is refused at once", [$("#error").hidden, $("#error-text").textContent, $("#resolved").hidden], [false, "YouTube 주소만 사용할 수 있습니다.", true]);
  $("#error-close").click();
  expect("the banner closes", $("#error").hidden, true);

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

Expected: `{"pass":true,"failures":[]}`

- [ ] **Step 7: 시나리오 D — 설치 확인창**

`http://localhost:1420/?demo=broken`을 열고 그 직후:

```javascript
(async () => {
  // Run on http://localhost:1420/?demo=broken right after it loaded: no tools at all, and the way to install yt-dlp.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const panel = () => $("#tools").innerText.replace(/\n+/g, " | ");
  const toolButtons = () => [...$("#tools").querySelectorAll("button")];
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };

  await wait(500);
  expect("nothing is found", panel().startsWith("yt-dlp ✗ | ffmpeg ✗ | JS 런타임 ✗ | yt-dlp 설치"), true);
  expect("ffmpeg and the JavaScript runtime are asked for", [panel().includes("ffmpeg가 필요합니다. 예: winget install Gyan.FFmpeg"), panel().includes("Node.js 22 이상 또는 Deno 2.3 이상")], [true, true]);
  expect("the install button is there and the update button is not", toolButtons().map((button) => button.textContent), ["yt-dlp 설치"]);

  toolButtons()[0].click();
  expect("it asks before it downloads", $("#install-dialog").open, true);
  $("#install-dialog button[value='cancel']").click();
  await wait(100);
  expect("cancelling closes it and downloads nothing", [$("#install-dialog").open, panel().includes("작업 중")], [false, false]);

  toolButtons()[0].click();
  $("#install-dialog button[value='install']").click();
  await wait(300);
  expect("installing: busy, the button is locked", [panel().includes("작업 중…"), toolButtons()[0].disabled], [true, true]);
  await wait(2200);
  expect("installed: yt-dlp is found, the update button replaces the install button", [panel().startsWith("yt-dlp 2026.10.01 ✓ | ffmpeg ✗"), toolButtons().map((button) => button.textContent)], [true, ["yt-dlp 업데이트"]]);
  expect("installed: the result of the install is said", panel().includes("yt-dlp 2026.10.01 을(를) 설치했습니다."), true);

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

Expected: `{"pass":true,"failures":[]}`

- [ ] **Step 8: 눈으로 본다**

브라우저 창이 가려져 있으면 스크린샷이 시간 초과로 실패한다. 그러면 창을 앞으로 가져와 다시 시도한다(그래도 안 되면 이 단계는 건너뛰고 보고서에 적는다). `resize_window`로 1000×760을 주고 `http://localhost:1420/?demo&speed=1`에서 조회와 시작을 누른 뒤 스크린샷을 찍는다:

```javascript
(async () => { const wait = (ms) => new Promise((r) => setTimeout(r, ms)); const box = document.querySelector('#input'); box.value = 'https://www.youtube.com/playlist?list=PLabcdefghijklmnop'; box.dispatchEvent(new Event('input', { bubbles: true })); document.querySelector('#resolve').click(); await wait(1000); [...document.querySelectorAll('#actions button')].find((b) => b.textContent === '다운로드 시작').click(); await wait(2500); return 'running'; })()
```

Expected: 프로토타입과 같은 모양: 위에 도구 카드, 주소 입력과 조회 버튼이 한 줄, 아래에 옵션과 곡 목록, 진행 중인 곡에는 상태 글과 막대, 맨 아래에 `취소`. 끝나면 `resize_window`의 `preset: "desktop"`으로 크기를 되돌린다.

- [ ] **Step 9: 개발 서버를 끈다**

```powershell
Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -like '*vite*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }; "stopped"
```

- [ ] **Step 10: Commit**

```bash
git add -A shell
git commit -m "feat(shell): 프로토타입 화면을 실제 상태 로직 위의 화면으로 바꾼다"
```

---

### Task 8: 실제 셸에서 실제 화면을 구동한다

**Files:**
- Create: `shell/src/check-screen.ts`
- Modify: `shell/src/check.ts`, `shell/src/main.ts`(훅 두 줄), `shell/src-tauri/src/lib.rs`(`check_env`)

**Interfaces:**
- Consumes: 2단계의 점검 페이지(`check.html`, `check_record`, 점검 환경 변수 `XGS_BRIDGE_CHECK_FILE`), Task 7의 화면
- Produces: 명령 `check_env(name)`(점검 모드에서만, 이름은 `XGS_CHECK_VIDEO_URL`, `XGS_CHECK_PLAYLIST_URL`, `XGS_CHECK_OUTPUT_DIR` 셋만); `check.html`이 2단계의 브리지 점검을 마치고 세 값이 있으면 같은 창을 `index.html?check`로 보내고, 화면이 뜬 뒤 `check-screen.ts`가 사람처럼 조작해 `screen*` 항목을 기록한다

- [ ] **Step 1: Rust에 `check_env`를 더한다**

`shell/src-tauri/src/lib.rs`를 아래로 바꾼다(`check_env` 하나가 늘었을 뿐이다):

```rust
use serde_json::{json, Value};
use std::io::Write;
use std::time::Duration;
use tauri::ipc::Channel;
use tauri::{Manager, RunEvent, State};
use xgs_sidecar::{job, Bridge, BridgeError, Lifecycle, Sidecar, SidecarConfig};

/// How long a call waits for a sidecar that is still starting. Starting itself gives up after [SIDECAR_START_LIMIT].
const READY_WAIT: Duration = Duration::from_secs(90);
const SIDECAR_START_LIMIT: Duration = Duration::from_secs(60);
/// When the app closes: how long the sidecar gets to end by itself, and how long one that is still starting is waited for.
const STOP_GRACE: Duration = Duration::from_secs(3);
const STOP_START_WAIT: Duration = Duration::from_secs(10);

/// Runs [call] once the sidecar is ready, on a thread meant for waiting: a call lasts as long as the sidecar works (a
/// resolve takes as long as yt-dlp does), and the thread of the window must stay free.
async fn with_bridge<T, F>(lifecycle: &Lifecycle, call: F) -> Result<T, BridgeError>
where
    T: Send + 'static,
    F: FnOnce(Bridge) -> Result<T, BridgeError> + Send + 'static,
{
    let lifecycle = lifecycle.clone();
    tauri::async_runtime::spawn_blocking(move || call(lifecycle.wait_ready(READY_WAIT)?))
        .await
        .map_err(|error| BridgeError::new(format!("내부 오류: {error}")))?
}

/// Resolves when the sidecar is ready, so the page can show "starting" until then.
#[tauri::command]
async fn sidecar_ready(lifecycle: State<'_, Lifecycle>) -> Result<(), BridgeError> {
    with_bridge(&lifecycle, |_| Ok(())).await
}

#[tauri::command]
async fn tools(lifecycle: State<'_, Lifecycle>) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, |bridge| bridge.tools()).await
}

#[tauri::command]
async fn install_yt_dlp(lifecycle: State<'_, Lifecycle>) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, |bridge| bridge.install_yt_dlp()).await
}

#[tauri::command]
async fn update_yt_dlp(lifecycle: State<'_, Lifecycle>) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, |bridge| bridge.update_yt_dlp()).await
}

#[tauri::command]
async fn resolve(lifecycle: State<'_, Lifecycle>, input: String) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, move |bridge| bridge.resolve(&input)).await
}

#[tauri::command]
async fn start_job(lifecycle: State<'_, Lifecycle>, request: Value) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, move |bridge| bridge.start_job(&request)).await
}

#[tauri::command]
async fn cancel_job(lifecycle: State<'_, Lifecycle>, job_id: String) -> Result<(), BridgeError> {
    with_bridge(&lifecycle, move |bridge| bridge.cancel_job(&job_id)).await
}

/// Sends the events of the job to [on_event] as they come and resolves after `job-done`. It rejects when the stream fails
/// (the server's `error` event, a cut connection) or when the page that listens is gone.
#[tauri::command]
async fn watch_job(
    lifecycle: State<'_, Lifecycle>,
    job_id: String,
    on_event: Channel<Value>,
) -> Result<(), BridgeError> {
    with_bridge(&lifecycle, move |bridge| bridge.watch_job(&job_id, |event| on_event.send(event).is_ok())).await
}

/// Check mode is on when the check script names a file in `XGS_BRIDGE_CHECK_FILE`; a normal run has no such variable and
/// the two commands below refuse.
fn check_file() -> Result<std::path::PathBuf, String> {
    std::env::var_os("XGS_BRIDGE_CHECK_FILE").map(Into::into).ok_or_else(|| "not in check mode".to_string())
}

/// The check page (`check.html`) reports what it saw as one `key=value` line of the check file.
#[tauri::command]
fn check_record(key: String, value: String) -> Result<(), String> {
    let mut file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(check_file()?)
        .map_err(|error| error.to_string())?;
    writeln!(file, "{key}={}", value.replace(['\r', '\n'], " ")).map_err(|error| error.to_string())
}

/// What the check script told the page to use: an address of a video, an address of a playlist, a folder for the files. Only
/// these names, and only in check mode.
#[tauri::command]
fn check_env(name: String) -> Result<Option<String>, String> {
    check_file()?;
    const ALLOWED: [&str; 3] = ["XGS_CHECK_VIDEO_URL", "XGS_CHECK_PLAYLIST_URL", "XGS_CHECK_OUTPUT_DIR"];
    if !ALLOWED.contains(&name.as_str()) {
        return Err(format!("{name} is not one of the values of the check"));
    }
    Ok(std::env::var(&name).ok())
}
/// Does nothing. The check page times how long a round trip to Rust takes while the sidecar starts: a window whose thread is
/// busy answers late.
#[tauri::command]
fn check_ping() {}

/// Sends [count] numbered progress events to [on_event] as fast as it can, to check the event path of the webview (order,
/// loss, speed) without a download.
#[tauri::command]
async fn check_burst(count: u32, on_event: Channel<Value>) -> Result<(), String> {
    check_file()?;
    tauri::async_runtime::spawn_blocking(move || {
        for seq in 0..count {
            let event = json!({"type":"progress","rank":1,"stage":"DOWNLOADING","percent":f64::from(seq % 100),"seq":seq});
            on_event.send(event).map_err(|error| error.to_string())?;
        }
        Ok(())
    })
    .await
    .map_err(|error| error.to_string())?
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // First of all: whatever happens to this process, the sidecar and its children go with it. (Whether this worked is not
    // recorded anywhere yet; the sidecar also ends by itself when its stdin closes.)
    let _ = job::kill_children_when_this_process_ends();

    let config = SidecarConfig::from_env();
    let lifecycle = Lifecycle::start(move || {
        let config = config?;
        Sidecar::spawn(config.command(), &config.stderr_log(), SIDECAR_START_LIMIT)
    });

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(lifecycle)
        .invoke_handler(tauri::generate_handler![
            sidecar_ready,
            tools,
            install_yt_dlp,
            update_yt_dlp,
            resolve,
            start_job,
            cancel_job,
            watch_job,
            check_record,
            check_ping,
            check_env,
            check_burst
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    app.run(|handle, event| {
        if let RunEvent::Exit = event {
            handle.state::<Lifecycle>().shutdown(STOP_GRACE, STOP_START_WAIT);
        }
    });
}
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\shell\src-tauri; cargo clippy 2>&1 | Select-String "^warning|^error|Finished"
```

Expected: `Finished`, 경고 없음.

- [ ] **Step 2: 점검 파일을 쓴다**

`shell/src/check-screen.ts`:

```typescript
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
```

`shell/src/check.ts`(2단계의 것에서 끝이 바뀌었다: 세 값이 있으면 `index.html?check`로 넘어간다):

```typescript
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

  // With real addresses and a folder from the script, the real screen is used next, as the top-level page (src/check-screen.ts):
  // it records `screenDone` and `done` itself.
  const wanted = ["XGS_CHECK_VIDEO_URL", "XGS_CHECK_PLAYLIST_URL", "XGS_CHECK_OUTPUT_DIR"];
  const given = await Promise.all(wanted.map((name) => invoke<string | null>("check_env", { name })));
  if (given.every(Boolean)) {
    await record("bridgeChecksDone", 1);
    location.href = "/index.html?check";
    return;
  }

  await record("done", 1);
}

main().catch(async (reason) => {
  status.textContent = `실패: ${errorMessage(reason)}`;
  await invoke("check_record", { key: "error", value: errorMessage(reason) }).catch(() => {});
});
```

`shell/src/main.ts`의 `boot()`에서 `mount(new AppStateHolder({ api, defaultOutputDir }));` 바로 뒤에 더한다:

```typescript
  // The developer tooling uses the screen like a person does (see check-screen.ts); a normal start has no `?check`.
  if (params.has("check")) void import("./check-screen.ts").then((module) => module.runScreenChecks());
```

- [ ] **Step 3: 컴파일하고 시험하고 빌드한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; npx tsc --noEmit; "tsc exit $LASTEXITCODE"; node --test "tests/*.test.ts" 2>&1 | Select-String "^ℹ (tests|pass|fail)"
```

Expected: `tsc exit 0`, `ℹ tests 212`, `ℹ pass 212`.

- [ ] **Step 4: 확인용 스크립트를 둔다**

`%TEMP%\xgs-bridge-check\helpers.ps1`(2단계와 같은 도우미):

```powershell
# Dot-sourced by the steps that run the check build of the shell. $ShellExe can be set before the dot-source.
Remove-Item Env:XGS_SIDECAR_LIB, Env:XGS_SIDECAR_JAVA -ErrorAction SilentlyContinue
if (-not $ShellExe) { $ShellExe = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe" }
$Image = "C:\Projects\xGetSongs\server\build\sidecar-image"

function Get-Sidecar {
    Get-CimInstance Win32_Process -Filter "Name='java.exe'" | Where-Object { $_.CommandLine -like '*SidecarMainKt*' }
}

# A fresh app data folder, log folder and record file: never the user's real ones.
function New-CheckRun {
    $base = Join-Path $env:TEMP ("xgs-bridge-check-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    New-Item -ItemType Directory $base | Out-Null
    [pscustomobject]@{ Base = $base; AppData = "$base\data"; LogDir = "$base\log"; Record = "$base\check-record.txt" }
}

function Start-CheckShell($run) {
    $env:XGS_SIDECAR_DIR = $Image
    $env:XGS_SIDECAR_APPDATA = $run.AppData
    $env:XGS_SIDECAR_LOGDIR = $run.LogDir
    $env:XGS_BRIDGE_CHECK_FILE = $run.Record
    Start-Process -FilePath $ShellExe -PassThru
}

# The lines the check page wrote, as a hashtable (a key that was written twice keeps its last value).
function Read-CheckRecord($run) {
    $values = [ordered]@{}
    if (Test-Path $run.Record) {
        foreach ($line in Get-Content $run.Record -Encoding UTF8) {
            $key, $value = $line -split '=', 2
            $values[$key] = $value
        }
    }
    $values
}

# Waits until the page wrote done= or error=, at most $Seconds.
function Wait-CheckRecord($run, [int]$Seconds = 90) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        Start-Sleep -Milliseconds 200
        $record = Read-CheckRecord $run
    } while ((Get-Date) -lt $deadline -and -not ($record.Contains("done") -or $record.Contains("error")))
    $record
}

# Waits until the shell has its real window (one with a title; a handle found earlier belongs to a window that cannot be
# closed), and returns how many ms that took.
function Wait-CheckWindow($process, [int]$Seconds = 10) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not $process.MainWindowTitle -and $watch.Elapsed.TotalSeconds -lt $Seconds) { Start-Sleep -Milliseconds 5; $process.Refresh() }
    [int]$watch.ElapsedMilliseconds
}

# Closes the window and returns how many ms it took until the shell and the sidecar were both gone (null: after 20 s).
function Close-CheckShell($process) {
    [void](Wait-CheckWindow $process)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [void]$process.CloseMainWindow()
    while ($watch.Elapsed.TotalSeconds -lt 20 -and ((Get-Sidecar) -or -not $process.HasExited)) { Start-Sleep -Milliseconds 50 }
    if ((Get-Sidecar) -or -not $process.HasExited) { return $null }
    [int]$watch.Elapsed.TotalMilliseconds
}
```

`%TEMP%\xgs-bridge-check\screen-check.ps1`:

```powershell
# Runs the check build of the shell with the real screen on the real sidecar and the real yt-dlp (a COPY of the app's own), and prints
# what the screen did. Needs the helpers of the bridge check (New-CheckRun, Start-CheckShell, Wait-CheckRecord, Close-CheckShell).
param(
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$VideoUrl = 'https://www.youtube.com/watch?v=9qkpcLK422o',
    [string]$PlaylistUrl = 'https://www.youtube.com/playlist?list=PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV',
    [int]$Seconds = 420
)
. $Helpers
$run = New-CheckRun
New-Item -ItemType Directory "$($run.AppData)\bin" -Force | Out-Null
Copy-Item "$env:APPDATA\xGetSongs\bin\yt-dlp.exe" "$($run.AppData)\bin\yt-dlp.exe"
$files = Join-Path $run.Base "files"
$env:XGS_CHECK_VIDEO_URL = $VideoUrl
$env:XGS_CHECK_PLAYLIST_URL = $PlaylistUrl
$env:XGS_CHECK_OUTPUT_DIR = $files

# The screen's own default folder is in the user's Music folder: the check must never write there.
$music = Join-Path $env:USERPROFILE "Music\xGetSongs"
$before = if (Test-Path $music) { (Get-ChildItem $music -Recurse -File | Measure-Object).Count } else { -1 }

$watch = [Diagnostics.Stopwatch]::StartNew()
$process = Start-CheckShell $run
$record = Wait-CheckRecord $run $Seconds
$elapsed = [int]$watch.Elapsed.TotalSeconds
$closedMs = Close-CheckShell $process

"=== the screen checks ($elapsed s, closed after $closedMs ms)"
foreach ($key in $record.Keys) { "{0,-24} {1}" -f $key, $record[$key] }
"=== files the check wrote"
if (Test-Path $files) { Get-ChildItem $files -Recurse -File | ForEach-Object { "{0,10:N0}  {1}" -f $_.Length, $_.FullName.Substring($files.Length) } } else { "(no folder)" }
$after = if (Test-Path $music) { (Get-ChildItem $music -Recurse -File | Measure-Object).Count } else { -1 }
"=== the user's own default folder: $before files before, $after after"
"=== marker: " + ((Get-Content "$($run.LogDir)\last-run.txt" -ErrorAction SilentlyContinue) -join ' | ')
"=== left over: sidecars $(@(Get-Sidecar).Count), yt-dlp/ffmpeg $(@(Get-Process yt-dlp, ffmpeg -ErrorAction SilentlyContinue).Count)"
$run.Base | Set-Content "$env:TEMP\xgs-bridge-check\last-screen-run.txt"
Remove-Item Env:XGS_CHECK_VIDEO_URL, Env:XGS_CHECK_PLAYLIST_URL, Env:XGS_CHECK_OUTPUT_DIR
```

(두 파일은 저장소 밖에 둔다. 폴더가 없으면 `New-Item -ItemType Directory -Force "$env:TEMP\xgs-bridge-check"`로 만든다.) 스크립트의 영상과 재생목록 주소는 사용자가 이 확인에 쓰라고 준 것이다. 다른 값으로 바꾸려면 인자(`-VideoUrl`, `-PlaylistUrl`)로 준다. 재생목록은 시작한 뒤 첫 진행에서 취소하므로 몇 곡이든 괜찮다.

- [ ] **Step 5: 점검용 셸을 빌드한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\shell; npm run tauri build -- --no-bundle --config check.conf.json 2>&1 | Select-Object -Last 4; Get-Item src-tauri\target\release\shell.exe | Select-Object Length, LastWriteTime
```

Expected: 빌드 성공, `shell.exe`가 방금 시각(2~5분).

- [ ] **Step 6: 실제 화면을 실제 서버로 구동한다 (S4, S5의 앞 절반, 악화 한도)**

```powershell
& "$env:TEMP\xgs-bridge-check\screen-check.ps1"
```

Expected(이 계획을 쓰며 같은 코드로 돌린 값은 괄호 안): 아래 항목이 모두 맞고, 2분 안에 끝난다(43초).

| 항목 | 기대 |
|---|---|
| `tools` … `burstOutOfOrder` (2단계의 브리지 점검) | 2단계와 같다. `tools=ffmpeg,jsRuntime,ytDlp`, 오류 문구 6개(`resolveEmpty`, `resolveUnknown`, `startJobExpired`, `cancelUnknown`, `cancelBadId`, `watchUnknown`)가 서버의 글자 그대로, `burstReceived=5000`, `burstOutOfOrder=0`, `waitedMs` 10,000 이하(1,280). 다른 점 하나: 임시 앱 데이터에 yt-dlp 복사본이 있으므로 `updateWithoutYtDlp=(skipped: yt-dlp is installed)` |
| `bridgeChecksDone` | `1` |
| `screenTools` | `yt-dlp … ✓ \| ffmpeg … ✓ \| JS 런타임 … ✓ (after N ms)` (3,098ms) |
| `screenDefaultFolder` | `C:\Users\<이름>\Music\xGetSongs` |
| `screenVideoPreview` | `단일 영상 \| 1 row \| 001 <가수> - <제목>.m4a` |
| `screenDestination` | `저장 위치: <임시 폴더>\files` |
| `screenVideoRankedName` | `042 <가수> - <제목>.m4a` |
| `screenVideoRunning` | `취소 \| options disabled true` |
| `screenVideoResult` | `완료 — 성공 1 · 건너뜀 0 · 실패 0 · 소요 N초 \| 완료 · 가사 … \| 042 … \| N ms` (24초) |
| `screenPlaylistPreview` | `<재생목록 제목> · N개 \| N rows` (101개) |
| `screenCancelResult` | `취소됨 — … \| ready N done 0 still moving 0 \| N ms after the button`(`still moving`이 0이고 `ready`가 목록의 곡 수와 같다. 643ms) |
| `screenError`, `screenDone`, `done` | `none`, `1`, `1`. `error` 항목은 없다 |
| 스크립트가 찍는 파일 목록 | 임시 `files` 폴더에 `042 ….m4a` 하나(100,000바이트 넘게, 3,007,075)뿐이다. 취소한 재생목록의 `cancelled` 폴더에는 완성된 곡이 없다 |
| 사용자의 기본 폴더 | `101 files before, 101 after`처럼 **전과 후가 같다** |
| 마커와 남은 프로세스 | `state=exited \| … \| reason=user`, `sidecars 0, yt-dlp/ffmpeg 0` |

하나라도 어긋나면 기록(`screen*`)을 읽고 원인이 화면 코드인지 확인 방법인지 가려 고친 뒤 처음부터 다시 돌린다. 한 곡이 취소 전에 끝나 `done`이 0이 아니어도 `still moving 0`이면 괜찮다. `error=gave up waiting …`이 나오면 그 대기 이름이 어느 단계인지 보고 종료 시각의 파일 목록을 같이 본다.

- [ ] **Step 7: 창 닫기, 시작 직후 닫기, 강제 종료 (S5)**

2단계와 같은 확인이다(그때의 도우미가 `Wait-CheckWindow`로 제목이 있는 창을 기다린다):

```powershell
. "$env:TEMP\xgs-bridge-check\helpers.ps1"
foreach ($n in 1..3) {
    $run = New-CheckRun
    $process = Start-CheckShell $run
    $windowMs = Wait-CheckWindow $process
    $closedMs = Close-CheckShell $process
    $marker = (Get-Content "$($run.LogDir)\last-run.txt" -ErrorAction SilentlyContinue) -join ' | '
    "early close $n : window after $windowMs ms, everything gone after $closedMs ms, left over: $(@(Get-Sidecar).Count) :: $marker"
}
foreach ($n in 1..3) {
    $run = New-CheckRun
    $process = Start-CheckShell $run
    Wait-CheckRecord $run 90 | Out-Null
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Stop-Process -Id $process.Id -Force
    while ((Get-Sidecar) -and $watch.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 25 }
    "force kill $n : sidecar gone after $([int]$watch.Elapsed.TotalMilliseconds) ms, left over: $(@(Get-Sidecar).Count)"
}
```

Expected: 시작 직후 닫기 3번 모두 15,000ms 이하(2단계에서 2.7~3.0초)로 남은 사이드카 0, 마커 `state=exited`와 `reason=user`. 강제 종료 3번 모두 5,000ms 이하(2단계에서 약 150ms), 남은 사이드카 0. (이 두 실행에는 `XGS_CHECK_*`가 없으므로 점검 페이지는 브리지 점검만 하고 `done`으로 끝난다.)

- [ ] **Step 8: Commit**

```bash
git add -A shell
git commit -m "test(shell): 점검 페이지가 실제 화면을 실제 사이드카로 구동하게 한다"
```

---

### Task 9: 전체 시험, 보고서, 문서

**Files:**
- Create: `docs/superpowers/specs/2026-10-11-tauri-frontend-port-report.md`
- Modify: `docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`(§5의 3단계 행)

- [ ] **Step 1: 전체를 한 번 더 돌린다 (S6)**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\shell
node --test "tests/*.test.ts" 2>&1 | Select-String "^ℹ (tests|pass|fail|skipped)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"; npm run build 2>&1 | Select-String "built in|error"
node tools/differential/fuzz.mts 20000 2>&1 | Select-Object -Last 1
Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test 2>&1 | Select-String "test result"
Set-Location C:\Projects\xGetSongs\shell\src-tauri; cargo clippy 2>&1 | Select-String "^warning|^error|Finished"
```

Expected: `ℹ tests 212`·`ℹ pass 212`·`ℹ fail 0`·`ℹ skipped 0`, `tsc exit 0`, `built in`, `80000 cases (20000 per kind), 0 mismatches`, 단위 `75 passed`와 통합 `1 ignored`, clippy `Finished`.

- [ ] **Step 2: 보고서를 쓴다**

`docs/superpowers/specs/2026-10-11-tauri-frontend-port-report.md`를 아래 틀로 만들고 Task 3·7·8의 실제 값으로 채운다:

```markdown
# Tauri 프런트엔드 이식(3단계) 보고서

작성일: 2026-10-11
설계: [2026-10-10-tauri-kotlin-sidecar-design.md](2026-10-10-tauri-kotlin-sidecar-design.md) §5의 3단계
계획: [2026-10-11-tauri-frontend-port.md](../plans/2026-10-11-tauri-frontend-port.md)

환경: (Windows, Node, Vite, Rust/Tauri 버전, 사이드카 이미지, 실제 yt-dlp 버전, 셸은 `--config check.conf.json`으로 만든 릴리스 빌드)

## 관문

| 번호 | 조건 | 결과 | 근거 |
|---|---|---|---|
| S1 | 옮긴 원본 테스트 | | 39 + 15 + 35 + 95 개와 점검 테스트 4개 |
| S2 | Kotlin 구현과 대조 | | 80,000건 불일치 N건. 일부러 틀리게 한 `trim`은 N건을 잡았다 |
| S3 | 데모 서버 위 브라우저 시나리오 | | A~D |
| S4 | 실제 셸에서 실제 서버로 | | 점검 기록(`screen*`) |
| S5 | 앞 단계 관문 유지 | | 브리지 점검 12개, 닫기, 시작 직후 닫기, 강제 종료 |
| S6 | 전체 | | 테스트 수, tsc, 빌드, cargo, clippy |
| 악화 한도 | `waitedMs` ≤ 10초 | | |

## 측정

(점검 기록의 값: `waitedMs`, `screenTools`의 대기, 영상 한 곡의 조회·다운로드 시간, 재생목록 조회, 취소가 끝나기까지의 시간, 창 닫기 시간, 시작 직후 닫기, 강제 종료.)

## 막힌 점과 고친 것

(계획과 달랐던 점. 없으면 "없음".)

## 판정

(통과 / 재시도 후 통과 / 중단. 통과이면 다음은 설계 §5의 4단계 "데스크톱 기능"이다.)

## 사용자가 직접 확인할 것

이 단계의 화면은 폴더 선택 창과 탐색기 열기가 없고(출력 폴더는 직접 입력) 옵션이 다음 실행에 저장되지 않는다(4단계). 그 밖의 것은 실제로 써 볼 수 있다:

- 일반 빌드(`npm run tauri build -- --no-bundle`)를 만들어 `shell\src-tauri\target\release\shell.exe`를 실행한다.
- 한글 입력: 주소, 출력 폴더, 앨범명 칸에 한글을 입력·조합하고(조합 밑줄, 커서), 붙여넣기·선택·우클릭 메뉴를 써 본다. 다운로드 중에 한글을 입력해도 글자가 깨지지 않는다.
- 키보드만으로 조회부터 다운로드 시작까지, Enter로 조회, Tab 순서, 다크·라이트 설정.
- 실제 재생목록 조회와 다운로드, 도중 취소, 실패 항목 재시도.

## 이 단계가 확인하지 않은 것

- 한글 IME 조합 중의 입력(자동으로 시험할 수 없다. 코드는 조합 중인 입력란에 값을 쓰지 않는다).
- 999곡 목록의 성능(확인은 100곡·101곡이다).
- 우클릭 메뉴의 새로고침(F5·Ctrl+R은 막았다. 새로 읽으면 돌고 있는 작업의 이벤트를 다시 받지 못한다).
- `install_yt_dlp`와 `update_yt_dlp`의 실제 호출(내려받는 호출이라 데모에서만 확인했다).
- 2단계에서 넘어온 항목: 사이드카가 실행 중에 죽는 경우, Job Object 생성 결과의 기록, 셸에 로거를 붙일 때의 `ureq` 로그의 토큰.
```

- [ ] **Step 3: 설계 문서를 갱신한다**

`docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`의 §5 표에서 3단계 행을 아래로 바꾼다:

```markdown
| 3 | 프런트엔드 이식: `AppStateHolder` 논리와 `Labels`, 화면. `shared` 로직 재사용 방식 확정 | 지금의 `AppStateHolderTest` 시나리오가 새 테스트에서 통과 ([계획서](../plans/2026-10-11-tauri-frontend-port.md), [보고서](2026-10-11-tauri-frontend-port-report.md): 통과). 재사용 방식은 TS 포팅과 같은 케이스 표 테스트이고 Kotlin 구현과의 대조 도구가 있다 |
```

§2 표의 "`shared`의 순수 로직"과 "프런트엔드 프레임워크" 행의 "3단계에서 정한다"를 각각 "TS 포팅 + 같은 케이스 표 테스트 + Kotlin 대조 도구(3단계에서 정함)"와 "없음, vanilla TS(3단계에서 정함)"로 바꾼다.

- [ ] **Step 4: Commit**

```bash
git add docs
git commit -m "문서: 프런트엔드 이식(3단계)의 종단 확인 결과와 설계의 3단계 행"
```

---

## Self-Review

**1. Spec coverage** (설계 §5의 3단계, §2의 두 "3단계에서 정한다")

- "`AppStateHolder` 논리": Task 5(+ Task 4의 `UiState`·`Labels`). "화면": Task 7. "`shared` 로직 재사용 방식 확정": 위 "이 계획이 내린 결정"과 Task 1~3. 끝났다는 증거 "지금의 `AppStateHolderTest` 시나리오가 새 테스트에서 통과": Task 5의 98개와 점검 테스트.
- "프런트엔드 프레임워크": 없음으로 정했고 화면이 그대로 그것을 보인다(Task 7).
- Compose 화면의 요소 중 옮긴 것: 도구 패널(설치 확인창, 업데이트, 안내문, 작업 중·결과 메시지), 주소 입력과 조회, 오류 배너, `ResolveInfo`(제목·개수·999개 경고·"이 영상만 받기"), 옵션(출력 폴더, 저장 위치, 앨범명, 세 체크박스, 동시 다운로드 안내, 영상의 순위 칸), 곡 목록(순번, 파일 이름 또는 제목, 가수명 추정 경고, 상태 글과 진행 막대), 행동 줄(시작·취소·요약·재시도·다시 다운로드·새로 시작). 미룬 것: 폴더 선택, 탐색기, 설정 저장, 창 닫기 시의 저장(4단계).
- 이 계획 밖(의도적): 4단계의 데스크톱 기능, 새로고침 뒤의 작업 이어 보기(서버 변경이 필요하다), 단일 exe와 포장(5단계), `app/` 삭제(6단계).

**2. Placeholder scan:** 코드 단계는 이 계획을 쓰며 실제로 쓰고 돌려 본 코드를 담았다(`node --test` 212개, `tsc`, 브라우저 시나리오 4개 `pass: true`, 대조 80,000건 0불일치, 실제 셸 점검). 보고서의 빈 칸은 측정값을 채우는 기록 칸이다. 네 이식 스위트의 기대 개수는 원본 `@Test` 수(39, 15, 35, 95)와 같다.

**3. Type consistency:** `XgsApi`(Task 4)는 2단계의 `bridge`가 만족하는 모양이고(`tools`, `installYtDlp`, `updateYtDlp`, `resolve`, `startJob`, `watchJob`, `cancelJob`), `FakeApi`(Task 5)와 데모(Task 6)가 같은 모양을 구현한다. `UiState.elapsedMs`(밀리초)는 `holder.ts`의 시계, `summaryText`·`elapsedLabel`, 테스트의 `clock.ms`가 같다. 화면의 요소 이름(`#input`, `#resolve`, `#list`, `#actions`, …)은 `index.html`, `main.ts`, 브라우저 시나리오 A~D, `check-screen.ts`가 같다. 환경 변수 `XGS_CHECK_VIDEO_URL`·`XGS_CHECK_PLAYLIST_URL`·`XGS_CHECK_OUTPUT_DIR`은 Rust(`check_env`), `check.ts`, `check-screen.ts`, `screen-check.ps1`이 같다.

**4. Review Focus:** 다섯 항목은 위 Review Focus 절에 적은 Task의 테스트와 확인에 대응한다. 1번(한글 조합)과 4번(우클릭 새로고침)은 자동으로 시험할 수 없어서 보고서의 "사용자가 직접 확인할 것"과 "확인하지 않은 것"으로 넘긴다.
