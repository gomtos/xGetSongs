# Tauri 사이드카 완성 (1단계) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 사이드카가 지금 Compose 앱과 같은 진단(파일 로그, 시작 정보, 종료 기록, `last-run.txt`, 멈춤 덤프)을 남기고, 번들한 런타임과 줄인 lib로 실행된다.

**Architecture:** `Diagnostics` 코드를 새 `:diagnostics` 모듈로 옮겨 Compose 앱과 사이드카가 함께 쓴다(UI 스레드 전제를 빼고, 사이드카는 `Dispatchers.Default`가 응답하는지로 멈춤을 감시한다). 셸은 `--log-dir`로 로그 폴더를 알려 주고, 종료할 때 stdin에 `exit` 줄을 써서 "사용자가 닫음"과 "셸이 갑자기 사라짐"을 구분하게 한다. Gradle `sidecarImage`가 `lib`(다른 OS의 네이티브 jar 제외)와 `runtime`(JDK 복사)을 한 폴더로 만들고, Rust는 `XGS_SIDECAR_DIR` 또는 실행 파일 옆 `sidecar` 폴더에서 그것을 찾는다.

**Tech Stack:** Kotlin 2.4.20 / JDK 21 / Ktor 3.6.0 / logback 1.5.38 (기존), Rust 1.99 (`sidecar-host`), Gradle (`Sync`, `Test` 태스크), PowerShell 확인 스크립트.

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md) §5의 1단계, 그리고 스파이크의 마무리 항목([보고서](../specs/2026-10-10-tauri-sidecar-spike-report.md) "판정")

## Global Constraints

- 사이드카는 `--app-data <절대 경로>`를 반드시 받고 기본값이 없다. `createServices`가 시작할 때 `<app-data>\work`를 지우므로, 테스트와 확인은 임시 폴더만 쓰고 사용자의 `%APPDATA%\xGetSongs`는 건드리지 않는다.
- 시작 신호는 stdout의 한 줄 `XGS-READY <port> <token>`뿐이다. 로그는 파일과 stderr로만 보낸다.
- 종료 신호는 stdin이다. 닫히면 사이드카가 끝난다. 이 계획이 더하는 것은 닫기 전에 쓰는 `exit` 줄 하나뿐이다.
- 토큰, 요청 헤더, 가사, 영상 제목, 파일 이름은 로그에 남기지 않는다(`LogRedaction`과 `JobLog`가 이미 지킨다). 새 테스트는 로그에 토큰이 없음을 확인한다.
- `Guard.kt`(Origin 거부, Host 검사, 토큰)는 바꾸지 않는다.
- Compose 앱(`app/`)의 동작은 그대로다. 옮긴 진단 테스트와 `app`의 남은 테스트가 모두 통과해야 한다. `README.md`는 이 계획에서 고치지 않는다(설계 §7: 5~6단계). 로그 문구와 `ui-hang-*.txt` 파일 이름도 Compose 앱과 같게 둔다.
- 번들할 런타임의 출처는 Gradle 속성 `xgs.runtimeDir`이고 기본은 Gradle이 쓰는 JDK 21 도구 체인이다(이 PC에서는 JetBrains Runtime 21.0.9, 92MB). `jlink`로 줄이려면 `jmods`가 있는 JDK가 필요한데 이 PC의 JBR에는 없다(`jlink`가 `--module-path is not specified and this runtime image does not contain jmods directory`로 거부한다). 줄이기와 포장은 5단계로 미룬다.
- Gradle은 `--no-daemon`으로 부른다(사용자의 VS Code가 공유 데몬을 쓴다). 열려 있는 Gradle 데몬은 건드리지 않는다.
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구(로그 문구 포함)와 커밋 메시지는 한국어. `main`에 직접 커밋한다. 커밋 메시지 끝에는 이 세션이 정한 `Co-Authored-By` 줄을 붙인다.
- Bash 도구는 명령 텍스트의 백슬래시를 한 번 더 해석한다. 백슬래시가 든 코드(Rust 문자열, 정규식)는 Write·Edit 도구로 쓰고, 셸 명령에는 백슬래시를 쓰지 않는다.
- 소프트웨어 설치와 내려받기는 사용자에게 알리고 허락을 받은 뒤에 한다(이 계획은 새 설치가 필요 없다).

## Review Focus

계획의 테스트가 직접 다루지 않아 사용자에게 먼저 문제가 될 것, 가능성이 높은 순서:

1. **로그 폴더를 만들 수 없다**(읽기 전용 설치 폴더, 권한 없음). 셸이 준 폴더가 안 되면 앱 데이터 폴더의 `logs`로, 거기도 안 되면 임시 폴더로 물러나야 하고 사이드카는 뜨는 것을 멈추지 않아야 한다. → Task 4의 `aLogFolderThatCannotBeMadeFallsBackToTheAppDataFolder`
2. **로그에 토큰이 들어간다.** 시작 신호는 stdout에만 가고 로그에는 포트만 나가야 한다. → Task 4의 `theLogHoldsTheStartRecordsButNeverTheToken`
3. **셸이 `exit` 줄을 쓰다 말고 죽는다**(줄이 잘림). 잘린 줄은 사용자 종료가 아니라 "창을 닫지 않은 종료"여야 한다. → Task 3의 `aPartialExitLineAtTheEndIsNotAnExit`
4. **다른 OS의 jar를 뺀 뒤 서버가 못 뜬다.** → Task 6의 `theBundledRuntimeRunsTheSidecarAndStopsOnTheExitLine`(번들 런타임과 줄인 lib로 실제로 시작해 `/tools`가 200이어야 한다)
5. **한글 로그가 깨진다**(콘솔 코드 페이지, 기본 인코딩). 로그 파일은 UTF-8이어야 한다. → Task 2의 `theLogFileIsWrittenInUtf8WithTheGivenFolder`

## File Structure

| 파일 | 책임 |
|---|---|
| `diagnostics/` (새 Gradle 모듈) | 진단 코드: `Diagnostics`, `ExitLogger`, `HangDetector`, `RunMarker`, `ThreadDump`, `UiWatchdog`와 그 테스트. UI 라이브러리(AWT, Swing)를 쓰지 않는다 |
| `app/` (수정) | 위 모듈을 쓴다. `Main.kt`가 UI 스레드(`EventQueue`)와 `headless`를 넘긴다. 앱의 `logback.xml`과 `LogbackConfigTest`는 그대로 남는다 |
| `server/.../sidecar/SidecarArgs.kt` (수정) | `--log-dir` 선택 인자 |
| `server/.../sidecar/SidecarLogging.kt` (새) | 로그 폴더 후보 순서 |
| `server/.../sidecar/SidecarDiagnostics.kt` (새) | 로그 폴더를 정하고 logback에 알리고 진단을 켠다. 멈춤 감시는 `Dispatchers.Default`에 심장 박동을 보낸다 |
| `server/.../sidecar/ParentWatch.kt` (수정) | stdin의 `exit` 줄(사용자 종료)과 닫힘(셸이 사라짐)을 구분한다 |
| `server/.../sidecar/SidecarMain.kt` (수정) | 진단을 켜고 종료 이유를 알린다 |
| `server/src/main/resources/logback-sidecar.xml` (수정) | 파일 로그(회전) + stderr 콘솔 |
| `server/build.gradle.kts` (수정) | `sidecarImage`, `sidecarImageTest`, 배포 아카이브 끄기 |
| `sidecar-host/src/config.rs`, `sidecar.rs` (수정) | 이미지 폴더 찾기, `--log-dir`, 종료 때 `exit` 줄 |
| `docs/superpowers/specs/2026-10-10-tauri-sidecar-complete-report.md` (새) | 종단 확인 결과 |

---

### Task 1: `:diagnostics` 모듈로 옮긴다

**Files:**
- Create: `diagnostics/build.gradle.kts`, `diagnostics/src/test/resources/logback-test.xml`(앱 것의 복사)
- Move: `app/src/desktopMain/kotlin/com/xgetsongs/app/diagnostics/{Diagnostics,ExitLogger,HangDetector,RunMarker,ThreadDump,UiWatchdog}.kt` → `diagnostics/src/main/kotlin/com/xgetsongs/diagnostics/`
- Move: `app/src/desktopTest/kotlin/com/xgetsongs/app/diagnostics/{DiagnosticsTest,ExitLoggerTest,HangDetectorTest,RunMarkerTest,ThreadDumpTest,UiWatchdogTest,LogCapture}.kt` → `diagnostics/src/test/kotlin/com/xgetsongs/diagnostics/`
- Modify: `settings.gradle.kts`, `app/build.gradle.kts`, `app/src/desktopMain/kotlin/com/xgetsongs/app/Main.kt`, 옮긴 `Diagnostics.kt`와 `DiagnosticsTest.kt`
- 그대로 둔다: `app/src/desktopMain/resources/logback.xml`, `app/src/desktopTest/.../diagnostics/LogbackConfigTest.kt`(앱의 `logback.xml`을 시험하므로 앱에 남는다)

**Interfaces:**
- Consumes: 기존 `com.xgetsongs.shared.log.LogRedaction`
- Produces (Task 2~4가 쓴다), 패키지 `com.xgetsongs.diagnostics`:
  - `object Diagnostics { fun start(logDir: Path, postToUi: (Runnable) -> Unit, registerHook: (Thread) -> Unit = Runtime.getRuntime()::addShutdownHook, processes: Processes = SystemProcesses, headless: Boolean? = null): DiagnosticsHandle }`
  - `class DiagnosticsHandle { var runningJobs: () -> Int; fun markUserExit(); fun stop() }`
  - `const val LOG_DIR_PROPERTY = "xgs.logDir"`, `fun chooseLogDirectory(candidates: List<Path>, report: (String) -> Unit = System.err::println): Path`, `fun logDirectoryCandidates(applicationDir: Path?, appDataDir: Path, tempDir: Path): List<Path>`, `fun applicationDirectory(...)`, `interface Processes`, `object SystemProcesses`
  - 모듈 안에서만(테스트용) `internal`: `startupRecord(...)`, `ensureLogDirectory(...)`, 나머지 클래스

현재 기준값(2026-10-10 측정): 옮길 테스트는 `DiagnosticsTest` 41, `ExitLoggerTest` 17, `HangDetectorTest` 26, `RunMarkerTest` 32, `ThreadDumpTest` 15, `UiWatchdogTest` 26 = 157개이고, 앱에 남는 `LogbackConfigTest`는 12개다.

- [ ] **Step 1: 모듈 뼈대를 만들고 파일을 옮긴다**

`settings.gradle.kts`에서 `include(":shared")` 다음 줄에 `include(":diagnostics")`를 더한다.

`diagnostics/build.gradle.kts`:

```kotlin
plugins {
    alias(libs.plugins.kotlin.jvm)
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    implementation(project(":shared"))
    implementation(libs.slf4j.api)

    testImplementation(kotlin("test"))
    testImplementation(libs.logback.classic) // ListAppender: the tests read what is logged
}

tasks.test {
    useJUnitPlatform()
}

// The diagnostics run in every process of the app, including the sidecar, whose runtime need not have java.desktop: no AWT
// and no Swing in here. The UI thread is handed in by the caller (see Diagnostics.start).
val checkNoAwt by tasks.registering {
    val sources = fileTree("src/main/kotlin") { include("**/*.kt") }
    inputs.files(sources)
    doLast {
        val forbidden = Regex("""\bjava\.awt\.|\bjavax\.swing\.""")
        val offenders = sources.files.flatMap { file ->
            file.readLines().withIndex()
                .filter { forbidden.containsMatchIn(it.value) }
                .map { "${file.name}:${it.index + 1}: ${it.value.trim()}" }
        }
        if (offenders.isNotEmpty()) {
            throw GradleException("diagnostics must not use AWT or Swing:\n" + offenders.joinToString("\n"))
        }
    }
}

tasks.matching { it.name == "test" || it.name == "check" }.configureEach {
    dependsOn(checkNoAwt)
}
```

