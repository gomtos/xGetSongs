# Tauri 사이드카 스파이크 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kotlin 서버를 사이드카 프로세스로 띄우는 진입점과 그것을 제어하는 최소 Tauri 셸을 만들고, 시작·종료·프로세스 정리·크기를 재서 마이그레이션의 go/no-go 관문(설계 §5.1)을 판정한다.

**Architecture:** `server` 모듈에 `sidecar` 패키지를 추가한다(`--app-data` 인자, stdout 시작 신호 한 줄, stdin 닫힘 감시). Rust 쪽은 Tauri에 의존하지 않는 `sidecar-host` 크레이트(시작 신호 해석, 사이드카 시작·종료, Job Object, 토큰 달린 HTTP GET)로 만들고, `shell/`의 Tauri 앱이 그것을 불러 `GET /tools`를 화면에 보여 준다. Compose 앱과 기존 서버 코드는 바꾸지 않는다.

**Tech Stack:** Kotlin 2.4.20 / JDK 21 / Ktor 3.6.0 (기존), Rust stable (MSVC) + `ureq` 2 + `win32job` 2, Tauri 2 (`vanilla-ts`), Node 24 / npm.

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md)

## Global Constraints

- 사이드카는 `--app-data <절대 경로>`를 반드시 받는다. 기본값이 없다. `createServices`가 시작할 때 `<app-data>\work`를 지우므로, 스파이크는 사용자의 `%APPDATA%\xGetSongs`를 절대 쓰지 않고 임시 폴더(`%TEMP%\xgs-spike-appdata`)를 쓴다.
- 사이드카의 stdout에는 시작 신호 한 줄(`XGS-READY <port> <token>`)만 쓴다. 로그는 stderr로 보낸다.
- 토큰은 로그와 오류 메시지에 쓰지 않는다. 스파이크의 화면(디버그용)에만 예외로 보여 준다. 이 예외는 다음 단계에서 없앤다.
- 마이그레이션의 동기는 UI 품질이다. 이 스파이크는 그 비용인 사이드카의 안전성(M1~M4)을 확인하고, 크기와 시작 시간은 개선이 아니라 설계 §5.1의 악화 한도(`waitedMs` 10초 이하, 나머지는 기록만)로 본다. UI가 나아지는지는 이 계획서가 아니라 0a(UI 프로토타입)가 판정하며, 그 UI 관문을 통과하지 못하면 이 스파이크를 시작하지 않는다.
- 서버의 `Guard.kt`(Origin 거부, Host 검사, 토큰)는 바꾸지 않는다.
- Gradle은 `--no-daemon`으로 부른다(사용자의 VS Code가 공유 데몬을 쓴다).
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구와 커밋 메시지는 한국어. `main`에 직접 커밋한다(브랜치를 만들지 않는다). 커밋 메시지 끝에는 이 세션이 정한 `Co-Authored-By` 줄을 붙인다.
- 소프트웨어 설치(Rust)와 내려받기는 사용자에게 알리고 허락을 받은 뒤에 한다.
- Rust 코드는 이 계획을 쓰는 시점에 컴파일해 보지 못했다(이 PC에 Rust가 없음). 컴파일 오류가 나면 계획의 함수 시그니처와 테스트를 기준으로 고친다.

## Review Focus

스펙이 암시하지만 테스트가 놓치기 쉬운 것, 사용자에게 가장 먼저 문제가 될 순서:

1. **시작 신호 앞이나 사이에 다른 줄이 섞인다** (JVM 경고가 stdout에 나올 수 있다). 시작 신호가 아닌 줄은 무시하고 접두사로 시작하는 줄만 인정해야 한다. → Task 4의 `parse_handshake` 테스트, Task 5의 "잡음 사이의 신호" 테스트
2. **사이드카가 시작 신호 전에 죽거나 멈춘다.** 셸이 영원히 기다리면 안 되고, 멈춘 프로세스는 남기지 않는다. → Task 3의 "시작하지 못하는 서버" 테스트, Task 5의 "먼저 끝남"과 "시간 초과" 테스트
3. **stdout 파이프가 가득 차서 사이드카가 막힌다.** 신호를 읽은 뒤에도 stdout을 계속 비워야 한다. → Task 5의 "신호 뒤에도 비움" 테스트
4. **`--app-data`가 비었거나 상대 경로다.** 시작할 때 `work`를 지우므로 엉뚱한 폴더를 지울 수 있다. → Task 3의 `SidecarArgsTest`
5. **셸이 stdin 닫기 신호도 못 보낼 만큼 갑자기 죽는다.** 사이드카가 stdin 닫힘으로 스스로 끝나더라도, yt-dlp 같은 자손은 따로 정리돼야 한다. → Task 7의 M3(자손 확인)

## File Structure

| 파일 | 책임 |
|---|---|
| `server/src/main/kotlin/com/xgetsongs/server/sidecar/Handshake.kt` (새) | 시작 신호 한 줄을 만든다 |
| `server/src/main/kotlin/com/xgetsongs/server/sidecar/ParentWatch.kt` (새) | stdin이 끝나면 한 번 알린다 |
| `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarArgs.kt` (새) | `--app-data` 인자를 검사한다 |
| `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarMain.kt` (새) | `runSidecar`(순서 조립)와 `main` |
| `server/src/main/resources/logback-sidecar.xml` (새) | 사이드카의 로그를 stderr로 보낸다 |
| `server/build.gradle.kts` (수정) | `application` 플러그인 |
| `server/src/test/kotlin/com/xgetsongs/server/sidecar/*Test.kt` (새) | 위 네 파일의 테스트 |
| `sidecar-host/` (새 Rust 크레이트) | `handshake.rs`, `config.rs`, `sidecar.rs`, `http.rs`, `job.rs`, `lib.rs` |
| `shell/` (0a가 만든 Tauri 앱에 더함) | `src-tauri/src/lib.rs`(연결, 결과 파일), `spike.html`과 `src/spike.ts`(스파이크 화면), `spike.conf.json`(스파이크 빌드가 그 페이지를 열게 함), `vite.config.ts`(두 페이지 빌드). 0a의 `index.html`과 `src/main.ts`는 건드리지 않는다 |
| `docs/superpowers/specs/2026-10-10-tauri-sidecar-spike-report.md` (새) | 측정값과 관문 판정 |

---

### Task 1: 시작 신호 줄 (Kotlin)

**Files:**
- Create: `server/src/main/kotlin/com/xgetsongs/server/sidecar/Handshake.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/HandshakeTest.kt`

**Interfaces:**
- Consumes: 없음
- Produces: `object Handshake { const val PREFIX: String; fun line(port: Int, token: String): String }` — Task 3의 `runSidecar`가 쓴다. Rust의 `parse_handshake`(Task 4)가 같은 형식을 읽는다: `XGS-READY <port> <token>`.

- [ ] **Step 1: Write the failing test**

`server/src/test/kotlin/com/xgetsongs/server/sidecar/HandshakeTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import java.util.Base64
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class HandshakeTest {
    @Test
    fun theLineHasThePrefixThePortAndTheToken() {
        assertEquals("XGS-READY 51234 abc_DEF-123", Handshake.line(51234, "abc_DEF-123"))
    }

    @Test
    fun aTokenOfTheServerFitsInOneWord() {
        // LocalServer makes its tokens with URL-safe Base64 and no padding.
        val token = Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(32) { it.toByte() })

        assertEquals("XGS-READY 80 $token", Handshake.line(80, token))
    }

    @Test
    fun aTokenThatIsEmptyOrHoldsWhitespaceIsRefused() {
        assertFailsWith<IllegalArgumentException> { Handshake.line(80, "") }
        assertFailsWith<IllegalArgumentException> { Handshake.line(80, "two words") }
        assertFailsWith<IllegalArgumentException> { Handshake.line(80, "line\nbreak") }
    }

    @Test
    fun aPortOutsideTheRangeIsRefused() {
        assertFailsWith<IllegalArgumentException> { Handshake.line(0, "t") }
        assertFailsWith<IllegalArgumentException> { Handshake.line(65536, "t") }
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run (PowerShell, repo root): `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.HandshakeTest" --no-daemon`
Expected: FAIL — compile error `Unresolved reference 'Handshake'`.

- [ ] **Step 3: Write minimal implementation**

`server/src/main/kotlin/com/xgetsongs/server/sidecar/Handshake.kt`:

```kotlin
package com.xgetsongs.server.sidecar

/**
 * The one line the sidecar prints on stdout once its server listens: `XGS-READY <port> <token>`. The shell that started
 * the process reads it to learn where the server is. Nothing else may be written to stdout (the logs go to stderr).
 */
object Handshake {
    const val PREFIX = "XGS-READY"

    fun line(port: Int, token: String): String {
        require(port in 1..65535) { "port out of range: $port" }
        // The message must not repeat the token: it is a secret.
        require(token.isNotEmpty() && token.none { it.isWhitespace() }) { "the token must be one word" }
        return "$PREFIX $port $token"
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.HandshakeTest" --no-daemon`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add server/src/main/kotlin/com/xgetsongs/server/sidecar/Handshake.kt server/src/test/kotlin/com/xgetsongs/server/sidecar/HandshakeTest.kt
git commit -m "feat(server): 사이드카 시작 신호 줄을 만든다"
```

---

### Task 2: 부모 감시 (Kotlin)

**Files:**
- Create: `server/src/main/kotlin/com/xgetsongs/server/sidecar/ParentWatch.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/ParentWatchTest.kt`

**Interfaces:**
- Consumes: 없음
- Produces: `class ParentWatch(input: InputStream, onGone: () -> Unit) { fun start(): Thread }` — Task 3의 `runSidecar`가 `System.in`으로 만든다. `onGone`은 입력이 끝나거나(`read`가 -1) `IOException`이 나면 정확히 한 번 불린다.

- [ ] **Step 1: Write the failing test**

`server/src/test/kotlin/com/xgetsongs/server/sidecar/ParentWatchTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ParentWatchTest {
    private val gone = CountDownLatch(1)
    private val calls = AtomicInteger()

    private fun watch(input: InputStream) = ParentWatch(input) {
        calls.incrementAndGet()
        gone.countDown()
    }.start()

    @Test
    fun anInputThatEndsAtOnceMeansTheParentIsGone() {
        watch(ByteArrayInputStream(ByteArray(0)))

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(1, calls.get())
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
        assertEquals(1, calls.get())
    }

    @Test
    fun aFailingInputAlsoMeansTheParentIsGone() {
        watch(object : InputStream() {
            override fun read(): Int = throw IOException("pipe broken")
        })

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(1, calls.get())
    }

    @Test
    fun theWatchingThreadNeverKeepsTheJvmAlive() {
        val writer = PipedOutputStream()
        val thread = watch(PipedInputStream(writer))

        assertTrue(thread.isDaemon)
        writer.close()
        assertTrue(gone.await(30, TimeUnit.SECONDS))
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.ParentWatchTest" --no-daemon`
Expected: FAIL — compile error `Unresolved reference 'ParentWatch'`.

- [ ] **Step 3: Write minimal implementation**

`server/src/main/kotlin/com/xgetsongs/server/sidecar/ParentWatch.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import java.io.IOException
import java.io.InputStream
import kotlin.concurrent.thread

/**
 * Calls [onGone] once when [input] ends or fails. The shell that owns the other end of the sidecar's stdin closes it to ask
 * the sidecar to stop, and the operating system closes it when the shell dies, so this one signal covers both.
 */
class ParentWatch(private val input: InputStream, private val onGone: () -> Unit) {
    /** Starts the watching thread. It is a daemon: it never keeps the JVM alive. */
    fun start(): Thread = thread(name = "parent-watch", isDaemon = true) {
        val buffer = ByteArray(256)
        try {
            while (input.read(buffer) != -1) {
                // What the shell writes is not interpreted: only the end of the stream matters.
            }
        } catch (e: IOException) {
            // A stream that fails is as good as one that ended.
        }
        onGone()
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.ParentWatchTest" --no-daemon`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add server/src/main/kotlin/com/xgetsongs/server/sidecar/ParentWatch.kt server/src/test/kotlin/com/xgetsongs/server/sidecar/ParentWatchTest.kt
git commit -m "feat(server): 사이드카가 stdin 닫힘으로 부모의 종료를 알아챈다"
```

---

### Task 3: 사이드카 진입점과 실행 배포 (Kotlin)

**Files:**
- Create: `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarArgs.kt`
- Create: `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarMain.kt`
- Create: `server/src/main/resources/logback-sidecar.xml`
- Modify: `server/build.gradle.kts` (플러그인 블록과 파일 끝)
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarArgsTest.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarMainTest.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarProcessTest.kt`

**Interfaces:**
- Consumes: `Handshake.line(port, token)` (Task 1), `ParentWatch(input, onGone).start()` (Task 2), 기존 `LocalServer.start(appDataDir: Path): LocalServer` (`port`, `token`, `stop()`), 테스트에서는 기존 `TestServices().services`와 `LocalServer.start(services)`.
- Produces: `data class SidecarArgs(val appData: Path)` with `SidecarArgs.parse(args: Array<String>): SidecarArgs?`(잘못되면 null)과 `SidecarArgs.USAGE`. `internal fun runSidecar(args, stdin: InputStream, stdout: PrintStream, stderr: PrintStream, startServer: (Path) -> LocalServer, exit: (Int) -> Unit): Thread?`(부모 감시 스레드, 시작하지 못하면 null; `main`이 그것을 `join`해서 JVM을 붙잡는다). `fun main(args: Array<String>)`. Gradle 결과물 `server/build/install/xgs-server/lib/*.jar`와 메인 클래스 `com.xgetsongs.server.sidecar.SidecarMainKt` — Task 6·7이 이 경로와 이름을 쓴다.

- [ ] **Step 1: Write the failing tests**

`server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarArgsTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class SidecarArgsTest {
    private val absolute = Files.createTempDirectory("xgs-args").toAbsolutePath()

    @Test
    fun anAbsoluteAppDataFolderIsAccepted() {
        assertEquals(SidecarArgs(absolute), SidecarArgs.parse(arrayOf("--app-data", absolute.toString())))
    }

    @Test
    fun missingOrUnknownOrExtraArgumentsAreRefused() {
        assertNull(SidecarArgs.parse(arrayOf()))
        assertNull(SidecarArgs.parse(arrayOf("--app-data")))
        assertNull(SidecarArgs.parse(arrayOf("--data", absolute.toString())))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--verbose")))
    }

    @Test
    fun aBlankFolderIsRefused() {
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "")))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "   ")))
    }

    @Test
    fun aRelativeFolderIsRefusedBecauseTheServerClearsItsWorkFolderAtStart() {
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "data")))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "..\\data")))
    }

    @Test
    fun aPathThatCannotBeParsedIsRefused() {
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "C:\\bad\u0000path")))
    }
}
```

`server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarMainTest.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import com.xgetsongs.server.LocalServer
import com.xgetsongs.server.TestServices
import com.xgetsongs.shared.api.ApiHeaders
import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.http.HttpStatusCode
import kotlinx.coroutines.runBlocking
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.io.PrintStream
import java.net.ConnectException
import java.net.Socket
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class SidecarMainTest {
    private val fakes = TestServices()
    private val stdout = ByteArrayOutputStream()
    private val stderr = ByteArrayOutputStream()
    private val exits = LinkedBlockingQueue<Int>()

    private fun out() = stdout.toString("UTF-8")
    private fun err() = stderr.toString("UTF-8")

    private fun runWith(args: Array<String>, stdin: InputStream, startServer: (Path) -> LocalServer) =
        runSidecar(args, stdin, PrintStream(stdout, true, "UTF-8"), PrintStream(stderr, true, "UTF-8"), startServer) { exits.put(it) }

    @Test
    fun startsTheServerPrintsOnlyTheHandshakeAndStopsWhenStdinEnds() {
        val appData = Files.createTempDirectory("xgs-sidecar").resolve("data")
        val parentWriter = PipedOutputStream()

        runWith(arrayOf("--app-data", appData.toString()), PipedInputStream(parentWriter)) { LocalServer.start(fakes.services) }

        val lines = out().lines().filter { it.isNotBlank() }
        assertEquals(1, lines.size, "nothing but the handshake on stdout: $lines")
        val parts = lines.single().split(' ')
        assertEquals(3, parts.size, lines.single())
        assertEquals("XGS-READY", parts[0])
        val port = parts[1].toInt()
        val token = parts[2]
        assertTrue(Files.isDirectory(appData), "the app data folder is made")
        runBlocking {
            HttpClient(CIO) {
                defaultRequest {
                    url("http://127.0.0.1:$port")
                    header(ApiHeaders.TOKEN, token)
                }
            }.use { assertEquals(HttpStatusCode.OK, it.get("/tools").status) }
        }

        parentWriter.close() // the shell goes away

        assertEquals(0, exits.poll(30, TimeUnit.SECONDS))
        assertFailsWith<ConnectException> { Socket("127.0.0.1", port).close() }
    }

    @Test
    fun wrongArgumentsPrintTheUsageAndExitWithTwoWithoutStartingAServer() {
        runWith(arrayOf("--bogus"), ByteArrayInputStream(ByteArray(0))) { error("the server must not start") }

        assertEquals(listOf(2), exits.toList())
        assertTrue("--app-data" in err(), err())
        assertEquals("", out())
    }

    @Test
    fun aServerThatCannotStartIsReportedAndTheProcessExitsWithOne() {
        val appData = Files.createTempDirectory("xgs-sidecar").toAbsolutePath()

        runWith(arrayOf("--app-data", appData.toString()), ByteArrayInputStream(ByteArray(0))) {
            throw IllegalStateException("port in use")
        }

        assertEquals(listOf(1), exits.toList())
        assertTrue("IllegalStateException" in err(), err())
        assertEquals("", out(), "no handshake for a server that is not there")
    }
}
```

`server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarProcessTest.kt` (실제 프로세스를 띄운다. 단위 테스트의 JVM은 테스트 실행기가 살려 두므로, 사이드카 자신이 `main` 직후 종료하지 않는지는 이 테스트만 잡는다. 계획을 쓸 때는 이 테스트가 없었고, Step 5의 실제 프로세스 확인이 그 결함을 먼저 드러냈다):

```kotlin
package com.xgetsongs.server.sidecar

import com.xgetsongs.shared.api.ApiHeaders
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
 * Starts the real thing in a process of its own. The unit tests of [runSidecar] run in a JVM that the test runner keeps
 * alive, so they cannot tell whether the sidecar itself stays up: its server's threads are daemons, and a JVM with
 * nothing else to wait for starts to shut down as soon as main returns (and takes the server with it, a moment later).
 */
class SidecarProcessTest {
    @Test
    fun theRealProcessKeepsServingUntilItsStdinIsClosedAndThenEndsWithZero() {
        val appData = Files.createTempDirectory("xgs-sidecar-process").toAbsolutePath()
        val java = Path.of(System.getProperty("java.home"), "bin", "java").toString()
        val process = ProcessBuilder(
            java,
            "-Dlogback.configurationFile=logback-sidecar.xml",
            "-cp", System.getProperty("java.class.path"),
            "com.xgetsongs.server.sidecar.SidecarMainKt",
            "--app-data", appData.toString(),
        ).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        try {
            val line = process.inputStream.bufferedReader().readLine()
            assertTrue(line != null && line.startsWith("XGS-READY "), "the handshake line: $line")
            val (port, token) = line.split(' ').let { it[1] to it[2] }

            // Long enough for a JVM that is shutting down to be gone, which a shorter wait would not show.
            Thread.sleep(3000)
            assertTrue(process.isAlive, "the process is still running while its stdin is open")
            val request = HttpRequest.newBuilder(URI.create("http://127.0.0.1:$port/tools")).header(ApiHeaders.TOKEN, token).build()
            val response = HttpClient.newHttpClient().send(request, HttpResponse.BodyHandlers.ofString())
            assertEquals(200, response.statusCode(), "the server still answers")

            process.outputStream.close() // the shell goes away
            assertTrue(process.waitFor(30, TimeUnit.SECONDS), "the process ends after its stdin is closed")
            assertEquals(0, process.exitValue())
        } finally {
            process.destroyForcibly()
        }
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `.\gradlew.bat :server:test --tests "com.xgetsongs.server.sidecar.SidecarArgsTest" --tests "com.xgetsongs.server.sidecar.SidecarMainTest" --tests "com.xgetsongs.server.sidecar.SidecarProcessTest" --no-daemon`
Expected: FAIL — compile errors `Unresolved reference 'SidecarArgs'` and `'runSidecar'`.

- [ ] **Step 3: Write minimal implementation**

`server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarArgs.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import java.nio.file.InvalidPathException
import java.nio.file.Path

/**
 * The command line of the sidecar: `--app-data <absolute folder>`. There is no default on purpose: the server clears
 * `<app-data>\work` when it starts, so a guessed folder could be one that another running copy is using.
 */
data class SidecarArgs(val appData: Path) {
    companion object {
        const val USAGE = "사용법: --app-data <절대 경로>"

        /** Null when the arguments are not exactly `--app-data` and an absolute folder. */
        fun parse(args: Array<String>): SidecarArgs? {
            if (args.size != 2 || args[0] != "--app-data" || args[1].isBlank()) return null
            val path = try {
                Path.of(args[1])
            } catch (e: InvalidPathException) {
                return null
            }
            return if (path.isAbsolute) SidecarArgs(path) else null
        }
    }
}
```

`server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarMain.kt`:

```kotlin
package com.xgetsongs.server.sidecar

import com.xgetsongs.server.LocalServer
import java.io.InputStream
import java.io.PrintStream
import java.nio.file.Files
import java.nio.file.Path
import kotlin.system.exitProcess

// No logger is declared in this file: logback reads its configuration the first time anything asks for one, and the
// start script already names the file (-Dlogback.configurationFile=logback-sidecar.xml, see build.gradle.kts).

/**
 * Starts the server for the shell that owns this process: tells it where the server is on stdout, and stops when
 * [stdin] ends. [exit] ends the process; the tests pass a recorder, so this function returns after it.
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
    exit: (Int) -> Unit,
): Thread? {
    val parsed = SidecarArgs.parse(args)
    if (parsed == null) {
        stderr.println(SidecarArgs.USAGE)
        exit(2)
        return null
    }
    val server = try {
        Files.createDirectories(parsed.appData)
        startServer(parsed.appData)
    } catch (e: Exception) {
        stderr.println("사이드카를 시작하지 못했습니다: ${e.javaClass.simpleName}: ${e.message}")
        exit(1)
        return null
    }
    stdout.println(Handshake.line(server.port, server.token))
    stdout.flush()
    return ParentWatch(stdin) {
        server.stop()
        exit(0)
    }.start()
}

fun main(args: Array<String>) {
    // Waiting for the watching thread keeps the JVM alive until the parent is gone; that thread ends the process itself.
    runSidecar(args, System.`in`, System.out, System.err, { LocalServer.start(it) }, { exitProcess(it) })?.join()
}
```

`server/src/main/resources/logback-sidecar.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!--
  The sidecar's logging, chosen by -Dlogback.configurationFile=logback-sidecar.xml in the start script. stdout carries the
  handshake line, so everything goes to stderr. The Compose app has its own logback.xml; this file has another name so
  that the two never compete on one class path.
-->
<configuration>
    <appender name="STDERR" class="ch.qos.logback.core.ConsoleAppender">
        <target>System.err</target>
        <encoder>
            <charset>UTF-8</charset>
            <pattern>%d{HH:mm:ss.SSS} %-5level [%thread] %logger{20} - %msg%n</pattern>
        </encoder>
    </appender>

    <logger name="io.netty" level="WARN"/>
    <logger name="io.ktor" level="INFO"/>
    <logger name="com.xgetsongs" level="DEBUG"/>

    <root level="INFO">
        <appender-ref ref="STDERR"/>
    </root>
</configuration>
```

`server/build.gradle.kts` — 맨 위 `plugins` 블록을 다음으로 바꾸고, 파일 끝에 `application` 블록을 붙인다:

```kotlin
plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
    application
}
```

```kotlin
application {
    mainClass.set("com.xgetsongs.server.sidecar.SidecarMainKt")
    applicationName = "xgs-server"
    // stdout is the handshake channel of the sidecar, so its logs go to stderr (see logback-sidecar.xml).
    applicationDefaultJvmArgs = listOf("-Dlogback.configurationFile=logback-sidecar.xml")
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `.\gradlew.bat :server:test --no-daemon`
Expected: PASS — 새 테스트(`SidecarArgsTest` 5, `SidecarMainTest` 3, `SidecarProcessTest` 1)와 기존 서버 테스트 전부.

- [ ] **Step 5: Verify the real process by hand**

Run (PowerShell, repo root):

```powershell
.\gradlew.bat :server:installDist --no-daemon
$lib = "C:\Projects\xGetSongs\server\build\install\xgs-server\lib"
$dir = Join-Path $env:TEMP "xgs-spike-appdata"
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = "java"
foreach ($a in @("-Dlogback.configurationFile=logback-sidecar.xml", "-cp", "$lib\*", "com.xgetsongs.server.sidecar.SidecarMainKt", "--app-data", $dir)) { $psi.ArgumentList.Add($a) }
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)
$line = $p.StandardOutput.ReadLine()
$line
$port, $token = ($line -split ' ')[1, 2]
curl.exe -s -H "X-XGS-Token: $token" "http://127.0.0.1:$port/tools"
$p.StandardInput.Close()
$p.WaitForExit(5000)
"exited=$($p.HasExited) code=$($p.ExitCode)"
```

Expected: `XGS-READY <port> <token>` 한 줄, `/tools`의 JSON(`ytDlp`, `ffmpeg`, `jsRuntime`), 마지막 줄 `exited=True code=0`. 시작 신호를 읽은 뒤 3초가 지나도 `/tools`가 응답해야 한다(서버가 곧바로 내려가면 `curl`이 연결하지 못한다).

- [ ] **Step 6: Commit**

```bash
git add server/build.gradle.kts server/src/main/kotlin/com/xgetsongs/server/sidecar server/src/main/resources/logback-sidecar.xml server/src/test/kotlin/com/xgetsongs/server/sidecar
git commit -m "feat(server): 사이드카로 실행하는 진입점을 추가한다"
```

---

### Task 4: Rust 크레이트와 시작 신호 해석

**Files:**
- Create: `sidecar-host/Cargo.toml`, `sidecar-host/.gitignore`, `sidecar-host/src/lib.rs`
- Create: `sidecar-host/src/handshake.rs`, `sidecar-host/src/config.rs`

**Interfaces:**
- Consumes: Task 1의 줄 형식 `XGS-READY <port> <token>`, Task 3의 메인 클래스 이름과 `logback-sidecar.xml`.
- Produces (Task 5·6이 쓴다):
  - `pub struct Handshake { pub port: u16, pub token: String }` (`Debug, Clone, PartialEq`), `pub fn parse_handshake(line: &str) -> Option<Handshake>`
  - `pub struct SidecarConfig { pub java: PathBuf, pub lib_dir: PathBuf, pub app_data: PathBuf }`, `SidecarConfig::from_env() -> Result<SidecarConfig, String>`, `SidecarConfig::from_lookup(get: impl Fn(&str) -> Option<OsString>) -> Result<SidecarConfig, String>`, `SidecarConfig::command(&self) -> std::process::Command`, `SidecarConfig::stderr_log(&self) -> PathBuf`

- [ ] **Step 1: Install Rust (사용자 허락 후)**

0a(UI 프로토타입)에서 이미 설치했으면(`rustc --version`이 나오면) 이 단계는 건너뛴다. 아니면 사용자에게 알린다: "Rust 툴체인(rustup, 약 300MB)을 `winget`으로 설치합니다. 허락하시나요?" 허락을 받으면:

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

- [ ] **Step 2: Create the crate skeleton**

`sidecar-host/Cargo.toml`:

```toml
[package]
name = "xgs-sidecar"
version = "0.1.0"
edition = "2021"
description = "Starts, talks to and stops the Kotlin sidecar of xGetSongs. No Tauri here, so its tests build fast."

[dependencies]
# Plain HTTP to 127.0.0.1 only: no TLS needed.
ureq = { version = "2", default-features = false }

[target.'cfg(windows)'.dependencies]
win32job = "2"
```

`sidecar-host/.gitignore`:

```
/target
```

`sidecar-host/src/lib.rs`:

```rust
pub mod config;
pub mod handshake;

pub use config::SidecarConfig;
pub use handshake::{parse_handshake, Handshake};
```

- [ ] **Step 3: Write the failing tests**

`sidecar-host/src/handshake.rs`:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_the_port_and_the_token() {
        assert_eq!(
            parse_handshake("XGS-READY 51234 abc_DEF-123"),
            Some(Handshake { port: 51234, token: "abc_DEF-123".to_string() })
        );
    }

    #[test]
    fn tolerates_the_line_ending() {
        assert_eq!(
            parse_handshake("XGS-READY 80 t\r\n"),
            Some(Handshake { port: 80, token: "t".to_string() })
        );
    }

    #[test]
    fn a_line_that_does_not_start_with_the_prefix_is_not_a_handshake() {
        assert_eq!(parse_handshake("12:00:00.000 INFO  LocalServer - XGS-READY 80 t"), None);
        assert_eq!(parse_handshake("[0.002s][warning][cds] something about sharing"), None);
        assert_eq!(parse_handshake(""), None);
    }

    #[test]
    fn a_bad_port_is_refused() {
        assert_eq!(parse_handshake("XGS-READY 0 t"), None);
        assert_eq!(parse_handshake("XGS-READY 70000 t"), None);
        assert_eq!(parse_handshake("XGS-READY abc t"), None);
    }

    #[test]
    fn missing_or_extra_fields_are_refused() {
        assert_eq!(parse_handshake("XGS-READY"), None);
        assert_eq!(parse_handshake("XGS-READY 80"), None);
        assert_eq!(parse_handshake("XGS-READY 80 t extra"), None);
    }
}
```

`sidecar-host/src/config.rs`:

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

    #[test]
    fn the_lib_folder_is_required() {
        let error = SidecarConfig::from_lookup(lookup(&[])).unwrap_err();
        assert!(error.contains("XGS_SIDECAR_LIB"), "{error}");
    }

    #[test]
    fn java_defaults_to_the_one_on_the_path_and_the_app_data_to_a_temp_folder() {
        let config = SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_LIB", r"C:\x\lib")])).unwrap();
        assert_eq!(config.java, PathBuf::from("java"));
        assert_eq!(config.app_data, std::env::temp_dir().join("xgs-spike-appdata"));
    }

    #[test]
    fn the_given_values_are_used() {
        let config = SidecarConfig::from_lookup(lookup(&[
            ("XGS_SIDECAR_LIB", r"C:\x\lib"),
            ("XGS_SIDECAR_JAVA", r"C:\jdk\bin\java.exe"),
            ("XGS_SIDECAR_APPDATA", r"C:\data"),
        ]))
        .unwrap();
        assert_eq!(config.lib_dir, PathBuf::from(r"C:\x\lib"));
        assert_eq!(config.java, PathBuf::from(r"C:\jdk\bin\java.exe"));
        assert_eq!(config.app_data, PathBuf::from(r"C:\data"));
    }

    #[test]
    fn the_command_runs_the_main_class_with_the_class_path_and_the_app_data_folder() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
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
            ]
        );
    }

    #[test]
    fn the_stderr_log_lives_in_the_app_data_folder() {
        let config = SidecarConfig {
            java: PathBuf::from("java"),
            lib_dir: PathBuf::from(r"C:\x\lib"),
            app_data: PathBuf::from(r"C:\data"),
        };
        assert_eq!(config.stderr_log(), PathBuf::from(r"C:\data\sidecar-stderr.log"));
    }
}
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `cd sidecar-host; cargo test`
Expected: FAIL — compile errors (`cannot find function parse_handshake`, `cannot find type SidecarConfig`). 첫 실행은 의존성을 내려받느라 1~2분 걸린다.

- [ ] **Step 5: Write minimal implementation**

`sidecar-host/src/handshake.rs` — 테스트 모듈 **위에** 붙인다:

```rust
/// The first word of the line the sidecar prints when its server listens.
pub const HANDSHAKE_PREFIX: &str = "XGS-READY";

#[derive(Debug, Clone, PartialEq)]
pub struct Handshake {
    pub port: u16,
    pub token: String,
}

/// Reads `XGS-READY <port> <token>`, exactly. Anything else (a log line, a JVM warning, a line that only contains the
/// prefix somewhere in the middle) is not a handshake.
pub fn parse_handshake(line: &str) -> Option<Handshake> {
    let mut parts = line.split_whitespace();
    if parts.next()? != HANDSHAKE_PREFIX {
        return None;
    }
    let port = parts.next()?.parse::<u16>().ok().filter(|port| *port != 0)?;
    let token = parts.next()?.to_string();
    if parts.next().is_some() {
        return None;
    }
    Some(Handshake { port, token })
}
```

`sidecar-host/src/config.rs` — 테스트 모듈 **위에** 붙인다:

```rust
use std::ffi::OsString;
use std::path::PathBuf;
use std::process::Command;

const MAIN_CLASS: &str = "com.xgetsongs.server.sidecar.SidecarMainKt";

/// How to start the sidecar: the `java` to run, the folder with its jars (`server/build/install/xgs-server/lib`), and the
/// app data folder it gets as `--app-data`.
#[derive(Debug)]
pub struct SidecarConfig {
    pub java: PathBuf,
    pub lib_dir: PathBuf,
    pub app_data: PathBuf,
}

impl SidecarConfig {
    pub fn from_env() -> Result<SidecarConfig, String> {
        SidecarConfig::from_lookup(|key| std::env::var_os(key))
    }

    /// The spike reads `XGS_SIDECAR_LIB` (required), `XGS_SIDECAR_JAVA` (default: `java` on the PATH) and
    /// `XGS_SIDECAR_APPDATA` (default: a temp folder, never the user's real app data).
    pub fn from_lookup(get: impl Fn(&str) -> Option<OsString>) -> Result<SidecarConfig, String> {
        let lib_dir = get("XGS_SIDECAR_LIB").map(PathBuf::from).ok_or_else(|| {
            "XGS_SIDECAR_LIB is not set (the lib folder of server/build/install/xgs-server)".to_string()
        })?;
        let java = get("XGS_SIDECAR_JAVA").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("java"));
        let app_data = get("XGS_SIDECAR_APPDATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| std::env::temp_dir().join("xgs-spike-appdata"));
        Ok(SidecarConfig { java, lib_dir, app_data })
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
        command
    }

    /// Where what the sidecar writes to stderr is kept (the shell has no console to show it).
    pub fn stderr_log(&self) -> PathBuf {
        self.app_data.join("sidecar-stderr.log")
    }
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd sidecar-host; cargo test`
Expected: PASS (handshake 5, config 5).

- [ ] **Step 7: Commit**

```bash
git add sidecar-host
git commit -m "feat(sidecar-host): 시작 신호를 해석하고 사이드카 실행 명령을 만든다"
```

---

### Task 5: 사이드카 시작·종료와 HTTP (Rust)

**Files:**
- Create: `sidecar-host/src/sidecar.rs`, `sidecar-host/src/http.rs`
- Modify: `sidecar-host/src/lib.rs`

**Interfaces:**
- Consumes: `Handshake`, `parse_handshake` (Task 4)
- Produces (Task 6이 쓴다):
  - `pub struct Sidecar { pub handshake: Handshake, .. }`
  - `Sidecar::spawn(command: Command, stderr_log: &Path, ready_timeout: Duration) -> Result<Sidecar, String>`
  - `Sidecar::shutdown(&mut self, grace: Duration) -> bool` — stdin을 닫고 `grace`까지 기다린다. 스스로 끝났으면 `true`, 아니면 프로세스 트리를 죽이고 `false`.
  - `pub fn http_get(handshake: &Handshake, path: &str) -> Result<String, String>` — `http://127.0.0.1:<port><path>`에 `X-XGS-Token` 헤더를 붙여 GET

- [ ] **Step 1: Write the failing tests**

`sidecar-host/src/lib.rs` 를 다음으로 바꾼다:

```rust
pub mod config;
pub mod handshake;
pub mod http;
pub mod sidecar;

pub use config::SidecarConfig;
pub use handshake::{parse_handshake, Handshake};
pub use http::http_get;
pub use sidecar::Sidecar;
```

`sidecar-host/src/http.rs`:

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;

    /// A one-shot server: answers the first request with [response] and returns the request it got, in lower case.
    fn serve_once(response: &'static str) -> (u16, std::thread::JoinHandle<String>) {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let handle = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut request = Vec::new();
            let mut buffer = [0u8; 512];
            while !request.windows(4).any(|window| window == b"\r\n\r\n") {
                let read = stream.read(&mut buffer).unwrap();
                if read == 0 {
                    break;
                }
                request.extend_from_slice(&buffer[..read]);
            }
            stream.write_all(response.as_bytes()).unwrap();
            String::from_utf8_lossy(&request).to_lowercase()
        });
        (port, handle)
    }

    #[test]
    fn sends_the_token_header_to_the_loopback_port_and_returns_the_body() {
        let (port, server) = serve_once("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");

        let body = http_get(&Handshake { port, token: "tok".to_string() }, "/tools").unwrap();

        assert_eq!(body, "ok");
        let request = server.join().unwrap();
        assert!(request.starts_with("get /tools http/1.1"), "{request}");
        assert!(request.contains("x-xgs-token: tok"), "{request}");
    }

    #[test]
    fn an_error_status_is_an_error() {
        let (port, server) =
            serve_once("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");

        let result = http_get(&Handshake { port, token: "wrong".to_string() }, "/tools");

        assert!(result.is_err());
        let message = result.unwrap_err();
        assert!(!message.contains("wrong"), "the token is not repeated in the error: {message}");
        server.join().unwrap();
    }
}
```

`sidecar-host/src/sidecar.rs`:

```rust
#[cfg(all(test, windows))]
mod tests {
    use super::*;
    use std::time::Duration;

    /// A stand-in for the sidecar: PowerShell running [script].
    fn powershell(script: &str) -> Command {
        let mut command = Command::new("powershell");
        command.args(["-NoProfile", "-NonInteractive", "-Command", script]);
        command
    }

    const READY: &str = "[Console]::Out.WriteLine('XGS-READY 4321 tok'); [Console]::Out.Flush();";
    const WAIT_FOR_STDIN_TO_END: &str = "[Console]::In.ReadToEnd() | Out-Null";

    fn log_path(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!("xgs-sidecar-test-{}-{}.log", std::process::id(), name))
    }

    #[test]
    fn reads_the_handshake_among_other_lines_and_ends_when_stdin_closes() {
        let script = format!("[Console]::Out.WriteLine('noise 1'); {READY} {WAIT_FOR_STDIN_TO_END}");

        let mut sidecar =
            Sidecar::spawn(powershell(&script), &log_path("noise"), Duration::from_secs(30)).unwrap();

        assert_eq!(sidecar.handshake, Handshake { port: 4321, token: "tok".to_string() });
        assert!(sidecar.shutdown(Duration::from_secs(10)), "it ends on its own when stdin closes");
    }

    #[test]
    fn keeps_reading_stdout_after_the_handshake_so_the_sidecar_never_blocks() {
        // 5000 lines of 100 characters are far more than a pipe holds: a sidecar whose stdout is not read blocks in WriteLine.
        let script = format!(
            "{READY} 1..5000 | ForEach-Object {{ [Console]::Out.WriteLine(('x' * 100)) }}; {WAIT_FOR_STDIN_TO_END}"
        );

        let mut sidecar =
            Sidecar::spawn(powershell(&script), &log_path("drain"), Duration::from_secs(30)).unwrap();
        std::thread::sleep(Duration::from_millis(2000));

        assert!(sidecar.shutdown(Duration::from_secs(10)), "it was never blocked on a full pipe");
    }

    #[test]
    fn a_sidecar_that_ends_before_the_handshake_is_an_error_at_once() {
        let started = std::time::Instant::now();

        let error = Sidecar::spawn(powershell("exit 3"), &log_path("early"), Duration::from_secs(60))
            .err()
            .unwrap();

        assert!(error.contains("ended before"), "{error}");
        assert!(started.elapsed() < Duration::from_secs(30), "it did not wait for the timeout");
    }

    #[test]
    fn a_sidecar_that_never_says_it_is_ready_is_killed_after_the_timeout() {
        let started = std::time::Instant::now();

        let error = Sidecar::spawn(
            powershell("Start-Sleep -Seconds 60"),
            &log_path("silent"),
            Duration::from_secs(2),
        )
        .err()
        .unwrap();

        assert!(error.contains("did not say"), "{error}");
        assert!(started.elapsed() < Duration::from_secs(30), "it gave up at the timeout");
    }

    #[test]
    fn a_sidecar_that_ignores_stdin_is_killed_after_the_grace_period() {
        let script = format!("{READY} Start-Sleep -Seconds 60");
        let mut sidecar =
            Sidecar::spawn(powershell(&script), &log_path("stubborn"), Duration::from_secs(30)).unwrap();

        assert!(!sidecar.shutdown(Duration::from_millis(500)), "it had to be killed");

        assert!(sidecar.child.try_wait().unwrap().is_some(), "and it is gone");
    }

    #[test]
    fn what_the_sidecar_writes_to_stderr_is_kept_in_the_log_file() {
        let path = log_path("stderr");
        let script = format!("[Console]::Error.WriteLine('oops from the sidecar'); {READY} {WAIT_FOR_STDIN_TO_END}");
        let mut sidecar = Sidecar::spawn(powershell(&script), &path, Duration::from_secs(30)).unwrap();
        assert!(sidecar.shutdown(Duration::from_secs(10)));

        let deadline = std::time::Instant::now() + Duration::from_secs(10);
        let mut content = String::new();
        while std::time::Instant::now() < deadline {
            content = std::fs::read_to_string(&path).unwrap_or_default();
            if content.contains("oops from the sidecar") {
                break;
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        assert!(content.contains("oops from the sidecar"), "{content}");
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn a_program_that_does_not_exist_is_an_error() {
        let error = Sidecar::spawn(
            Command::new("definitely-not-a-program-xgs"),
            &log_path("missing"),
            Duration::from_secs(5),
        )
        .err()
        .unwrap();

        assert!(error.contains("could not start"), "{error}");
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd sidecar-host; cargo test`
Expected: FAIL — compile errors (`cannot find function http_get`, `cannot find type Sidecar`).

- [ ] **Step 3: Write minimal implementation**

`sidecar-host/src/http.rs` — 테스트 모듈 **위에** 붙인다:

```rust
use crate::handshake::Handshake;
use std::time::Duration;

/// GET `http://127.0.0.1:<port><path>` with the sidecar's token. The body on success, a message without the token otherwise.
pub fn http_get(handshake: &Handshake, path: &str) -> Result<String, String> {
    let url = format!("http://127.0.0.1:{}{}", handshake.port, path);
    ureq::get(&url)
        .set("X-XGS-Token", &handshake.token)
        .timeout(Duration::from_secs(30))
        .call()
        .map_err(|error| error.to_string())?
        .into_string()
        .map_err(|error| error.to_string())
}
```

`sidecar-host/src/sidecar.rs` — 테스트 모듈 **위에** 붙인다:

```rust
use crate::handshake::{parse_handshake, Handshake};
use std::io::{BufRead, BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::mpsc::{self, Sender};
use std::time::{Duration, Instant};

/// Windows: do not open a console window for the child.
#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

/// A running sidecar. Dropping it closes its stdin, which asks the sidecar to stop on its own.
pub struct Sidecar {
    child: Child,
    stdin: Option<ChildStdin>,
    pub handshake: Handshake,
}

impl Sidecar {
    /// Starts [command] and waits up to [ready_timeout] for its handshake line. The child is killed when the line does
    /// not come. What the child writes to stderr goes to [stderr_log].
    pub fn spawn(mut command: Command, stderr_log: &Path, ready_timeout: Duration) -> Result<Sidecar, String> {
        command.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(CREATE_NO_WINDOW);
        }
        let mut child = command.spawn().map_err(|error| format!("could not start the sidecar: {error}"))?;
        let stdin = child.stdin.take();
        let stdout = child.stdout.take().expect("stdout is piped");
        let stderr = child.stderr.take().expect("stderr is piped");

        let log = stderr_log.to_path_buf();
        std::thread::spawn(move || copy_to_file(stderr, &log));
        let (sender, receiver) = mpsc::channel();
        std::thread::spawn(move || drain_stdout(stdout, sender));

        match receiver.recv_timeout(ready_timeout) {
            Ok(handshake) => Ok(Sidecar { child, stdin, handshake }),
            Err(error) => {
                kill_tree(child.id());
                let _ = child.kill();
                let _ = child.wait();
                Err(match error {
                    mpsc::RecvTimeoutError::Timeout => {
                        format!("the sidecar did not say it was ready within {} s", ready_timeout.as_secs())
                    }
                    mpsc::RecvTimeoutError::Disconnected => "the sidecar ended before it said it was ready".to_string(),
                })
            }
        }
    }

    /// Ends the sidecar: closing its stdin is the request to stop, and after [grace] it is killed together with whatever it
    /// started. Returns true when it ended on its own.
    pub fn shutdown(&mut self, grace: Duration) -> bool {
        drop(self.stdin.take());
        let deadline = Instant::now() + grace;
        loop {
            match self.child.try_wait() {
                Ok(Some(_)) => return true,
                Ok(None) if Instant::now() < deadline => std::thread::sleep(Duration::from_millis(25)),
                _ => break,
            }
        }
        kill_tree(self.child.id());
        let _ = self.child.kill();
        let _ = self.child.wait();
        false
    }
}

/// Reads stdout to its end: the handshake line goes to [sender], everything else is thrown away. Reading on after the
/// handshake matters: a child whose stdout pipe is full blocks in its next write.
fn drain_stdout(stdout: ChildStdout, sender: Sender<Handshake>) {
    let mut reader = BufReader::new(stdout);
    let mut line = Vec::new();
    loop {
        line.clear();
        match reader.read_until(b'\n', &mut line) {
            Ok(0) | Err(_) => break,
            Ok(_) => {
                if let Some(handshake) = parse_handshake(&String::from_utf8_lossy(&line)) {
                    let _ = sender.send(handshake);
                }
            }
        }
    }
}

/// Keeps what the child writes to stderr in a file (spike: it is not rotated). When the file cannot be made the data is
/// still read and dropped, so the child never blocks on a full pipe.
fn copy_to_file(mut source: impl Read, path: &PathBuf) {
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    match std::fs::File::create(path) {
        Ok(mut file) => {
            let _ = std::io::copy(&mut source, &mut file);
        }
        Err(_) => {
            let _ = std::io::copy(&mut source, &mut std::io::sink());
        }
    }
}

/// Ends [pid] and every process it started (`kill()` alone ends only the process itself).
#[cfg(windows)]
fn kill_tree(pid: u32) {
    use std::os::windows::process::CommandExt;
    let pid = pid.to_string();
    let _ = Command::new("taskkill")
        .args(["/T", "/F", "/PID", pid.as_str()])
        .creation_flags(CREATE_NO_WINDOW)
        .output();
}

#[cfg(not(windows))]
fn kill_tree(_pid: u32) {}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd sidecar-host; cargo test`
Expected: PASS (Task 4의 10개 + http 2 + sidecar 7). PowerShell 시작 때문에 1~2분 걸릴 수 있다.

- [ ] **Step 5: Commit**

```bash
git add sidecar-host
git commit -m "feat(sidecar-host): 사이드카를 시작하고 끝내고 HTTP로 부른다"
```

---

### Task 6: Job Object와 Tauri 셸 연결

**Files:**
- Create: `sidecar-host/src/job.rs`
- Modify: `sidecar-host/src/lib.rs`
- Modify: `shell/src-tauri/Cargo.toml`, `shell/src-tauri/src/lib.rs`, `shell/vite.config.ts`
- Create: `shell/spike.html`, `shell/src/spike.ts`, `shell/spike.conf.json`

`shell/index.html`과 `shell/src/main.ts`(0a의 UI 프로토타입)는 건드리지 않는다. 스파이크 화면은 `spike.html`로 따로 두고, 스파이크 빌드만 `spike.conf.json`으로 그 페이지를 연다.

**Interfaces:**
- Consumes: `Sidecar::spawn(command, stderr_log, timeout)`, `Sidecar::shutdown(grace)`, `Sidecar.handshake`, `http_get(&handshake, path)`, `SidecarConfig::from_env()`, `command()`, `stderr_log()`, `app_data` (Task 4·5)
- Produces: `xgs_sidecar::job::kill_children_when_this_process_ends() -> Result<(), String>`. Tauri 명령 `sidecar_probe`(`Result<Probe, String>`, `Probe { port, token, readyMs, toolsMs, tools }`, 준비 전에는 오류 문자열 `"starting"`)와 `spike_record(key, value)`. 셸은 앱 데이터 폴더(`XGS_SIDECAR_APPDATA`, 기본 `%TEMP%\xgs-spike-appdata`)에 결과 파일 넷을 남긴다: `spike-job.txt`(Job Object 결과), `spike-ready.json`(사이드카가 준비되면 `port`, `token`, `readyMs`), `spike-failed.txt`(시작에 실패하면 사유), `spike-record.txt`(화면이 보낸 `key=value` 줄: `waitedMs`, `toolsMs`, `directFetch`). 빌드 결과 `shell/src-tauri/target/release/shell.exe` — Task 7이 쓴다.

- [ ] **Step 1: Job Object 모듈**

`sidecar-host/src/lib.rs`에 `pub mod job;`를 `pub mod http;` 다음 줄에 추가한다.

`sidecar-host/src/job.rs`:

```rust
//! Ties the sidecar's life to this process: when this process ends, however it ends (closed, crashed, killed), Windows ends
//! the processes in the job too. That reaches the grandchildren (yt-dlp, ffmpeg) that closing stdin does not.
//!
//! There is no unit test: the effect is on the process that calls it. Task 7 (M3) checks it with a real force-kill.

#[cfg(windows)]
pub fn kill_children_when_this_process_ends() -> Result<(), String> {
    let job = win32job::Job::create().map_err(|error| error.to_string())?;
    let mut info = job.query_extended_limit_info().map_err(|error| error.to_string())?;
    info.limit_kill_on_job_close();
    job.set_extended_limit_info(&mut info).map_err(|error| error.to_string())?;
    job.assign_current_process().map_err(|error| error.to_string())?;
    // Dropping the job would close its handle, and with it end this very process. The handle has to live as long as the
    // process does; Windows closes it when the process ends.
    std::mem::forget(job);
    Ok(())
}

#[cfg(not(windows))]
pub fn kill_children_when_this_process_ends() -> Result<(), String> {
    Ok(())
}
```

Run: `cd sidecar-host; cargo test`
Expected: PASS (컴파일 오류 없음, 테스트 수는 그대로).

- [ ] **Step 2: `shell/`이 있는지 확인한다**

`shell/src-tauri/Cargo.toml`이 있으면 된다(0a 계획서의 Task 1이 만든다). 없으면 0a 계획서의 Task 1을 먼저 한다. `shell`의 Cargo 에디션은 2024이고 라이브러리 이름은 `shell_lib`다.

- [ ] **Step 3: 의존성 추가**

`shell/src-tauri/Cargo.toml`의 `[dependencies]` 아래에 한 줄을 더한다 (이미 있는 `tauri`, `tauri-plugin-opener`, `serde`, `serde_json`은 그대로 둔다):

```toml
xgs-sidecar = { path = "../../sidecar-host" }
```

- [ ] **Step 4: Rust 쪽을 연결한다**

`shell/src-tauri/src/lib.rs` 전체를 다음으로 바꾼다. 스캐폴더가 만든 `greet` 명령은 지우고, `tauri_plugin_opener::init()` 줄은 그대로 둔다:

```rust
use serde::Serialize;
use std::io::Write;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::{Duration, Instant};
use tauri::{Manager, RunEvent};
use xgs_sidecar::{http_get, job, Sidecar, SidecarConfig};

/// Where the sidecar is in its life. Starting takes seconds (a JVM), so it is not waited for on the UI thread.
enum Phase {
    Starting,
    Ready { sidecar: Sidecar, ready_ms: u128 },
    Failed(String),
    Stopped,
}

struct SidecarState(Mutex<Phase>);

/// The sidecar's app data folder, where the spike leaves its result files for the scripted checks of the plan.
struct SpikeFolder(PathBuf);

/// What the page shows. The token is here for the spike's own checks (the direct-fetch demo, a curl by hand) only: the real
/// bridge keeps it in Rust.
#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Probe {
    port: u16,
    token: String,
    ready_ms: u128,
    tools_ms: u128,
    tools: String,
}

/// Asks the sidecar for its tools. `Err("starting")` until the sidecar has said it is ready.
#[tauri::command]
fn sidecar_probe(state: tauri::State<'_, SidecarState>) -> Result<Probe, String> {
    let phase = state.0.lock().map_err(|_| "the state is poisoned".to_string())?;
    match &*phase {
        Phase::Starting => Err("starting".to_string()),
        Phase::Failed(reason) => Err(reason.clone()),
        Phase::Stopped => Err("stopped".to_string()),
        Phase::Ready { sidecar, ready_ms } => {
            let started = Instant::now();
            let tools = http_get(&sidecar.handshake, "/tools")?;
            Ok(Probe {
                port: sidecar.handshake.port,
                token: sidecar.handshake.token.clone(),
                ready_ms: *ready_ms,
                tools_ms: started.elapsed().as_millis(),
                tools,
            })
        }
    }
}

/// The page reports what it measured as one `key=value` line of `spike-record.txt`.
#[tauri::command]
fn spike_record(folder: tauri::State<'_, SpikeFolder>, key: String, value: String) -> Result<(), String> {
    let mut file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(folder.0.join("spike-record.txt"))
        .map_err(|error| error.to_string())?;
    writeln!(file, "{key}={}", value.replace(['\r', '\n'], " ")).map_err(|error| error.to_string())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // First of all: whatever happens to this process, the sidecar and its children go with it.
    let job_result = job::kill_children_when_this_process_ends();

    let folder = SidecarConfig::from_env()
        .map(|config| config.app_data)
        .unwrap_or_else(|_| std::env::temp_dir().join("xgs-spike-appdata"));
    let _ = std::fs::create_dir_all(&folder);
    // The results of an earlier run must not be mistaken for this one.
    for stale in ["spike-ready.json", "spike-failed.txt", "spike-record.txt"] {
        let _ = std::fs::remove_file(folder.join(stale));
    }
    let _ = std::fs::write(
        folder.join("spike-job.txt"),
        match &job_result {
            Ok(()) => "ok".to_string(),
            Err(reason) => format!("failed: {reason}"),
        },
    );

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(SidecarState(Mutex::new(Phase::Starting)))
        .manage(SpikeFolder(folder))
        .setup(|app| {
            let handle = app.handle().clone();
            std::thread::spawn(move || {
                let started = Instant::now();
                let result = SidecarConfig::from_env().and_then(|config| {
                    Sidecar::spawn(config.command(), &config.stderr_log(), Duration::from_secs(60))
                });
                let folder = handle.state::<SpikeFolder>().0.clone();
                let phase = match result {
                    Ok(sidecar) => {
                        let ready_ms = started.elapsed().as_millis();
                        let ready = format!(
                            r#"{{"port":{},"token":"{}","readyMs":{}}}"#,
                            sidecar.handshake.port, sidecar.handshake.token, ready_ms
                        );
                        let _ = std::fs::write(folder.join("spike-ready.json"), ready);
                        Phase::Ready { sidecar, ready_ms }
                    }
                    Err(reason) => {
                        let _ = std::fs::write(folder.join("spike-failed.txt"), &reason);
                        Phase::Failed(reason)
                    }
                };
                if let Ok(mut state) = handle.state::<SidecarState>().0.lock() {
                    *state = phase;
                }
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![sidecar_probe, spike_record])
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    app.run(|handle, event| {
        if let RunEvent::Exit = event {
            let state = handle.state::<SidecarState>();
            let mut phase = state.0.lock().unwrap();
            if let Phase::Ready { sidecar, .. } = &mut *phase {
                sidecar.shutdown(Duration::from_secs(3));
            }
            *phase = Phase::Stopped;
        }
    });
}
```

- [ ] **Step 5: 스파이크 화면과 빌드 설정**

`shell/spike.html`:

```html
<!doctype html>
<html lang="ko">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>xGetSongs 사이드카 스파이크</title>
    <script type="module" src="/src/spike.ts" defer></script>
  </head>
  <body>
    <main>
      <h1>사이드카 스파이크</h1>
      <p id="status">사이드카를 기다리는 중…</p>
      <pre id="out"></pre>
      <button id="direct" disabled>웹뷰에서 서버로 직접 fetch (막혀야 정상)</button>
      <pre id="direct-out"></pre>
    </main>
  </body>
</html>
```

`shell/src/spike.ts`:

```ts
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
```

`shell/spike.conf.json`:

```json
{
  "app": {
    "windows": [
      {
        "title": "xGetSongs 사이드카 스파이크",
        "width": 1000,
        "height": 760,
        "url": "spike.html"
      }
    ]
  }
}
```

`shell/vite.config.ts`: 맨 위 import에 `import { fileURLToPath } from "node:url";`를 더하고, `defineConfig(() => ({ ... }))` 안(`clearScreen`과 같은 단계)에 `build` 키를 더해서 빌드가 두 페이지를 만들게 한다(프로토타입 `index.html`과 스파이크 `spike.html`):

```ts
  build: {
    rollupOptions: {
      input: {
        main: fileURLToPath(new URL("./index.html", import.meta.url)),
        spike: fileURLToPath(new URL("./spike.html", import.meta.url)),
      },
    },
  },
```

(Vite 8이 이 옵션을 다른 이름으로 부르면 경고가 나오니 그 이름에 맞춘다.)

- [ ] **Step 6: 컴파일을 확인한다**

```powershell
cd shell
npm run build
cd src-tauri
cargo check
```

Expected: `npm run build`(tsc + vite)가 오류 없이 끝나고 `dist/`에 `index.html`과 `spike.html`이 모두 생기며, `cargo check`가 통과한다. Rust 컴파일 오류가 나면 Global Constraints대로 고친다.

- [ ] **Step 7: 스파이크 빌드를 만들고 한 번 돌려 본다**

```powershell
cd C:\Projects\xGetSongs
.\gradlew.bat :server:installDist --no-daemon
$env:XGS_SIDECAR_LIB = "C:\Projects\xGetSongs\server\build\install\xgs-server\lib"
cd shell
npm run tauri build -- --no-bundle --config spike.conf.json
.\src-tauri\target\release\shell.exe
```

Expected: 제목 `xGetSongs 사이드카 스파이크`인 창이 뜨고 수 초 뒤 "사이드카 준비됨"과 JSON이 보인다. `%TEMP%\xgs-spike-appdata\`에 `spike-job.txt`(`ok`), `spike-ready.json`, `spike-record.txt`(`waitedMs=`, `toolsMs=`, `directFetch=blocked: …`)가 생긴다. 창을 닫은 뒤 `Get-CimInstance Win32_Process -Filter "Name='java.exe'" | Where-Object { $_.CommandLine -like '*SidecarMainKt*' }`이 아무것도 돌려주지 않는다.

- [ ] **Step 8: Commit**

```bash
git add sidecar-host shell
git commit -m "feat(shell): Tauri 셸이 Kotlin 사이드카를 띄우고 /tools를 부르는 스파이크 화면을 추가한다"
```

---

### Task 7: 측정과 관문 보고서

**Files:**
- Create: `docs/superpowers/specs/2026-10-10-tauri-sidecar-spike-report.md`

**Interfaces:**
- Consumes: Task 3의 `installDist` 결과, Task 6의 `shell.exe`(`--config spike.conf.json`으로 빌드, 환경 변수 `XGS_SIDECAR_LIB` 설정)과 그것이 남기는 결과 파일(`spike-ready.json`, `spike-record.txt`, `spike-job.txt`)
- Produces: 관문 판정(설계 §5.1의 M1~M4와 참고 지표). 이후 단계의 계획서가 이 판정을 근거로 쓴다.

- [ ] **Step 1: 도우미를 정의한다**

셸이 `spike-ready.json`과 `spike-record.txt`에 측정값을 남기므로 화면을 눈으로 읽지 않고 스크립트로 잰다. 새 PowerShell에서:

```powershell
$app = Join-Path $env:TEMP "xgs-spike-appdata"
$shell = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe"
$env:XGS_SIDECAR_LIB = "C:\Projects\xGetSongs\server\build\install\xgs-server\lib"
function Get-Sidecar { Get-CimInstance Win32_Process -Filter "Name='java.exe'" | Where-Object { $_.CommandLine -like '*SidecarMainKt*' } }

# Starts the shell and waits until its page has reported everything; returns what was measured.
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
    [pscustomobject]@{
        Process = $process
        WindowAfterMs = $windowAfterMs
        Record = $values
        Ready = (Get-Content "$app\spike-ready.json" -ErrorAction SilentlyContinue | ConvertFrom-Json)
    }
}

# Closes the window like a user does; returns the milliseconds until the shell and the sidecar are both gone.
function Close-Spike($process) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [void]$process.CloseMainWindow()
    while ($watch.Elapsed.TotalSeconds -lt 15 -and ((Get-Sidecar) -or -not $process.HasExited)) { Start-Sleep -Milliseconds 50 }
    [int]$watch.Elapsed.TotalMilliseconds
}

Get-Sidecar
```

Expected: 마지막 `Get-Sidecar`가 아무것도 돌려주지 않는다(이전 실행의 잔재가 없다). 나오면 `Stop-Process -Id <ProcessId> -Force`로 정리한다.

- [ ] **Step 2: M1·M2·M4와 시간을 3회 잰다**

```powershell
$runs = 1..3 | ForEach-Object {
    $run = Start-Spike
    $originCode = curl.exe -s -o NUL -w "%{http_code}" -H "Origin: http://tauri.localhost" -H "X-XGS-Token: $($run.Ready.token)" "http://127.0.0.1:$($run.Ready.port)/tools"
    $closedMs = Close-Spike $run.Process
    [pscustomobject]@{
        Run = $_; WindowAfterMs = $run.WindowAfterMs; ReadyMs = $run.Ready.readyMs
        WaitedMs = [int]$run.Record.waitedMs; ToolsMs = [int]$run.Record.toolsMs
        DirectFetch = $run.Record.directFetch; OriginCurl = $originCode
        ClosedMs = $closedMs; SidecarLeft = @(Get-Sidecar).Count; JobObject = (Get-Content "$app\spike-job.txt")
    }
}
$runs | Format-List
"median waitedMs: " + (($runs.WaitedMs | Sort-Object)[1])
```

Expected: 세 번 모두
- **M1:** `ToolsMs`가 있다(화면이 Rust의 `/tools` 호출이 성공한 뒤에만 이 줄을 남긴다).
- **M2:** `ClosedMs`가 3000 이하이고 `SidecarLeft`가 0이다.
- **M4:** `DirectFetch`가 `blocked: …`로 시작하고 `OriginCurl`이 `403`이다. (`reached the server`가 나오면 가드가 뚫린 것이니 중단하고 원인을 조사한다.)
- `JobObject`가 `ok`다.

`WaitedMs` 중앙값이 10000 이하인지도 적는다(악화 한도). `ReadyMs`, `ToolsMs`, `WindowAfterMs`(프로세스 시작에서 창이 보일 때까지)는 기록만 한다.

비교용으로(선택) Compose 앱의 같은 구간을 잰다: `.\run.bat`으로 띄우고 `log\xgetsongs.log`에서 `시작 정보` 줄과 `내장 서버 시작` 줄의 시각 차이를 적는다.

- [ ] **Step 3: M3 — 강제 종료와 자손**

자손(`yt-dlp.exe`)을 만들려면 사이드카가 yt-dlp를 찾을 수 있어야 한다. 사이드카는 `<앱 데이터>\bin`을 먼저 보므로 스파이크용 임시 폴더에 복사본을 둔다(사용자의 원본 파일은 건드리지 않는다). 사용자의 `%APPDATA%\xGetSongs\bin\yt-dlp.exe`나 PATH의 `yt-dlp`를 쓰고, 둘 다 없으면 이 확인을 건너뛰고 보고서에 "자손 확인 못 함"으로 적는다.

영상 하나의 조회(`yt-dlp -J`)는 몇 초 동안 `yt-dlp.exe`를 자손으로 돌리므로, 그 사이에 셸을 강제 종료한다:

```powershell
New-Item -ItemType Directory -Force "$app\bin" | Out-Null
$ytdlp = @("$env:APPDATA\xGetSongs\bin\yt-dlp.exe", (Get-Command yt-dlp -ErrorAction SilentlyContinue).Source) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
Copy-Item $ytdlp "$app\bin\yt-dlp.exe" -Force

$run = Start-Spike
$job = Start-Job -ScriptBlock {
    param($port, $token)
    Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$port/resolve" -Headers @{ "X-XGS-Token" = $token } -ContentType "application/json" -Body (@{ input = "https://www.youtube.com/watch?v=dQw4w9WgXcQ" } | ConvertTo-Json)
} -ArgumentList $run.Ready.port, $run.Ready.token
$seen = $false
$watch = [Diagnostics.Stopwatch]::StartNew()
while ($watch.Elapsed.TotalSeconds -lt 15 -and -not $seen) { Start-Sleep -Milliseconds 100; $seen = [bool](Get-Process yt-dlp -ErrorAction SilentlyContinue) }
"yt-dlp seen as a descendant: $seen (after $([int]$watch.Elapsed.TotalMilliseconds) ms)"
Stop-Process -Id $run.Process.Id -Force
Start-Sleep -Seconds 5
"5 s after the force kill: sidecar left = $(@(Get-Sidecar).Count), yt-dlp left = $(@(Get-Process yt-dlp -ErrorAction SilentlyContinue).Count)"
Remove-Job $job -Force
```

Expected(M3 통과): `yt-dlp seen as a descendant: True`이고 5초 뒤 `sidecar left = 0, yt-dlp left = 0`이다. `seen`이 `False`이면 조회가 너무 빨리 끝났거나 yt-dlp를 못 찾은 것이니 `$app\sidecar-stderr.log`를 보고 다시 한다. 사이드카만 사라지고 `yt-dlp`가 남으면 Job Object가 동작하지 않은 것이다(`$app\spike-job.txt`의 결과도 본다). 이 확인은 셸이 stdin 닫기 신호도 못 보낼 만큼 갑자기 죽는 경우를 다룬다: 사이드카 자신은 stdin 닫힘으로 끝나지만 자손은 Job Object만이 정리한다.

- [ ] **Step 4: 크기를 잰다**

```powershell
cd C:\Projects\xGetSongs
"{0:N1} MB  sidecar lib (installDist)" -f ((Get-ChildItem server\build\install\xgs-server\lib | Measure-Object Length -Sum).Sum / 1MB)
"{0:N1} MB  tauri shell.exe" -f ((Get-Item shell\src-tauri\target\release\shell.exe).Length / 1MB)
.\gradlew.bat :app:packageUberJarForCurrentOS --no-daemon
Get-ChildItem app\build\compose\jars\*.jar | ForEach-Object { "{0:N1} MB  compose uber jar ({1})" -f ($_.Length / 1MB), $_.Name }
Get-ChildItem "$env:USERPROFILE\.gradle\caches\modules-2\files-2.1\org.openjfx" -Recurse -Filter "javafx-web-*-win.jar" | ForEach-Object { "{0:N1} MB  javafx-web (both include it)" -f ($_.Length / 1MB) }
```

Expected: 네 숫자가 나온다. JRE 크기는 두 방식에 공통이라 이 비교에 넣지 않는다(JDK 21의 jpackage가 없어서 앱 이미지는 5단계에서 잰다). 비교의 의미: (사이드카 lib + 셸 exe) 대 Compose 업 jar. JavaFX 웹뷰가 어느 쪽에서도 빠지지 않는다는 점도 보고서에 적는다.

- [ ] **Step 5: 보고서를 쓴다**

`docs/superpowers/specs/2026-10-10-tauri-sidecar-spike-report.md`에 실제 측정값으로 채워서 쓴다. 구조는 다음과 같다(숫자와 판정은 위 단계의 결과를 그대로 적는다):

```markdown
# Tauri 사이드카 스파이크 보고서

작성일: 2026-10-10 (측정한 날로 고친다)
설계: [2026-10-10-tauri-kotlin-sidecar-design.md](2026-10-10-tauri-kotlin-sidecar-design.md)
계획: [2026-10-10-tauri-sidecar-spike.md](../plans/2026-10-10-tauri-sidecar-spike.md)

## 필수 관문

| 번호 | 조건 | 결과 | 근거 (측정값, 걸린 시간) |
|---|---|---|---|
| M1 | 신호를 읽고 Rust에서 `/tools` 200 | 통과/실패 | |
| M2 | 창 닫기 → 3초 안에 java 없음 | 통과/실패 | |
| M3 | 강제 종료 → 5초 안에 java와 yt-dlp 없음 | 통과/실패/자손 확인 못 함 | |
| M4 | 웹뷰 직접 fetch가 막힘 | 통과/실패 | |

## 악화 한도와 기록

동기가 UI 품질이므로 개선이 아니라 나빠지지 않는 선만 본다(설계 §5.1).

| 항목 | 값 | 한도 | 판정 |
|---|---|---|---|
| waitedMs (3회, 중앙값) | | 10초 이하 | 통과/초과 |
| readyMs (3회, 중앙값) | | 기록만 | |
| toolsMs (3회, 중앙값) | | 기록만 | |
| Compose 앱: 시작 정보 → 내장 서버 시작 (선택) | | 기록만 | |
| 사이드카 lib 합계 | | 기록만 | |
| Tauri shell.exe | | 기록만 | |
| Compose 업 jar | | 기록만 | |
| javafx-web jar (양쪽 공통) | | 기록만 | |
| Job Object 생성 성공 여부 | | 기록만 | |

## 막힌 점과 고친 것

(계획과 달랐던 점: 컴파일 오류, API 차이, 스캐폴더가 만든 파일의 차이 등. 없으면 "없음".)

## 판정

(통과 / 재시도 후 통과 / 중단 중 하나를 쓰고 이유를 적는다. M1~M4가 모두 통과하고 `waitedMs`가 한도 안이면 통과다. `waitedMs`가 한도를 넘으면 대응(시작 중 화면, 사이드카 미리 띄우기)을 적고 사용자가 진행 여부를 정한다. 판정이 통과이면 다음으로 쓸 계획서는 설계 §5의 1단계 "사이드카 완성"이다. UI 관문(U1~U3)의 판정은 0a 보고서에 있다.)
```

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-10-10-tauri-sidecar-spike-report.md
git commit -m "문서: Tauri 사이드카 스파이크의 측정 결과와 관문 판정"
```

---

## Self-Review

**1. Spec coverage** (설계 문서 대조)

- §2 사이드카 + HTTP/SSE 유지 → Task 3·5. 시작 신호 → Task 1·4. 종료 신호(stdin) → Task 2·3·5. 프로세스 수명(Job Object, `taskkill`) → Task 5(`kill_tree`)·6(`job.rs`)·7(M3). `--app-data` 필수·절대 경로 → Task 3(`SidecarArgs`). `externalBin` 미사용(번들 JRE는 5단계, 스파이크는 환경 변수의 `java`) → Task 4(`SidecarConfig`).
- §2.2 Rust 브리지의 필요성 증명 → Task 6의 직접 fetch 버튼, Task 7의 M4. (브리지 자체는 2단계.)
- §5.1 필수 M1~M4와 참고 지표 → Task 7의 Step 2~6과 보고서 표가 그대로 대응한다.
- 이 계획 밖(의도적): `Diagnostics` 이동·번들 JRE(1단계), 브리지 명령 8개와 SSE(2단계), 프런트엔드·`shared` 재사용(3단계). 이 계획의 사이드카는 stderr 로그만 쓴다.

**2. Placeholder scan:** 코드 단계는 모두 실제 코드를 담았다. Task 7의 보고서 표의 빈 칸은 측정 중에 채우는 기록 칸이다. Task 6 Step 4의 `.plugin(...)` 줄은 스캐폴더 결과에 따라 달라질 수 있어 "읽고 맞춘다"로 명시했다.

**3. Type consistency:** `Handshake.line(port, token)`(Kotlin)과 `parse_handshake`(Rust)는 `XGS-READY <port> <token>`으로 같다. `Sidecar::spawn(command, stderr_log, ready_timeout)` 시그니처는 Task 5의 정의, 테스트, Task 6의 호출이 같다. `SidecarConfig::{from_env, from_lookup, command, stderr_log}`는 Task 4에서 정의하고 Task 6에서 `from_env`, `command`, `stderr_log`를 쓴다. 메인 클래스 `com.xgetsongs.server.sidecar.SidecarMainKt`와 설치 경로 `server/build/install/xgs-server/lib`는 Task 3·4·6·7에서 같다.

**4. Review Focus:** 다섯 항목 모두 위의 Review Focus 절에 적은 Task의 테스트나 측정 단계에 대응한다.