파일을 옮기고 패키지 이름을 바꾼다(PowerShell이 아니라 Git Bash로, 백슬래시 없이):

```bash
mkdir -p diagnostics/src/main/kotlin/com/xgetsongs/diagnostics diagnostics/src/test/kotlin/com/xgetsongs/diagnostics diagnostics/src/test/resources
for f in Diagnostics ExitLogger HangDetector RunMarker ThreadDump UiWatchdog; do git mv app/src/desktopMain/kotlin/com/xgetsongs/app/diagnostics/$f.kt diagnostics/src/main/kotlin/com/xgetsongs/diagnostics/$f.kt; done
for f in DiagnosticsTest ExitLoggerTest HangDetectorTest RunMarkerTest ThreadDumpTest UiWatchdogTest LogCapture; do git mv app/src/desktopTest/kotlin/com/xgetsongs/app/diagnostics/$f.kt diagnostics/src/test/kotlin/com/xgetsongs/diagnostics/$f.kt; done
cp app/src/desktopTest/resources/logback-test.xml diagnostics/src/test/resources/logback-test.xml
sed -i 's/com.xgetsongs.app.diagnostics/com.xgetsongs.diagnostics/g' diagnostics/src/main/kotlin/com/xgetsongs/diagnostics/*.kt diagnostics/src/test/kotlin/com/xgetsongs/diagnostics/*.kt
git status --short | head -20
```

Expected: `R`(이동) 13줄과 새 파일(`diagnostics/build.gradle.kts`, `logback-test.xml`), 수정된 `settings.gradle.kts`.

- [ ] **Step 2: AWT 검사가 실패하는 것을 확인한다 (RED)**

Run: `.\gradlew.bat :diagnostics:checkNoAwt --no-daemon`
Expected: FAIL — `diagnostics must not use AWT or Swing:`와 함께 `Diagnostics.kt:`의 `import java.awt.EventQueue`, `import java.awt.GraphicsEnvironment` 두 줄.

- [ ] **Step 3: `headless`가 없을 때를 정하는 테스트를 먼저 쓴다 (RED)**

`diagnostics/src/test/kotlin/com/xgetsongs/diagnostics/DiagnosticsTest.kt`에서 `aMissingPropertyDoesNotBreakTheStartupRecord` 테스트 바로 다음(`// ---- the log folder` 앞)에 더한다:

```kotlin
    @Test
    fun theHeadlessLineIsLeftOutWhenNobodyAskedAnAwtForIt() {
        val text = startupRecord(null, { null }, 4, 512, null, "C:\\w", Path.of("C:\\l"))

        assertFalse("headless" in text, text)
        assertTrue(text.endsWith("로그 폴더: C:\\l"), text)
    }
```

Run: `.\gradlew.bat :diagnostics:compileTestKotlin --no-daemon`
Expected: FAIL — `Null can not be a value of a non-null type Boolean`(`startupRecord`의 `headless`가 아직 `Boolean`이다).

- [ ] **Step 4: 구현한다 (GREEN)**

`diagnostics/src/main/kotlin/com/xgetsongs/diagnostics/Diagnostics.kt`를 다음과 같이 고친다(Edit 도구, 각각 한 번씩):

(a) AWT import 두 줄을 지운다:

```kotlin
import com.xgetsongs.shared.log.LogRedaction
import org.slf4j.LoggerFactory
import java.nio.file.Files
```

(원래는 `import org.slf4j.LoggerFactory` 다음에 `import java.awt.EventQueue`와 `import java.awt.GraphicsEnvironment`가 있었다.)

(b) 다른 모듈이 쓰는 것을 공개한다:

```kotlin
const val LOG_DIR_PROPERTY = "xgs.logDir"

/** What this process and the others on the machine look like: the marker of the last run is judged with it. */
interface Processes {
```

```kotlin
object SystemProcesses : Processes {
```

```kotlin
object Diagnostics {
```

(c) `start`에서 UI 스레드를 호출자가 넘기게 하고 `headless`를 받는다:

```kotlin
    fun start(
        logDir: Path,
        postToUi: (Runnable) -> Unit,
        registerHook: (Thread) -> Unit = Runtime.getRuntime()::addShutdownHook,
        processes: Processes = SystemProcesses,
        headless: Boolean? = null,
    ): DiagnosticsHandle {
```

그리고 본문의 `headless = GraphicsEnvironment.isHeadless(),`를 `headless = headless,`로 바꾼다. `start`의 KDoc 끝에 한 줄을 더한다: `* [postToUi] runs a Runnable on the thread whose answers the watchdog waits for (the app: the AWT event queue; the sidecar: a coroutine dispatcher). [headless] is only written into the startup record; null leaves the line out.`

(d) 핸들 클래스의 생성자를 모듈 안으로 가둔다:

```kotlin
class DiagnosticsHandle internal constructor(private val exitLogger: ExitLogger, private val watchdog: UiWatchdog) {
```

(e) 시작 기록:

```kotlin
internal fun startupRecord(
    appVersion: String?,
    property: (String) -> String?,
    processors: Int,
    maxHeapMb: Long,
    headless: Boolean?,
    workingDir: String,
    logDir: Path,
): String = buildString {
```

그리고 함수 끝의 두 줄을 바꾼다:

```kotlin
    append("  로그 폴더: $logDir")
    if (headless != null) append("\n  headless=$headless")
}
```

(원래는 `appendLine("  로그 폴더: $logDir")`와 `append("  headless=$headless")`였다.)

(f) 로그 폴더 함수 셋을 공개한다: `fun logDirectoryCandidates(`, `fun applicationDirectory(`, `fun chooseLogDirectory(`(앞의 `internal`만 뺀다. `ensureLogDirectory`는 그대로 `internal`).

Run: `.\gradlew.bat :diagnostics:test --no-daemon`
Expected: PASS — 158개(`DiagnosticsTest` 42, `ExitLoggerTest` 17, `HangDetectorTest` 26, `RunMarkerTest` 32, `ThreadDumpTest` 15, `UiWatchdogTest` 26). `checkNoAwt`도 통과한다.

- [ ] **Step 5: 앱을 새 모듈에 맞춘다**

`app/build.gradle.kts`의 `desktopMain` 의존성에서 `implementation(project(":server"))` 다음 줄에 더한다:

```kotlin
                implementation(project(":diagnostics"))
```

`app/src/desktopMain/kotlin/com/xgetsongs/app/Main.kt`에서 다섯 import를 새 패키지로 바꾸고 AWT import 둘을 더한다(알파벳 순서 위치에 넣는다):

```kotlin
import com.xgetsongs.diagnostics.Diagnostics
import com.xgetsongs.diagnostics.LOG_DIR_PROPERTY
import com.xgetsongs.diagnostics.applicationDirectory
import com.xgetsongs.diagnostics.chooseLogDirectory
import com.xgetsongs.diagnostics.logDirectoryCandidates
```

```kotlin
import java.awt.EventQueue
import java.awt.GraphicsEnvironment
```

그리고 `val diagnostics = Diagnostics.start(logDir)`를 바꾼다:

```kotlin
    val diagnostics = Diagnostics.start(logDir, postToUi = EventQueue::invokeLater, headless = GraphicsEnvironment.isHeadless())
```

Run: `.\gradlew.bat :diagnostics:test :app:desktopTest :app:compileKotlinDesktop --no-daemon`
Expected: PASS. `:app:desktopTest`의 `com.xgetsongs.app.diagnostics.LogbackConfigTest`는 12개이고, 앱의 나머지 테스트도 그대로 통과한다.

- [ ] **Step 6: Commit**

```bash
git add settings.gradle.kts diagnostics app
git commit -m "refactor: Diagnostics를 :diagnostics 모듈로 옮겨 앱과 사이드카가 함께 쓰게 한다"
```

---

### Task 2: 사이드카의 `--log-dir`과 파일 로그 설정

**Files:**
- Modify: `server/build.gradle.kts`(의존성), `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarArgs.kt`, `server/src/main/resources/logback-sidecar.xml`
- Create: `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarLogging.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarArgsTest.kt`(수정), `SidecarLoggingTest.kt`(새), `SidecarLogbackConfigTest.kt`(새)

**Interfaces:**
- Consumes: Task 1의 `chooseLogDirectory`(이 Task에서는 쓰지 않고 Task 4가 쓴다), 기존 `SidecarArgs`
- Produces: `data class SidecarArgs(val appData: Path, val logDir: Path? = null)`와 `SidecarArgs.parse(args)`(`--app-data`는 필수, `--log-dir`은 선택, 순서 무관, 모두 절대 경로, 중복·알 수 없는 인자는 null), `SidecarArgs.USAGE`. `internal fun sidecarLogCandidates(args: SidecarArgs, tempDir: Path): List<Path>`. 리소스 `/logback-sidecar.xml`: appender `FILE`(`${xgs.logDir}/xgetsongs.log`, UTF-8, 즉시 flush, 5MB·7일·50MB 회전)와 `CONSOLE`(`System.err`), `com.xgetsongs` DEBUG, `io.netty` WARN.

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`SidecarArgsTest.kt`에 테스트를 더한다(기존 5개는 그대로 둔다):

```kotlin
    @Test
    fun aLogFolderIsOptionalAndMayComeFirst() {
        val log = absolute.resolve("log")

        assertEquals(SidecarArgs(absolute, log), SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--log-dir", log.toString())))
        assertEquals(SidecarArgs(absolute, log), SidecarArgs.parse(arrayOf("--log-dir", log.toString(), "--app-data", absolute.toString())))
        assertNull(SidecarArgs(absolute).logDir)
    }

    @Test
    fun aLogFolderWithoutAnAppDataFolderIsRefused() {
        assertNull(SidecarArgs.parse(arrayOf("--log-dir", absolute.toString())))
    }

    @Test
    fun aRelativeOrRepeatedOrMissingLogFolderIsRefused() {
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--log-dir", "log")))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--log-dir")))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--app-data", absolute.toString())))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--log-dir", absolute.toString(), "--log-dir", absolute.toString())))
    }
```

`server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarLoggingTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals

class SidecarLoggingTest {
    private val temp = Files.createTempDirectory("xgs-logging").toAbsolutePath()
    private val appData = temp.resolve("data")

    @Test
    fun theFolderTheShellAskedForComesFirstThenTheAppDataFolderThenTheTempFolder() {
        val log = temp.resolve("shell-log")

        assertEquals(
            listOf(log, appData.resolve("logs"), temp.resolve("xgetsongs-logs")),
            sidecarLogCandidates(SidecarArgs(appData, log), temp),
        )
    }

    @Test
    fun withoutAFolderFromTheShellTheAppDataFolderIsTriedFirst() {
        assertEquals(
            listOf(appData.resolve("logs"), temp.resolve("xgetsongs-logs")),
            sidecarLogCandidates(SidecarArgs(appData), temp),
        )
    }
}
```

`server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarLogbackConfigTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import ch.qos.logback.classic.LoggerContext
import ch.qos.logback.classic.joran.JoranConfigurator
import ch.qos.logback.classic.util.LogbackMDCAdapter
import ch.qos.logback.core.ConsoleAppender
import ch.qos.logback.core.rolling.RollingFileAppender
import ch.qos.logback.core.rolling.TimeBasedRollingPolicy
import java.nio.charset.StandardCharsets.UTF_8
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** Loads the logback-sidecar.xml that the sidecar starts with into a context of its own, so the tests never touch the real logging. */
class SidecarLogbackConfigTest {
    private val dir: Path = Files.createTempDirectory("xgs-sidecar-logback")
    private val contexts = mutableListOf<LoggerContext>()

    @AfterTest
    fun cleanUp() {
        contexts.forEach { it.stop() }
        dir.toFile().deleteRecursively()
    }

    private fun configure(): LoggerContext {
        val context = LoggerContext()
        context.mdcAdapter = LogbackMDCAdapter() // a context that logback did not create itself has to be given one
        contexts += context
        context.putProperty("xgs.logDir", dir.toString())
        val configurator = JoranConfigurator()
        configurator.context = context
        configurator.doConfigure(checkNotNull(javaClass.getResource("/logback-sidecar.xml")) { "logback-sidecar.xml is not on the class path" })
        return context
    }

    private fun root(context: LoggerContext) = context.getLogger(org.slf4j.Logger.ROOT_LOGGER_NAME)

    @Test
    fun theLogFileIsWrittenInUtf8WithTheGivenFolder() {
        val context = configure()

        context.getLogger("com.xgetsongs.test").info("한글 로그 한 줄")

        val text = Files.readString(dir.resolve("xgetsongs.log"), UTF_8)
        assertTrue("한글 로그 한 줄" in text, text)
    }

    @Test
    fun theLogFileKeepsEveryLineAtOnceAndRollsOverByDayAndSize() {
        val file = root(configure()).getAppender("FILE") as RollingFileAppender<*>

        assertEquals(dir.resolve("xgetsongs.log"), Path.of(file.file))
        assertTrue(file.isImmediateFlush, "a crash or a kill must not lose the last lines")
        assertEquals(7, (file.rollingPolicy as TimeBasedRollingPolicy<*>).maxHistory)
    }

    @Test
    fun theConsoleIsStderrBecauseStdoutCarriesTheHandshake() {
        val console = root(configure()).getAppender("CONSOLE") as ConsoleAppender<*>

        assertEquals("System.err", console.target)
    }

    @Test
    fun theAppsOwnDebugLinesGoToTheFileAndNettyNoiseDoesNot() {
        val context = configure()

        context.getLogger("com.xgetsongs.engine.x").debug("debug of the app")
        context.getLogger("io.netty.channel").info("noise of netty")

        val text = Files.readString(dir.resolve("xgetsongs.log"), UTF_8)
        assertTrue("debug of the app" in text, text)
        assertFalse("noise of netty" in text, text)
    }
}
```

- [ ] **Step 2: 실패하는 것을 확인한다 (RED)**

`server/build.gradle.kts`의 `dependencies` 블록에 `implementation(project(":diagnostics"))`를 `api(project(":engine"))` 다음 줄에 더한다.

Run: `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.SidecarArgsTest" --tests "com.xgetsongs.server.sidecar.SidecarLoggingTest" --tests "com.xgetsongs.server.sidecar.SidecarLogbackConfigTest" --no-daemon`
Expected: FAIL — 컴파일 오류 `Too many arguments for ... SidecarArgs`(두 번째 인자), `Unresolved reference 'logDir'`, `Unresolved reference 'sidecarLogCandidates'`.

- [ ] **Step 3: 구현한다**

`server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarArgs.kt` 전체:

```kotlin
package com.xgetsongs.server.sidecar

import java.nio.file.InvalidPathException
import java.nio.file.Path

/**
 * The command line of the sidecar: `--app-data <absolute folder>` and, optionally, `--log-dir <absolute folder>`, in either
 * order. There is no default for the app data folder on purpose: the server clears `<app-data>\work` when it starts, so a
 * guessed folder could be one that another running copy is using. The log folder is where the shell wants the log files
 * (next to its own executable); without it the sidecar logs into `logs` of the app data folder.
 */
data class SidecarArgs(val appData: Path, val logDir: Path? = null) {
    companion object {
        const val USAGE = "사용법: --app-data <절대 경로> [--log-dir <절대 경로>]"

        /** Null when the arguments are not `--app-data` and, at most once each, `--log-dir`, with absolute folders. */
        fun parse(args: Array<String>): SidecarArgs? {
            var appData: Path? = null
            var logDir: Path? = null
            var index = 0
            while (index < args.size) {
                val path = absolute(args.getOrNull(index + 1)) ?: return null
                when (args[index]) {
                    "--app-data" -> {
                        if (appData != null) return null
                        appData = path
                    }
                    "--log-dir" -> {
                        if (logDir != null) return null
                        logDir = path
                    }
                    else -> return null
                }
                index += 2
            }
            return appData?.let { SidecarArgs(it, logDir) }
        }

        private fun absolute(text: String?): Path? {
            if (text == null || text.isBlank()) return null
            val path = try {
                Path.of(text)
            } catch (e: InvalidPathException) {
                return null
            }
            return path.takeIf { it.isAbsolute }
        }
    }
}
```

`server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarLogging.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import java.nio.file.Path

/**
 * The folders the sidecar tries for its log files, in order: the one the shell asked for, `logs` in the app data folder,
 * and the temp folder (an install folder can be read-only). The first one that can be made is used.
 */
internal fun sidecarLogCandidates(args: SidecarArgs, tempDir: Path): List<Path> =
    listOfNotNull(args.logDir, args.appData.resolve("logs"), tempDir.resolve("xgetsongs-logs"))
```

`server/src/main/resources/logback-sidecar.xml` 전체:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!--
  The sidecar's logging, chosen by -Dlogback.configurationFile=logback-sidecar.xml in the start script. The folder comes from
  the system property xgs.logDir, which the sidecar sets before anything asks for a logger (logback reads this file the first
  time that happens). stdout carries the handshake line, so the console is stderr. The Compose app has its own logback.xml;
  this file has another name so that the two never compete on one class path.
-->
<configuration>
    <property name="LOG_DIR" value="${xgs.logDir:-${java.io.tmpdir}/xgetsongs-logs}"/>

    <!-- The console shows INFO and above only; DEBUG lines go to the file alone. -->
    <appender name="CONSOLE" class="ch.qos.logback.core.ConsoleAppender">
        <target>System.err</target>
        <filter class="ch.qos.logback.classic.filter.ThresholdFilter">
            <level>INFO</level>
        </filter>
        <encoder>
            <charset>UTF-8</charset>
            <pattern>%d{HH:mm:ss.SSS} %-5level [%thread] %logger{20} - %msg%n</pattern>
        </encoder>
    </appender>

    <!-- Every line is flushed at once, so a crash or a kill does not lose the last lines. -->
    <appender name="FILE" class="ch.qos.logback.core.rolling.RollingFileAppender">
        <file>${LOG_DIR}/xgetsongs.log</file>
        <immediateFlush>true</immediateFlush>
        <rollingPolicy class="ch.qos.logback.core.rolling.SizeAndTimeBasedRollingPolicy">
            <fileNamePattern>${LOG_DIR}/xgetsongs.%d{yyyy-MM-dd}.%i.log</fileNamePattern>
            <maxFileSize>5MB</maxFileSize>
            <maxHistory>7</maxHistory>
            <totalSizeCap>50MB</totalSizeCap>
        </rollingPolicy>
        <encoder>
            <charset>UTF-8</charset>
            <pattern>%d{yyyy-MM-dd HH:mm:ss.SSS} %-5level [%thread] %logger{20} - %msg%n</pattern>
        </encoder>
    </appender>

    <logger name="io.netty" level="WARN"/>
    <logger name="io.ktor" level="INFO"/>
    <logger name="com.xgetsongs" level="DEBUG"/>

    <!-- The file comes first: a console that blocks (a full pipe) must not delay the write to the file. -->
    <root level="INFO">
        <appender-ref ref="FILE"/>
        <appender-ref ref="CONSOLE"/>
    </root>
</configuration>
```

- [ ] **Step 4: 통과하는 것을 확인한다 (GREEN)**

Run: `.\gradlew.bat :server:test --no-daemon`
Expected: PASS — 새 테스트 9개(`SidecarArgsTest` 3, `SidecarLoggingTest` 2, `SidecarLogbackConfigTest` 4)와 기존 서버 테스트 전부. `SidecarArgsTest`는 모두 8개가 된다.

- [ ] **Step 5: Commit**

```bash
git add server
git commit -m "feat(server): 사이드카가 --log-dir을 받고 파일 로그(회전)를 쓰는 logback 설정을 쓴다"
```

---

### Task 3: 종료 이유를 구분한다 (`exit` 줄, Kotlin)

**Files:**
- Modify: `server/src/main/kotlin/com/xgetsongs/server/sidecar/ParentWatch.kt`, `SidecarMain.kt`(`runSidecar`만)
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/ParentWatchTest.kt`(바꿈), `SidecarMainTest.kt`(더함)

**Interfaces:**
- Consumes: 기존 `ParentWatch`, `runSidecar`
- Produces: `class ParentWatch(input: InputStream, onGone: (userRequested: Boolean) -> Unit)`와 `ParentWatch.EXIT_LINE = "exit"`. stdin에서 한 줄이 `exit`이면 `onGone(true)`를 한 번 부르고 더 읽지 않는다. 입력이 끝나거나 실패하면(그때까지 `exit`이 없었으면) `onGone(false)`. `internal fun runSidecar(args, stdin, stdout, stderr, startServer, onParentGone: (Boolean) -> Unit = {}, exit: (Int) -> Unit): Thread?` — `onParentGone`은 서버를 멈추기 전에 불린다.

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`ParentWatchTest.kt` 전체를 바꾼다:

```kotlin
package com.xgetsongs.server.sidecar

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ParentWatchTest {
    private val gone = CountDownLatch(1)
    private val reasons = CopyOnWriteArrayList<Boolean>()

    private fun watch(input: InputStream) = ParentWatch(input) { userRequested ->
        reasons += userRequested
        gone.countDown()
    }.start()

    @Test
    fun anInputThatEndsAtOnceMeansTheParentIsGone() {
        watch(ByteArrayInputStream(ByteArray(0)))

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(false), reasons.toList())
    }

    @Test
    fun bytesFromTheParentAreIgnoredUntilTheEnd() {
        val writer = PipedOutputStream()
        watch(PipedInputStream(writer))

        writer.write("ping\n".toByteArray())
        writer.flush()
        assertFalse(gone.await(300, TimeUnit.MILLISECONDS), "the pipe is still open")

        writer.close() // the parent goes away
        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(false), reasons.toList())
    }

    @Test
    fun aFailingInputAlsoMeansTheParentIsGone() {
        watch(object : InputStream() {
            override fun read(): Int = throw IOException("pipe broken")
        })

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(false), reasons.toList())
    }

    @Test
    fun theWatchingThreadNeverKeepsTheJvmAlive() {
        val writer = PipedOutputStream()
        val thread = watch(PipedInputStream(writer))

        assertTrue(thread.isDaemon)
        writer.close()
        assertTrue(gone.await(30, TimeUnit.SECONDS))
    }

    @Test
    fun anExitLineMeansTheUserAskedForTheEndAndDoesNotWaitForTheStreamToEnd() {
        val writer = PipedOutputStream()
        watch(PipedInputStream(writer))

        writer.write("exit\n".toByteArray())
        writer.flush() // the pipe stays open

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(true), reasons.toList())
    }

    @Test
    fun theExitLineMayComeAfterOtherLines() {
        watch(ByteArrayInputStream("hello\nworld\nexit\n".toByteArray()))

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(true), reasons.toList())
    }

    @Test
    fun aPartialExitLineAtTheEndIsNotAnExit() {
        watch(ByteArrayInputStream("exi".toByteArray())) // the shell died in the middle of writing

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(false), reasons.toList())
    }

    @Test
    fun theEndAfterAnExitLineIsNotReportedAgain() {
        watch(ByteArrayInputStream("exit\n".toByteArray()))

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        Thread.sleep(300)
        assertEquals(listOf(true), reasons.toList())
    }
}
```

`SidecarMainTest.kt`에서 import에 `java.util.concurrent.LinkedBlockingQueue`가 이미 있다. 클래스 안(마지막 테스트 다음)에 더한다:

```kotlin
    @Test
    fun anExitLineFromTheShellStopsTheServerAndIsReportedAsTheUsers() {
        val appData = Files.createTempDirectory("xgs-sidecar").resolve("data")
        val parentWriter = PipedOutputStream()
        val asked = LinkedBlockingQueue<Boolean>()

        runSidecar(
            arrayOf("--app-data", appData.toString()),
            PipedInputStream(parentWriter),
            PrintStream(stdout, true, "UTF-8"),
            PrintStream(stderr, true, "UTF-8"),
            startServer = { LocalServer.start(fakes.services) },
            onParentGone = { asked.put(it) },
            exit = { exits.put(it) },
        )
        val port = out().lines().first { it.isNotBlank() }.split(' ')[1].toInt()

        parentWriter.write("exit\n".toByteArray())
        parentWriter.flush() // the pipe stays open: the line alone ends it

        assertEquals(true, asked.poll(30, TimeUnit.SECONDS))
        assertEquals(0, exits.poll(30, TimeUnit.SECONDS))
        assertFailsWith<ConnectException> { Socket("127.0.0.1", port).close() }
    }

    @Test
    fun aClosedPipeWithoutAnExitLineIsReportedAsNotAskedFor() {
        val appData = Files.createTempDirectory("xgs-sidecar").resolve("data")
        val parentWriter = PipedOutputStream()
        val asked = LinkedBlockingQueue<Boolean>()

        runSidecar(
            arrayOf("--app-data", appData.toString()),
            PipedInputStream(parentWriter),
            PrintStream(stdout, true, "UTF-8"),
            PrintStream(stderr, true, "UTF-8"),
            startServer = { LocalServer.start(fakes.services) },
            onParentGone = { asked.put(it) },
            exit = { exits.put(it) },
        )

        parentWriter.close()

        assertEquals(false, asked.poll(30, TimeUnit.SECONDS))
        assertEquals(0, exits.poll(30, TimeUnit.SECONDS))
    }
```

- [ ] **Step 2: 실패하는 것을 확인한다 (RED)**

Run: `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.ParentWatchTest" --tests "com.xgetsongs.server.sidecar.SidecarMainTest" --no-daemon`
Expected: FAIL — 컴파일 오류 `Cannot infer a type for this parameter`(`ParentWatch`의 람다가 `() -> Unit`이라서)와 `No parameter with name 'onParentGone' found`.

- [ ] **Step 3: 구현한다**

`ParentWatch.kt` 전체:

```kotlin
package com.xgetsongs.server.sidecar

import java.io.IOException
import java.io.InputStream
import kotlin.concurrent.thread

/**
 * Tells once that the shell that owns the other end of the sidecar's stdin is gone, and whether it asked for that.
 *
 * The shell writes a line `exit` when the user closes the window; that line alone ends the watch (`onGone(true)`). When the
 * stream just ends or fails without it, the shell died or was killed and the operating system closed the pipe
 * (`onGone(false)`). The exit record of the log tells the two apart by this.
 */
class ParentWatch(private val input: InputStream, private val onGone: (userRequested: Boolean) -> Unit) {
    /** Starts the watching thread. It is a daemon: it never keeps the JVM alive. */
    fun start(): Thread = thread(name = "parent-watch", isDaemon = true) {
        var userRequested = false
        try {
            val reader = input.bufferedReader(Charsets.UTF_8)
            while (true) {
                val line = reader.readLine() ?: break
                if (line.trim() == EXIT_LINE) {
                    userRequested = true
                    break
                }
                // Any other line is not interpreted: only the exit line and the end of the stream matter.
            }
        } catch (e: IOException) {
            // A stream that fails is as good as one that ended.
        }
        onGone(userRequested)
    }

    companion object {
        /** The line the shell writes before it closes the pipe when the user asked for the end. */
        const val EXIT_LINE = "exit"
    }
}
```

`SidecarMain.kt`의 `runSidecar` 부분(KDoc, 시그니처, 마지막 `return ParentWatch...`)만 바꾼다:

```kotlin
/**
 * Starts the server for the shell that owns this process: tells it where the server is on stdout, and stops when
 * [stdin] ends or carries an `exit` line. [onParentGone] is told whether the shell asked for the end (the `exit` line)
 * before the server is stopped. [exit] ends the process; the tests pass a recorder, so this function returns after it.
 *
 * Returns the thread that watches [stdin], or null when the sidecar did not start. The caller has to wait for that thread:
 * the server's own threads are daemons, so nothing else would keep the JVM from shutting down (and taking the server with
 * it) as soon as main returns.
 */
internal fun runSidecar(
    args: Array<String>,
    stdin: InputStream,
    stdout: PrintStream,
    stderr: PrintStream,
    startServer: (Path) -> LocalServer,
    onParentGone: (userRequested: Boolean) -> Unit = {},
    exit: (Int) -> Unit,
): Thread? {
```

그리고 함수 끝을 바꾼다:

```kotlin
    return ParentWatch(stdin) { userRequested ->
        onParentGone(userRequested)
        server.stop()
        exit(0)
    }.start()
```

`main`에서 `runSidecar`를 부르는 줄은 `exit`를 이름 인자로 넘기게 바꾼다(`main`이 두 람다를 위치 인자로 넘기는데, 새 `onParentGone`이 여섯 번째 자리를 차지해서 그대로 두면 `exit`가 비고 컴파일되지 않는다). Task 4가 이 줄을 다시 쓴다:

```kotlin
    runSidecar(args, System.`in`, System.out, System.err, { LocalServer.start(it) }, exit = { exitProcess(it) })?.join()
```

- [ ] **Step 4: 통과하는 것을 확인한다 (GREEN)**

Run: `.\gradlew.bat :server:test --no-daemon`
Expected: PASS — `ParentWatchTest` 8개, `SidecarMainTest` 5개, 나머지 기존 테스트 전부(`SidecarProcessTest` 포함).

- [ ] **Step 5: Commit**

```bash
git add server
git commit -m "feat(server): 사이드카가 stdin의 exit 줄로 사용자 종료와 셸의 갑작스러운 소멸을 구분한다"
```

---

### Task 4: 사이드카에서 진단을 켠다

**Files:**
- Create: `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarDiagnostics.kt`
- Modify: `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarMain.kt`(`main`)
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarDiagnosticsTest.kt`(새), `SidecarDiagnosticsProcessTest.kt`(새)

**Interfaces:**
- Consumes: Task 1의 `Diagnostics.start`, `DiagnosticsHandle`, `LOG_DIR_PROPERTY`, `chooseLogDirectory`, `Processes`, `SystemProcesses`; Task 2의 `SidecarArgs`, `sidecarLogCandidates`, `/logback-sidecar.xml`; Task 3의 `ParentWatch`, `runSidecar(..., onParentGone, exit)`
- Produces: `internal object SidecarDiagnostics { fun start(args: SidecarArgs, tempDir: Path = Path.of(System.getProperty("java.io.tmpdir")), registerHook: (Thread) -> Unit = Runtime.getRuntime()::addShutdownHook, processes: Processes = SystemProcesses): DiagnosticsHandle }` — 로그 폴더를 고르고 `xgs.logDir`을 정한 뒤 진단을 켠다(로거를 만들기 전에 불러야 한다). 멈춤 감시의 심장 박동은 `Dispatchers.Default`로 간다. `main`은 진단을 켜고, `runningJobs`를 연결하고, `exit` 줄이면 `markUserExit()`를 부르고, 끝내기 전에 `stop()`을 부른다.

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarDiagnosticsTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import com.xgetsongs.diagnostics.LOG_DIR_PROPERTY
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.joinAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertTrue

class SidecarDiagnosticsTest {
    private val base: Path = Files.createTempDirectory("xgs-sidecar-diag").toAbsolutePath()
    private val previousHandler = Thread.getDefaultUncaughtExceptionHandler()

    @AfterTest
    fun cleanUp() {
        Thread.setDefaultUncaughtExceptionHandler(previousHandler) // Diagnostics.start hooks it
        System.clearProperty(LOG_DIR_PROPERTY)
        base.toFile().deleteRecursively()
    }

    private fun hangFiles(logDir: Path): List<Path> =
        if (!Files.isDirectory(logDir)) emptyList()
        else Files.list(logDir).use { files -> files.filter { it.fileName.toString().startsWith("ui-hang-") }.toList() }

    @Test
    fun aDispatcherThatStopsAnsweringLeavesAThreadDumpInTheLogFolder() = runBlocking {
        val logDir = base.resolve("log")
        val handle = SidecarDiagnostics.start(SidecarArgs(base.resolve("data"), logDir), tempDir = base, registerHook = { })
        try {
            // Every worker of the default dispatcher is busy for longer than the watchdog's limit (5 s): the heartbeats
            // the watchdog posts there cannot run, which is what the server's downloads would look like when stuck.
            val stall = List(Runtime.getRuntime().availableProcessors()) { launch(Dispatchers.Default) { Thread.sleep(9_000) } }
            val deadline = System.nanoTime() + 25_000_000_000L
            while (hangFiles(logDir).isEmpty() && System.nanoTime() < deadline) delay(200)

            val files = hangFiles(logDir)
            assertTrue(files.isNotEmpty(), "a hang dump was written")
            assertTrue("DefaultDispatcher-worker" in Files.readString(files.first()), "the dump shows the stuck workers")
            stall.joinAll()
        } finally {
            handle.stop()
        }
    }
}
```

`server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarDiagnosticsProcessTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** Starts the real sidecar in a process of its own and reads what it leaves in its log folder. */
class SidecarDiagnosticsProcessTest {
    private val base: Path = Files.createTempDirectory("xgs-diag-process").toAbsolutePath()
    private val appData = base.resolve("data")

    @AfterTest
    fun cleanUp() {
        base.toFile().deleteRecursively()
    }

    private class Running(val process: Process, val port: Int, val token: String)

    private fun start(vararg extraArgs: String): Running {
        val java = Path.of(System.getProperty("java.home"), "bin", "java").toString()
        val process = ProcessBuilder(
            listOf(
                java, "-Dlogback.configurationFile=logback-sidecar.xml",
                "-cp", System.getProperty("java.class.path"),
                "com.xgetsongs.server.sidecar.SidecarMainKt", "--app-data", appData.toString(),
            ) + extraArgs,
        ).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        val line = process.inputStream.bufferedReader().readLine()
        assertTrue(line != null && line.startsWith("XGS-READY "), "the handshake line: $line")
        val parts = line.split(' ')
        return Running(process, parts[1].toInt(), parts[2])
    }

    /** Ends the process the way the shell does: [exitLine] writes the line before the pipe is closed. */
    private fun end(running: Running, exitLine: Boolean) {
        runCatching {
            if (exitLine) {
                running.process.outputStream.write("exit\n".toByteArray())
                running.process.outputStream.flush()
            }
            running.process.outputStream.close()
        }
        assertTrue(running.process.waitFor(30, TimeUnit.SECONDS), "the process ends")
        assertEquals(0, running.process.exitValue())
    }

    private fun logText(logDir: Path): String {
        val file = logDir.resolve("xgetsongs.log")
        assertTrue(Files.isRegularFile(file), "the log file is in $logDir")
        return Files.readString(file)
    }

    private fun awaitLog(logDir: Path, text: String) {
        val deadline = System.nanoTime() + 30_000_000_000L
        while (System.nanoTime() < deadline && !(Files.isRegularFile(logDir.resolve("xgetsongs.log")) && text in Files.readString(logDir.resolve("xgetsongs.log")))) {
            Thread.sleep(50)
        }
    }

    @Test
    fun anExitLineIsLoggedAsTheUsersEndAndTheMarkerSaysSo() {
        val logDir = base.resolve("log")
        val running = start("--log-dir", logDir.toString())
        awaitLog(logDir, "내장 서버 시작")
        assertTrue("state=running" in Files.readString(logDir.resolve("last-run.txt")))

        end(running, exitLine = true)

        val text = logText(logDir)
        for (expected in listOf("시작 정보", "내장 서버 시작", "사용자가 창 닫기를 요청함", "내장 서버 정지", "JVM 종료 시작: 사용자가 창을 닫음")) {
            assertTrue(expected in text, "$expected in the log:\n$text")
        }
        val marker = Files.readString(logDir.resolve("last-run.txt"))
        assertTrue("state=exited" in marker && "reason=user" in marker, marker)
    }

    @Test
    fun aClosedPipeWithoutAnExitLineIsLoggedAsAnEndNobodyAskedFor() {
        val logDir = base.resolve("log")
        val running = start("--log-dir", logDir.toString())
        awaitLog(logDir, "내장 서버 시작")

        end(running, exitLine = false)

        val text = logText(logDir)
        assertTrue("창을 닫지 않은 종료" in text, text)
        assertFalse("사용자가 창 닫기를 요청함" in text, text)
        val marker = Files.readString(logDir.resolve("last-run.txt"))
        assertTrue("state=exited" in marker && "reason=other" in marker, marker)
    }

    @Test
    fun theLogHoldsTheStartRecordsButNeverTheToken() {
        val logDir = base.resolve("log")
        val running = start("--log-dir", logDir.toString())
        awaitLog(logDir, "내장 서버 시작")

        end(running, exitLine = true)

        val text = logText(logDir)
        assertTrue("127.0.0.1:${running.port}" in text, text)
        assertFalse(running.token in text, "the token is a secret: it is on stdout only")
    }

    @Test
    fun aLogFolderThatCannotBeMadeFallsBackToTheAppDataFolder() {
        Files.createDirectories(base)
        val blocker = Files.writeString(base.resolve("blocker"), "a file, so nothing can be made below it")
        val running = start("--log-dir", blocker.resolve("log").toString())
        val fallback = appData.resolve("logs")
        awaitLog(fallback, "내장 서버 시작")

        end(running, exitLine = true)

        assertTrue("시작 정보" in logText(fallback))
    }
}
```

- [ ] **Step 2: 실패하는 것을 확인한다 (RED)**

Run: `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.SidecarDiagnosticsTest" --tests "com.xgetsongs.server.sidecar.SidecarDiagnosticsProcessTest" --no-daemon`
Expected: FAIL — 컴파일 오류 `Unresolved reference 'SidecarDiagnostics'`.

- [ ] **Step 3: 구현한다**

`server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarDiagnostics.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import com.xgetsongs.diagnostics.Diagnostics
import com.xgetsongs.diagnostics.DiagnosticsHandle
import com.xgetsongs.diagnostics.LOG_DIR_PROPERTY
import com.xgetsongs.diagnostics.Processes
import com.xgetsongs.diagnostics.SystemProcesses
import com.xgetsongs.diagnostics.chooseLogDirectory
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import java.nio.file.Path

// No logger is declared in this file: logback reads its configuration the first time anything asks for one, and the log
// folder it needs is set by [SidecarDiagnostics.start] first.

/**
 * What the Compose app's `Main` does around its window, for the sidecar: the log folder is chosen and handed to logback,
 * then the diagnostics (startup record, uncaught exceptions, exit record, `last-run.txt`, hang dumps) are started.
 *
 * The sidecar has no UI thread. What can hang here is the work the server does on `Dispatchers.Default` (downloads, the
 * event streams), so the watchdog's heartbeats go there: when every worker of that dispatcher is stuck the heartbeat does
 * not run, and the thread dump of the stuck workers is written the way the app's UI hang dump is.
 */
internal object SidecarDiagnostics {
    private val heartbeats = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** Call this before anything asks for a logger. */
    fun start(
        args: SidecarArgs,
        tempDir: Path = Path.of(System.getProperty("java.io.tmpdir") ?: "."),
        registerHook: (Thread) -> Unit = Runtime.getRuntime()::addShutdownHook,
        processes: Processes = SystemProcesses,
    ): DiagnosticsHandle {
        val logDir = chooseLogDirectory(sidecarLogCandidates(args, tempDir))
        System.setProperty(LOG_DIR_PROPERTY, logDir.toString())
        return Diagnostics.start(
            logDir,
            postToUi = { heartbeat -> heartbeats.launch { heartbeat.run() } },
            registerHook = registerHook,
            processes = processes,
        )
    }
}
```

`SidecarMain.kt`의 `main`을 바꾼다(그리고 import는 그대로 둔다):

```kotlin
fun main(args: Array<String>) {
    // The arguments are read first, without a logger: logback learns the log folder from them (see SidecarDiagnostics).
    val diagnostics = SidecarArgs.parse(args)?.let { SidecarDiagnostics.start(it) }
    // Waiting for the watching thread keeps the JVM alive until the parent is gone; that thread ends the process itself.
    runSidecar(
        args, System.`in`, System.out, System.err,
        startServer = { appData ->
            LocalServer.start(appData).also { server -> diagnostics?.runningJobs = server::runningJobs }
        },
        onParentGone = { userRequested -> if (userRequested) diagnostics?.markUserExit() },
        exit = { code ->
            diagnostics?.stop()
            exitProcess(code)
        },
    )?.join()
}
```

- [ ] **Step 4: 통과하는 것을 확인한다 (GREEN)**

Run: `.\gradlew.bat :server:test --no-daemon`
Expected: PASS — 새 테스트 5개(`SidecarDiagnosticsTest` 1: 약 9초, `SidecarDiagnosticsProcessTest` 4)와 기존 전부.

- [ ] **Step 5: 실제 배포본에서 한 번 본다**

```powershell
.\gradlew.bat :server:installDist --no-daemon
```

그다음 Node 등 임의의 도구로 `server\build\install\xgs-server\lib\*`를 클래스 경로로 `com.xgetsongs.server.sidecar.SidecarMainKt --app-data <임시 폴더> --log-dir <임시 폴더>\log`를 파이프와 함께 시작하고(스파이크 보고서의 확인 방식과 같다), 시작 신호를 읽은 뒤 `exit\n`을 쓴다. Expected: 프로세스가 코드 0으로 끝나고 `<임시 폴더>\log\xgetsongs.log`에 `시작 정보`, `내장 서버 시작`, `사용자가 창 닫기를 요청함`, `내장 서버 정지`, `JVM 종료 시작: 사용자가 창을 닫음`이 있고, 콘솔(stderr)에는 INFO 이상만 나온다. 이 확인은 Task 4의 프로세스 테스트가 이미 자동으로 하므로, 테스트가 통과했다면 한 번 눈으로 보는 것으로 충분하다.

- [ ] **Step 6: Commit**

```bash
git add server
git commit -m "feat(server): 사이드카가 앱과 같은 진단(로그, 종료 기록, 마커, 멈춤 덤프)을 남긴다"
```

---

### Task 5: Rust 셸 쪽 (`--log-dir`, 이미지 찾기, `exit` 줄)

**Files:**
- Modify: `sidecar-host/src/config.rs`, `sidecar-host/src/sidecar.rs`

**Interfaces:**
- Consumes: Task 3·4가 정한 사이드카 인자(`--app-data`, `--log-dir`)와 종료 프로토콜(`exit` 줄 뒤 stdin 닫기)
- Produces:
  - `SidecarConfig { java: PathBuf, lib_dir: PathBuf, app_data: PathBuf, log_dir: Option<PathBuf> }`
  - `SidecarConfig::from_lookup(get: impl Fn(&str) -> Option<OsString>, exe_dir: Option<&Path>) -> Result<SidecarConfig, String>`: ① `XGS_SIDECAR_LIB`가 있으면 그것과 `XGS_SIDECAR_JAVA`(기본 `java`) ② 아니면 이미지 폴더(`XGS_SIDECAR_DIR`, 없으면 `<exe_dir>\sidecar`)의 `runtime\bin\java.exe`와 `lib`. 이미지 폴더가 정해졌는데 `java.exe`가 없으면 오류. `app_data`는 `XGS_SIDECAR_APPDATA`, 기본은 임시 폴더(`xgs-spike-appdata`). `log_dir`은 `XGS_SIDECAR_LOGDIR`, 없으면 `<exe_dir>\log`, 둘 다 없으면 `None`
  - `SidecarConfig::from_env()`는 위에 `std::env`와 실행 파일 폴더를 넣는다. `command()`는 `log_dir`이 있으면 `--log-dir <폴더>`를 붙인다
  - `Sidecar::shutdown(grace)`는 stdin에 `exit\n`을 쓰고 닫는다(사용자가 끝낸 것). `Sidecar`를 그냥 버리면 `exit` 줄 없이 stdin만 닫힌다(셸이 사라진 것과 같다)

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`sidecar-host/src/config.rs`의 테스트 모듈 전체를 다음으로 바꾼다(구현은 아직 그대로 둔다):

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::ffi::OsString;

    fn lookup(values: &[(&str, &str)]) -> impl Fn(&str) -> Option<OsString> {
        let map: HashMap<String, OsString> =
            values.iter().map(|(k, v)| (k.to_string(), OsString::from(v))).collect();
        move |key| map.get(key).cloned()
    }

    /// A folder shaped like a sidecar image: `runtime\bin\java.exe` (an empty file is enough) and `lib`.
    fn fake_image(name: &str) -> PathBuf {
        let image = std::env::temp_dir().join(format!("xgs-config-test-{}-{}", std::process::id(), name));
        let _ = std::fs::remove_dir_all(&image);
        std::fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
        std::fs::write(image.join("runtime").join("bin").join("java.exe"), b"").unwrap();
        std::fs::create_dir_all(image.join("lib")).unwrap();
        image
    }

    #[test]
    fn nothing_to_start_is_an_error_that_names_the_ways_to_say_where_it_is() {
        let error = SidecarConfig::from_lookup(lookup(&[]), None).unwrap_err();
        assert!(error.contains("XGS_SIDECAR_LIB"), "{error}");
        assert!(error.contains("XGS_SIDECAR_DIR"), "{error}");
    }

    #[test]
    fn the_lib_folder_variable_keeps_working_for_development() {
        let config = SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_LIB", r"C:\x\lib")]), None).unwrap();
        assert_eq!(config.java, PathBuf::from("java"));
        assert_eq!(config.lib_dir, PathBuf::from(r"C:\x\lib"));
        assert_eq!(config.app_data, std::env::temp_dir().join("xgs-spike-appdata"));
        assert_eq!(config.log_dir, None);
    }

    #[test]
    fn the_given_values_are_used() {
        let config = SidecarConfig::from_lookup(
            lookup(&[
                ("XGS_SIDECAR_LIB", r"C:\x\lib"),
                ("XGS_SIDECAR_JAVA", r"C:\jdk\bin\java.exe"),
                ("XGS_SIDECAR_APPDATA", r"C:\data"),
                ("XGS_SIDECAR_LOGDIR", r"C:\logs"),
            ]),
            None,
        )
        .unwrap();
        assert_eq!(config.java, PathBuf::from(r"C:\jdk\bin\java.exe"));
        assert_eq!(config.app_data, PathBuf::from(r"C:\data"));
        assert_eq!(config.log_dir, Some(PathBuf::from(r"C:\logs")));
    }

    #[test]
    fn an_image_folder_from_the_variable_brings_its_own_runtime_and_jars() {
        let image = fake_image("variable");

        let config =
            SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_DIR", image.to_str().unwrap())]), None).unwrap();

        assert_eq!(config.java, image.join("runtime").join("bin").join("java.exe"));
        assert_eq!(config.lib_dir, image.join("lib"));
        let _ = std::fs::remove_dir_all(&image);
    }

    #[test]
    fn a_sidecar_folder_next_to_the_executable_is_found_without_any_variable() {
        let exe_dir = std::env::temp_dir().join(format!("xgs-config-test-{}-exe", std::process::id()));
        let _ = std::fs::remove_dir_all(&exe_dir);
        let image = exe_dir.join("sidecar");
        std::fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
        std::fs::write(image.join("runtime").join("bin").join("java.exe"), b"").unwrap();

        let config = SidecarConfig::from_lookup(lookup(&[]), Some(&exe_dir)).unwrap();

        assert_eq!(config.java, image.join("runtime").join("bin").join("java.exe"));
        assert_eq!(config.lib_dir, image.join("lib"));
        assert_eq!(config.log_dir, Some(exe_dir.join("log")), "the logs go next to the executable");
        let _ = std::fs::remove_dir_all(&exe_dir);
    }

    #[test]
    fn an_image_folder_without_a_runtime_is_an_error_and_not_a_silent_fallback() {
        let image = std::env::temp_dir().join(format!("xgs-config-test-{}-empty", std::process::id()));
        let _ = std::fs::remove_dir_all(&image);
        std::fs::create_dir_all(&image).unwrap();

        let error =
            SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_DIR", image.to_str().unwrap())]), None).unwrap_err();

        assert!(error.contains("java.exe"), "{error}");
        let _ = std::fs::remove_dir_all(&image);
    }

    #[test]
    fn the_command_runs_the_main_class_with_the_class_path_and_the_folders() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
            log_dir: Some(PathBuf::from(r"C:\logs")),
        };
        let command = config.command();
        assert_eq!(command.get_program(), "java");
        let args: Vec<String> = command.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert_eq!(
            args,
            vec![
                "-Dlogback.configurationFile=logback-sidecar.xml",
                "-cp",
                r"C:\x\lib\*",
                "com.xgetsongs.server.sidecar.SidecarMainKt",
                "--app-data",
                r"C:\data",
                "--log-dir",
                r"C:\logs",
            ]
        );
    }

    #[test]
    fn without_a_log_folder_the_command_leaves_the_choice_to_the_sidecar() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
            log_dir: None,
        };
        let args: Vec<String> = config.command().get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert!(!args.iter().any(|a| a == "--log-dir"), "{args:?}");
    }

    #[test]
    fn the_stderr_log_lives_in_the_app_data_folder() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
            log_dir: None,
        };
        assert_eq!(config.stderr_log(), PathBuf::from(r"C:\data\sidecar-stderr.log"));
    }
}
```

`sidecar-host/src/sidecar.rs`의 테스트 모듈 끝(마지막 테스트 다음)에 더한다:

```rust
    /// Waits for [path] to hold something and returns it (the stand-in writes it just before it ends).
    fn read_when_written(path: &PathBuf) -> String {
        let deadline = std::time::Instant::now() + Duration::from_secs(10);
        while std::time::Instant::now() < deadline {
            let content = std::fs::read_to_string(path).unwrap_or_default();
            if !content.is_empty() {
                return content;
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        String::new()
    }

    #[test]
    fn shutdown_tells_the_sidecar_that_the_user_asked_for_the_end() {
        let result = log_path("exit-line");
        let _ = std::fs::remove_file(&result);
        let script = format!(
            "{READY} $line = [Console]::In.ReadLine(); [IO.File]::WriteAllText('{}', [string]$line)",
            result.display()
        );
        let mut sidecar = Sidecar::spawn(powershell(&script), &log_path("exit-line-err"), Duration::from_secs(30)).unwrap();

        assert!(sidecar.shutdown(Duration::from_secs(10)));

        assert_eq!(read_when_written(&result), "exit");
        let _ = std::fs::remove_file(&result);
    }

    #[test]
    fn a_dropped_sidecar_closes_its_stdin_without_an_exit_line() {
        let result = log_path("dropped");
        let _ = std::fs::remove_file(&result);
        let script = format!(
            "{READY} $line = [Console]::In.ReadLine(); if ($null -eq $line) {{ $line = 'eof' }}; [IO.File]::WriteAllText('{}', [string]$line)",
            result.display()
        );
        let sidecar = Sidecar::spawn(powershell(&script), &log_path("dropped-err"), Duration::from_secs(30)).unwrap();

        drop(sidecar); // the shell is gone: the pipe closes and nobody wrote an exit line

        assert_eq!(read_when_written(&result), "eof");
        let _ = std::fs::remove_file(&result);
    }
```

- [ ] **Step 2: 실패하는 것을 확인한다 (RED)**

Run: `cd sidecar-host; cargo test`
Expected: FAIL — 컴파일 오류 (`from_lookup`이 인자 하나만 받음, `SidecarConfig`에 `log_dir` 필드가 없음).

- [ ] **Step 3: 구현한다**

`sidecar-host/src/config.rs`의 테스트 모듈 **위** 구현 전체를 다음으로 바꾼다:

```rust
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::Command;

const MAIN_CLASS: &str = "com.xgetsongs.server.sidecar.SidecarMainKt";

/// How to start the sidecar: the `java` to run, the folder with its jars, the app data folder it gets as `--app-data` and,
/// when the shell knows one, the folder it should write its log files to (`--log-dir`).
#[derive(Debug)]
pub struct SidecarConfig {
    pub java: PathBuf,
    pub lib_dir: PathBuf,
    pub app_data: PathBuf,
    pub log_dir: Option<PathBuf>,
}

impl SidecarConfig {
    pub fn from_env() -> Result<SidecarConfig, String> {
        let exe_dir = std::env::current_exe().ok().and_then(|exe| exe.parent().map(Path::to_path_buf));
        SidecarConfig::from_lookup(|key| std::env::var_os(key), exe_dir.as_deref())
    }

    /// Where java and the jars come from, in this order:
    /// 1. `XGS_SIDECAR_LIB` (the lib folder of `installDist`) with `XGS_SIDECAR_JAVA` (default: `java` on the PATH):
    ///    for development;
    /// 2. a sidecar image, a folder with `runtime\bin\java.exe` and `lib`: `XGS_SIDECAR_DIR`, else the `sidecar` folder next
    ///    to the executable ([exe_dir]). A folder that is named but holds no runtime is an error, not a reason to look on.
    ///
    /// The app data folder is `XGS_SIDECAR_APPDATA`, by default a temp folder, never the user's real app data (the spike
    /// must not clear the `work` folder of a running copy of the app). The log folder is `XGS_SIDECAR_LOGDIR`, else `log`
    /// next to the executable, else the sidecar chooses.
    pub fn from_lookup(
        get: impl Fn(&str) -> Option<OsString>,
        exe_dir: Option<&Path>,
    ) -> Result<SidecarConfig, String> {
        let (java, lib_dir) = if let Some(lib) = get("XGS_SIDECAR_LIB") {
            let java = get("XGS_SIDECAR_JAVA").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("java"));
            (java, PathBuf::from(lib))
        } else {
            let image = get("XGS_SIDECAR_DIR")
                .map(PathBuf::from)
                .or_else(|| exe_dir.map(|dir| dir.join("sidecar")))
                .ok_or_else(|| {
                    "no sidecar to start: set XGS_SIDECAR_LIB (the lib folder of server/build/install/xgs-server) or \
                     XGS_SIDECAR_DIR (an image folder with runtime and lib), or put a sidecar folder next to the executable"
                        .to_string()
                })?;
            let java = image.join("runtime").join("bin").join("java.exe");
            if !java.is_file() {
                return Err(format!(
                    "no sidecar runtime: {} is missing (set XGS_SIDECAR_LIB for a development build, or XGS_SIDECAR_DIR)",
                    java.display()
                ));
            }
            (java, image.join("lib"))
        };
        let app_data = get("XGS_SIDECAR_APPDATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| std::env::temp_dir().join("xgs-spike-appdata"));
        let log_dir = get("XGS_SIDECAR_LOGDIR").map(PathBuf::from).or_else(|| exe_dir.map(|dir| dir.join("log")));
        Ok(SidecarConfig { java, lib_dir, app_data, log_dir })
    }

    pub fn command(&self) -> Command {
        let mut command = Command::new(&self.java);
        command
            .arg("-Dlogback.configurationFile=logback-sidecar.xml")
            .arg("-cp")
            .arg(self.lib_dir.join("*"))
            .arg(MAIN_CLASS)
            .arg("--app-data")
            .arg(&self.app_data);
        if let Some(log_dir) = &self.log_dir {
            command.arg("--log-dir").arg(log_dir);
        }
        command
    }

    /// Where what the sidecar writes to stderr is kept (the shell has no console to show it).
    pub fn stderr_log(&self) -> PathBuf {
        self.app_data.join("sidecar-stderr.log")
    }
}
```

`sidecar-host/src/sidecar.rs`에서 `use std::io::{BufRead, BufReader, Read};`를 `use std::io::{BufRead, BufReader, Read, Write};`로 바꾸고, `shutdown`의 앞부분을 바꾼다:

```rust
    /// Ends the sidecar: the user asked for it, so an `exit` line is written before stdin is closed (a pipe that only
    /// closes tells the sidecar that the shell is gone), and after [grace] it is killed together with whatever it started.
    /// Returns true when it ended on its own.
    pub fn shutdown(&mut self, grace: Duration) -> bool {
        if let Some(stdin) = self.stdin.as_mut() {
            let _ = stdin.write_all(b"exit\n");
            let _ = stdin.flush();
        }
        drop(self.stdin.take());
```

(그 아래 `let deadline = ...` 이하는 그대로 둔다.)

- [ ] **Step 4: 통과하는 것을 확인한다 (GREEN)**

Run: `cd sidecar-host; cargo test`
Expected: PASS — 전체 25개(`config` 9, `handshake` 5, `http` 2, `sidecar` 9). 경고가 없어야 한다.

- [ ] **Step 5: 셸이 여전히 컴파일되는지 본다**

Run: `cd shell/src-tauri; cargo check`
Expected: `Finished`(스파이크의 `lib.rs`는 `SidecarConfig::from_env()`, `command()`, `stderr_log()`, `app_data`만 써서 바뀌지 않는다).

- [ ] **Step 6: Commit**

```bash
git add sidecar-host
git commit -m "feat(sidecar-host): 이미지 폴더와 --log-dir을 찾고 종료할 때 exit 줄을 쓴다"
```

---

### Task 6: 사이드카 이미지 (줄인 lib + 번들 런타임)

**Files:**
- Modify: `server/build.gradle.kts`
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt`(새)

**Interfaces:**
- Consumes: Task 3·4의 사이드카, `installDist`와 같은 jar 모음
- Produces: Gradle 태스크 `:server:sidecarImage`(결과 `server/build/sidecar-image/`의 `lib`와 `runtime`)와 `:server:sidecarImageTest`(`@Tag("image")` 테스트만 돌리고 시스템 속성 `xgs.sidecarImageDir`을 준다). 기본 `test`는 `image` 태그를 건너뛴다. 배포 아카이브(`distZip`, `distTar`)는 끈다. Gradle 속성 `xgs.runtimeDir`로 런타임 출처를 바꿀 수 있다.

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import com.xgetsongs.shared.api.ApiHeaders
import org.junit.jupiter.api.Tag
import java.io.File
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * Runs the sidecar from the image the shell will start it from: the bundled runtime and the trimmed lib. Run through
 * `:server:sidecarImageTest`, which builds the image first and says where it is.
 */
@Tag("image")
class SidecarImageTest {
    private val image: Path = Path.of(checkNotNull(System.getProperty("xgs.sidecarImageDir")) { "run this through :server:sidecarImageTest" })

    @Test
    fun theImageHoldsNoNativesOfOtherPlatformsAndTheServerItself() {
        val names = Files.list(image.resolve("lib")).use { files -> files.map { it.fileName.toString() }.toList() }

        assertTrue(names.none { "-linux-" in it || "-osx-" in it }, "jars of other platforms: $names")
        assertTrue(names.any { it.startsWith("server") && it.endsWith(".jar") }, "the server jar: $names")
        assertTrue(Files.isRegularFile(image.resolve("runtime").resolve("bin").resolve("java.exe")), "the bundled runtime")
    }

    @Test
    fun theBundledRuntimeRunsTheSidecarAndStopsOnTheExitLine() {
        val base = Files.createTempDirectory("xgs-image-test").toAbsolutePath()
        val logDir = base.resolve("log")
        val java = image.resolve("runtime").resolve("bin").resolve("java.exe").toString()
        val process = ProcessBuilder(
            java, "-Dlogback.configurationFile=logback-sidecar.xml",
            "-cp", image.resolve("lib").toString() + File.separator + "*",
            "com.xgetsongs.server.sidecar.SidecarMainKt",
            "--app-data", base.resolve("data").toString(), "--log-dir", logDir.toString(),
        ).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        try {
            val line = process.inputStream.bufferedReader().readLine()
            assertTrue(line != null && line.startsWith("XGS-READY "), "the handshake line: $line")
            val (port, token) = line.split(' ').let { it[1] to it[2] }

            Thread.sleep(3000)
            assertTrue(process.isAlive, "the process is still running while its stdin is open")
            val request = HttpRequest.newBuilder(URI.create("http://127.0.0.1:$port/tools")).header(ApiHeaders.TOKEN, token).build()
            assertEquals(200, HttpClient.newHttpClient().send(request, HttpResponse.BodyHandlers.ofString()).statusCode(), "the server answers")

            process.outputStream.write("exit\n".toByteArray())
            process.outputStream.flush()
            assertTrue(process.waitFor(30, TimeUnit.SECONDS), "the process ends on the exit line")
            assertEquals(0, process.exitValue())
            assertTrue("JVM 종료 시작: 사용자가 창을 닫음" in Files.readString(logDir.resolve("xgetsongs.log")))
        } finally {
            process.destroyForcibly()
            base.toFile().deleteRecursively()
        }
    }
}
```

- [ ] **Step 2: 실패하는 것을 확인한다 (RED)**

Run: `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.SidecarImageTest" --no-daemon`
Expected: FAIL — `IllegalStateException: run this through :server:sidecarImageTest`(이 속성은 아직 아무도 주지 않는다. `sidecarImageTest` 태스크도 아직 없다).

- [ ] **Step 3: Gradle 태스크를 만든다**

`server/build.gradle.kts`의 `tasks.test { useJUnitPlatform() }`를 지우고 파일 끝에 다음을 더한다:

```kotlin
tasks.test {
    // The image tests need the image: they run through sidecarImageTest.
    useJUnitPlatform { excludeTags("image") }
}

// The shell starts the sidecar from the image below; the archives of the application plugin are not used.
tasks.distZip { enabled = false }
tasks.distTar { enabled = false }

/** Where the runtime to bundle comes from: `-Pxgs.runtimeDir=<a JDK or runtime folder>`, else the JDK 21 toolchain. */
val runtimeHome = providers.gradleProperty("xgs.runtimeDir").map { file(it) }
    .orElse(javaToolchains.launcherFor(java.toolchain).map { it.metadata.installationPath.asFile })

val sidecarImage by tasks.registering(Sync::class) {
    description = "Builds the folder the shell starts the sidecar from: the jars of the server and a runtime (java)."
    group = "distribution"
    into(layout.buildDirectory.dir("sidecar-image"))
    into("lib") {
        from(tasks.jar)
        from(configurations.runtimeClasspath)
        // A sidecar that only runs on Windows does not need the natives of the other platforms.
        exclude("*-linux-*", "*-osx-*")
    }
    into("runtime") {
        from(runtimeHome)
        exclude("lib/src.zip", "jmods/**", "include/**")
    }
}

val sidecarImageTest by tasks.registering(Test::class) {
    description = "Runs the tests that start the sidecar from the image (bundled runtime, trimmed lib)."
    group = "verification"
    dependsOn(sidecarImage)
    // The result depends on the image, which Gradle cannot see completely, so never skip a run.
    outputs.upToDateWhen { false }
    testClassesDirs = sourceSets.test.get().output.classesDirs
    classpath = sourceSets.test.get().runtimeClasspath
    useJUnitPlatform { includeTags("image") }
    systemProperty("xgs.sidecarImageDir", layout.buildDirectory.dir("sidecar-image").get().asFile.absolutePath)
    testLogging {
        events("passed", "skipped", "failed")
    }
}
```

- [ ] **Step 4: 통과하는 것을 확인한다 (GREEN)**

Run: `.\gradlew.bat :server:sidecarImageTest --no-daemon`
Expected: PASS — `SidecarImageTest` 2개. `server\build\sidecar-image\lib`에 jar가 77개(`installDist`의 81개에서 다른 OS의 네이티브 4개가 빠진다), `runtime\bin\java.exe`가 있다. 로그에서 `passed`가 두 줄 보인다.

Run: `.\gradlew.bat :server:test --no-daemon`
Expected: PASS — `SidecarImageTest`는 건너뛴다(`image` 태그). 나머지 테스트 전부 통과.

- [ ] **Step 5: 크기를 잰다**

```powershell
"{0:N1} MB  image total" -f ((Get-ChildItem server\build\sidecar-image -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
"{0:N1} MB  lib ({1} files)" -f ((Get-ChildItem server\build\sidecar-image\lib | Measure-Object Length -Sum).Sum / 1MB), (Get-ChildItem server\build\sidecar-image\lib | Measure-Object).Count
"{0:N1} MB  runtime" -f ((Get-ChildItem server\build\sidecar-image\runtime -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
```

Expected: lib가 스파이크 때(76.8MB, 81개)보다 약 9.7MB 작다(약 67MB, 77개). runtime은 약 92MB다. 값을 Task 7의 보고서에 적는다.

- [ ] **Step 6: Commit**

```bash
git add server
git commit -m "feat(server): 번들 런타임과 줄인 lib로 사이드카 이미지를 만들고 그 이미지로 시작을 시험한다"
```

---

### Task 7: 셸에서 이미지로 종단 확인하고 보고서를 쓴다

**Files:**
- Create: `docs/superpowers/specs/2026-10-10-tauri-sidecar-complete-report.md`
- Modify: `docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`(§5 표의 1단계 행)

**Interfaces:**
- Consumes: Task 5의 Rust(`from_env`가 이미지와 로그 폴더를 찾음), Task 6의 이미지 `server/build/sidecar-image`, 스파이크의 화면(`shell/spike.html`, `spike.conf.json`)
- Produces: 설계 §5의 1단계 증거("사이드카만으로 로그·종료 기록이 지금과 같다")와 크기·시작 시간 기록

- [ ] **Step 1: 셸을 다시 빌드하고 이미지를 만든다**

`sidecar-host`가 바뀌었으므로 스파이크 셸을 다시 만든다(몇 분 걸린다):

```powershell
$env:Path = "$env:USERPROFILE\.cargo\bin;$env:Path"
.\gradlew.bat :server:sidecarImage --no-daemon
cd shell
npm run tauri build -- --no-bundle --config spike.conf.json
cd ..
```

Expected: `Built application at: ...\shell\src-tauri\target\release\shell.exe`.

- [ ] **Step 2: 도우미를 정의한다**

이미지를 쓰는 점만 스파이크와 다르다(환경 변수 `XGS_SIDECAR_LIB`, `XGS_SIDECAR_JAVA`는 지운다). 새 PowerShell에서:

```powershell
Remove-Item Env:XGS_SIDECAR_LIB, Env:XGS_SIDECAR_JAVA, Env:XGS_SIDECAR_LOGDIR -ErrorAction SilentlyContinue
$app = Join-Path $env:TEMP "xgs-spike-appdata"
$shell = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe"
$logDir = Join-Path (Split-Path $shell) "log"
$env:XGS_SIDECAR_DIR = "C:\Projects\xGetSongs\server\build\sidecar-image"
function Get-Sidecar { Get-CimInstance Win32_Process -Filter "Name='java.exe'" | Where-Object { $_.CommandLine -like '*SidecarMainKt*' } }

function Start-Spike {
    $started = Get-Date
    $process = Start-Process -FilePath $shell -PassThru
    $windowAfterMs = $null
    $deadline = (Get-Date).AddSeconds(90)
    do {
        Start-Sleep -Milliseconds 100
        if ($null -eq $windowAfterMs -and (Get-Process -Id $process.Id -ErrorAction SilentlyContinue).MainWindowTitle) {
            $windowAfterMs = [int]((Get-Date) - $started).TotalMilliseconds
        }
        $record = if (Test-Path "$app\spike-record.txt") { Get-Content "$app\spike-record.txt" } else { @() }
    } while ((Get-Date) -lt $deadline -and -not ($record -match '^(directFetch|error)='))
    $values = @{}
    foreach ($line in $record) { $key, $value = $line -split '=', 2; $values[$key] = $value }
    [pscustomobject]@{ Process = $process; WindowAfterMs = $windowAfterMs; Record = $values; Ready = (Get-Content "$app\spike-ready.json" -ErrorAction SilentlyContinue | ConvertFrom-Json) }
}

function Close-Spike($process) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [void]$process.CloseMainWindow()
    while ($watch.Elapsed.TotalSeconds -lt 15 -and ((Get-Sidecar) -or -not $process.HasExited)) { Start-Sleep -Milliseconds 50 }
    [int]$watch.Elapsed.TotalMilliseconds
}

Remove-Item -Recurse -Force $logDir -ErrorAction SilentlyContinue
"leftover sidecars: $(@(Get-Sidecar).Count)"
```

Expected: `leftover sidecars: 0`.

- [ ] **Step 3: 번들 런타임으로 시작 시간을 3회 잰다**

```powershell
$runs = 1..3 | ForEach-Object {
    $run = Start-Spike
    $javaPath = (Get-Sidecar | Select-Object -First 1).ExecutablePath
    $closedMs = Close-Spike $run.Process
    [pscustomobject]@{ Run = $_; WindowAfterMs = $run.WindowAfterMs; ReadyMs = $run.Ready.readyMs; WaitedMs = [int]$run.Record.waitedMs; ToolsMs = [int]$run.Record.toolsMs; ClosedMs = $closedMs; SidecarLeft = @(Get-Sidecar).Count; Java = $javaPath; Token = $run.Ready.token }
}
$runs | Format-Table Run, WindowAfterMs, ReadyMs, WaitedMs, ToolsMs, ClosedMs, SidecarLeft -AutoSize | Out-String -Width 200
"java that ran the sidecar: " + $runs[0].Java
"median waitedMs: " + (($runs.WaitedMs | Sort-Object)[1]) + "   median readyMs: " + (($runs.ReadyMs | Sort-Object)[1])
```

Expected: `Java`가 `server\build\sidecar-image\runtime\bin\java.exe`(PATH의 `java`가 아니다), `SidecarLeft`가 0, `ClosedMs`가 3000 이하, `waitedMs` 중앙값이 10000 이하(스파이크는 1,945ms, 같은 PC에서 번들 런타임이 크게 느려지지는 않는다). 값을 적는다.

- [ ] **Step 4: 사용자가 창을 닫은 경우의 로그를 본다**

```powershell
"files in the log folder: "; Get-ChildItem $logDir | ForEach-Object { "  $($_.Name)  $($_.Length) bytes" }
$text = Get-Content "$logDir\xgetsongs.log" -Raw
foreach ($expected in @("시작 정보", "내장 서버 시작", "사용자가 창 닫기를 요청함", "내장 서버 정지", "JVM 종료 시작: 사용자가 창을 닫음")) { "{0,-34} {1}" -f $expected, ($text -match [regex]::Escape($expected)) }
"a token of the three runs in the log: " + (@($runs.Token | Where-Object { $text.Contains($_) }).Count -gt 0)
"the handshake line in the log: " + $text.Contains("XGS-READY")
"--- marker"; Get-Content "$logDir\last-run.txt"
"--- the start record"; ($text -split "`n" | Select-Object -First 8) -join "`n"
```

Expected: 로그 폴더가 셸 실행 파일 옆 `log\`이고(`xgetsongs.log`, `last-run.txt`. `sidecar-stderr.log`는 앱 데이터 폴더에 따로 있다), 다섯 줄이 모두 `True`, 마커가 `state=exited`와 `reason=user`다. 세 번의 실행의 토큰이 로그에 없고(`False`) 시작 신호 줄도 로그에 없다(`False`).

- [ ] **Step 5: 셸이 갑자기 죽은 경우를 본다**

```powershell
$run = Start-Spike
$before = (Get-Content "$logDir\last-run.txt" -Raw)
Stop-Process -Id $run.Process.Id -Force
$watch = [Diagnostics.Stopwatch]::StartNew()
while ($watch.Elapsed.TotalSeconds -lt 10 -and (Get-Sidecar)) { Start-Sleep -Milliseconds 50 }
"sidecar left after the force kill: $(@(Get-Sidecar).Count) (after $([int]$watch.Elapsed.TotalMilliseconds) ms)"
"--- marker after the force kill"; Get-Content "$logDir\last-run.txt"
"--- the last lines of the log"; Get-Content "$logDir\xgetsongs.log" -Tail 4
"--- the next start says what happened to the run before"
$next = Start-Spike
Start-Sleep -Milliseconds 500
Get-Content "$logDir\xgetsongs.log" | Select-String "이전 실행|정상 종료 기록|다른 실행" | Select-Object -Last 2 | ForEach-Object { $_.Line }
[void](Close-Spike $next.Process)
```

Expected: 사이드카가 사라진다. 두 가지 중 하나가 일어난다 — Job Object가 JVM을 먼저 죽이면 마커가 `state=running`인 채 남고 다음 시작의 로그에 `이전 실행(PID …, 시작 …)이 정상 종료 기록 없이 끝났음` 경고가 나온다. stdin 닫힘이 먼저 사이드카를 끝내면 마커가 `reason=other`이고 로그에 `창을 닫지 않은 종료` 경고가 남는다. 어느 쪽이든 Compose 앱과 같은 종류의 흔적이다. 어느 쪽인지와 로그 줄을 보고서에 적는다.

- [ ] **Step 6: 전체 테스트를 돌린다**

```powershell
.\gradlew.bat check :server:sidecarImageTest --no-daemon
cd sidecar-host; cargo test; cd ..
```

Expected: `BUILD SUCCESSFUL`(모든 모듈 테스트 통과, `sidecarImageTest` 2개 통과)와 `cargo test`의 `25 passed`. 실패한 테스트가 있으면 이름과 함께 보고서에 적는다(내가 만들지 않은 실패도 숨기지 않는다).

- [ ] **Step 7: 보고서를 쓴다**

`docs/superpowers/specs/2026-10-10-tauri-sidecar-complete-report.md`에 실제 측정값으로 채워서 쓴다. 구조는 다음과 같다(값과 판정은 위 단계의 결과를 그대로 적는다):

```markdown
# Tauri 사이드카 완성(1단계) 보고서

작성일: 2026-10-10 (측정한 날로 고친다)
설계: [2026-10-10-tauri-kotlin-sidecar-design.md](2026-10-10-tauri-kotlin-sidecar-design.md) §5의 1단계
계획: [2026-10-10-tauri-sidecar-complete.md](../plans/2026-10-10-tauri-sidecar-complete.md)

## 증거: 사이드카만으로 로그·종료 기록이 지금과 같다

| README의 문제 확인 항목 | Compose 앱 | 사이드카 | 근거 |
|---|---|---|---|
| 로그 파일 위치, 회전(5MB·7일·50MB), UTF-8, 즉시 flush | `log\xgetsongs.log` | 셸 실행 파일 옆 `log\` (안 되면 앱 데이터 `logs`, 임시 폴더) | |
| 시작 정보(Java, OS, 메모리, 도구 경로) | 있음 | | |
| 작업 시작·옵션·항목 결과·취소·연결 끊김 | 있음 | 서버의 `JobLog`가 같은 logback 설정으로 파일에 간다 | |
| 처리되지 않은 예외 | 있음 | `Diagnostics.start`가 같은 핸들러를 건다 | |
| 멈춤 덤프 `ui-hang-*.txt` | UI 스레드(AWT)가 5초 넘게 응답 없음 | `Dispatchers.Default`의 모든 워커가 5초 넘게 응답 없음 | `aDispatcherThatStopsAnsweringLeavesAThreadDumpInTheLogFolder` |
| 종료 기록: 사용자가 닫음 / 닫지 않은 종료 | 창 닫기 버튼 | 셸이 쓰는 `exit` 줄 / `exit` 없이 파이프만 닫힘 | 프로세스 테스트 2개와 Step 4·5 |
| `last-run.txt` | 있음 | 같은 형식(`state`, `reason`) | |
| 토큰·가사·제목·파일명은 로그에 없음 | | 로그에 토큰 없음 | `theLogHoldsTheStartRecordsButNeverTheToken` |

## 측정

| 항목 | 값 |
|---|---|
| 사이드카 이미지 전체 | MB |
| lib (스파이크 76.8MB, 81개) | MB, 개 |
| runtime (JBR 복사) | MB |
| waitedMs 3회 중앙값 (스파이크 1,945ms) | ms |
| readyMs 3회 중앙값 (스파이크 1,405ms) | ms |
| 창 닫기 → 정리 | ms |
| 번들 런타임이 실제로 쓰였는가 | `java` 경로 |

## 셸이 갑자기 죽은 경우

(Step 5의 결과: 사이드카가 사라진 시간, 마커 상태, 다음 시작의 로그 줄.)

## 막힌 점과 고친 것

(계획과 달랐던 점. 없으면 "없음".)

## 판정

(통과 / 재시도 후 통과 / 중단. 통과이면 다음은 설계 §5의 2단계 "Rust 브리지"다.)

## 이 단계가 확인하지 않은 것

- `jlink`로 줄인 런타임(5단계, `jmods`가 있는 JDK가 필요하다), 콜드 스타트, Defender·SmartScreen, 실제 다운로드 중의 취소와 정리.
- 로그 문구와 `ui-hang-*.txt` 파일 이름은 Compose 앱과 같게 두었다. 사이드카에서는 "UI"가 서버 응답을 뜻하므로 6단계에서 정리한다.
- `README.md`의 로그 설명(위치 규칙 등)은 5~6단계에서 고친다.
```

- [ ] **Step 8: 설계 문서의 1단계 행을 갱신한다**

`docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`의 §5 표에서 1단계 행의 마지막 칸에는 이 계획서 링크가 이미 있다(계획을 쓸 때 걸어 두었다). 거기에 보고서 링크를 더한다:

```markdown
| 1 | 사이드카 완성: `Diagnostics`(로그, 종료 기록, `last-run.txt`)를 `app`에서 옮기고 파일 로그를 쓴다. 번들한 JRE로 실행한다 | 사이드카만으로 로그·종료 기록이 지금과 같다 ([계획서](../plans/2026-10-10-tauri-sidecar-complete.md), [보고서](2026-10-10-tauri-sidecar-complete-report.md)). 런타임은 JDK 복사본이고 `jlink`로 줄이는 것은 5단계(`jmods`가 있는 JDK가 필요) |
```

- [ ] **Step 9: Commit**

```bash
git add docs
git commit -m "문서: 사이드카 완성(1단계)의 종단 확인 결과와 설계의 1단계 행"
```

---

## Self-Review

**1. Spec coverage** (설계 §5 1단계, 스파이크 보고서 "판정")

- "`Diagnostics`를 `app`에서 옮기고 파일 로그를 쓴다" → Task 1(모듈), Task 2(`logback-sidecar.xml` 파일 로그), Task 4(진단 켜기).
- "종료 기록": 사용자가 닫음 / 닫지 않은 종료 → Task 3(`exit` 줄), Task 5(Rust가 `exit`를 씀), Task 4의 프로세스 테스트와 Task 7 Step 4·5.
- `last-run.txt`, 시작 정보, 멈춤 덤프 → Task 4(`SidecarDiagnostics`, 프로세스·덤프 테스트).
- "번들한 JRE로 실행한다" → Task 5(이미지 찾기), Task 6(`sidecarImage`와 `SidecarImageTest`), Task 7 Step 3(실제로 이미지의 `java`가 쓰이는지 확인).
- 스파이크의 마무리 항목: lib 줄이기(다른 OS의 네이티브 jar 제외, Task 6), 배포 아카이브 끄기(Task 6), stderr 로그 무제한(파일 로그가 대체하고 stderr는 `sidecar-stderr.log`로 남지만 사이드카는 이제 stderr에 INFO 이상만 쓴다. 스파이크 셸의 `copy_to_file`은 여전히 제한이 없으나 그 양이 INFO 이상 몇 줄이다).
- 이 계획 밖(의도적): 시작 중 2초의 화면 표시(2~3단계), `jlink`와 포장(5단계), `README` 갱신(5~6단계), 로그 문구의 "UI" 정리(6단계), 토큰을 Rust 안에 숨기는 것(2단계).

**2. Placeholder scan:** 코드 단계는 실제 코드를 담았다. 보고서의 빈 칸은 측정값을 채우는 기록 칸이다. Kotlin과 Rust 코드는 이 계획을 쓰는 시점에 컴파일해 보지 못했다(Task 1은 파일 이동과 작은 수정이라 위험이 작고, Task 2~6의 새 코드는 실행하면서 처음 컴파일된다). 컴파일 오류가 나면 계획의 시그니처와 테스트를 기준으로 고친다.

**3. Type consistency:** `SidecarArgs(appData, logDir)`(Task 2)는 Task 4의 `sidecarLogCandidates`, `SidecarDiagnostics.start(args, ...)`와 같다. `ParentWatch`의 `onGone: (Boolean) -> Unit`(Task 3)은 `runSidecar`의 `onParentGone`과 `main`(Task 4)이 쓴다. `Diagnostics.start(logDir, postToUi, registerHook, processes, headless)`(Task 1)는 `SidecarDiagnostics`(Task 4), 앱 `Main.kt`(Task 1)와 같다. Rust의 `SidecarConfig.log_dir`과 `from_lookup(get, exe_dir)`(Task 5)는 `from_env`와 테스트가 같은 시그니처를 쓴다. 환경 변수 이름(`XGS_SIDECAR_LIB`, `_JAVA`, `_DIR`, `_APPDATA`, `_LOGDIR`)과 인자 이름(`--app-data`, `--log-dir`)은 Kotlin·Rust·확인 스크립트에서 같다. 이미지 폴더 구조(`runtime\bin\java.exe`, `lib`)는 Task 5의 탐색과 Task 6의 `Sync`가 같다.

**4. Review Focus:** 다섯 항목 모두 위의 Review Focus 절에 적은 Task의 테스트에 대응한다.
