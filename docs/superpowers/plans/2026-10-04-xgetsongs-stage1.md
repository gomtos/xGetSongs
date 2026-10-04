# xGetSongs Stage 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build stage 1 of xGetSongs: a Windows desktop app that turns a YouTube playlist ID or a single video URL into `NNN 가수 - 제목.mp3` files.

**Architecture:** Kotlin Multiplatform. The app always talks to a Ktor server (`server`), which the desktop app starts on `127.0.0.1`; the server drives `yt-dlp` and `ffmpeg` through the `engine`. Pure logic (input classification, title parsing, file names, API models) lives in `shared` so it can later compile for Wasm. Moving to the web later means deploying the same server and adding a `wasmJs` target (design spec §10).

**Tech Stack:** Kotlin 2.4.20, Gradle 9.5.1 (Kotlin DSL), JDK 21, Compose Multiplatform 1.12.1 (desktop target), Ktor 3.6.0 (server, SSE, client), kotlinx.coroutines 1.11.0, kotlinx.serialization 1.11.0, yt-dlp + ffmpeg (external processes), Node 22+ or Deno 2.3+ (needed by yt-dlp for YouTube).

Design spec: `docs/superpowers/specs/2026-10-04-xgetsongs-design.md`. Task 20 brings the spec in line with decisions made while planning.

## Global Constraints

Every task's requirements implicitly include this section.

- **Package root:** `com.xgetsongs`. Modules: `:shared` (Kotlin Multiplatform, `jvm()` target only for now), `:engine` (kotlin-jvm), `:server` (kotlin-jvm), `:app` (Kotlin Multiplatform with `jvm("desktop")`). Dependency direction: `app → shared`, `app (desktop only) → server → engine → shared`.
- **Pinned versions** (all of them were resolved and built together): Kotlin 2.4.20, Gradle 9.5.1, Compose Multiplatform 1.12.1, Compose Material3 1.9.0, Ktor 3.6.0, kotlinx.coroutines 1.11.0, kotlinx.serialization 1.11.0, logback 1.5.38. They live in `gradle/libs.versions.toml`; do not scatter version numbers elsewhere.
- **`shared` must stay pure Kotlin.** No `java.*`/`javax.*` in `shared/src/commonMain` (it must later compile for `wasmJs`). The Gradle task `checkCommonPurity` enforces it and runs before `jvmTest`/`check`. Do not use JVM-only calls such as `String.format` in any `common*` source set.
- **File name format:** `{rank as 3 digits} {artist} - {title}.mp3`. Rank is the playlist position, `1..999` (more than 999 entries: keep the first 999 and warn). Artist and title keep the original text; only quotes around the title and noise such as `Official MV` are removed. Windows-forbidden characters `\ / : * ? " < > |` become the full-width look-alikes `＼ ／ ： ＊ ？ ＂ ＜ ＞ ｜`; control characters are dropped; trailing spaces and dots are dropped. The name without extension is at most 180 UTF-16 units, the artist at most 80; truncation ends with `…` and never splits a surrogate pair.
- **Title parsing order:** title patterns → yt-dlp `artist`/`track` metadata → channel name (marked low confidence, shown with ⚠). Titles from `… - Topic` channels are never split.
- **yt-dlp is always run with an argument list, never a shell string.** The URL is rebuilt from the validated ID, comes last and follows `--`. Every command built by `YtDlpCommands` starts with `--ignore-config`, `--no-warnings`, `--encoding utf-8` (the `--version` and `-U` calls of `DefaultToolManager` pass only the flags they need). Downloads write `<workDir>/<videoId>.mp3`; the engine, not yt-dlp, decides the final file name.
- **Server security (design spec §10.2):** the embedded server binds `127.0.0.1` on a random port, requires the per-start random token in the `X-XGS-Token` header, rejects any request that carries an `Origin` header, and accepts only loopback `Host` values. `outputDir` is honoured only in `LOCAL` mode; `HOSTED` mode rejects it.
- **Job behaviour:** default concurrency 2, clamped to 1..4. Transient failures are retried twice with delays of 2 s and 4 s. Unavailable videos are skipped (their rank stays unused). A fatal error (disk full, no permission, missing yt-dlp) aborts the whole job. Every job works in its own temp folder, which is removed when the job ends or is cancelled.
- **UI language:** user-facing strings are Korean. Code, identifiers, comments and commit messages are English.
- **Commands are for PowerShell** from the repository root (`.\gradlew.bat …`). In Git Bash use `./gradlew …`.
- **Commits:** one commit per task, Conventional Commits style, ending with the line `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

**Counting tests.** Gradle prints only `BUILD SUCCESSFUL`. To see how many tests ran, read the XML reports (replace the folder for the module):

```powershell
Get-ChildItem shared\build\test-results\jvmTest\*.xml | ForEach-Object { (Select-String -Path $_.FullName -Pattern '<testsuite ').Line -replace ' timestamp.*','' }
```

Report folders: `shared\build\test-results\jvmTest`, `engine\build\test-results\test`, `server\build\test-results\test`, `app\build\test-results\desktopTest`.

## How this plan was verified

The code in every task was written and run in a scratch copy of the project before it was put into this plan: `.\gradlew.bat check` passed with **194 tests, 0 failures** (shared 58, engine 75, server 30, app 31), and the desktop window was started once and looked at (Korean text renders; the tool panel showed this PC's real state). The code blocks below are the files from that run.

Then the plan itself was replayed in an empty directory, task by task in the order below: for every task the tests alone failed to build, and with the implementation they passed with exactly the test counts stated in that task. (The replay found one timing-dependent concurrency test; it now holds the downloads at a gate instead of relying on virtual time, and the engine and server suites were run repeatedly without a failure.)

**Not verified yet, and the first thing that can break:**

- Real `yt-dlp` output. `yt-dlp` was not installed, so the JSON shapes in `YtDlpResolverTest` (flat playlist entries, `[Private video]`/`[Deleted video]` titles, `availability` values) come from knowledge of yt-dlp, not from a capture. Task 19 captures real output and fixes any difference.
- A real download and mp3 conversion (Task 19, `integrationTest`).
- `packageMsi` installer packaging (not part of this plan).

## Pitfalls already found (do not rediscover them)

- Gradle 9 refuses to run `gradle wrapper` without a `settings.gradle.kts`; create a minimal one first (Task 1).
- With only a `jvm()` target, `compileKotlinMetadata` does **not** reject `java.*` in `commonMain`; that is why `checkCommonPurity` exists.
- `kotlinx-coroutines-test`: `advanceUntilIdle()` does not run tasks of `backgroundScope`; use `runCurrent()`.
- Ktor's in-memory `testApplication` cannot stream a live (still open) SSE response; tests that need that run against the real `LocalServer` (Task 14). Finite SSE streams work fine in `testApplication`.
- The Ktor SSE client wraps exceptions thrown inside its `sse { }` block in `SSEClientException`; `HttpXgsApi.events` therefore remembers the server's error and throws `ApiError` after the block.
- In Ktor 3 the pipeline property `call` needs `import io.ktor.server.application.call`, and the pipeline call type is `PipelineCall`.
- `compose.runtime`/`compose.foundation`/`compose.ui`/`compose.material3` accessors are deprecated; the catalog uses the direct artifacts.
- yt-dlp needs a JavaScript runtime for YouTube since late 2025: Deno 2.3+ is picked up automatically, Node 22+ only with `--js-runtimes node:<path>`. The commands add that flag from the discovered runtime.

## File Structure

```
gradlew, gradlew.bat, gradle/wrapper/*        Gradle 9.5.1 wrapper (generated in Task 1)
settings.gradle.kts, build.gradle.kts, gradle.properties, .gitignore, README.md
gradle/libs.versions.toml                     every version and library, once

shared/                                       pure Kotlin (commonMain), jvm target for tests
  build.gradle.kts
  src/commonMain/kotlin/com/xgetsongs/shared/
    input/InputClassifier.kt                  user input -> playlist/video ID or a rejection
    filename/FilenameFormatter.kt             "NNN artist - title.mp3", Windows-safe
    title/TitleParser.kt                      video title -> (artist, title, confidence)
    api/ApiModels.kt                          JSON DTOs shared by server and clients, ApiJson
  src/commonTest/kotlin/com/xgetsongs/shared/{input,filename,title,api}/*Test.kt

engine/                                       JVM: everything that touches yt-dlp, ffmpeg and files
  build.gradle.kts
  src/main/kotlin/com/xgetsongs/engine/
    Services.kt                               Resolver, DownloadService, ToolManager, JobHandle, ...
    process/ProcessRunner.kt                  ProcessRunner + SystemProcessRunner (kills process trees)
    tools/ToolLocator.kt                      ToolPaths, ToolPathProvider, ToolLocator
    tools/DefaultToolManager.kt               tool versions, yt-dlp install/update
    ytdlp/YtDlpCommands.kt                    every yt-dlp command line
    ytdlp/ProgressParser.kt                   progress lines -> ProgressUpdate
    ytdlp/ErrorClassifier.kt                  stderr -> Failure(kind, message)
    ytdlp/YtDlpResolver.kt                    Resolver + VideoMetadataSource
    output/OutputSink.kt, LocalFolderSink.kt  where finished files go
    job/ItemDownloader.kt                     one video via yt-dlp
    job/DefaultDownloadService.kt             concurrency, retries, cancel, events
  src/test/kotlin/com/xgetsongs/engine/...    unit tests, testutil/Fakes.kt, integration/

server/                                       JVM: Ktor
  build.gradle.kts
  src/main/kotlin/com/xgetsongs/server/
    ServerConfig.kt                           ServerMode, ServerConfig, Services
    Guard.kt                                  token / Origin / Host protection
    Registries.kt                             ResolveCache, JobRegistry
    Application.kt                            routes
    LocalServer.kt                            createServices(), LocalServer (Netty on 127.0.0.1)
  src/test/kotlin/com/xgetsongs/server/...

app/                                          Compose Multiplatform, desktop target
  build.gradle.kts
  src/commonMain/kotlin/com/xgetsongs/app/
    api/XgsApi.kt                             XgsApi, HttpXgsApi, ApiError, configureXgs
    state/UiState.kt, Labels.kt, AppStateHolder.kt
    ui/App.kt, ToolsPanel.kt, InputPanel.kt, OptionsPanel.kt, PreviewList.kt, ActionBar.kt
  src/desktopMain/kotlin/com/xgetsongs/app/Main.kt, FolderPicker.kt
  src/commonTest/..., src/desktopTest/...
```

---

### Task 1: Gradle scaffold and `InputClassifier`

**Files:**
- Create (generated): `gradlew`, `gradlew.bat`, `gradle/wrapper/gradle-wrapper.jar`, `gradle/wrapper/gradle-wrapper.properties`
- Create: `settings.gradle.kts`, `build.gradle.kts`, `gradle.properties`, `.gitignore`, `gradle/libs.versions.toml`
- Create: `shared/build.gradle.kts`
- Create: `shared/src/commonMain/kotlin/com/xgetsongs/shared/input/InputClassifier.kt`
- Test: `shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt`

**Interfaces:**
- Consumes: nothing.
- Produces: `sealed interface ParsedInput { val canonicalUrl: String }` with `data class Playlist(val id: String, val alsoVideoId: String? = null)` and `data class Video(val id: String)`; `sealed interface ClassifyResult` with `Ok(val input: ParsedInput)` and `Rejected(val reason: RejectReason)`; `enum class RejectReason(val message: String)` = `EMPTY`, `UNSUPPORTED_HOST`, `MIX_PLAYLIST`, `UNRECOGNIZED`; `object InputClassifier { fun classify(raw: String): ClassifyResult }` (all in package `com.xgetsongs.shared.input`).

- [ ] **Step 1: Generate the Gradle wrapper**

Gradle 9 needs a settings file even to create a wrapper. Create a minimal one, then run any installed Gradle (the machine this was built on has 9.5.1 cached):

```powershell
Set-Content -Path settings.gradle.kts -Value 'rootProject.name = "xGetSongs"' -Encoding utf8
$gradle = (Get-ChildItem "$env:USERPROFILE\.gradle\wrapper\dists\gradle-9.5.1-all\*\gradle-9.5.1\bin\gradle.bat").FullName
& $gradle wrapper --gradle-version 9.5.1 --distribution-type all
```

If that Gradle is not cached, use any Gradle 8+ (`gradle wrapper --gradle-version 9.5.1 --distribution-type all`), installing one with `winget install Gradle.Gradle` if needed.

Expected: `BUILD SUCCESSFUL`, and `gradlew`, `gradlew.bat`, `gradle\wrapper\gradle-wrapper.jar`, `gradle\wrapper\gradle-wrapper.properties` exist. The properties file contains `distributionUrl=https\://services.gradle.org/distributions/gradle-9.5.1-all.zip`.

- [ ] **Step 2: Write the build files**

`settings.gradle.kts` (replace the minimal one):

```kotlin
rootProject.name = "xGetSongs"

pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
        google()
    }
}

dependencyResolutionManagement {
    repositories {
        mavenCentral()
        google()
    }
}

include(":shared")
```

`build.gradle.kts`:

```kotlin
plugins {
    alias(libs.plugins.kotlin.multiplatform) apply false
    alias(libs.plugins.kotlin.jvm) apply false
    alias(libs.plugins.kotlin.serialization) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.compose.multiplatform) apply false
}
```

`gradle.properties`:

```properties
org.gradle.jvmargs=-Xmx2g -Dfile.encoding=UTF-8
kotlin.code.style=official
```

`.gitignore`:

```
.gradle/
build/
.kotlin/
.idea/
*.iml
local.properties
out/
```

`gradle/libs.versions.toml` (all libraries are declared now; later tasks only use them):

```toml
[versions]
kotlin = "2.4.20"
compose = "1.12.1"
composeMaterial3 = "1.9.0"
ktor = "3.6.0"
coroutines = "1.11.0"
serialization = "1.11.0"
logback = "1.5.38"

[libraries]
kotlinx-coroutines-core = { module = "org.jetbrains.kotlinx:kotlinx-coroutines-core", version.ref = "coroutines" }
kotlinx-coroutines-test = { module = "org.jetbrains.kotlinx:kotlinx-coroutines-test", version.ref = "coroutines" }
kotlinx-coroutines-swing = { module = "org.jetbrains.kotlinx:kotlinx-coroutines-swing", version.ref = "coroutines" }
kotlinx-serialization-json = { module = "org.jetbrains.kotlinx:kotlinx-serialization-json", version.ref = "serialization" }

ktor-server-core = { module = "io.ktor:ktor-server-core", version.ref = "ktor" }
ktor-server-netty = { module = "io.ktor:ktor-server-netty", version.ref = "ktor" }
ktor-server-content-negotiation = { module = "io.ktor:ktor-server-content-negotiation", version.ref = "ktor" }
ktor-server-sse = { module = "io.ktor:ktor-server-sse", version.ref = "ktor" }
ktor-server-status-pages = { module = "io.ktor:ktor-server-status-pages", version.ref = "ktor" }
ktor-server-test-host = { module = "io.ktor:ktor-server-test-host", version.ref = "ktor" }
ktor-serialization-kotlinx-json = { module = "io.ktor:ktor-serialization-kotlinx-json", version.ref = "ktor" }

ktor-client-core = { module = "io.ktor:ktor-client-core", version.ref = "ktor" }
ktor-client-cio = { module = "io.ktor:ktor-client-cio", version.ref = "ktor" }
ktor-client-content-negotiation = { module = "io.ktor:ktor-client-content-negotiation", version.ref = "ktor" }

compose-runtime = { module = "org.jetbrains.compose.runtime:runtime", version.ref = "compose" }
compose-foundation = { module = "org.jetbrains.compose.foundation:foundation", version.ref = "compose" }
compose-ui = { module = "org.jetbrains.compose.ui:ui", version.ref = "compose" }
compose-material3 = { module = "org.jetbrains.compose.material3:material3", version.ref = "composeMaterial3" }

logback-classic = { module = "ch.qos.logback:logback-classic", version.ref = "logback" }

[plugins]
kotlin-multiplatform = { id = "org.jetbrains.kotlin.multiplatform", version.ref = "kotlin" }
kotlin-jvm = { id = "org.jetbrains.kotlin.jvm", version.ref = "kotlin" }
kotlin-serialization = { id = "org.jetbrains.kotlin.plugin.serialization", version.ref = "kotlin" }
kotlin-compose = { id = "org.jetbrains.kotlin.plugin.compose", version.ref = "kotlin" }
compose-multiplatform = { id = "org.jetbrains.compose", version.ref = "compose" }
```

- [ ] **Step 3: Write `shared/build.gradle.kts`**

`checkCommonPurity` fails the build if `commonMain` mentions `java.`/`javax.` (Global Constraints):

```kotlin
plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)
    jvm()

    sourceSets {
        commonMain.dependencies {
            implementation(libs.kotlinx.serialization.json)
        }
        commonTest.dependencies {
            implementation(kotlin("test"))
        }
    }
}

// shared must stay pure Kotlin so it can later compile for wasmJs. With only the jvm() target the
// compiler does not enforce this, so fail the build if commonMain mentions java.* / javax.*.
val checkCommonPurity by tasks.registering {
    val sources = fileTree("src/commonMain") { include("**/*.kt") }
    inputs.files(sources)
    doLast {
        val forbidden = Regex("""\bjavax?\.""")
        val offenders = sources.files.flatMap { file ->
            file.readLines().withIndex()
                .filter { forbidden.containsMatchIn(it.value) }
                .map { "${file.name}:${it.index + 1}: ${it.value.trim()}" }
        }
        if (offenders.isNotEmpty()) {
            throw GradleException("shared/commonMain must not use java.* APIs:\n" + offenders.joinToString("\n"))
        }
    }
}

tasks.matching { it.name == "jvmTest" || it.name == "check" }.configureEach {
    dependsOn(checkCommonPurity)
}
```

- [ ] **Step 4: Write the failing test**

`shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt`:

```kotlin
package com.xgetsongs.shared.input

import kotlin.test.Test
import kotlin.test.assertEquals

class InputClassifierTest {
    private val playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"
    private val videoId = "dQw4w9WgXcQ"

    private fun ok(input: ParsedInput) = ClassifyResult.Ok(input)
    private fun rejected(reason: RejectReason) = ClassifyResult.Rejected(reason)

    @Test
    fun barePlaylistId() {
        assertEquals(ok(ParsedInput.Playlist(playlistId)), InputClassifier.classify(playlistId))
    }

    @Test
    fun bareVideoId() {
        assertEquals(ok(ParsedInput.Video(videoId)), InputClassifier.classify(videoId))
    }

    @Test
    fun elevenCharIdStartingWithPlIsAVideo() {
        assertEquals(ok(ParsedInput.Video("PLxxxxxxxxx")), InputClassifier.classify("PLxxxxxxxxx"))
    }

    @Test
    fun playlistUrl() {
        assertEquals(
            ok(ParsedInput.Playlist(playlistId)),
            InputClassifier.classify("https://www.youtube.com/playlist?list=$playlistId"),
        )
    }

    @Test
    fun watchUrl() {
        assertEquals(
            ok(ParsedInput.Video(videoId)),
            InputClassifier.classify("https://www.youtube.com/watch?v=$videoId"),
        )
    }

    @Test
    fun shortUrlsAndShorts() {
        assertEquals(ok(ParsedInput.Video(videoId)), InputClassifier.classify("https://youtu.be/$videoId?si=abc"))
        assertEquals(ok(ParsedInput.Video(videoId)), InputClassifier.classify("https://www.youtube.com/shorts/$videoId"))
    }

    @Test
    fun urlWithoutScheme() {
        assertEquals(ok(ParsedInput.Video(videoId)), InputClassifier.classify("music.youtube.com/watch?v=$videoId"))
    }

    @Test
    fun watchUrlWithListIsPlaylistAndRemembersVideo() {
        assertEquals(
            ok(ParsedInput.Playlist(playlistId, alsoVideoId = videoId)),
            InputClassifier.classify("https://www.youtube.com/watch?v=$videoId&list=$playlistId"),
        )
    }

    @Test
    fun mixListWithVideoFallsBackToVideo() {
        assertEquals(
            ok(ParsedInput.Video(videoId)),
            InputClassifier.classify("https://www.youtube.com/watch?v=$videoId&list=RD$videoId"),
        )
    }

    @Test
    fun mixListAloneIsRejected() {
        assertEquals(
            rejected(RejectReason.MIX_PLAYLIST),
            InputClassifier.classify("https://www.youtube.com/playlist?list=RDCLAK5uy_abcdefgh"),
        )
    }

    @Test
    fun blankIsRejected() {
        assertEquals(rejected(RejectReason.EMPTY), InputClassifier.classify("   "))
    }

    @Test
    fun foreignHostsAreRejected() {
        assertEquals(rejected(RejectReason.UNSUPPORTED_HOST), InputClassifier.classify("https://evil.com/watch?v=$videoId"))
        assertEquals(
            rejected(RejectReason.UNSUPPORTED_HOST),
            InputClassifier.classify("https://www.youtube.com.evil.com/watch?v=$videoId"),
        )
        assertEquals(
            rejected(RejectReason.UNSUPPORTED_HOST),
            InputClassifier.classify("https://youtube.com@evil.com/watch?v=$videoId"),
        )
    }

    @Test
    fun garbageIsRejected() {
        assertEquals(rejected(RejectReason.UNRECOGNIZED), InputClassifier.classify("PLshort"))
        assertEquals(rejected(RejectReason.UNRECOGNIZED), InputClassifier.classify("hello world"))
        assertEquals(rejected(RejectReason.UNSUPPORTED_HOST), InputClassifier.classify("PLabc; rm -rf /"))
    }

    @Test
    fun canonicalUrlIsRebuiltFromIdOnly() {
        val result = InputClassifier.classify(
            "https://www.youtube.com/watch?v=$videoId&list=PLx%20--exec&t=10s",
        ) as ClassifyResult.Ok
        assertEquals("https://www.youtube.com/watch?v=$videoId", result.input.canonicalUrl)
    }

    @Test
    fun playlistCanonicalUrl() {
        val result = InputClassifier.classify(playlistId) as ClassifyResult.Ok
        assertEquals("https://www.youtube.com/playlist?list=$playlistId", result.input.canonicalUrl)
    }
}
```

- [ ] **Step 5: Run the test to verify it fails**

Run: `.\gradlew.bat :shared:jvmTest`
(The first run downloads Gradle, Kotlin and the plugins; allow several minutes.)
Expected: FAIL — compilation error `Unresolved reference 'InputClassifier'` (and `ParsedInput`, `ClassifyResult`, `RejectReason`).

- [ ] **Step 6: Write the implementation**

`shared/src/commonMain/kotlin/com/xgetsongs/shared/input/InputClassifier.kt`:

```kotlin
package com.xgetsongs.shared.input

/** A user input that has been validated and reduced to YouTube IDs. */
sealed interface ParsedInput {
    /** Canonical URL rebuilt from the ID only. The raw user text is never passed on. */
    val canonicalUrl: String

    data class Playlist(val id: String, val alsoVideoId: String? = null) : ParsedInput {
        override val canonicalUrl: String get() = "https://www.youtube.com/playlist?list=$id"
    }

    data class Video(val id: String) : ParsedInput {
        override val canonicalUrl: String get() = "https://www.youtube.com/watch?v=$id"
    }
}

enum class RejectReason(val message: String) {
    EMPTY("재생목록 ID 또는 영상 주소를 입력하세요."),
    UNSUPPORTED_HOST("YouTube 주소만 사용할 수 있습니다."),
    MIX_PLAYLIST("자동 생성 믹스 목록은 지원하지 않습니다."),
    UNRECOGNIZED("재생목록 ID 또는 영상 주소로 인식할 수 없습니다."),
}

sealed interface ClassifyResult {
    data class Ok(val input: ParsedInput) : ClassifyResult
    data class Rejected(val reason: RejectReason) : ClassifyResult
}

object InputClassifier {
    private val PLAYLIST_PREFIXES = listOf("PL", "UU", "LL", "FL", "OL")
    private const val MIX_PREFIX = "RD"
    private const val MIN_PLAYLIST_ID_LENGTH = 12
    private const val VIDEO_ID_LENGTH = 11
    private val ALLOWED_HOSTS = setOf(
        "youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com", "youtu.be",
    )
    private val VIDEO_PATH_PREFIXES = listOf("shorts", "embed", "live")

    fun classify(raw: String): ClassifyResult {
        val text = raw.trim()
        if (text.isEmpty()) return ClassifyResult.Rejected(RejectReason.EMPTY)
        return when {
            isIdChars(text) -> classifyBareId(text)
            !text.contains('.') && !text.contains('/') -> ClassifyResult.Rejected(RejectReason.UNRECOGNIZED)
            else -> classifyUrl(text)
        }
    }

    private fun isIdChars(s: String): Boolean =
        s.isNotEmpty() && s.all { (it.isLetterOrDigit() && it.code < 128) || it == '_' || it == '-' }

    private fun isPlaylistId(s: String): Boolean =
        s.length >= MIN_PLAYLIST_ID_LENGTH && isIdChars(s) && PLAYLIST_PREFIXES.any { s.startsWith(it) }

    private fun isMixId(s: String): Boolean =
        s.length >= MIN_PLAYLIST_ID_LENGTH && isIdChars(s) && s.startsWith(MIX_PREFIX)

    private fun isVideoId(s: String): Boolean = s.length == VIDEO_ID_LENGTH && isIdChars(s)

    private fun classifyBareId(text: String): ClassifyResult = when {
        isPlaylistId(text) -> ok(ParsedInput.Playlist(text))
        isMixId(text) -> ClassifyResult.Rejected(RejectReason.MIX_PLAYLIST)
        isVideoId(text) -> ok(ParsedInput.Video(text))
        else -> ClassifyResult.Rejected(RejectReason.UNRECOGNIZED)
    }

    private fun classifyUrl(text: String): ClassifyResult {
        val afterScheme = text.substringAfter("://", missingDelimiterValue = text)
        val hostEnd = afterScheme.indexOfFirst { it == '/' || it == '?' || it == '#' }
        val host = (if (hostEnd < 0) afterScheme else afterScheme.substring(0, hostEnd)).lowercase()
        if (host !in ALLOWED_HOSTS) return ClassifyResult.Rejected(RejectReason.UNSUPPORTED_HOST)

        val rest = if (hostEnd < 0) "" else afterScheme.substring(hostEnd)
        val beforeFragment = rest.substringBefore('#')
        val path = beforeFragment.substringBefore('?')
        val query = if (beforeFragment.contains('?')) beforeFragment.substringAfter('?') else ""
        val params = query.split('&').filter { it.contains('=') }
            .associate { it.substringBefore('=') to it.substringAfter('=') }

        val segments = path.split('/').filter { it.isNotEmpty() }
        val videoCandidate = when {
            host == "youtu.be" -> segments.firstOrNull()
            segments.firstOrNull() in VIDEO_PATH_PREFIXES -> segments.getOrNull(1)
            else -> params["v"]
        }
        val videoId = videoCandidate?.takeIf { isVideoId(it) }
        val listId = params["list"]

        return when {
            listId != null && isPlaylistId(listId) -> ok(ParsedInput.Playlist(listId, alsoVideoId = videoId))
            listId != null && isMixId(listId) ->
                if (videoId != null) ok(ParsedInput.Video(videoId))
                else ClassifyResult.Rejected(RejectReason.MIX_PLAYLIST)
            videoId != null -> ok(ParsedInput.Video(videoId))
            else -> ClassifyResult.Rejected(RejectReason.UNRECOGNIZED)
        }
    }

    private fun ok(input: ParsedInput): ClassifyResult = ClassifyResult.Ok(input)
}
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `.\gradlew.bat :shared:jvmTest`
Expected: `BUILD SUCCESSFUL`; `InputClassifierTest` reports 15 tests, 0 failures (see "Counting tests").

- [ ] **Step 8: Prove that the purity check really fails**

Create `shared/src/commonMain/kotlin/com/xgetsongs/shared/Bad.kt`:

```kotlin
package com.xgetsongs.shared
import java.io.File
fun bad(): Any = File("x")
```

Run: `.\gradlew.bat :shared:jvmTest`
Expected: FAIL with `shared/commonMain must not use java.* APIs:` and `Bad.kt:2: import java.io.File`.
Then delete `Bad.kt` and run `.\gradlew.bat :shared:jvmTest` again; expected `BUILD SUCCESSFUL`.

- [ ] **Step 9: Commit**

```powershell
git add gradlew gradlew.bat gradle settings.gradle.kts build.gradle.kts gradle.properties .gitignore shared
git commit -m "feat(shared): add Gradle scaffold and InputClassifier" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `FilenameFormatter`

**Files:**
- Create: `shared/src/commonMain/kotlin/com/xgetsongs/shared/filename/FilenameFormatter.kt`
- Test: `shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt`

**Interfaces:**
- Consumes: nothing.
- Produces: `object FilenameFormatter` with `const val MIN_RANK = 1`, `MAX_RANK = 999`, `MAX_BASE_LENGTH = 180`, `MAX_ARTIST_LENGTH = 80`; `fun format(rank: Int, artist: String, title: String): String` (throws `IllegalArgumentException` when rank is outside 1..999); `fun sanitize(text: String): String`.

- [ ] **Step 1: Write the failing test**

```kotlin
package com.xgetsongs.shared.filename

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class FilenameFormatterTest {
    @Test
    fun formatsTheDocumentedExample() {
        assertEquals(
            "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).mp3",
            FilenameFormatter.format(1, "소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)"),
        )
    }

    @Test
    fun padsRankToThreeDigits() {
        assertTrue(FilenameFormatter.format(7, "A", "B").startsWith("007 "))
        assertTrue(FilenameFormatter.format(42, "A", "B").startsWith("042 "))
        assertTrue(FilenameFormatter.format(999, "A", "B").startsWith("999 "))
    }

    @Test
    fun rejectsRankOutsideOneTo999() {
        assertFailsWith<IllegalArgumentException> { FilenameFormatter.format(0, "A", "B") }
        assertFailsWith<IllegalArgumentException> { FilenameFormatter.format(1000, "A", "B") }
    }

    @Test
    fun replacesForbiddenCharactersWithFullWidthOnes() {
        assertEquals(
            "005 태연 (TAEYEON) - 만찬가 (晩餐歌 ／ BANSANKA) ： J-POP REMAKE Vol.1.mp3",
            FilenameFormatter.format(5, "태연 (TAEYEON)", "만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"),
        )
        assertEquals("001 A - ＼／：＊？＂＜＞｜.mp3", FilenameFormatter.format(1, "A", "\\/:*?\"<>|"))
    }

    @Test
    fun dropsControlCharactersAndTrailingDotsAndSpaces() {
        assertEquals("001 A - Mr.mp3", FilenameFormatter.format(1, "A", "Mr.\t\n. "))
        assertEquals("001 A - B.C.mp3", FilenameFormatter.format(1, "A", "B.C"))
    }

    @Test
    fun emptyTitleGetsAPlaceholder() {
        assertEquals("001 A - untitled.mp3", FilenameFormatter.format(1, "A", "  "))
    }

    @Test
    fun longTitleIsTruncatedWithEllipsisKeepingRankAndArtist() {
        val name = FilenameFormatter.format(12, "ARTIST", "가".repeat(500))
        val base = name.removeSuffix(".mp3")
        assertEquals(FilenameFormatter.MAX_BASE_LENGTH, base.length)
        assertTrue(base.startsWith("012 ARTIST - 가"))
        assertTrue(base.endsWith("…"))
    }

    @Test
    fun veryLongArtistIsTruncatedToo() {
        val name = FilenameFormatter.format(1, "A".repeat(300), "Song")
        val base = name.removeSuffix(".mp3")
        assertTrue(base.length <= FilenameFormatter.MAX_BASE_LENGTH)
        assertTrue(base.startsWith("001 ${"A".repeat(10)}"))
        assertTrue(base.endsWith(" - Song"))
        assertTrue(base.contains("…"))
    }

    @Test
    fun truncationNeverSplitsASurrogatePair() {
        val name = FilenameFormatter.format(1, "A", "😀".repeat(200))
        val base = name.removeSuffix(".mp3")
        assertTrue(base.length <= FilenameFormatter.MAX_BASE_LENGTH)
        base.forEachIndexed { i, c ->
            if (c.isHighSurrogate()) assertTrue(base.getOrNull(i + 1)?.isLowSurrogate() == true, "lone high surrogate at $i")
            if (c.isLowSurrogate()) assertTrue(base.getOrNull(i - 1)?.isHighSurrogate() == true, "lone low surrogate at $i")
        }
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :shared:jvmTest`
Expected: FAIL — `Unresolved reference 'FilenameFormatter'`.

- [ ] **Step 3: Write the implementation**

```kotlin
package com.xgetsongs.shared.filename

/** Builds `{rank 3 digits} {artist} - {title}.mp3` file names that are safe on Windows. */
object FilenameFormatter {
    const val MIN_RANK = 1
    const val MAX_RANK = 999

    /** Limit for the file name without extension. */
    const val MAX_BASE_LENGTH = 180
    const val MAX_ARTIST_LENGTH = 80
    private const val ELLIPSIS = "…"
    private const val EXTENSION = ".mp3"
    private const val EMPTY_TITLE_PLACEHOLDER = "untitled"

    private val FULLWIDTH = mapOf(
        '\\' to '＼', '/' to '／', ':' to '：', '*' to '＊', '?' to '？',
        '"' to '＂', '<' to '＜', '>' to '＞', '|' to '｜',
    )

    fun format(rank: Int, artist: String, title: String): String {
        require(rank in MIN_RANK..MAX_RANK) { "rank must be in $MIN_RANK..$MAX_RANK but was $rank" }
        val cleanArtist = truncate(sanitize(artist), MAX_ARTIST_LENGTH)
        val prefix = "${rank.toString().padStart(3, '0')} $cleanArtist - "
        val cleanTitle = sanitize(title).ifEmpty { EMPTY_TITLE_PLACEHOLDER }
        val titleBudget = MAX_BASE_LENGTH - prefix.length
        val base = (prefix + truncate(cleanTitle, titleBudget)).trimEnd('.', ' ')
        return base + EXTENSION
    }

    /** Replaces forbidden characters with full-width look-alikes and drops control characters. */
    fun sanitize(text: String): String {
        val sb = StringBuilder(text.length)
        for (c in text) {
            when {
                c.code < 0x20 || c.code == 0x7F -> Unit
                else -> sb.append(FULLWIDTH[c] ?: c)
            }
        }
        return sb.toString().trim()
    }

    /** Cuts [text] to at most [max] UTF-16 units, ending with an ellipsis, never splitting a surrogate pair. */
    private fun truncate(text: String, max: Int): String {
        if (text.length <= max) return text
        var end = max - ELLIPSIS.length
        if (end > 0 && text[end - 1].isHighSurrogate()) end--
        return text.substring(0, end.coerceAtLeast(0)).trimEnd() + ELLIPSIS
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :shared:jvmTest`
Expected: `BUILD SUCCESSFUL`; `FilenameFormatterTest` 9 tests, `InputClassifierTest` 15 tests, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add shared
git commit -m "feat(shared): add FilenameFormatter" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `TitleParser`

The hardest piece of pure logic. The tests are real titles from the reference playlist "Melon Daily Top 100" (design spec §6.2) plus synthetic edge cases.

**Files:**
- Create: `shared/src/commonMain/kotlin/com/xgetsongs/shared/title/TitleParser.kt`
- Test: `shared/src/commonTest/kotlin/com/xgetsongs/shared/title/TitleParserTest.kt`

**Interfaces:**
- Consumes: nothing.
- Produces: `enum class Confidence { HIGH, MEDIUM, LOW }`; `data class ParsedTrack(val artist: String, val title: String, val confidence: Confidence)`; `object TitleParser { const val UNKNOWN_ARTIST = "Unknown Artist"; fun parse(rawTitle: String, channel: String? = null, metaArtist: String? = null, metaTrack: String? = null): ParsedTrack }` (package `com.xgetsongs.shared.title`). `Confidence.LOW` means the artist came from the channel name.

- [ ] **Step 1: Write the failing test**

```kotlin
package com.xgetsongs.shared.title

import kotlin.test.Test
import kotlin.test.assertEquals

class TitleParserTest {
    private fun assertParsed(
        raw: String,
        artist: String,
        title: String,
        confidence: Confidence = Confidence.HIGH,
        channel: String? = null,
        metaArtist: String? = null,
        metaTrack: String? = null,
    ) {
        val parsed = TitleParser.parse(raw, channel, metaArtist, metaTrack)
        assertEquals(ParsedTrack(artist, title, confidence), parsed, "title: $raw")
    }

    // ---- fixtures taken from the reference playlist "Melon Daily Top 100" ----------------

    @Test
    fun quotedTitleWithOfficialMusicVideoSuffix() =
        assertParsed(
            "소연 (SOYEON) '퇴사할게여 (Narr. 기안84)' Official Music Video",
            "소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)",
        )

    @Test
    fun curlyQuotes() =
        assertParsed("RESCENE (리센느) ‘LOVE ATTACK’ Official MV", "RESCENE (리센느)", "LOVE ATTACK")

    @Test
    fun curlyQuotesWithApostropheInside() =
        assertParsed("ILLIT (아일릿) ‘It’s Me’ Official MV", "ILLIT (아일릿)", "It’s Me")

    @Test
    fun dashSeparatorWithTrailingMv() =
        assertParsed("아이오아이 (I.O.I) - 갑자기 (Suddenly) MV", "아이오아이 (I.O.I)", "갑자기 (Suddenly)")

    @Test
    fun dashSeparatorWithQuotedTitle() =
        assertParsed("ATEEZ(에이티즈) - 'BAD' Official MV", "ATEEZ(에이티즈)", "BAD")

    @Test
    fun leadingMvTagAndUnderscoreSeparator() =
        assertParsed(
            "[MV] 태연 (TAEYEON)_ 만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1",
            "태연 (TAEYEON)", "만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1",
        )

    @Test
    fun spacedUnderscoreSeparator() =
        assertParsed(
            "[MV] Woody(우디) _ Sadder Than Yesterday(어제보다 슬픈 오늘)",
            "Woody(우디)", "Sadder Than Yesterday(어제보다 슬픈 오늘)",
        )

    @Test
    fun liveClipSuffix() = assertParsed("WOODZ 'Drowning' Live Clip", "WOODZ", "Drowning")

    @Test
    fun leadingEmojiAndProgramTailAfterPipe() =
        assertParsed(
            "🎤진영&최유리 - 생각을 멈추다 보면 | 들어봐! 유리의 숲2 EP.01 진영 편",
            "진영&최유리", "생각을 멈추다 보면",
        )

    @Test
    fun trailingBracketBlockAndPipeTail() =
        assertParsed(
            "성시경 - 너의 모든 순간 [유희열의 스케치북/You Heeyeol’s Sketchbook] | KBS 210528 방송",
            "성시경", "너의 모든 순간",
        )

    @Test
    fun apostropheInsideArtistNameIsNotAQuote() =
        assertParsed(
            "Girls' Generation-HRS 소녀시대-효리수 'Skibidi' Performance Video",
            "Girls' Generation-HRS 소녀시대-효리수", "Skibidi",
        )

    @Test
    fun textAfterClosingQuoteIsDiscarded() =
        assertParsed(
            "볼빨간사춘기 BOL4 '여름아 부탁해' Special Clip (with 적재)",
            "볼빨간사춘기 BOL4", "여름아 부탁해",
        )

    @Test
    fun backtickInsideQuotedTitleIsKept() =
        assertParsed(
            "AKMU - '어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)' M/V",
            "AKMU", "어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)",
        )

    @Test
    fun otherReferenceTitles() {
        assertParsed("BIGBANG - ‘BiiiG’ M/V", "BIGBANG", "BiiiG")
        assertParsed("[MV] 한로로 (HANRORO) - 사랑하게 될 거야 (Landing in Love)", "한로로 (HANRORO)", "사랑하게 될 거야 (Landing in Love)")
        assertParsed("Hearts2Hearts 하츠투하츠 'RUDE!' MV", "Hearts2Hearts 하츠투하츠", "RUDE!")
        assertParsed("IU '이 별로부터(Unknown Planet)' MV", "IU", "이 별로부터(Unknown Planet)")
        assertParsed("aespa 에스파 'LEMONADE' MV", "aespa 에스파", "LEMONADE")
        assertParsed("다비치 (DAVICHI) '타임캡슐' Official Music Video", "다비치 (DAVICHI)", "타임캡슐")
        assertParsed("YENA(최예나) - '캐치 캐치' M/V", "YENA(최예나)", "캐치 캐치")
        assertParsed("도경수 Doh Kyung Soo 'Popcorn' MV", "도경수 Doh Kyung Soo", "Popcorn")
    }

    // ---- synthetic edge cases -------------------------------------------------------------

    @Test
    fun leadingParenthesisInArtistNameIsKept() =
        assertParsed("(G)I-DLE - 'TOMBOY' Official Music Video", "(G)I-DLE", "TOMBOY")

    @Test
    fun apostropheBeforeSeparator() = assertParsed("Girls' Generation - Gee", "Girls' Generation", "Gee")

    @Test
    fun separatorInsideQuotesBelongsToTheTitle() =
        assertParsed("IU 'Love - Poem' MV", "IU", "Love - Poem")

    @Test
    fun apostropheInsideQuotedTitle() =
        assertParsed("Artist 'Don't Stop' MV", "Artist", "Don't Stop")

    @Test
    fun enDashSeparator() = assertParsed("Artist – Title", "Artist", "Title")

    @Test
    fun nestedLeadingTags() = assertParsed("[Official MV] [MV] Artist - Song", "Artist", "Song")

    @Test
    fun leadingEmoji() = assertParsed("🎶 Artist - Song", "Artist", "Song")

    @Test
    fun noiseMustBeASeparateWord() = assertParsed("Artist - HAMV", "Artist", "HAMV")

    @Test
    fun noiseInParentheses() = assertParsed("Artist - Song (Official Video)", "Artist", "Song")

    @Test
    fun parenthesesThatAreNotNoiseAreKept() =
        assertParsed("Artist - Song (feat. Someone)", "Artist", "Song (feat. Someone)")

    // ---- fallbacks ------------------------------------------------------------------------

    @Test
    fun metadataIsUsedWhenTitleHasNoPattern() =
        assertParsed(
            "Dynamite", "BTS", "Dynamite", Confidence.MEDIUM,
            channel = "BTS - Topic", metaArtist = "BTS", metaTrack = "Dynamite",
        )

    @Test
    fun topicChannelTitleIsNeverSplit() =
        assertParsed(
            "Song - Remix", "Artist", "Song - Remix", Confidence.MEDIUM,
            channel = "Artist - Topic", metaArtist = "Artist", metaTrack = "Song - Remix",
        )

    @Test
    fun topicChannelWithoutMetadataFallsBackToChannelName() =
        assertParsed("Dynamite", "BTS", "Dynamite", Confidence.LOW, channel = "BTS - Topic")

    @Test
    fun vevoSuffixIsRemovedFromChannelName() =
        assertParsed("7 rings (Official Video)", "ArianaGrande", "7 rings", Confidence.LOW, channel = "ArianaGrandeVevo")

    @Test
    fun unknownArtistWhenNothingIsAvailable() =
        assertParsed("Some Song", TitleParser.UNKNOWN_ARTIST, "Some Song", Confidence.LOW)
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :shared:jvmTest`
Expected: FAIL — `Unresolved reference 'TitleParser'`, `'Confidence'`, `'ParsedTrack'`.

- [ ] **Step 3: Write the implementation**

```kotlin
package com.xgetsongs.shared.title

enum class Confidence { HIGH, MEDIUM, LOW }

data class ParsedTrack(val artist: String, val title: String, val confidence: Confidence)

/**
 * Splits a YouTube video title into artist and song title.
 *
 * Order: title patterns (`Artist - Title`, `Artist 'Title'`) -> yt-dlp `artist`/`track` metadata ->
 * channel name. Text is kept as written; only quotes around the title and noise such as
 * `Official MV` are removed. `Title - Artist` ordering is not supported.
 */
object TitleParser {
    const val UNKNOWN_ARTIST = "Unknown Artist"
    private const val TOPIC_SUFFIX = " - Topic"
    private const val VEVO_SUFFIX = "vevo"

    private val LEADING_TAGS = setOf(
        "mv", "m/v", "official mv", "official m/v", "official video", "official audio",
        "official music video", "music video", "audio", "lyric video", "performance video",
        "가사", "뮤직비디오",
    )

    /** Longest first so "Official Music Video" is removed as a whole, not just "Music Video". */
    private val TRAILING_NOISE = listOf(
        "Official Music Video", "Official Video", "Official Audio", "Official MV", "Official M/V",
        "Performance Video", "Special Video", "Special Clip", "Lyric Video", "Music Video",
        "Live Clip", "Visualizer", "Audio", "M/V", "MV",
    ).map { it.lowercase() }.sortedByDescending { it.length }

    private val SEPARATORS = listOf(" - ", " – ", " — ", "_ ")
    private val SINGLE_QUOTES = charArrayOf('\'', '‘', '’')
    private val DOUBLE_QUOTES = charArrayOf('"', '“', '”')
    private val ALL_QUOTES = SINGLE_QUOTES + DOUBLE_QUOTES

    fun parse(
        rawTitle: String,
        channel: String? = null,
        metaArtist: String? = null,
        metaTrack: String? = null,
    ): ParsedTrack {
        val text = clean(rawTitle)

        // Auto-generated "Artist - Topic" channels carry the bare track name, so " - " in the
        // title belongs to the track and must not be treated as an artist separator.
        val isTopicChannel = channel?.trim()?.endsWith(TOPIC_SUFFIX, ignoreCase = true) == true
        if (!isTopicChannel) {
            split(text)?.let { (artist, title) -> return ParsedTrack(artist, title, Confidence.HIGH) }
        }

        val title = unwrapQuotes(text).ifEmpty { rawTitle.trim() }
        val artist = metaArtist?.trim().orEmpty()
        if (artist.isNotEmpty()) {
            val track = metaTrack?.trim().orEmpty().ifEmpty { title }
            return ParsedTrack(artist, track, Confidence.MEDIUM)
        }
        return ParsedTrack(artistFromChannel(channel), title, Confidence.LOW)
    }

    private fun artistFromChannel(channel: String?): String {
        var name = channel?.trim().orEmpty()
        if (name.endsWith(TOPIC_SUFFIX, ignoreCase = true)) name = name.dropLast(TOPIC_SUFFIX.length)
        if (name.length > VEVO_SUFFIX.length && name.endsWith(VEVO_SUFFIX, ignoreCase = true)) {
            name = name.dropLast(VEVO_SUFFIX.length)
        }
        return name.trim().ifEmpty { UNKNOWN_ARTIST }
    }

    // ---- cleaning -------------------------------------------------------------------------

    private fun clean(raw: String): String {
        var s = collapseWhitespace(raw)
        while (true) {
            val next = collapseWhitespace(
                stripTrailingNoiseToken(
                    stripTrailingNoiseParen(
                        stripTrailingBracketBlock(cutAtPipe(stripLeadingTag(stripLeadingJunk(s)))),
                    ),
                ),
            )
            if (next == s) return s
            s = next
        }
    }

    private fun collapseWhitespace(s: String): String {
        val sb = StringBuilder(s.length)
        var previousWasSpace = false
        for (c in s) {
            if (c.isWhitespace()) {
                if (!previousWasSpace) sb.append(' ')
                previousWasSpace = true
            } else {
                sb.append(c)
                previousWasSpace = false
            }
        }
        return sb.toString().trim()
    }

    /** Drops leading emoji and symbols; stops at a letter, digit, bracket or quote. */
    private fun stripLeadingJunk(s: String): String {
        val i = s.indexOfFirst { it.isLetterOrDigit() || it == '[' || it == '(' || it in ALL_QUOTES }
        return if (i <= 0) s else s.substring(i)
    }

    private fun stripLeadingTag(s: String): String {
        if (!s.startsWith("[")) return s
        val end = s.indexOf(']')
        if (end < 0) return s
        if (s.substring(1, end).trim().lowercase() !in LEADING_TAGS) return s
        return s.substring(end + 1).trim().ifEmpty { s }
    }

    private fun cutAtPipe(s: String): String {
        val i = s.indexOf(" | ")
        return if (i <= 0) s else s.substring(0, i).trim()
    }

    private fun stripTrailingBracketBlock(s: String): String {
        if (!s.endsWith("]")) return s
        val start = s.lastIndexOf('[')
        if (start <= 0) return s
        return s.substring(0, start).trim().ifEmpty { s }
    }

    private fun stripTrailingNoiseParen(s: String): String {
        if (!s.endsWith(")")) return s
        val start = s.lastIndexOf('(')
        if (start <= 0) return s
        val inner = s.substring(start + 1, s.length - 1).trim().lowercase()
        return if (inner in TRAILING_NOISE) s.substring(0, start).trim().ifEmpty { s } else s
    }

    private fun stripTrailingNoiseToken(s: String): String {
        for (token in TRAILING_NOISE) {
            if (s.length > token.length &&
                s.endsWith(token, ignoreCase = true) &&
                s[s.length - token.length - 1].isWhitespace()
            ) {
                return s.substring(0, s.length - token.length)
                    .trimEnd { it.isWhitespace() || it == '-' || it == '–' || it == '—' }
                    .ifEmpty { s }
            }
        }
        return s
    }

    // ---- splitting ------------------------------------------------------------------------

    /** Splits at whichever comes first: a separator (`Artist - Title`) or an opening quote (`Artist 'Title'`). */
    private fun split(text: String): Pair<String, String>? {
        val separator = findSeparator(text)
        val quote = findQuotedSpan(text, 0)
        val result = when {
            separator != null && (quote == null || separator.first < quote.first) ->
                text.substring(0, separator.first).trim() to
                    unwrapQuotes(text.substring(separator.first + separator.second).trim())
            quote != null && quote.first > 0 ->
                text.substring(0, quote.first).trim() to text.substring(quote.first + 1, quote.second).trim()
            else -> null
        }
        return result?.takeIf { it.first.isNotEmpty() && it.second.isNotEmpty() }
    }

    /** Returns (index, length) of the earliest separator, ignoring position 0. */
    private fun findSeparator(s: String): Pair<Int, Int>? {
        var best: Pair<Int, Int>? = null
        for (separator in SEPARATORS) {
            val i = s.indexOf(separator, startIndex = 1)
            if (i > 0 && (best == null || i < best.first)) best = i to separator.length
        }
        return best
    }

    private fun quoteFamily(c: Char): CharArray? = when (c) {
        in SINGLE_QUOTES -> SINGLE_QUOTES
        in DOUBLE_QUOTES -> DOUBLE_QUOTES
        else -> null
    }

    /**
     * Finds the first quoted span at or after [from] as (openIndex, closeIndex). An opening quote must
     * follow whitespace or the start of the text, which skips apostrophes inside words such as
     * "Girls'". A closing quote must not be followed by a letter or digit, which skips "Don't".
     */
    private fun findQuotedSpan(s: String, from: Int): Pair<Int, Int>? {
        for (i in from until s.length) {
            val family = quoteFamily(s[i]) ?: continue
            if (i > 0 && !s[i - 1].isWhitespace()) continue
            val close = findCloser(s, i, family) ?: continue
            return i to close
        }
        return null
    }

    private fun findCloser(s: String, open: Int, family: CharArray): Int? {
        for (j in open + 2 until s.length) {
            if (s[j] in family && (j == s.length - 1 || !s[j + 1].isLetterOrDigit())) return j
        }
        return null
    }

    private fun unwrapQuotes(s: String): String {
        val span = findQuotedSpan(s, 0)
        return if (span != null && span.first == 0) s.substring(1, span.second).trim() else s
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :shared:jvmTest`
Expected: `BUILD SUCCESSFUL`; `TitleParserTest` 29 tests, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add shared
git commit -m "feat(shared): add TitleParser" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: API models

**Files:**
- Create: `shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`
- Test: `shared/src/commonTest/kotlin/com/xgetsongs/shared/api/ApiModelsTest.kt`

**Interfaces:**
- Consumes: nothing.
- Produces (package `com.xgetsongs.shared.api`, all `@Serializable` except `ApiJson`/`ApiHeaders`): `object ApiJson { val instance: Json }`; `object ApiHeaders { const val TOKEN = "X-XGS-Token" }`; `enum InputKind { PLAYLIST, VIDEO }`; `ResolveRequest(input)`; `ResolvedItem(rank, videoId, title, channel?, artist, track, lowConfidence, available, unavailableReason?, expectedFileName?)`; `ResolveResponse(resolveId, kind, playlistTitle?, items, truncated, alsoVideoId?)`; `JobOptions(outputDir?, overwrite, singleRank, concurrency)`; `JobRequest(resolveId, options, ranks?)`; `JobCreated(jobId)`; `enum Stage { DOWNLOADING, CONVERTING }`; `enum JobStatus { COMPLETED, CANCELLED, FAILED }`; `JobSummary(succeeded, skipped, failed)`; `sealed interface JobEvent` with `ItemStarted(rank, videoId, fileName)`, `Progress(rank, stage, percent?)`, `ItemDone(rank, fileName)`, `ItemSkipped(rank, reason)`, `ItemFailed(rank, message)`, `JobDone(status, summary)` and the extension `val JobEvent.sseName: String`; `ToolInfo(found, version?, path?)`; `ToolsStatus(ytDlp, ffmpeg, jsRuntime)`; `ActionResult(message)`; `ErrorResponse(message)`.

- [ ] **Step 1: Write the failing test**

```kotlin
package com.xgetsongs.shared.api

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ApiModelsTest {
    private val json = ApiJson.instance

    private fun roundTrip(event: JobEvent): JobEvent =
        json.decodeFromString(JobEvent.serializer(), json.encodeToString(JobEvent.serializer(), event))

    @Test
    fun everyJobEventSurvivesARoundTrip() {
        val events = listOf(
            JobEvent.ItemStarted(1, "abc", "001 A - B.mp3"),
            JobEvent.Progress(1, Stage.DOWNLOADING, 42.5),
            JobEvent.Progress(1, Stage.CONVERTING),
            JobEvent.ItemDone(1, "001 A - B.mp3"),
            JobEvent.ItemSkipped(2, "이미 존재"),
            JobEvent.ItemFailed(3, "boom"),
            JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 1, 1)),
        )
        events.forEach { assertEquals(it, roundTrip(it)) }
    }

    @Test
    fun eventTypeDiscriminatorMatchesSseName() {
        val events = listOf(
            JobEvent.ItemStarted(1, "abc", "x.mp3"),
            JobEvent.Progress(1, Stage.DOWNLOADING, 1.0),
            JobEvent.ItemDone(1, "x.mp3"),
            JobEvent.ItemSkipped(1, "r"),
            JobEvent.ItemFailed(1, "m"),
            JobEvent.JobDone(JobStatus.CANCELLED, JobSummary(0, 0, 0)),
        )
        events.forEach { event ->
            val encoded = json.encodeToString(JobEvent.serializer(), event)
            assertTrue(encoded.contains("\"type\":\"${event.sseName}\""), encoded)
        }
    }

    @Test
    fun jobRequestDefaultsApplyWhenFieldsAreOmitted() {
        val request = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1"}""")
        assertEquals(JobRequest("r1", JobOptions(), null), request)
        assertEquals(2, request.options.concurrency)
        assertEquals(1, request.options.singleRank)
    }

    @Test
    fun resolveResponseRoundTrips() {
        val response = ResolveResponse(
            resolveId = "r1",
            kind = InputKind.PLAYLIST,
            playlistTitle = "Melon Daily Top 100",
            items = listOf(
                ResolvedItem(1, "id1", "A - B", "ch", "A", "B", false, true, null, "001 A - B.mp3"),
                ResolvedItem(2, "id2", "[Private video]", available = false, unavailableReason = "비공개 영상"),
            ),
            truncated = true,
            alsoVideoId = "id1",
        )
        val decoded = json.decodeFromString(
            ResolveResponse.serializer(),
            json.encodeToString(ResolveResponse.serializer(), response),
        )
        assertEquals(response, decoded)
    }

    @Test
    fun unknownKeysAreIgnored() {
        val decoded = json.decodeFromString(ErrorResponse.serializer(), """{"message":"m","extra":1}""")
        assertEquals(ErrorResponse("m"), decoded)
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :shared:jvmTest`
Expected: FAIL — `Unresolved reference 'ApiJson'`, `'JobEvent'`, ...

- [ ] **Step 3: Write the implementation**

```kotlin
package com.xgetsongs.shared.api

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/** The one JSON configuration used by the server and every client. */
object ApiJson {
    val instance: Json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
    }
}

object ApiHeaders {
    /** Every request to the local server must carry the token the server generated at start-up. */
    const val TOKEN = "X-XGS-Token"
}

@Serializable
enum class InputKind { PLAYLIST, VIDEO }

@Serializable
data class ResolveRequest(val input: String)

/** One playlist entry (or the single video) as shown in the preview list. */
@Serializable
data class ResolvedItem(
    val rank: Int,
    val videoId: String,
    val title: String,
    val channel: String? = null,
    val artist: String = "",
    val track: String = "",
    /** True when the artist came from the channel name (the preview shows a warning). */
    val lowConfidence: Boolean = false,
    val available: Boolean = true,
    val unavailableReason: String? = null,
    /** Null when the item is unavailable. */
    val expectedFileName: String? = null,
)

@Serializable
data class ResolveResponse(
    /** Assigned by the server; the engine returns an empty string. */
    val resolveId: String = "",
    val kind: InputKind,
    val playlistTitle: String? = null,
    val items: List<ResolvedItem>,
    /** True when the playlist had more than 999 entries and only the first 999 are listed. */
    val truncated: Boolean = false,
    /** Set when a watch URL carried both `v=` and `list=`, so the UI can offer "this video only". */
    val alsoVideoId: String? = null,
)

@Serializable
data class JobOptions(
    /** Only honoured in LOCAL server mode. */
    val outputDir: String? = null,
    val overwrite: Boolean = false,
    /** Rank for a single video (1..999). Ignored for playlists. */
    val singleRank: Int = 1,
    /** Clamped to 1..4 by the engine. */
    val concurrency: Int = 2,
)

@Serializable
data class JobRequest(
    val resolveId: String,
    val options: JobOptions = JobOptions(),
    /** Restrict the job to these ranks (used to retry failed items). Null means every available item. */
    val ranks: List<Int>? = null,
)

@Serializable
data class JobCreated(val jobId: String)

@Serializable
enum class Stage { DOWNLOADING, CONVERTING }

@Serializable
enum class JobStatus { COMPLETED, CANCELLED, FAILED }

@Serializable
data class JobSummary(val succeeded: Int, val skipped: Int, val failed: Int)

@Serializable
sealed interface JobEvent {
    @Serializable
    @SerialName("item-started")
    data class ItemStarted(val rank: Int, val videoId: String, val fileName: String) : JobEvent

    @Serializable
    @SerialName("progress")
    data class Progress(val rank: Int, val stage: Stage, val percent: Double? = null) : JobEvent

    @Serializable
    @SerialName("item-done")
    data class ItemDone(val rank: Int, val fileName: String) : JobEvent

    @Serializable
    @SerialName("item-skipped")
    data class ItemSkipped(val rank: Int, val reason: String) : JobEvent

    @Serializable
    @SerialName("item-failed")
    data class ItemFailed(val rank: Int, val message: String) : JobEvent

    @Serializable
    @SerialName("job-done")
    data class JobDone(val status: JobStatus, val summary: JobSummary) : JobEvent
}

/** The SSE `event:` name for this event; identical to its serial name. */
val JobEvent.sseName: String
    get() = when (this) {
        is JobEvent.ItemStarted -> "item-started"
        is JobEvent.Progress -> "progress"
        is JobEvent.ItemDone -> "item-done"
        is JobEvent.ItemSkipped -> "item-skipped"
        is JobEvent.ItemFailed -> "item-failed"
        is JobEvent.JobDone -> "job-done"
    }

@Serializable
data class ToolInfo(val found: Boolean, val version: String? = null, val path: String? = null)

@Serializable
data class ToolsStatus(
    val ytDlp: ToolInfo,
    val ffmpeg: ToolInfo,
    /** The JavaScript runtime yt-dlp needs for YouTube: Deno 2.3+ or Node 22+. */
    val jsRuntime: ToolInfo,
)

@Serializable
data class ActionResult(val message: String)

@Serializable
data class ErrorResponse(val message: String)
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :shared:jvmTest`
Expected: `BUILD SUCCESSFUL`; `ApiModelsTest` 5 tests; the whole module has 58 tests, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add shared
git commit -m "feat(shared): add API models" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `engine` module and `ProcessRunner`

**Files:**
- Modify: `settings.gradle.kts` (add `include(":engine")` after `include(":shared")`)
- Create: `engine/build.gradle.kts`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/process/ProcessRunner.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/process/SystemProcessRunnerTest.kt`

**Interfaces:**
- Consumes: `:shared` (as an `api` dependency, so later modules see its types).
- Produces (package `com.xgetsongs.engine.process`): `interface ProcessRunner { suspend fun run(command: List<String>, onStdout: (String) -> Unit = {}, onStderr: (String) -> Unit = {}): Int }` — runs the command, calls the callbacks once per output line (possibly from different threads), returns the exit code, and kills the whole process tree when the calling coroutine is cancelled; `class SystemProcessRunner : ProcessRunner` (reads output as UTF-8).

- [ ] **Step 1: Add the module**

Edit `settings.gradle.kts` so the last lines read:

```kotlin
include(":shared")
include(":engine")
```

Create `engine/build.gradle.kts`:

```kotlin
plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    api(project(":shared"))
    api(libs.kotlinx.coroutines.core)
    implementation(libs.kotlinx.serialization.json)

    testImplementation(kotlin("test"))
    testImplementation(libs.kotlinx.coroutines.test)
}

tasks.test {
    useJUnitPlatform()
}
```

- [ ] **Step 2: Write the failing test**

These tests start real `java` child processes (Java's single-file source launcher), so the suite takes a few seconds.

```kotlin
package com.xgetsongs.engine.process

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SystemProcessRunnerTest {
    private val dir: Path = Files.createTempDirectory("xgs-runner")
    private val java: String = Path.of(System.getProperty("java.home"), "bin", "java").toString()
    private val runner = SystemProcessRunner()

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    private fun source(name: String, body: String): String {
        val file = dir.resolve("$name.java")
        Files.writeString(file, "public class $name { public static void main(String[] args) throws Exception { $body } }")
        return file.toString()
    }

    @Test
    fun capturesOutputLinesAndExitCode() = runBlocking {
        val out = mutableListOf<String>()
        val err = mutableListOf<String>()
        val file = source("Hello", """System.out.println("out-line"); System.err.println("err-line"); System.exit(3);""")

        val exitCode = runner.run(listOf(java, file), { out += it }, { err += it })

        assertEquals(3, exitCode)
        assertEquals(listOf("out-line"), out)
        assertEquals(listOf("err-line"), err)
    }

    @Test
    fun decodesOutputAsUtf8() = runBlocking {
        val out = mutableListOf<String>()
        val file = source("Korean", """System.out.println("한글 제목");""")

        runner.run(listOf(java, "-Dstdout.encoding=UTF-8", file), { out += it })

        assertEquals(listOf("한글 제목"), out)
    }

    @Test
    fun cancellingTheCallerKillsTheProcess() = runBlocking {
        val started = CompletableDeferred<Unit>()
        val file = source("Sleeper", """System.out.println("started"); Thread.sleep(60000);""")
        val job = launch {
            runner.run(listOf(java, file), { if (it == "started") started.complete(Unit) })
        }
        withTimeout(30_000) { started.await() }

        val begin = System.nanoTime()
        withTimeout(15_000) { job.cancelAndJoin() }
        val elapsedSeconds = (System.nanoTime() - begin) / 1_000_000_000.0

        assertTrue(elapsedSeconds < 10, "cancel took $elapsedSeconds s; the process was probably not killed")
    }
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `.\gradlew.bat :engine:test`
Expected: FAIL — `Unresolved reference 'SystemProcessRunner'`.

- [ ] **Step 4: Write the implementation**

```kotlin
package com.xgetsongs.engine.process

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withContext

/** Runs an external program. Implementations must never pass [command] through a shell. */
interface ProcessRunner {
    /**
     * Runs [command], calling [onStdout]/[onStderr] once per output line (possibly from different
     * threads), and returns the exit code. Cancelling the calling coroutine kills the whole process tree.
     */
    suspend fun run(
        command: List<String>,
        onStdout: (String) -> Unit = {},
        onStderr: (String) -> Unit = {},
    ): Int
}

class SystemProcessRunner : ProcessRunner {
    override suspend fun run(
        command: List<String>,
        onStdout: (String) -> Unit,
        onStderr: (String) -> Unit,
    ): Int = withContext(Dispatchers.IO) {
        val builder = ProcessBuilder(command)
        builder.environment()["PYTHONIOENCODING"] = "utf-8"
        builder.environment()["PYTHONUTF8"] = "1"
        val process = builder.start()
        process.outputStream.close()

        coroutineScope {
            val stdout = async { process.inputStream.bufferedReader(Charsets.UTF_8).forEachLine(onStdout) }
            val stderr = async { process.errorStream.bufferedReader(Charsets.UTF_8).forEachLine(onStderr) }
            try {
                val exitCode = runInterruptible { process.waitFor() }
                stdout.await()
                stderr.await()
                exitCode
            } finally {
                // Killing the process closes its streams, which lets the blocked readers finish.
                if (process.isAlive) {
                    process.descendants().forEach { it.destroyForcibly() }
                    process.destroyForcibly()
                }
            }
        }
    }
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `.\gradlew.bat :engine:test`
Expected: `BUILD SUCCESSFUL`; `SystemProcessRunnerTest` 3 tests, 0 failures. The cancel test would hang for about 60 s (and then fail) if the process were not killed.

- [ ] **Step 6: Commit**

```powershell
git add settings.gradle.kts engine
git commit -m "feat(engine): add module and SystemProcessRunner" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `ToolLocator` and shared test fakes

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/tools/ToolLocator.kt`
- Create: `engine/src/test/kotlin/com/xgetsongs/engine/testutil/Fakes.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/tools/ToolLocatorTest.kt`

**Interfaces:**
- Consumes: `ProcessRunner` (Task 5).
- Produces (main, package `com.xgetsongs.engine.tools`): `data class ToolPaths(val ytDlp: Path?, val ffmpeg: Path?, val jsRuntime: Path?)`; `fun interface ToolPathProvider { fun current(): ToolPaths }`; `class ToolLocator(appBinDir: Path, pathEnv: String = System.getenv("PATH").orEmpty(), ffmpegOverride: Path? = null) : ToolPathProvider` (searches `appBinDir` first, then PATH; `deno` is preferred over `node`; re-evaluated on every call).
- Produces (test helpers, package `com.xgetsongs.engine.testutil`): `class FakeProcessRunner(handler)` recording `commands` and `maxActive`; `val TEST_TOOLS: ToolPaths`; `fun toolsOf(paths = TEST_TOOLS): ToolPathProvider`; `fun outputDirOf(command)`, `fun videoIdOf(command)`, `fun writeFakeMp3(command)` (they read the `-o <dir>/<id>.%(ext)s` argument of a download command).

- [ ] **Step 1: Write the test helpers and the failing test**

`engine/src/test/kotlin/com/xgetsongs/engine/testutil/Fakes.kt`:

```kotlin
package com.xgetsongs.engine.testutil

import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.tools.ToolPaths
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicInteger

/** A [ProcessRunner] whose behaviour is a lambda; records every command and the peak concurrency. */
class FakeProcessRunner(
    private val handler: suspend (command: List<String>, onStdout: (String) -> Unit, onStderr: (String) -> Unit) -> Int,
) : ProcessRunner {
    val commands = CopyOnWriteArrayList<List<String>>()
    val maxActive = AtomicInteger()
    private val active = AtomicInteger()

    override suspend fun run(command: List<String>, onStdout: (String) -> Unit, onStderr: (String) -> Unit): Int {
        commands += command
        val now = active.incrementAndGet()
        maxActive.accumulateAndGet(now) { a, b -> maxOf(a, b) }
        try {
            return handler(command, onStdout, onStderr)
        } finally {
            active.decrementAndGet()
        }
    }
}

val TEST_TOOLS = ToolPaths(
    ytDlp = Path.of("C:/tools/yt-dlp.exe"),
    ffmpeg = Path.of("C:/tools/ffmpeg.exe"),
    jsRuntime = Path.of("C:/tools/node.exe"),
)

fun toolsOf(paths: ToolPaths = TEST_TOOLS) = ToolPathProvider { paths }

/** The directory a download command writes to, read from its `-o` argument. */
fun outputDirOf(command: List<String>): Path {
    val template = command[command.indexOf("-o") + 1]
    return Path.of(template).parent
}

/** The video ID a download command is for, read from the `-o` argument (`<dir>/<id>.%(ext)s`). */
fun videoIdOf(command: List<String>): String {
    val template = command[command.indexOf("-o") + 1]
    return Path.of(template).fileName.toString().removeSuffix(".%(ext)s")
}

/** Pretends yt-dlp finished: creates `<dir>/<id>.mp3` the way the real tool would. */
fun writeFakeMp3(command: List<String>) {
    Files.writeString(outputDirOf(command).resolve("${videoIdOf(command)}.mp3"), "mp3-data")
}
```

`engine/src/test/kotlin/com/xgetsongs/engine/tools/ToolLocatorTest.kt`:

```kotlin
package com.xgetsongs.engine.tools

import java.io.File
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class ToolLocatorTest {
    private val root: Path = Files.createTempDirectory("xgs-tools")
    private val appBin = Files.createDirectories(root.resolve("app-bin"))
    private val pathDir = Files.createDirectories(root.resolve("path-dir"))

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private fun touch(dir: Path, name: String): Path = Files.writeString(dir.resolve(name), "x")

    private fun locator(ffmpegOverride: Path? = null) =
        ToolLocator(appBin, pathEnv = pathDir.toString() + File.pathSeparator + "Z:\\does\\not\\exist", ffmpegOverride = ffmpegOverride)

    @Test
    fun findsToolsOnPath() {
        touch(pathDir, "yt-dlp.exe")
        touch(pathDir, "ffmpeg.exe")
        touch(pathDir, "node.exe")

        val paths = locator().current()

        assertEquals(pathDir.resolve("yt-dlp.exe"), paths.ytDlp)
        assertEquals(pathDir.resolve("ffmpeg.exe"), paths.ffmpeg)
        assertEquals(pathDir.resolve("node.exe"), paths.jsRuntime)
    }

    @Test
    fun appBinDirectoryWinsOverPath() {
        touch(pathDir, "yt-dlp.exe")
        touch(appBin, "yt-dlp.exe")

        assertEquals(appBin.resolve("yt-dlp.exe"), locator().current().ytDlp)
    }

    @Test
    fun denoIsPreferredOverNode() {
        touch(pathDir, "node.exe")
        touch(pathDir, "deno.exe")

        assertEquals(pathDir.resolve("deno.exe"), locator().current().jsRuntime)
    }

    @Test
    fun missingToolsAreNull() {
        val paths = locator().current()

        assertNull(paths.ytDlp)
        assertNull(paths.ffmpeg)
        assertNull(paths.jsRuntime)
    }

    @Test
    fun ffmpegOverrideIsUsedWhenItExists() {
        touch(pathDir, "ffmpeg.exe")
        val custom = touch(root, "my-ffmpeg.exe")

        assertEquals(custom, locator(ffmpegOverride = custom).current().ffmpeg)
        assertEquals(pathDir.resolve("ffmpeg.exe"), locator(ffmpegOverride = root.resolve("gone.exe")).current().ffmpeg)
    }

    @Test
    fun toolInstalledLaterIsPickedUpWithoutRestart() {
        val locator = locator()
        assertNull(locator.current().ytDlp)

        touch(appBin, "yt-dlp.exe")

        assertEquals(appBin.resolve("yt-dlp.exe"), locator.current().ytDlp)
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :engine:test`
Expected: FAIL — `Unresolved reference 'ToolLocator'`, `'ToolPaths'`, `'ToolPathProvider'`.

- [ ] **Step 3: Write the implementation**

```kotlin
package com.xgetsongs.engine.tools

import java.io.File
import java.nio.file.Files
import java.nio.file.Path

data class ToolPaths(
    val ytDlp: Path?,
    val ffmpeg: Path?,
    /** Deno or Node, which yt-dlp needs to solve YouTube's JavaScript challenges. */
    val jsRuntime: Path?,
)

fun interface ToolPathProvider {
    fun current(): ToolPaths
}

/**
 * Finds the external tools. The app's own [appBinDir] wins over PATH so an app-managed yt-dlp is
 * preferred. Re-evaluated on every call so a freshly installed tool is picked up immediately.
 */
class ToolLocator(
    private val appBinDir: Path,
    private val pathEnv: String = System.getenv("PATH").orEmpty(),
    private val ffmpegOverride: Path? = null,
) : ToolPathProvider {
    override fun current(): ToolPaths = ToolPaths(
        ytDlp = find("yt-dlp"),
        ffmpeg = ffmpegOverride?.takeIf { Files.isRegularFile(it) } ?: find("ffmpeg"),
        jsRuntime = find("deno") ?: find("node"),
    )

    private fun find(name: String): Path? {
        val dirs = listOf(appBinDir) + pathEnv.split(File.pathSeparator)
            .filter { it.isNotBlank() }
            .mapNotNull { runCatching { Path.of(it.trim('"')) }.getOrNull() }
        for (dir in dirs) {
            for (candidate in listOf("$name.exe", name)) {
                val path = dir.resolve(candidate)
                if (Files.isRegularFile(path)) return path
            }
        }
        return null
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :engine:test`
Expected: `BUILD SUCCESSFUL`; `ToolLocatorTest` 6 tests, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add engine
git commit -m "feat(engine): add ToolLocator and test fakes" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: yt-dlp commands, progress parsing and error classification

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/YtDlpCommands.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/ProgressParser.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/ErrorClassifier.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/YtDlpCommandsTest.kt`, `ProgressParserTest.kt`, `ErrorClassifierTest.kt`

**Interfaces:**
- Consumes: `ToolPaths` (Task 6), `TEST_TOOLS` (Task 6 test helper).
- Produces (package `com.xgetsongs.engine.ytdlp`): `object YtDlpCommands { const val PROGRESS_PREFIX = "XGSP"; const val POSTPROCESS_PREFIX = "XGSPP"; fun resolvePlaylist(tools: ToolPaths, url: String): List<String>; fun resolveVideo(tools: ToolPaths, url: String): List<String>; fun download(tools: ToolPaths, url: String, outputDir: Path, videoId: String): List<String> }` (throws `IllegalArgumentException` when `tools.ytDlp` is null); `sealed interface ProgressUpdate { data class Downloading(val percent: Double?); data object Converting }`; `object ProgressParser { fun parse(line: String): ProgressUpdate? }`; `enum class FailureKind { UNAVAILABLE, TRANSIENT, FATAL, OTHER }`; `data class Failure(val kind: FailureKind, val message: String)`; `object ErrorClassifier { fun classify(stderrLines: List<String>): Failure }`.

- [ ] **Step 1: Write the failing tests**

`YtDlpCommandsTest.kt`:

```kotlin
package com.xgetsongs.engine.ytdlp

import com.xgetsongs.engine.testutil.TEST_TOOLS
import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertContains
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class YtDlpCommandsTest {
    private val url = "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

    @Test
    fun playlistListingIsFlatAndPutsTheUrlAfterDoubleDash() {
        val command = YtDlpCommands.resolvePlaylist(TEST_TOOLS, "https://www.youtube.com/playlist?list=PLabcdefghijkl")
        assertEquals(TEST_TOOLS.ytDlp.toString(), command.first())
        assertContains(command, "--flat-playlist")
        assertContains(command, "--ignore-config")
        assertEquals(listOf("--", "https://www.youtube.com/playlist?list=PLabcdefghijkl"), command.takeLast(2))
    }

    @Test
    fun videoMetadataUsesTheJsRuntime() {
        val command = YtDlpCommands.resolveVideo(TEST_TOOLS, url)
        val i = command.indexOf("--js-runtimes")
        assertEquals("node:${TEST_TOOLS.jsRuntime}", command[i + 1])
        assertEquals(listOf("--", url), command.takeLast(2))
    }

    @Test
    fun downloadExtractsMp3IntoTheOutputDirectory() {
        val command = YtDlpCommands.download(TEST_TOOLS, url, Path.of("C:/work/job-1"), "dQw4w9WgXcQ")
        assertContains(command, "-x")
        assertEquals("mp3", command[command.indexOf("--audio-format") + 1])
        assertEquals("0", command[command.indexOf("--audio-quality") + 1])
        assertTrue(command[command.indexOf("-o") + 1].endsWith("dQw4w9WgXcQ.%(ext)s"))
        assertEquals(TEST_TOOLS.ffmpeg.toString(), command[command.indexOf("--ffmpeg-location") + 1])
        assertEquals(2, command.count { it == "--progress-template" })
        assertEquals(listOf("--", url), command.takeLast(2))
    }

    @Test
    fun denoIsPassedWithItsPath() {
        val tools = TEST_TOOLS.copy(jsRuntime = Path.of("C:/tools/deno.exe"))
        val command = YtDlpCommands.resolveVideo(tools, url)
        assertEquals("deno:${tools.jsRuntime}", command[command.indexOf("--js-runtimes") + 1])
    }

    @Test
    fun noJsRuntimeFlagWhenNoneIsInstalled() {
        val command = YtDlpCommands.download(TEST_TOOLS.copy(jsRuntime = null, ffmpeg = null), url, Path.of("C:/w"), "id")
        assertFalse(command.contains("--js-runtimes"))
        assertFalse(command.contains("--ffmpeg-location"))
    }

    @Test
    fun ytDlpPathIsRequired() {
        assertFailsWith<IllegalArgumentException> { YtDlpCommands.resolveVideo(TEST_TOOLS.copy(ytDlp = null), url) }
    }
}
```

`ProgressParserTest.kt`:

```kotlin
package com.xgetsongs.engine.ytdlp

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class ProgressParserTest {
    @Test
    fun percentFromDownloadedAndTotalBytes() {
        val update = ProgressParser.parse("XGSP|downloading|500|1000|NA")
        assertEquals(ProgressUpdate.Downloading(50.0), update)
    }

    @Test
    fun fallsBackToTheEstimatedTotal() {
        val update = ProgressParser.parse("XGSP|downloading|250|NA|1000") as ProgressUpdate.Downloading
        assertEquals(25.0, update.percent)
    }

    @Test
    fun percentIsUnknownWithoutAnyTotal() {
        assertEquals(ProgressUpdate.Downloading(null), ProgressParser.parse("XGSP|downloading|500|NA|NA"))
    }

    @Test
    fun percentIsCappedAt100() {
        assertEquals(ProgressUpdate.Downloading(100.0), ProgressParser.parse("XGSP|downloading|2000|1000|NA"))
    }

    @Test
    fun finishedMeansHundredPercent() {
        assertEquals(ProgressUpdate.Downloading(100.0), ProgressParser.parse("XGSP|finished|1000|1000|NA"))
    }

    @Test
    fun postprocessorStartMeansConverting() {
        assertEquals(ProgressUpdate.Converting, ProgressParser.parse("XGSPP|started|ExtractAudio"))
    }

    @Test
    fun otherLinesAreIgnored() {
        assertNull(ProgressParser.parse("[youtube] Extracting URL"))
        assertNull(ProgressParser.parse("XGSPP|finished|ExtractAudio"))
        assertNull(ProgressParser.parse(""))
    }
}
```

`ErrorClassifierTest.kt`:

```kotlin
package com.xgetsongs.engine.ytdlp

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ErrorClassifierTest {
    private fun classify(vararg lines: String) = ErrorClassifier.classify(lines.toList())

    @Test
    fun privateVideoIsUnavailable() {
        val failure = classify("ERROR: [youtube] abc: Private video. Sign in if you've been granted access to this video")
        assertEquals(FailureKind.UNAVAILABLE, failure.kind)
        assertEquals("비공개 영상", failure.message)
    }

    @Test
    fun geoBlockedAndAgeRestrictedAreUnavailable() {
        assertEquals("지역 제한 영상", classify("ERROR: The uploader has not made this video available in your country").message)
        assertEquals(FailureKind.UNAVAILABLE, classify("ERROR: [youtube] abc: Sign in to confirm your age").kind)
        assertEquals(FailureKind.UNAVAILABLE, classify("ERROR: [youtube] abc: Video unavailable").kind)
    }

    @Test
    fun rateLimitAndNetworkErrorsAreTransient() {
        assertEquals(FailureKind.TRANSIENT, classify("ERROR: unable to download video data: HTTP Error 429: Too Many Requests").kind)
        assertEquals(FailureKind.TRANSIENT, classify("ERROR: [Errno 11001] getaddrinfo failed").kind)
        assertEquals(FailureKind.TRANSIENT, classify("WARNING: The read operation timed out").kind)
    }

    @Test
    fun diskAndPermissionErrorsAreFatal() {
        assertEquals(FailureKind.FATAL, classify("OSError: [Errno 28] No space left on device").kind)
        assertEquals(FailureKind.FATAL, classify("PermissionError: [Errno 13] Permission denied: 'x.mp3'").kind)
    }

    @Test
    fun fatalWinsOverTransient() {
        assertEquals(
            FailureKind.FATAL,
            classify("ERROR: unable to download video data", "OSError: [Errno 28] No space left on device").kind,
        )
    }

    @Test
    fun unknownErrorsKeepTheErrorLine() {
        val failure = classify("[youtube] abc: Downloading webpage", "ERROR: [youtube] abc: Something odd happened")
        assertEquals(FailureKind.OTHER, failure.kind)
        assertEquals("[youtube] abc: Something odd happened", failure.message)
    }

    @Test
    fun botCheckIsNotTreatedAsUnavailable() {
        val failure = classify("ERROR: [youtube] abc: Sign in to confirm you're not a bot")
        assertEquals(FailureKind.OTHER, failure.kind)
    }

    @Test
    fun emptyStderrStillGivesAMessage() {
        assertTrue(classify().message.isNotBlank())
    }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.\gradlew.bat :engine:test`
Expected: FAIL — `Unresolved reference 'YtDlpCommands'`, `'ProgressParser'`, `'ErrorClassifier'`.

- [ ] **Step 3: Write the implementations**

`YtDlpCommands.kt`:

```kotlin
package com.xgetsongs.engine.ytdlp

import com.xgetsongs.engine.tools.ToolPaths
import java.nio.file.Path

/**
 * Every yt-dlp command line lives here. Commands are argument lists (never shell strings) and the
 * URL always comes last, after `--`, so it can never be read as an option.
 */
object YtDlpCommands {
    const val PROGRESS_PREFIX = "XGSP"
    const val POSTPROCESS_PREFIX = "XGSPP"

    private val COMMON = listOf("--ignore-config", "--no-warnings", "--encoding", "utf-8")

    /** Lists a playlist's entries without extracting each video (fast). */
    fun resolvePlaylist(tools: ToolPaths, url: String): List<String> =
        listOf(ytDlp(tools)) + COMMON + listOf("--flat-playlist", "-J", "--", url)

    /** Full metadata for one video. */
    fun resolveVideo(tools: ToolPaths, url: String): List<String> =
        listOf(ytDlp(tools)) + COMMON + jsRuntimeArgs(tools) + listOf("--no-playlist", "-J", "--", url)

    /** Downloads the audio of [url] as `<outputDir>/<videoId>.mp3`. */
    fun download(tools: ToolPaths, url: String, outputDir: Path, videoId: String): List<String> {
        val progress = "download:$PROGRESS_PREFIX|%(progress.status)s|%(progress.downloaded_bytes)s|" +
            "%(progress.total_bytes)s|%(progress.total_bytes_estimate)s"
        val postprocess = "postprocess:$POSTPROCESS_PREFIX|%(progress.status)s|%(progress.postprocessor)s"
        val output = outputDir.resolve("$videoId.%(ext)s").toString()
        val ffmpeg = tools.ffmpeg?.let { listOf("--ffmpeg-location", it.toString()) }.orEmpty()
        return listOf(ytDlp(tools)) + COMMON + jsRuntimeArgs(tools) + ffmpeg + listOf(
            "--no-playlist", "--newline",
            "--progress-template", progress,
            "--progress-template", postprocess,
            "-x", "--audio-format", "mp3", "--audio-quality", "0",
            "-o", output,
            "--", url,
        )
    }

    private fun ytDlp(tools: ToolPaths): String =
        requireNotNull(tools.ytDlp) { "yt-dlp path is required" }.toString()

    private fun jsRuntimeArgs(tools: ToolPaths): List<String> {
        val path = tools.jsRuntime ?: return emptyList()
        val name = path.fileName.toString().lowercase().removeSuffix(".exe")
        return if (name == "deno" || name == "node") listOf("--js-runtimes", "$name:$path") else emptyList()
    }
}
```

`ProgressParser.kt`:

```kotlin
package com.xgetsongs.engine.ytdlp

sealed interface ProgressUpdate {
    /** [percent] is null when yt-dlp does not know the total size yet. */
    data class Downloading(val percent: Double?) : ProgressUpdate

    data object Converting : ProgressUpdate
}

/** Parses the lines produced by the `--progress-template`s in [YtDlpCommands.download]. */
object ProgressParser {
    fun parse(line: String): ProgressUpdate? {
        val text = line.trim()
        return when {
            text.startsWith("${YtDlpCommands.PROGRESS_PREFIX}|") -> parseDownload(text)
            text.startsWith("${YtDlpCommands.POSTPROCESS_PREFIX}|") -> parsePostprocess(text)
            else -> null
        }
    }

    private fun parseDownload(text: String): ProgressUpdate? {
        val parts = text.split('|')
        val status = parts.getOrNull(1) ?: return null
        return when (status) {
            "finished" -> ProgressUpdate.Downloading(100.0)
            "downloading" -> {
                val downloaded = parts.getOrNull(2)?.toDoubleOrNull()
                val total = parts.getOrNull(3)?.toDoubleOrNull() ?: parts.getOrNull(4)?.toDoubleOrNull()
                val percent = if (downloaded != null && total != null && total > 0) {
                    // Multiply first: 29 / 100 * 100 is 28.999999999999996 in floating point.
                    (downloaded * 100.0 / total).coerceIn(0.0, 100.0)
                } else {
                    null
                }
                ProgressUpdate.Downloading(percent)
            }
            else -> null
        }
    }

    private fun parsePostprocess(text: String): ProgressUpdate? {
        val status = text.split('|').getOrNull(1)
        return if (status == "started") ProgressUpdate.Converting else null
    }
}
```

`ErrorClassifier.kt`:

```kotlin
package com.xgetsongs.engine.ytdlp

enum class FailureKind {
    /** The video cannot be downloaded at all (private, removed, geo-blocked...). Skip it. */
    UNAVAILABLE,

    /** Worth retrying (network hiccup, rate limit). */
    TRANSIENT,

    /** Retrying or continuing is pointless (disk full, no permission). Abort the job. */
    FATAL,

    OTHER,
}

data class Failure(val kind: FailureKind, val message: String)

/** Maps yt-dlp's stderr to a [Failure]. */
object ErrorClassifier {
    private val FATAL = listOf(
        "no space left on device" to "디스크 공간이 부족합니다.",
        "not enough space on the disk" to "디스크 공간이 부족합니다.",
        "disk quota exceeded" to "디스크 공간이 부족합니다.",
        "permission denied" to "출력 폴더에 쓸 권한이 없습니다.",
    )

    private val UNAVAILABLE = listOf(
        "private video" to "비공개 영상",
        "sign in to confirm your age" to "연령 제한 영상",
        "age-restricted" to "연령 제한 영상",
        "available in your country" to "지역 제한 영상",
        "blocked it in your country" to "지역 제한 영상",
        "who has blocked it" to "지역 제한 영상",
        "members-only" to "멤버 전용 영상",
        "join this channel" to "멤버 전용 영상",
        "copyright" to "저작권으로 차단된 영상",
        "has been removed" to "삭제된 영상",
        "account associated with this video has been terminated" to "삭제된 영상",
        "video unavailable" to "사용할 수 없는 영상",
        "this video is not available" to "사용할 수 없는 영상",
    )

    private val TRANSIENT = listOf(
        "http error 429", "http error 403", "http error 500", "http error 502", "http error 503",
        "http error 504", "timed out", "connection reset", "connection aborted", "remote end closed",
        "incompleteread", "temporary failure in name resolution", "getaddrinfo failed",
        "network is unreachable", "unable to download",
    )

    fun classify(stderrLines: List<String>): Failure {
        val text = stderrLines.joinToString("\n").lowercase()
        FATAL.firstOrNull { text.contains(it.first) }?.let { return Failure(FailureKind.FATAL, it.second) }
        UNAVAILABLE.firstOrNull { text.contains(it.first) }?.let { return Failure(FailureKind.UNAVAILABLE, it.second) }
        val message = summarize(stderrLines)
        if (TRANSIENT.any { text.contains(it) }) return Failure(FailureKind.TRANSIENT, message)
        return Failure(FailureKind.OTHER, message)
    }

    private fun summarize(stderrLines: List<String>): String {
        val error = stderrLines.lastOrNull { it.trimStart().startsWith("ERROR:") }
            ?.trim()?.removePrefix("ERROR:")?.trim()
        val summary = error
            ?: stderrLines.lastOrNull { it.isNotBlank() }?.trim()
            ?: "yt-dlp가 비정상 종료했습니다."
        return summary.take(300)
    }
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.\gradlew.bat :engine:test`
Expected: `BUILD SUCCESSFUL`; `YtDlpCommandsTest` 6, `ProgressParserTest` 7, `ErrorClassifierTest` 8 tests, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add engine
git commit -m "feat(engine): add yt-dlp commands, progress parser and error classifier" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Engine interfaces and `OutputSink`

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/Services.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/output/OutputSink.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/output/LocalFolderSink.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/output/LocalFolderSinkTest.kt`

**Interfaces:**
- Consumes: shared API models (Task 4).
- Produces: `interface OutputSink { suspend fun exists(fileName: String): Boolean; suspend fun put(fileName: String, source: Path, overwrite: Boolean) }` and `class LocalFolderSink(directory: Path) : OutputSink` (creates the folder; rejects names with path segments, `.` and `..`; without overwrite an existing target makes `put` throw `FileAlreadyExistsException`). In `Services.kt` (package `com.xgetsongs.engine`): `class ResolveException(message)`, `class ToolException(message)`, `interface Resolver { suspend fun resolve(input: String): ResolveResponse }`, `data class DownloadRequest(items: List<ResolvedItem>, sink: OutputSink, overwrite: Boolean, concurrency: Int)`, `class JobHandle(val events: ReceiveChannel<JobEvent>, job: Job) { fun cancel(); suspend fun join() }` (the event channel has a single reader and is closed after `JobEvent.JobDone`), `interface DownloadService { fun start(request: DownloadRequest): JobHandle }`, `interface ToolManager { suspend fun status(): ToolsStatus; suspend fun installYtDlp(): ActionResult; suspend fun updateYtDlp(): ActionResult }`.

- [ ] **Step 1: Write the failing test**

```kotlin
package com.xgetsongs.engine.output

import kotlinx.coroutines.test.runTest
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LocalFolderSinkTest {
    private val root: Path = Files.createTempDirectory("xgs-sink")
    private val target = root.resolve("music/out")

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private fun source(content: String): Path {
        val file = Files.createTempFile(root, "src", ".mp3")
        Files.writeString(file, content)
        return file
    }

    @Test
    fun createsTheTargetDirectoryAndMovesTheFile() = runTest {
        val sink = LocalFolderSink(target)
        val source = source("data")

        sink.put("001 A - B.mp3", source, overwrite = false)

        assertEquals("data", Files.readString(target.resolve("001 A - B.mp3")))
        assertFalse(Files.exists(source))
    }

    @Test
    fun existsReflectsTheFolderContent() = runTest {
        val sink = LocalFolderSink(target)
        assertFalse(sink.exists("001 A - B.mp3"))

        sink.put("001 A - B.mp3", source("x"), overwrite = false)

        assertTrue(sink.exists("001 A - B.mp3"))
    }

    @Test
    fun existingFilesAreKeptUnlessOverwriteIsOn() = runTest {
        val sink = LocalFolderSink(target)
        sink.put("a.mp3", source("old"), overwrite = false)

        assertFailsWith<FileAlreadyExistsException> { sink.put("a.mp3", source("new"), overwrite = false) }
        assertEquals("old", Files.readString(target.resolve("a.mp3")))

        sink.put("a.mp3", source("new"), overwrite = true)
        assertEquals("new", Files.readString(target.resolve("a.mp3")))
    }

    @Test
    fun fileNamesWithPathSegmentsAreRejected() = runTest {
        val sink = LocalFolderSink(target)

        assertFailsWith<IllegalArgumentException> { sink.put("../evil.mp3", source("x"), overwrite = false) }
        assertFailsWith<IllegalArgumentException> { sink.put("sub/evil.mp3", source("x"), overwrite = false) }
        assertFailsWith<IllegalArgumentException> { sink.exists("..") }
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :engine:test`
Expected: FAIL — `Unresolved reference 'LocalFolderSink'`.

- [ ] **Step 3: Write the implementations**

`Services.kt`:

```kotlin
package com.xgetsongs.engine

import com.xgetsongs.engine.output.OutputSink
import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.ToolsStatus
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.ReceiveChannel

/** Thrown with a user-facing (Korean) message when an input cannot be resolved. */
class ResolveException(message: String) : Exception(message)

/** Thrown with a user-facing message when installing or updating a tool fails. */
class ToolException(message: String) : Exception(message)

interface Resolver {
    /** @throws ResolveException when the input is invalid or YouTube/yt-dlp reports an error. */
    suspend fun resolve(input: String): ResolveResponse
}

/**
 * [items] must already carry their final ranks. [concurrency] is clamped to 1..4 by the service.
 */
data class DownloadRequest(
    val items: List<ResolvedItem>,
    val sink: OutputSink,
    val overwrite: Boolean,
    val concurrency: Int,
)

/**
 * A running download. [events] is consumed by a single reader and is closed after the final
 * [JobEvent.JobDone] event.
 */
class JobHandle(val events: ReceiveChannel<JobEvent>, private val job: Job) {
    fun cancel() = job.cancel()
    suspend fun join() = job.join()
}

interface DownloadService {
    fun start(request: DownloadRequest): JobHandle
}

interface ToolManager {
    suspend fun status(): ToolsStatus

    /** @throws ToolException */
    suspend fun installYtDlp(): ActionResult

    /** @throws ToolException */
    suspend fun updateYtDlp(): ActionResult
}
```

`OutputSink.kt`:

```kotlin
package com.xgetsongs.engine.output

import java.nio.file.Path

/**
 * Where finished mp3 files end up. The desktop app writes to a folder ([LocalFolderSink]); a web
 * deployment will hand files to the browser instead.
 */
interface OutputSink {
    suspend fun exists(fileName: String): Boolean

    /** Moves the finished file at [source] into the sink under [fileName]. */
    suspend fun put(fileName: String, source: Path, overwrite: Boolean)
}
```

`LocalFolderSink.kt`:

```kotlin
package com.xgetsongs.engine.output

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

class LocalFolderSink(private val directory: Path) : OutputSink {
    init {
        Files.createDirectories(directory)
    }

    override suspend fun exists(fileName: String): Boolean = withContext(Dispatchers.IO) {
        Files.exists(resolve(fileName))
    }

    override suspend fun put(fileName: String, source: Path, overwrite: Boolean) {
        withContext(Dispatchers.IO) {
            val target = resolve(fileName)
            if (overwrite) {
                Files.move(source, target, StandardCopyOption.REPLACE_EXISTING)
            } else {
                Files.move(source, target) // throws FileAlreadyExistsException when the file exists
            }
        }
    }

    private fun resolve(fileName: String): Path {
        require(
            fileName.isNotBlank() &&
                fileName != "." && fileName != ".." &&
                fileName == Path.of(fileName).fileName.toString(),
        ) {
            "file name must not contain path segments: $fileName"
        }
        return directory.resolve(fileName)
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :engine:test`
Expected: `BUILD SUCCESSFUL`; `LocalFolderSinkTest` 4 tests, 0 failures. (The `".."` case matters: `Path.of("..").fileName` equals `".."`, so it needs the explicit check.)

- [ ] **Step 5: Commit**

```powershell
git add engine
git commit -m "feat(engine): add engine interfaces and LocalFolderSink" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 9: `YtDlpResolver`

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/YtDlpResolver.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/YtDlpResolverTest.kt`

**Interfaces:**
- Consumes: `InputClassifier`, `TitleParser`, `FilenameFormatter`, `ResolveResponse`/`ResolvedItem`/`InputKind` (shared); `ProcessRunner`, `ToolPathProvider`, `ToolPaths`, `YtDlpCommands`, `ErrorClassifier`, `Resolver`, `ResolveException` (engine); `FakeProcessRunner`, `TEST_TOOLS`, `toolsOf` (test helpers).
- Produces (package `com.xgetsongs.engine.ytdlp`): `data class VideoMeta(val title: String?, val channel: String?, val artist: String?, val track: String?)`; `fun interface VideoMetadataSource { suspend fun fetch(videoId: String): VideoMeta? }`; `class YtDlpResolver(runner: ProcessRunner, tools: ToolPathProvider, json: Json = ApiJson.instance) : Resolver, VideoMetadataSource`.

Behaviour to keep: a playlist is listed with `--flat-playlist -J`; rank = position in the list; `[Private video]`, `[Deleted video]` titles and non-public `availability` values become unavailable items that keep their rank and have no file name; more than 999 entries are cut and flagged `truncated`; a single video is resolved with full metadata as rank 1; a watch URL that also had a list keeps `alsoVideoId`.

- [ ] **Step 1: Write the failing test**

```kotlin
package com.xgetsongs.engine.ytdlp

import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.testutil.FakeProcessRunner
import com.xgetsongs.engine.testutil.TEST_TOOLS
import com.xgetsongs.engine.testutil.toolsOf
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.input.RejectReason
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class YtDlpResolverTest {
    private val playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"
    private val playlistUrl = "https://www.youtube.com/playlist?list=$playlistId"

    private val playlistJson = """
        {"_type":"playlist","id":"$playlistId","title":"Melon Daily Top 100","entries":[
          {"_type":"url","ie_key":"Youtube","id":"vid00000001","title":"소연 (SOYEON) '퇴사할게여 (Narr. 기안84)' Official Music Video","channel":"i-dle (아이들)","uploader":"i-dle (아이들)","availability":null},
          {"id":"vid00000002","title":"[Private video]","channel":null,"availability":"needs_auth"},
          {"id":"vid00000003","title":"[Deleted video]"},
          {"id":"vid00000004","title":"Dynamite","channel":"BTS - Topic"}
        ]}
    """.trimIndent()

    private fun runnerReturning(stdout: String, exitCode: Int = 0, stderr: List<String> = emptyList()) =
        FakeProcessRunner { _, onStdout, onStderr ->
            stdout.lines().forEach(onStdout)
            stderr.forEach(onStderr)
            exitCode
        }

    private fun resolver(runner: FakeProcessRunner, tools: com.xgetsongs.engine.tools.ToolPaths = TEST_TOOLS) =
        YtDlpResolver(runner, toolsOf(tools))

    @Test
    fun resolvesAPlaylistIntoRankedItems() = runTest {
        val runner = runnerReturning(playlistJson)

        val response = resolver(runner).resolve(playlistUrl)

        assertEquals(InputKind.PLAYLIST, response.kind)
        assertEquals("Melon Daily Top 100", response.playlistTitle)
        assertEquals(listOf(1, 2, 3, 4), response.items.map { it.rank })
        val first = response.items[0]
        assertEquals("vid00000001", first.videoId)
        assertEquals("소연 (SOYEON)", first.artist)
        assertEquals("퇴사할게여 (Narr. 기안84)", first.track)
        assertEquals("001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).mp3", first.expectedFileName)
        assertFalse(first.lowConfidence)
        assertTrue(first.available)
    }

    @Test
    fun unavailableEntriesKeepTheirRankButHaveNoFileName() = runTest {
        val items = resolver(runnerReturning(playlistJson)).resolve(playlistUrl).items

        assertFalse(items[1].available)
        assertEquals("비공개 영상", items[1].unavailableReason)
        assertNull(items[1].expectedFileName)
        assertEquals("삭제된 영상", items[2].unavailableReason)
        assertEquals(3, items[2].rank)
    }

    @Test
    fun channelFallbackIsMarkedLowConfidence() = runTest {
        val item = resolver(runnerReturning(playlistJson)).resolve(playlistUrl).items[3]

        assertTrue(item.lowConfidence)
        assertEquals("BTS", item.artist)
        assertEquals("004 BTS - Dynamite.mp3", item.expectedFileName)
    }

    @Test
    fun playlistCommandUsesTheCanonicalUrlAndFlatListing() = runTest {
        val runner = runnerReturning(playlistJson)

        resolver(runner).resolve("  $playlistId  ")

        val command = runner.commands.single()
        assertTrue("--flat-playlist" in command)
        assertEquals(playlistUrl, command.last())
    }

    @Test
    fun playlistsLongerThan999AreTruncated() = runTest {
        val entries = (1..1001).joinToString(",") { """{"id":"v${it.toString().padStart(10, '0')}","title":"A - B$it"}""" }
        val runner = runnerReturning("""{"title":"Big","entries":[$entries]}""")

        val response = resolver(runner).resolve(playlistUrl)

        assertEquals(999, response.items.size)
        assertTrue(response.truncated)
        assertEquals(999, response.items.last().rank)
    }

    @Test
    fun watchUrlWithListRemembersTheVideoId() = runTest {
        val runner = runnerReturning(playlistJson)

        val response = resolver(runner).resolve("https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=$playlistId")

        assertEquals("dQw4w9WgXcQ", response.alsoVideoId)
    }

    @Test
    fun resolvesASingleVideoAsRankOne() = runTest {
        val runner = runnerReturning(
            """{"id":"dQw4w9WgXcQ","title":"Rick Astley - Never Gonna Give You Up (Official Video)","channel":"Rick Astley","artist":"Rick Astley","track":"Never Gonna Give You Up"}""",
        )

        val response = resolver(runner).resolve("https://youtu.be/dQw4w9WgXcQ")

        assertEquals(InputKind.VIDEO, response.kind)
        val item = response.items.single()
        assertEquals(1, item.rank)
        assertEquals("001 Rick Astley - Never Gonna Give You Up.mp3", item.expectedFileName)
        assertEquals("https://www.youtube.com/watch?v=dQw4w9WgXcQ", runner.commands.single().last())
    }

    @Test
    fun invalidInputIsRejectedWithoutRunningAnything() = runTest {
        val runner = runnerReturning("{}")

        val error = assertFailsWith<ResolveException> { resolver(runner).resolve("https://evil.com/x") }

        assertEquals(RejectReason.UNSUPPORTED_HOST.message, error.message)
        assertTrue(runner.commands.isEmpty())
    }

    @Test
    fun missingYtDlpGivesAHelpfulError() = runTest {
        val error = assertFailsWith<ResolveException> {
            resolver(runnerReturning("{}"), TEST_TOOLS.copy(ytDlp = null)).resolve(playlistUrl)
        }
        assertTrue(error.message!!.contains("yt-dlp"))
    }

    @Test
    fun ytDlpFailureBecomesAResolveException() = runTest {
        val runner = runnerReturning(
            "", exitCode = 1,
            stderr = listOf("ERROR: [youtube:tab] $playlistId: The playlist does not exist."),
        )

        val error = assertFailsWith<ResolveException> { resolver(runner).resolve(playlistUrl) }

        assertTrue(error.message!!.contains("playlist does not exist"))
    }

    @Test
    fun privateVideoErrorIsTranslated() = runTest {
        val runner = runnerReturning("", exitCode = 1, stderr = listOf("ERROR: [youtube] abc: Private video. Sign in if you've been granted access"))

        val error = assertFailsWith<ResolveException> { resolver(runner).resolve("dQw4w9WgXcQ") }

        assertEquals("비공개 영상", error.message)
    }

    @Test
    fun unreadableOutputIsAResolveException() = runTest {
        assertFailsWith<ResolveException> { resolver(runnerReturning("not json")).resolve(playlistUrl) }
    }

    @Test
    fun fetchReturnsFullMetadata() = runTest {
        val runner = runnerReturning("""{"id":"dQw4w9WgXcQ","title":"Dynamite","channel":"BTS - Topic","artist":"BTS","track":"Dynamite"}""")

        val meta = assertNotNull(resolver(runner).fetch("dQw4w9WgXcQ"))

        assertEquals("BTS", meta.artist)
        assertEquals("Dynamite", meta.track)
        assertEquals("BTS - Topic", meta.channel)
    }

    @Test
    fun fetchReturnsNullWhenYtDlpFails() = runTest {
        assertNull(resolver(runnerReturning("", exitCode = 1, stderr = listOf("ERROR: boom"))).fetch("dQw4w9WgXcQ"))
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :engine:test`
Expected: FAIL — `Unresolved reference 'YtDlpResolver'`.

- [ ] **Step 3: Write the implementation**

```kotlin
package com.xgetsongs.engine.ytdlp

import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.Resolver
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.tools.ToolPaths
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.filename.FilenameFormatter
import com.xgetsongs.shared.input.ClassifyResult
import com.xgetsongs.shared.input.InputClassifier
import com.xgetsongs.shared.input.ParsedInput
import com.xgetsongs.shared.title.Confidence
import com.xgetsongs.shared.title.TitleParser
import kotlinx.serialization.SerializationException
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/** The metadata of one video that the title parser can use. */
data class VideoMeta(val title: String?, val channel: String?, val artist: String?, val track: String?)

fun interface VideoMetadataSource {
    /** Returns null when the metadata cannot be fetched; callers fall back to what they already know. */
    suspend fun fetch(videoId: String): VideoMeta?
}

@Serializable
internal data class RawEntry(
    val id: String? = null,
    val title: String? = null,
    val channel: String? = null,
    val uploader: String? = null,
    val availability: String? = null,
    val artist: String? = null,
    val track: String? = null,
)

@Serializable
internal data class RawPlaylist(
    val title: String? = null,
    val entries: List<RawEntry?> = emptyList(),
)

class YtDlpResolver(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
    private val json: Json = ApiJson.instance,
) : Resolver, VideoMetadataSource {

    override suspend fun resolve(input: String): ResolveResponse {
        val parsed = when (val result = InputClassifier.classify(input)) {
            is ClassifyResult.Ok -> result.input
            is ClassifyResult.Rejected -> throw ResolveException(result.reason.message)
        }
        val paths = requireYtDlp()
        return when (parsed) {
            is ParsedInput.Playlist -> resolvePlaylist(parsed, paths)
            is ParsedInput.Video -> resolveVideo(parsed, paths)
        }
    }

    override suspend fun fetch(videoId: String): VideoMeta? {
        val paths = tools.current().takeIf { it.ytDlp != null } ?: return null
        val url = ParsedInput.Video(videoId).canonicalUrl
        return try {
            val entry = json.decodeFromString(RawEntry.serializer(), runYtDlp(YtDlpCommands.resolveVideo(paths, url)))
            VideoMeta(entry.title, entry.channel ?: entry.uploader, entry.artist, entry.track)
        } catch (e: ResolveException) {
            null
        } catch (e: SerializationException) {
            null
        }
    }

    private suspend fun resolvePlaylist(input: ParsedInput.Playlist, paths: ToolPaths): ResolveResponse {
        val stdout = runYtDlp(YtDlpCommands.resolvePlaylist(paths, input.canonicalUrl))
        val playlist = parse(RawPlaylist.serializer(), stdout)
        val entries = playlist.entries
        val items = entries.take(FilenameFormatter.MAX_RANK).mapIndexed { index, entry -> toItem(index + 1, entry) }
        return ResolveResponse(
            kind = InputKind.PLAYLIST,
            playlistTitle = playlist.title,
            items = items,
            truncated = entries.size > FilenameFormatter.MAX_RANK,
            alsoVideoId = input.alsoVideoId,
        )
    }

    private suspend fun resolveVideo(input: ParsedInput.Video, paths: ToolPaths): ResolveResponse {
        val stdout = runYtDlp(YtDlpCommands.resolveVideo(paths, input.canonicalUrl))
        val entry = parse(RawEntry.serializer(), stdout)
        return ResolveResponse(
            kind = InputKind.VIDEO,
            playlistTitle = null,
            items = listOf(toItem(rank = 1, entry = entry)),
        )
    }

    private fun toItem(rank: Int, entry: RawEntry?): ResolvedItem {
        val id = entry?.id
        val title = entry?.title.orEmpty()
        val reason = unavailableReason(entry)
        if (id == null || reason != null) {
            return ResolvedItem(
                rank = rank,
                videoId = id.orEmpty(),
                title = title,
                available = false,
                unavailableReason = reason ?: "알 수 없는 항목",
            )
        }
        val channel = entry.channel ?: entry.uploader
        val parsed = TitleParser.parse(title, channel, entry.artist, entry.track)
        return ResolvedItem(
            rank = rank,
            videoId = id,
            title = title,
            channel = channel,
            artist = parsed.artist,
            track = parsed.title,
            lowConfidence = parsed.confidence == Confidence.LOW,
            expectedFileName = FilenameFormatter.format(rank, parsed.artist, parsed.title),
        )
    }

    private fun unavailableReason(entry: RawEntry?): String? {
        if (entry == null) return "알 수 없는 항목"
        return when {
            entry.title == "[Private video]" -> "비공개 영상"
            entry.title == "[Deleted video]" -> "삭제된 영상"
            entry.availability in UNAVAILABLE_AVAILABILITY -> "사용할 수 없는 영상 (${entry.availability})"
            else -> null
        }
    }

    private fun requireYtDlp(): ToolPaths =
        tools.current().takeIf { it.ytDlp != null }
            ?: throw ResolveException("yt-dlp를 찾을 수 없습니다. 도구 설치 후 다시 시도하세요.")

    private suspend fun runYtDlp(command: List<String>): String {
        val stdout = StringBuilder()
        val stderr = mutableListOf<String>()
        val exitCode = runner.run(
            command,
            onStdout = { synchronized(stdout) { stdout.append(it).append('\n') } },
            onStderr = { synchronized(stderr) { stderr += it } },
        )
        if (exitCode != 0) {
            val failure = synchronized(stderr) { ErrorClassifier.classify(stderr.toList()) }
            throw ResolveException(failure.message)
        }
        return stdout.toString()
    }

    private fun <T> parse(serializer: kotlinx.serialization.KSerializer<T>, text: String): T =
        try {
            json.decodeFromString(serializer, text)
        } catch (e: SerializationException) {
            throw ResolveException("yt-dlp 응답을 해석할 수 없습니다.")
        }

    private companion object {
        val UNAVAILABLE_AVAILABILITY = setOf("private", "needs_auth", "premium_only", "subscriber_only")
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :engine:test`
Expected: `BUILD SUCCESSFUL`; `YtDlpResolverTest` 14 tests, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add engine
git commit -m "feat(engine): add YtDlpResolver" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Download orchestration (`ItemDownloader`, `DefaultDownloadService`)

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/job/DefaultDownloadService.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/job/DefaultDownloadServiceTest.kt`

**Interfaces:**
- Consumes: `ProcessRunner`, `ToolPathProvider`, `YtDlpCommands`, `ProgressParser`, `ErrorClassifier`, `Failure`, `FailureKind`, `VideoMetadataSource`, `OutputSink`, `DownloadRequest`, `DownloadService`, `JobHandle` (engine); `TitleParser`, `FilenameFormatter`, `JobEvent`, `JobStatus`, `JobSummary`, `ResolvedItem`, `Stage` (shared).
- Produces (package `com.xgetsongs.engine.job`): `data class PreparedItem(val item: ResolvedItem, val fileName: String)`; `sealed interface DownloadResult { data class Downloaded(val file: Path); data class Failed(val failure: Failure) }`; `class ItemDownloader(runner, tools, metadata) { suspend fun prepare(item: ResolvedItem): PreparedItem; suspend fun download(prepared: PreparedItem, workDir: Path, emit: (JobEvent) -> Unit): DownloadResult }`; `class DefaultDownloadService(downloader: ItemDownloader, tempRoot: Path, scope: CoroutineScope, retryDelays: List<Duration> = listOf(2.seconds, 4.seconds)) : DownloadService`.

Behaviour to keep: `prepare` re-parses items whose artist came from the channel name using the full video metadata (so the file name can change; `ItemStarted` carries the final name); progress events are throttled to whole percents; a missing yt-dlp is a `FATAL` failure; the job always ends with exactly one `JobDone` (status `COMPLETED`, `FAILED` after a fatal error, or `CANCELLED`) and then closes the event channel; the job's work folder is deleted in every case.

- [ ] **Step 1: Write the failing test**

```kotlin
package com.xgetsongs.engine.job

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.JobHandle
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.output.OutputSink
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.testutil.FakeProcessRunner
import com.xgetsongs.engine.testutil.TEST_TOOLS
import com.xgetsongs.engine.testutil.toolsOf
import com.xgetsongs.engine.testutil.writeFakeMp3
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.ytdlp.VideoMeta
import com.xgetsongs.engine.ytdlp.VideoMetadataSource
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.filename.FilenameFormatter
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class DefaultDownloadServiceTest {
    private val root: Path = Files.createTempDirectory("xgs-service")
    private val outDir = root.resolve("out")
    private val tempRoot = root.resolve("work")

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private fun item(rank: Int, artist: String = "A$rank", track: String = "T$rank", lowConfidence: Boolean = false) =
        ResolvedItem(
            rank = rank,
            videoId = "vid%08d".format(rank),
            title = "$artist - $track",
            channel = "ch",
            artist = artist,
            track = track,
            lowConfidence = lowConfidence,
            expectedFileName = FilenameFormatter.format(rank, artist, track),
        )

    private fun TestScope.service(
        runner: ProcessRunner,
        tools: ToolPathProvider = toolsOf(),
        metadata: VideoMetadataSource = VideoMetadataSource { null },
    ) = DefaultDownloadService(ItemDownloader(runner, tools, metadata), tempRoot, this)

    private fun request(
        vararg items: ResolvedItem,
        overwrite: Boolean = false,
        concurrency: Int = 1,
        sink: OutputSink = LocalFolderSink(outDir),
    ) = DownloadRequest(items.toList(), sink, overwrite, concurrency)

    private suspend fun JobHandle.collect(): List<JobEvent> = events.receiveAsFlow().toList()

    private val succeeding = FakeProcessRunner { command, onStdout, _ ->
        onStdout("XGSP|downloading|50|100|NA")
        onStdout("XGSPP|started|ExtractAudio")
        writeFakeMp3(command)
        0
    }

    private fun failingWith(vararg stderr: String) = FakeProcessRunner { _, _, onStderr ->
        stderr.forEach(onStderr)
        1
    }

    private fun done(events: List<JobEvent>) = events.last() as JobEvent.JobDone

    @Test
    fun downloadsEveryItemAndMovesFilesIntoTheSink() = runTest {
        val handle = service(succeeding).start(request(item(1), item(2)))

        val events = handle.collect()

        assertTrue(Files.exists(outDir.resolve("001 A1 - T1.mp3")))
        assertTrue(Files.exists(outDir.resolve("002 A2 - T2.mp3")))
        assertEquals(setOf(1, 2), events.filterIsInstance<JobEvent.ItemStarted>().map { it.rank }.toSet())
        assertEquals(setOf("001 A1 - T1.mp3", "002 A2 - T2.mp3"), events.filterIsInstance<JobEvent.ItemDone>().map { it.fileName }.toSet())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(2, 0, 0)), done(events))
    }

    @Test
    fun reportsDownloadAndConvertingProgress() = runTest {
        val events = service(succeeding).start(request(item(1))).collect()

        val progress = events.filterIsInstance<JobEvent.Progress>()
        assertEquals(JobEvent.Progress(1, Stage.DOWNLOADING, 50.0), progress.first())
        assertEquals(JobEvent.Progress(1, Stage.CONVERTING, null), progress.last())
    }

    @Test
    fun progressEventsAreThrottledToWholePercents() = runTest {
        val noisy = FakeProcessRunner { command, onStdout, _ ->
            repeat(1000) { onStdout("XGSP|downloading|${it / 10}|100|NA") }
            writeFakeMp3(command)
            0
        }

        val events = service(noisy).start(request(item(1))).collect()

        assertEquals(100, events.filterIsInstance<JobEvent.Progress>().size)
    }

    @Test
    fun existingFilesAreSkippedUnlessOverwriteIsOn() = runTest {
        Files.createDirectories(outDir)
        Files.writeString(outDir.resolve("001 A1 - T1.mp3"), "old")

        val skipped = service(succeeding).start(request(item(1))).collect()
        assertEquals(listOf(JobEvent.ItemSkipped(1, "이미 존재")), skipped.filterIsInstance<JobEvent.ItemSkipped>())
        assertEquals("old", Files.readString(outDir.resolve("001 A1 - T1.mp3")))
        assertEquals(1, done(skipped).summary.skipped)

        service(succeeding).start(request(item(1), overwrite = true)).collect()
        assertEquals("mp3-data", Files.readString(outDir.resolve("001 A1 - T1.mp3")))
    }

    @Test
    fun transientFailuresAreRetriedWithBackoff() = runTest {
        val calls = AtomicInteger()
        val flaky = FakeProcessRunner { command, _, onStderr ->
            if (calls.incrementAndGet() == 1) {
                onStderr("ERROR: unable to download video data: HTTP Error 503: Service Unavailable")
                1
            } else {
                writeFakeMp3(command)
                0
            }
        }

        val events = service(flaky).start(request(item(1))).collect()

        assertEquals(2, flaky.commands.size)
        assertTrue(events.none { it is JobEvent.ItemFailed })
        assertEquals(JobSummary(1, 0, 0), done(events).summary)
    }

    @Test
    fun givesUpAfterTwoRetries() = runTest {
        val runner = failingWith("ERROR: HTTP Error 429: Too Many Requests")

        val events = service(runner).start(request(item(1))).collect()

        assertEquals(3, runner.commands.size)
        assertEquals(1, events.filterIsInstance<JobEvent.ItemFailed>().size)
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(0, 0, 1)), done(events))
    }

    @Test
    fun unavailableVideosAreSkippedWithoutRetry() = runTest {
        val runner = failingWith("ERROR: [youtube] x: Private video. Sign in if you've been granted access")

        val events = service(runner).start(request(item(1))).collect()

        assertEquals(1, runner.commands.size)
        assertEquals(listOf(JobEvent.ItemSkipped(1, "비공개 영상")), events.filterIsInstance<JobEvent.ItemSkipped>())
        assertEquals(JobSummary(0, 1, 0), done(events).summary)
    }

    @Test
    fun otherFailuresDoNotStopTheRemainingItems() = runTest {
        val runner = FakeProcessRunner { command, _, onStderr ->
            if (command.any { it.contains("vid00000001") }) {
                onStderr("ERROR: something odd")
                1
            } else {
                writeFakeMp3(command)
                0
            }
        }

        val events = service(runner).start(request(item(1), item(2))).collect()

        assertEquals(listOf(1), events.filterIsInstance<JobEvent.ItemFailed>().map { it.rank })
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)), done(events))
    }

    @Test
    fun fatalErrorAbortsTheWholeJob() = runTest {
        val runner = failingWith("OSError: [Errno 28] No space left on device")

        val events = service(runner).start(request(item(1), item(2), item(3))).collect()

        assertEquals(JobStatus.FAILED, done(events).status)
        assertEquals(listOf(1), events.filterIsInstance<JobEvent.ItemStarted>().map { it.rank })
        assertEquals(1, runner.commands.size)
    }

    @Test
    fun missingYtDlpAbortsTheJob() = runTest {
        val events = service(succeeding, tools = toolsOf(TEST_TOOLS.copy(ytDlp = null))).start(request(item(1))).collect()

        assertEquals(JobStatus.FAILED, done(events).status)
        assertTrue(events.filterIsInstance<JobEvent.ItemFailed>().single().message.contains("yt-dlp"))
    }

    @Test
    fun cancellingStopsRunningDownloadsAndReportsCancelled() = runTest {
        val started = CompletableDeferred<Unit>()
        val hanging = FakeProcessRunner { _, _, _ ->
            started.complete(Unit)
            awaitCancellation()
        }
        val handle = service(hanging).start(request(item(1)))
        started.await()

        handle.cancel()
        val events = handle.collect()

        assertEquals(JobStatus.CANCELLED, done(events).status)
        assertEquals(1, hanging.commands.size)
    }

    @Test
    fun workDirectoriesAreRemovedAfterTheJob() = runTest {
        service(succeeding).start(request(item(1), item(2))).collect()

        Files.list(tempRoot).use { assertEquals(0, it.count()) }
    }

    @Test
    fun cancellingAlsoRemovesWorkDirectories() = runTest {
        val started = CompletableDeferred<Unit>()
        val hanging = FakeProcessRunner { command, _, _ ->
            writeFakeMp3(command)
            started.complete(Unit)
            awaitCancellation()
        }
        val handle = service(hanging).start(request(item(1)))
        started.await()

        handle.cancel()
        handle.collect()

        Files.list(tempRoot).use { assertEquals(0, it.count()) }
    }

    /** Waits in real time until [count] downloads have started, then gives an unwanted extra one time to show up. */
    private suspend fun awaitStarted(runner: FakeProcessRunner, count: Int) = withContext(Dispatchers.Default) {
        withTimeout(10_000) { while (runner.commands.size < count) delay(10) }
        delay(200)
    }

    @Test
    fun concurrencyIsLimitedToTheRequestedNumber() = runTest {
        val gate = CompletableDeferred<Unit>()
        val held = FakeProcessRunner { command, _, _ ->
            gate.await()
            writeFakeMp3(command)
            0
        }
        val handle = service(held).start(request(*(1..6).map { item(it) }.toTypedArray(), concurrency = 2))

        awaitStarted(held, 2)
        assertEquals(2, held.commands.size, "the other four items must wait for a free slot")
        gate.complete(Unit)
        handle.collect()

        assertEquals(2, held.maxActive.get())
        assertEquals(6, held.commands.size)
    }

    @Test
    fun concurrencyIsClampedToFour() = runTest {
        val gate = CompletableDeferred<Unit>()
        val held = FakeProcessRunner { command, _, _ ->
            gate.await()
            writeFakeMp3(command)
            0
        }
        val handle = service(held).start(request(*(1..8).map { item(it) }.toTypedArray(), concurrency = 99))

        awaitStarted(held, 4)
        assertEquals(4, held.commands.size, "99 is clamped to 4 parallel downloads")
        gate.complete(Unit)
        handle.collect()

        assertEquals(4, held.maxActive.get())
    }

    @Test
    fun lowConfidenceItemsAreRenamedFromFullMetadata() = runTest {
        val metadata = VideoMetadataSource { VideoMeta("Dynamite", "BTS - Topic", "BTS", "Dynamite") }
        val lowConfidence = item(1, artist = "BTS - Topic", track = "Dynamite", lowConfidence = true)

        val events = service(succeeding, metadata = metadata).start(request(lowConfidence)).collect()

        assertEquals("001 BTS - Dynamite.mp3", events.filterIsInstance<JobEvent.ItemStarted>().single().fileName)
        assertTrue(Files.exists(outDir.resolve("001 BTS - Dynamite.mp3")))
    }

    @Test
    fun sinkFailuresAreReportedPerItem() = runTest {
        val brokenSink = object : OutputSink {
            override suspend fun exists(fileName: String) = false
            override suspend fun put(fileName: String, source: Path, overwrite: Boolean) {
                throw IOException("disk on fire")
            }
        }

        val events = service(succeeding).start(request(item(1), sink = brokenSink)).collect()

        val failed = events.filterIsInstance<JobEvent.ItemFailed>().single()
        assertTrue(failed.message.contains("disk on fire"))
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(0, 0, 1)), done(events))
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :engine:test`
Expected: FAIL — `Unresolved reference 'DefaultDownloadService'`, `'ItemDownloader'`.

- [ ] **Step 3: Write the implementations**

`ItemDownloader.kt`:

```kotlin
package com.xgetsongs.engine.job

import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.ytdlp.ErrorClassifier
import com.xgetsongs.engine.ytdlp.Failure
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.engine.ytdlp.ProgressParser
import com.xgetsongs.engine.ytdlp.ProgressUpdate
import com.xgetsongs.engine.ytdlp.VideoMetadataSource
import com.xgetsongs.engine.ytdlp.YtDlpCommands
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.filename.FilenameFormatter
import com.xgetsongs.shared.input.ParsedInput
import com.xgetsongs.shared.title.TitleParser
import java.nio.file.Files
import java.nio.file.Path

/** An item whose final file name is settled. */
data class PreparedItem(val item: ResolvedItem, val fileName: String)

sealed interface DownloadResult {
    /** The finished mp3, still inside the job's work directory. */
    data class Downloaded(val file: Path) : DownloadResult

    data class Failed(val failure: Failure) : DownloadResult
}

/** Downloads one video's audio with yt-dlp. Knows nothing about concurrency, retries or sinks. */
class ItemDownloader(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
    private val metadata: VideoMetadataSource,
) {
    /**
     * Settles the final file name. Items whose artist came from the channel name get a second chance:
     * the full metadata (yt-dlp `artist`/`track`) is fetched and the title is parsed again.
     */
    suspend fun prepare(item: ResolvedItem): PreparedItem {
        var artist = item.artist
        var track = item.track
        if (item.lowConfidence) {
            metadata.fetch(item.videoId)?.let { meta ->
                val parsed = TitleParser.parse(meta.title ?: item.title, meta.channel ?: item.channel, meta.artist, meta.track)
                artist = parsed.artist
                track = parsed.title
            }
        }
        return PreparedItem(item, FilenameFormatter.format(item.rank, artist, track))
    }

    /** Runs yt-dlp once. [emit] receives throttled [JobEvent.Progress] events. */
    suspend fun download(prepared: PreparedItem, workDir: Path, emit: (JobEvent) -> Unit): DownloadResult {
        val paths = tools.current()
        if (paths.ytDlp == null) {
            return DownloadResult.Failed(Failure(FailureKind.FATAL, "yt-dlp를 찾을 수 없습니다."))
        }
        val videoId = prepared.item.videoId
        val command = YtDlpCommands.download(paths, ParsedInput.Video(videoId).canonicalUrl, workDir, videoId)
        val rank = prepared.item.rank
        val stderr = mutableListOf<String>()
        val throttle = ProgressThrottle()

        val exitCode = runner.run(
            command,
            onStdout = { line ->
                ProgressParser.parse(line)?.let { update ->
                    throttle.accept(update)?.let { emit(JobEvent.Progress(rank, it.first, it.second)) }
                }
            },
            onStderr = { line -> synchronized(stderr) { stderr += line } },
        )

        if (exitCode != 0) {
            return DownloadResult.Failed(ErrorClassifier.classify(synchronized(stderr) { stderr.toList() }))
        }
        val file = workDir.resolve("$videoId.mp3")
        if (!Files.isRegularFile(file)) {
            return DownloadResult.Failed(Failure(FailureKind.OTHER, "변환된 mp3 파일을 찾을 수 없습니다."))
        }
        return DownloadResult.Downloaded(file)
    }

    /** Lets a progress update through only when the stage changes or the whole percent advances. */
    private class ProgressThrottle {
        private var stage: Stage? = null
        private var lastPercent = -1

        fun accept(update: ProgressUpdate): Pair<Stage, Double?>? = when (update) {
            is ProgressUpdate.Converting -> {
                if (stage == Stage.CONVERTING) {
                    null
                } else {
                    stage = Stage.CONVERTING
                    Stage.CONVERTING to null
                }
            }
            is ProgressUpdate.Downloading -> {
                val whole = update.percent?.toInt() ?: -1
                if (stage == Stage.DOWNLOADING && whole == lastPercent) {
                    null
                } else {
                    stage = Stage.DOWNLOADING
                    lastPercent = whole
                    Stage.DOWNLOADING to update.percent
                }
            }
        }
    }
}
```

`DefaultDownloadService.kt`:

```kotlin
package com.xgetsongs.engine.job

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.DownloadService
import com.xgetsongs.engine.JobHandle
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolvedItem
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicInteger
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds

/** Aborts the whole job (disk full, no permission...). */
private class FatalJobException(message: String) : Exception(message)

class DefaultDownloadService(
    private val downloader: ItemDownloader,
    private val tempRoot: Path,
    private val scope: CoroutineScope,
    private val retryDelays: List<Duration> = listOf(2.seconds, 4.seconds),
) : DownloadService {

    override fun start(request: DownloadRequest): JobHandle {
        val events = Channel<JobEvent>(Channel.UNLIMITED)
        val job = scope.launch { runJob(request, events) }
        return JobHandle(events, job)
    }

    private class Counters {
        val succeeded = AtomicInteger()
        val skipped = AtomicInteger()
        val failed = AtomicInteger()
        fun summary() = JobSummary(succeeded.get(), skipped.get(), failed.get())
    }

    private suspend fun runJob(request: DownloadRequest, events: Channel<JobEvent>) {
        val workRoot = withContext(Dispatchers.IO) {
            Files.createDirectories(tempRoot)
            Files.createTempDirectory(tempRoot, "job-")
        }
        val counters = Counters()
        var status = JobStatus.COMPLETED
        try {
            coroutineScope {
                val gate = Semaphore(request.concurrency.coerceIn(MIN_CONCURRENCY, MAX_CONCURRENCY))
                for (item in request.items) {
                    launch { gate.withPermit { processItem(item, request, workRoot, events, counters) } }
                }
            }
        } catch (e: FatalJobException) {
            status = JobStatus.FAILED
        } catch (e: CancellationException) {
            status = JobStatus.CANCELLED
            throw e
        } finally {
            withContext(NonCancellable) {
                workRoot.toFile().deleteRecursively()
                events.trySend(JobEvent.JobDone(status, counters.summary()))
                events.close()
            }
        }
    }

    private suspend fun processItem(
        item: ResolvedItem,
        request: DownloadRequest,
        workRoot: Path,
        events: Channel<JobEvent>,
        counters: Counters,
    ) {
        try {
            val prepared = downloader.prepare(item)
            events.trySend(JobEvent.ItemStarted(item.rank, item.videoId, prepared.fileName))
            if (!request.overwrite && request.sink.exists(prepared.fileName)) {
                events.trySend(JobEvent.ItemSkipped(item.rank, "이미 존재"))
                counters.skipped.incrementAndGet()
                return
            }
            val itemDir = withContext(Dispatchers.IO) { Files.createDirectories(workRoot.resolve(item.videoId)) }
            var retries = 0
            while (true) {
                when (val result = downloader.download(prepared, itemDir) { events.trySend(it) }) {
                    is DownloadResult.Downloaded -> {
                        request.sink.put(prepared.fileName, result.file, request.overwrite)
                        events.trySend(JobEvent.ItemDone(item.rank, prepared.fileName))
                        counters.succeeded.incrementAndGet()
                        return
                    }
                    is DownloadResult.Failed -> {
                        val failure = result.failure
                        when (failure.kind) {
                            FailureKind.UNAVAILABLE -> {
                                events.trySend(JobEvent.ItemSkipped(item.rank, failure.message))
                                counters.skipped.incrementAndGet()
                                return
                            }
                            FailureKind.TRANSIENT -> {
                                if (retries < retryDelays.size) {
                                    delay(retryDelays[retries])
                                    retries++
                                    continue
                                }
                                fail(item, failure.message, events, counters)
                                return
                            }
                            FailureKind.FATAL -> {
                                fail(item, failure.message, events, counters)
                                throw FatalJobException(failure.message)
                            }
                            FailureKind.OTHER -> {
                                fail(item, failure.message, events, counters)
                                return
                            }
                        }
                    }
                }
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: FatalJobException) {
            throw e
        } catch (e: IOException) {
            fail(item, "파일 처리 중 오류: ${e.message}", events, counters)
        } catch (e: Exception) {
            fail(item, e.message ?: e::class.simpleName.orEmpty(), events, counters)
        }
    }

    private fun fail(item: ResolvedItem, message: String, events: Channel<JobEvent>, counters: Counters) {
        events.trySend(JobEvent.ItemFailed(item.rank, message))
        counters.failed.incrementAndGet()
    }

    private companion object {
        const val MIN_CONCURRENCY = 1
        const val MAX_CONCURRENCY = 4
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :engine:test`
Expected: `BUILD SUCCESSFUL`; `DefaultDownloadServiceTest` 17 tests, 0 failures. The retry tests finish instantly because `runTest` skips the 2 s / 4 s delays.

- [ ] **Step 5: Commit**

```powershell
git add engine
git commit -m "feat(engine): add ItemDownloader and DefaultDownloadService" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 11: `DefaultToolManager`

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/tools/DefaultToolManager.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/tools/DefaultToolManagerTest.kt`

**Interfaces:**
- Consumes: `ToolManager`, `ToolException` (Task 8), `ProcessRunner`, `ToolPathProvider`, `ToolPaths`, `FakeProcessRunner`.
- Produces: `class DefaultToolManager(tools: ToolPathProvider, runner: ProcessRunner, binDir: Path, fetch: suspend (String) -> ByteArray = ::httpGet) : ToolManager` with `companion object { const val YTDLP_URL = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" }`.

Behaviour to keep: `status()` runs `--version`/`-version` and reports versions; the JS runtime counts as found only for Deno ≥ 2.3.0 or Node ≥ 22.0.0; `installYtDlp()` downloads to `<binDir>/yt-dlp.exe` (rejects downloads under 1 MB, verifies the result runs) and **downloads from GitHub — the UI asks the user first**; `updateYtDlp()` runs `yt-dlp -U`.

- [ ] **Step 1: Write the failing test**

```kotlin
package com.xgetsongs.engine.tools

import com.xgetsongs.engine.ToolException
import com.xgetsongs.engine.testutil.FakeProcessRunner
import kotlinx.coroutines.test.runTest
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class DefaultToolManagerTest {
    private val root: Path = Files.createTempDirectory("xgs-manager")
    private val binDir = root.resolve("bin")

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private fun runnerFor(outputs: Map<String, String>, exitCode: Int = 0) =
        FakeProcessRunner { command, onStdout, _ ->
            val tool = Path.of(command.first()).fileName.toString().removeSuffix(".exe")
            outputs[tool]?.lines()?.forEach(onStdout)
            if (outputs.containsKey(tool)) exitCode else 1
        }

    private fun paths(ytDlp: Boolean = true, js: String = "node") = ToolPaths(
        ytDlp = if (ytDlp) Path.of("C:/t/yt-dlp.exe") else null,
        ffmpeg = Path.of("C:/t/ffmpeg.exe"),
        jsRuntime = Path.of("C:/t/$js.exe"),
    )

    @Test
    fun reportsVersionsOfAllTools() = runTest {
        val runner = runnerFor(
            mapOf(
                "yt-dlp" to "2026.10.01",
                "ffmpeg" to "ffmpeg version 8.1-full_build-www.gyan.dev Copyright (c) 2000-2026 the FFmpeg developers",
                "node" to "v24.11.1",
            ),
        )

        val status = DefaultToolManager(ToolPathProvider { paths() }, runner, binDir).status()

        assertTrue(status.ytDlp.found)
        assertEquals("2026.10.01", status.ytDlp.version)
        assertEquals("8.1-full_build-www.gyan.dev", status.ffmpeg.version)
        assertTrue(status.jsRuntime.found)
        assertEquals("24.11.1", status.jsRuntime.version)
    }

    @Test
    fun denoVersionIsParsedFromItsBanner() = runTest {
        val runner = runnerFor(mapOf("yt-dlp" to "1", "ffmpeg" to "ffmpeg version 7", "deno" to "deno 2.5.0 (stable, release, x86_64-pc-windows-msvc)"))

        val status = DefaultToolManager(ToolPathProvider { paths(js = "deno") }, runner, binDir).status()

        assertTrue(status.jsRuntime.found)
        assertEquals("2.5.0", status.jsRuntime.version)
    }

    @Test
    fun oldJsRuntimesAreReportedAsNotUsable() = runTest {
        val old = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(mapOf("yt-dlp" to "1", "ffmpeg" to "ffmpeg version 7", "node" to "v20.11.0")),
            binDir,
        ).status()
        assertFalse(old.jsRuntime.found)
        assertEquals("20.11.0", old.jsRuntime.version)

        val oldDeno = DefaultToolManager(
            ToolPathProvider { paths(js = "deno") },
            runnerFor(mapOf("yt-dlp" to "1", "ffmpeg" to "ffmpeg version 7", "deno" to "deno 2.2.9 (stable)")),
            binDir,
        ).status()
        assertFalse(oldDeno.jsRuntime.found)
    }

    @Test
    fun missingToolsAreReportedNotFound() = runTest {
        val manager = DefaultToolManager(
            ToolPathProvider { ToolPaths(null, null, null) },
            runnerFor(emptyMap()),
            binDir,
        )

        val status = manager.status()

        assertFalse(status.ytDlp.found)
        assertFalse(status.ffmpeg.found)
        assertFalse(status.jsRuntime.found)
    }

    @Test
    fun installDownloadsYtDlpIntoTheAppBinDirectory() = runTest {
        val provider = ToolPathProvider {
            ToolPaths(binDir.resolve("yt-dlp.exe").takeIf { Files.exists(it) }, null, null)
        }
        val fetched = mutableListOf<String>()
        val manager = DefaultToolManager(
            provider,
            runnerFor(mapOf("yt-dlp" to "2026.10.01")),
            binDir,
            fetch = { url -> fetched += url; ByteArray(2_000_000) },
        )

        val result = manager.installYtDlp()

        assertEquals(listOf(DefaultToolManager.YTDLP_URL), fetched)
        assertTrue(Files.exists(binDir.resolve("yt-dlp.exe")))
        assertTrue(result.message.contains("2026.10.01"))
    }

    @Test
    fun installRejectsATooSmallDownload() = runTest {
        val manager = DefaultToolManager(ToolPathProvider { paths() }, runnerFor(emptyMap()), binDir, fetch = { ByteArray(10) })

        assertFailsWith<ToolException> { manager.installYtDlp() }
        assertFalse(Files.exists(binDir.resolve("yt-dlp.exe")))
    }

    @Test
    fun installReportsNetworkFailures() = runTest {
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(emptyMap()),
            binDir,
            fetch = { throw IOException("offline") },
        )

        val error = assertFailsWith<ToolException> { manager.installYtDlp() }

        assertTrue(error.message!!.contains("offline"))
    }

    @Test
    fun updateRunsYtDlpSelfUpdate() = runTest {
        val runner = runnerFor(mapOf("yt-dlp" to "Current version: 2026.10.01\nUpdated yt-dlp to 2026.11.02"))
        val manager = DefaultToolManager(ToolPathProvider { paths() }, runner, binDir)

        val result = manager.updateYtDlp()

        assertTrue(runner.commands.single().contains("-U"))
        assertEquals("Updated yt-dlp to 2026.11.02", result.message)
    }

    @Test
    fun updateFailureIsReported() = runTest {
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(mapOf("yt-dlp" to "ERROR: Unable to update"), exitCode = 1),
            binDir,
        )

        val error = assertFailsWith<ToolException> { manager.updateYtDlp() }

        assertEquals("ERROR: Unable to update", error.message)
    }

    @Test
    fun updateNeedsAnInstalledYtDlp() = runTest {
        val manager = DefaultToolManager(ToolPathProvider { paths(ytDlp = false) }, runnerFor(emptyMap()), binDir)

        assertFailsWith<ToolException> { manager.updateYtDlp() }
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :engine:test`
Expected: FAIL — `Unresolved reference 'DefaultToolManager'`.

- [ ] **Step 3: Write the implementation**

```kotlin
package com.xgetsongs.engine.tools

import com.xgetsongs.engine.ToolException
import com.xgetsongs.engine.ToolManager
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.ToolInfo
import com.xgetsongs.shared.api.ToolsStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withContext
import java.io.IOException
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.time.Duration

/**
 * Reports tool versions and installs/updates yt-dlp. Installing downloads a file from GitHub, so
 * callers must ask the user for consent before calling [installYtDlp].
 */
class DefaultToolManager(
    private val tools: ToolPathProvider,
    private val runner: ProcessRunner,
    private val binDir: Path,
    private val fetch: suspend (url: String) -> ByteArray = ::httpGet,
) : ToolManager {

    override suspend fun status(): ToolsStatus {
        val paths = tools.current()
        return ToolsStatus(
            ytDlp = info(paths.ytDlp, "--version") { it },
            ffmpeg = info(paths.ffmpeg, "-version") { it.removePrefix("ffmpeg version ").substringBefore(' ') },
            jsRuntime = jsRuntimeInfo(paths.jsRuntime),
        )
    }

    override suspend fun installYtDlp(): ActionResult {
        val bytes = try {
            fetch(YTDLP_URL)
        } catch (e: IOException) {
            throw ToolException("yt-dlp 다운로드에 실패했습니다: ${e.message}")
        }
        if (bytes.size < MIN_YTDLP_BYTES) throw ToolException("내려받은 파일이 너무 작습니다. 다시 시도하세요.")
        withContext(Dispatchers.IO) {
            Files.createDirectories(binDir)
            val partial = binDir.resolve("yt-dlp.exe.download")
            Files.write(partial, bytes)
            Files.move(partial, binDir.resolve("yt-dlp.exe"), StandardCopyOption.REPLACE_EXISTING)
        }
        val version = status().ytDlp.version
            ?: throw ToolException("설치한 yt-dlp를 실행할 수 없습니다.")
        return ActionResult("yt-dlp $version 을(를) 설치했습니다.")
    }

    override suspend fun updateYtDlp(): ActionResult {
        val ytDlp = tools.current().ytDlp ?: throw ToolException("yt-dlp가 설치되어 있지 않습니다.")
        val lines = mutableListOf<String>()
        val exitCode = runner.run(
            listOf(ytDlp.toString(), "--ignore-config", "-U"),
            onStdout = { synchronized(lines) { lines += it } },
            onStderr = { synchronized(lines) { lines += it } },
        )
        val output = synchronized(lines) { lines.filter { it.isNotBlank() } }
        if (exitCode != 0) {
            throw ToolException(output.lastOrNull() ?: "yt-dlp 업데이트에 실패했습니다.")
        }
        return ActionResult(output.lastOrNull() ?: "yt-dlp를 업데이트했습니다.")
    }

    private suspend fun info(path: Path?, versionArg: String, parse: (String) -> String): ToolInfo {
        if (path == null) return ToolInfo(found = false)
        val version = firstLine(path, versionArg)?.let(parse)
        return ToolInfo(found = version != null, version = version, path = path.toString())
    }

    private suspend fun jsRuntimeInfo(path: Path?): ToolInfo {
        if (path == null) return ToolInfo(found = false)
        val name = path.fileName.toString().lowercase().removeSuffix(".exe")
        val line = firstLine(path, "--version")
        val version = when (name) {
            "deno" -> line?.split(' ')?.getOrNull(1)
            else -> line?.removePrefix("v")
        }
        val minimum = if (name == "deno") DENO_MINIMUM else NODE_MINIMUM
        val supported = version != null && isAtLeast(version, minimum)
        return ToolInfo(found = supported, version = version, path = path.toString())
    }

    private suspend fun firstLine(path: Path, arg: String): String? {
        val lines = mutableListOf<String>()
        val exitCode = try {
            runner.run(listOf(path.toString(), arg), onStdout = { synchronized(lines) { lines += it } })
        } catch (e: IOException) {
            return null
        }
        if (exitCode != 0) return null
        return synchronized(lines) { lines.firstOrNull { it.isNotBlank() } }?.trim()
    }

    private fun isAtLeast(version: String, minimum: List<Int>): Boolean {
        val parts = version.split('.').map { it.takeWhile(Char::isDigit).toIntOrNull() ?: 0 }
        for (i in minimum.indices) {
            val actual = parts.getOrElse(i) { 0 }
            if (actual != minimum[i]) return actual > minimum[i]
        }
        return true
    }

    companion object {
        const val YTDLP_URL = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
        private const val MIN_YTDLP_BYTES = 1_000_000
        private val DENO_MINIMUM = listOf(2, 3, 0)
        private val NODE_MINIMUM = listOf(22, 0, 0)
    }
}

private suspend fun httpGet(url: String): ByteArray = runInterruptible(Dispatchers.IO) {
    val client = HttpClient.newBuilder()
        .followRedirects(HttpClient.Redirect.NORMAL)
        .connectTimeout(Duration.ofSeconds(15))
        .build()
    val response = client.send(HttpRequest.newBuilder(URI.create(url)).GET().build(), HttpResponse.BodyHandlers.ofByteArray())
    if (response.statusCode() != 200) throw IOException("HTTP ${response.statusCode()}")
    response.body()
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :engine:test`
Expected: `BUILD SUCCESSFUL`; `DefaultToolManagerTest` 10 tests; the engine module has 75 tests in total, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add engine
git commit -m "feat(engine): add DefaultToolManager" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 12: `server` module and the request guard

**Files:**
- Modify: `settings.gradle.kts` (add `include(":server")` after `include(":engine")`)
- Create: `server/build.gradle.kts`
- Create: `server/src/main/kotlin/com/xgetsongs/server/ServerConfig.kt`
- Create: `server/src/main/kotlin/com/xgetsongs/server/Guard.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/GuardTest.kt`

**Interfaces:**
- Consumes: `Resolver`, `DownloadService`, `ToolManager` (engine), `ApiHeaders`, `ApiJson`, `ErrorResponse` (shared).
- Produces (package `com.xgetsongs.server`): `enum class ServerMode { LOCAL, HOSTED }`; `data class ServerConfig(val token: String, val mode: ServerMode = ServerMode.LOCAL)`; `class Services(val resolver: Resolver, val downloads: DownloadService, val tools: ToolManager)`; `fun Application.installLocalGuard(config: ServerConfig)` — answers 403 for a non-loopback `Host` or any `Origin` header, 401 for a missing/wrong `X-XGS-Token`, and stops the pipeline before the route runs. It serialises its own JSON error body, so it works without `ContentNegotiation`.

- [ ] **Step 1: Add the module**

Edit `settings.gradle.kts` so the last lines read:

```kotlin
include(":shared")
include(":engine")
include(":server")
```

Create `server/build.gradle.kts` (the CIO client is only used by the real-server tests of Task 14):

```kotlin
plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    api(project(":engine"))
    implementation(libs.ktor.server.core)
    implementation(libs.ktor.server.netty)
    implementation(libs.ktor.server.content.negotiation)
    implementation(libs.ktor.server.sse)
    implementation(libs.ktor.server.status.pages)
    implementation(libs.ktor.serialization.kotlinx.json)
    runtimeOnly(libs.logback.classic)

    testImplementation(kotlin("test"))
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.ktor.server.test.host)
    testImplementation(libs.ktor.client.cio)
    testImplementation(libs.ktor.client.content.negotiation)
    testImplementation(libs.ktor.serialization.kotlinx.json)
}

tasks.test {
    useJUnitPlatform()
}
```

- [ ] **Step 2: Write the failing test**

The test mounts the guard in front of a single counting route, so it does not depend on the real routes.

```kotlin
package com.xgetsongs.server

import com.xgetsongs.shared.api.ApiHeaders
import io.ktor.client.HttpClient
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.statement.bodyAsText
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.server.response.respondText
import io.ktor.server.routing.get
import io.ktor.server.routing.routing
import io.ktor.server.testing.ApplicationTestBuilder
import io.ktor.server.testing.testApplication
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.Test
import kotlin.test.assertEquals

/** Tests the guard on its own, in front of a single route that counts how often it is reached. */
class GuardTest {
    private val token = "guard-test-token"
    private val reached = AtomicInteger()

    private fun ApplicationTestBuilder.guardedApp(): HttpClient {
        application {
            installLocalGuard(ServerConfig(token))
            routing {
                get("/ping") {
                    reached.incrementAndGet()
                    call.respondText("pong")
                }
            }
        }
        return createClient { }
    }

    @Test
    fun requestsWithoutTheTokenAreRefusedAndNeverReachTheRoute() = testApplication {
        val response = guardedApp().get("/ping")

        assertEquals(HttpStatusCode.Unauthorized, response.status)
        assertEquals(0, reached.get())
    }

    @Test
    fun aWrongTokenIsRefused() = testApplication {
        val response = guardedApp().get("/ping") { header(ApiHeaders.TOKEN, "nope") }

        assertEquals(HttpStatusCode.Unauthorized, response.status)
        assertEquals(0, reached.get())
    }

    @Test
    fun browserRequestsAreRefusedEvenWithAValidToken() = testApplication {
        val response = guardedApp().get("/ping") {
            header(ApiHeaders.TOKEN, token)
            header(HttpHeaders.Origin, "http://evil.example")
        }

        assertEquals(HttpStatusCode.Forbidden, response.status)
        assertEquals(0, reached.get())
    }

    @Test
    fun nonLoopbackHostsAreRefused() = testApplication {
        val response = guardedApp().get("/ping") {
            header(ApiHeaders.TOKEN, token)
            header(HttpHeaders.Host, "evil.example:8080")
        }

        assertEquals(HttpStatusCode.Forbidden, response.status)
        assertEquals(0, reached.get())
    }

    @Test
    fun loopbackHostsAreAccepted() = testApplication {
        val client = guardedApp()

        assertEquals(HttpStatusCode.OK, client.get("/ping") { header(ApiHeaders.TOKEN, token); header(HttpHeaders.Host, "127.0.0.1:51234") }.status)
        assertEquals(HttpStatusCode.OK, client.get("/ping") { header(ApiHeaders.TOKEN, token); header(HttpHeaders.Host, "localhost:51234") }.status)
    }

    @Test
    fun validRequestsReachTheRoute() = testApplication {
        val response = guardedApp().get("/ping") { header(ApiHeaders.TOKEN, token) }

        assertEquals(HttpStatusCode.OK, response.status)
        assertEquals("pong", response.bodyAsText())
        assertEquals(1, reached.get())
    }
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `.\gradlew.bat :server:test`
Expected: FAIL — `Unresolved reference 'installLocalGuard'`, `'ServerConfig'`.

- [ ] **Step 4: Write the implementations**

`ServerConfig.kt`:

```kotlin
package com.xgetsongs.server

import com.xgetsongs.engine.DownloadService
import com.xgetsongs.engine.Resolver
import com.xgetsongs.engine.ToolManager

/**
 * LOCAL: the desktop app's embedded server. Clients may choose the output folder.
 * HOSTED: a deployed web server. Clients must never choose server-side paths.
 */
enum class ServerMode { LOCAL, HOSTED }

data class ServerConfig(
    val token: String,
    val mode: ServerMode = ServerMode.LOCAL,
)

/** The engine pieces the routes talk to; tests replace them with fakes. */
class Services(
    val resolver: Resolver,
    val downloads: DownloadService,
    val tools: ToolManager,
)
```

`Guard.kt`:

```kotlin
package com.xgetsongs.server

import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.ErrorResponse
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.server.application.Application
import io.ktor.server.application.ApplicationCallPipeline
import io.ktor.server.application.PipelineCall
import io.ktor.server.application.call
import io.ktor.server.response.respondText
import io.ktor.util.pipeline.PipelineContext
import java.security.MessageDigest

private val LOCAL_HOSTS = setOf("127.0.0.1", "localhost", "[::1]", "::1")

/**
 * Protects a localhost server from other programs and from web pages the user happens to visit:
 * - the `Host` header must be a loopback name (blocks DNS rebinding),
 * - requests carrying an `Origin` header are refused (browsers always send it on cross-site calls;
 *   the desktop client never does),
 * - the secret token generated at start-up must be present.
 */
fun Application.installLocalGuard(config: ServerConfig) {
    val expected = config.token.toByteArray(Charsets.UTF_8)
    intercept(ApplicationCallPipeline.Plugins) {
        val host = call.request.local.serverHost.lowercase()
        when {
            host !in LOCAL_HOSTS -> reject(HttpStatusCode.Forbidden, "허용되지 않은 Host 입니다.")
            call.request.headers[HttpHeaders.Origin] != null -> reject(HttpStatusCode.Forbidden, "브라우저 요청은 허용되지 않습니다.")
            !tokenMatches(call.request.headers[ApiHeaders.TOKEN], expected) -> reject(HttpStatusCode.Unauthorized, "인증 토큰이 올바르지 않습니다.")
        }
    }
}

private suspend fun PipelineContext<Unit, PipelineCall>.reject(
    status: HttpStatusCode,
    message: String,
) {
    // Serialised by hand so the guard works without any other plugin installed.
    val body = ApiJson.instance.encodeToString(ErrorResponse.serializer(), ErrorResponse(message))
    call.respondText(body, ContentType.Application.Json, status)
    finish()
}

private fun tokenMatches(provided: String?, expected: ByteArray): Boolean =
    provided != null && MessageDigest.isEqual(provided.toByteArray(Charsets.UTF_8), expected)
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `.\gradlew.bat :server:test`
Expected: `BUILD SUCCESSFUL`; `GuardTest` 6 tests, 0 failures.

- [ ] **Step 6: Commit**

```powershell
git add settings.gradle.kts server
git commit -m "feat(server): add module and request guard" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Routes

**Files:**
- Create: `server/src/main/kotlin/com/xgetsongs/server/Registries.kt`
- Create: `server/src/main/kotlin/com/xgetsongs/server/Application.kt`
- Create: `server/src/test/kotlin/com/xgetsongs/server/TestSupport.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/RoutesTest.kt`

**Interfaces:**
- Consumes: `Services`, `ServerConfig`, `ServerMode`, `installLocalGuard` (Task 12); `DownloadRequest`, `LocalFolderSink`, `ResolveException`, `ToolException` (engine); API models, `FilenameFormatter` (shared).
- Produces: `class ResolveCache(maxEntries = 20) { fun put(response): ResolveResponse /* assigns resolveId */; fun get(id): ResolveResponse? }`; `class JobRegistry { fun register(handle): String; fun exists(id): Boolean; fun claim(id): JobHandle? /* first caller only */; fun cancel(id): Boolean; fun remove(id) }`; `class ApiException(val status: HttpStatusCode, message)`; `fun Application.module(services: Services, config: ServerConfig)`.
- Routes: `GET /tools`; `POST /tools/yt-dlp/install`; `POST /tools/yt-dlp/update`; `POST /resolve` (body `ResolveRequest` → `ResolveResponse` with a `resolveId`); `POST /jobs` (body `JobRequest` → `201 JobCreated`; 404 unknown/expired `resolveId`, 400 invalid options, 501 in `HOSTED` mode without `outputDir`); `GET /jobs/{id}/events` (SSE, one reader per job; unknown or already claimed jobs get a single `error` event); `DELETE /jobs/{id}` (204, or 404). Errors are JSON `ErrorResponse`; `ResolveException`/`ToolException` map to 422, malformed bodies to 400.
- Test helpers (package `com.xgetsongs.server`): `TEST_TOKEN`, `FakeResolver`, `FakeDownloads` (`requests`, `jobs`, `queued`, `closeAfterQueued`, `channel`), `FakeTools`, `TestServices`, `samplePlaylist()`, `sampleVideo()`, `sampleItem(...)`, `ApplicationTestBuilder.installServer(...)`, `ApplicationTestBuilder.apiClient(...)`.

- [ ] **Step 1: Write the test helpers and the failing test**

`TestSupport.kt`:

```kotlin
package com.xgetsongs.server

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.DownloadService
import com.xgetsongs.engine.JobHandle
import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.Resolver
import com.xgetsongs.engine.ToolException
import com.xgetsongs.engine.ToolManager
import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.ToolInfo
import com.xgetsongs.shared.api.ToolsStatus
import io.ktor.client.HttpClient
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.plugins.sse.SSE
import io.ktor.client.request.header
import io.ktor.serialization.kotlinx.json.json
import io.ktor.server.testing.ApplicationTestBuilder
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import java.util.concurrent.CopyOnWriteArrayList

const val TEST_TOKEN = "test-token"

class FakeResolver(
    var response: ResolveResponse = samplePlaylist(),
    var error: ResolveException? = null,
) : Resolver {
    val inputs = CopyOnWriteArrayList<String>()

    override suspend fun resolve(input: String): ResolveResponse {
        inputs += input
        error?.let { throw it }
        return response
    }
}

class FakeDownloads : DownloadService {
    val requests = CopyOnWriteArrayList<DownloadRequest>()
    val jobs = CopyOnWriteArrayList<Job>()

    /** Events queued for the next job; when [closeAfterQueued] is false the stream stays open. */
    var queued: List<JobEvent> = emptyList()
    var closeAfterQueued = true
    var channel: Channel<JobEvent>? = null

    override fun start(request: DownloadRequest): JobHandle {
        requests += request
        val events = Channel<JobEvent>(Channel.UNLIMITED)
        queued.forEach { events.trySend(it) }
        if (closeAfterQueued) events.close()
        channel = events
        val job = Job()
        jobs += job
        return JobHandle(events, job)
    }
}

class FakeTools : ToolManager {
    var installError: ToolException? = null

    override suspend fun status(): ToolsStatus {
        return ToolsStatus(
            ytDlp = ToolInfo(true, "2026.10.01", "C:/t/yt-dlp.exe"),
            ffmpeg = ToolInfo(true, "8.1", "C:/t/ffmpeg.exe"),
            jsRuntime = ToolInfo(true, "24.11.1", "C:/t/node.exe"),
        )
    }

    override suspend fun installYtDlp(): ActionResult {
        installError?.let { throw it }
        return ActionResult("installed")
    }

    override suspend fun updateYtDlp(): ActionResult = ActionResult("updated")
}

fun samplePlaylist() = ResolveResponse(
    kind = InputKind.PLAYLIST,
    playlistTitle = "Sample",
    items = listOf(
        sampleItem(1, "A", "One"),
        ResolvedItem(rank = 2, videoId = "vid00000002", title = "[Private video]", available = false, unavailableReason = "비공개 영상"),
        sampleItem(3, "C", "Three"),
    ),
)

fun sampleVideo() = ResolveResponse(kind = InputKind.VIDEO, items = listOf(sampleItem(1, "A", "One")))

fun sampleItem(rank: Int, artist: String, track: String) = ResolvedItem(
    rank = rank,
    videoId = "vid%08d".format(rank),
    title = "$artist - $track",
    artist = artist,
    track = track,
    expectedFileName = "%03d $artist - $track.mp3".format(rank),
)

class TestServices(
    val resolver: FakeResolver = FakeResolver(),
    val downloads: FakeDownloads = FakeDownloads(),
    val tools: FakeTools = FakeTools(),
) {
    val services = Services(resolver, downloads, tools)
}

fun ApplicationTestBuilder.installServer(services: Services, mode: ServerMode = ServerMode.LOCAL) {
    application { module(services, ServerConfig(TEST_TOKEN, mode)) }
}

/** A client that speaks the API: JSON bodies, SSE, and the token header. */
fun ApplicationTestBuilder.apiClient(token: String? = TEST_TOKEN): HttpClient = createClient {
    install(ContentNegotiation) { json(ApiJson.instance) }
    install(SSE)
    defaultRequest { if (token != null) header(ApiHeaders.TOKEN, token) }
}
```

`RoutesTest.kt`:

```kotlin
package com.xgetsongs.server

import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.ToolException
import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.ErrorResponse
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolveRequest
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.Stage
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.plugins.sse.sse
import io.ktor.client.request.delete
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.client.statement.HttpResponse
import io.ktor.client.statement.bodyAsText
import io.ktor.http.ContentType
import io.ktor.http.HttpStatusCode
import io.ktor.http.contentType
import io.ktor.server.testing.testApplication
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class RoutesTest {
    private val outDir: Path = Files.createTempDirectory("xgs-routes").resolve("out")

    private suspend fun HttpClient.resolve(input: String = "PLabcdefghijkl"): ResolveResponse =
        post("/resolve") {
            contentType(ContentType.Application.Json)
            setBody(ResolveRequest(input))
        }.body()

    private suspend fun HttpClient.startJob(request: JobRequest): HttpResponse =
        post("/jobs") {
            contentType(ContentType.Application.Json)
            setBody(request)
        }

    private fun options(dir: Path? = outDir, singleRank: Int = 1) = JobOptions(outputDir = dir?.toString(), singleRank = singleRank)

    // ---- /resolve --------------------------------------------------------------------------

    @Test
    fun resolveReturnsTheEngineResultWithAnId() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services)

        val response = apiClient().resolve("  some input ")

        assertTrue(response.resolveId.isNotBlank())
        assertEquals(3, response.items.size)
        assertEquals(listOf("  some input "), fakes.resolver.inputs)
    }

    @Test
    fun resolveErrorsBecomeUnprocessableEntity() = testApplication {
        val fakes = TestServices(resolver = FakeResolver(error = ResolveException("비공개 재생목록")))
        installServer(fakes.services)

        val response = apiClient().post("/resolve") {
            contentType(ContentType.Application.Json)
            setBody(ResolveRequest("x"))
        }

        assertEquals(HttpStatusCode.UnprocessableEntity, response.status)
        assertEquals("비공개 재생목록", response.body<ErrorResponse>().message)
    }

    @Test
    fun malformedBodiesAreBadRequests() = testApplication {
        installServer(TestServices().services)

        val response = apiClient().post("/resolve") {
            contentType(ContentType.Application.Json)
            setBody("{not json")
        }

        assertEquals(HttpStatusCode.BadRequest, response.status)
    }

    // ---- POST /jobs ------------------------------------------------------------------------

    @Test
    fun jobsAreStartedForAvailableItemsOnly() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        val response = client.startJob(JobRequest(resolved.resolveId, options()))

        assertEquals(HttpStatusCode.Created, response.status)
        assertTrue(response.body<JobCreated>().jobId.isNotBlank())
        val request = fakes.downloads.requests.single()
        assertEquals(listOf(1, 3), request.items.map { it.rank })
        assertTrue(Files.isDirectory(outDir), "the output folder is created")
    }

    @Test
    fun jobOptionsAreForwardedToTheEngine() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        client.startJob(JobRequest(resolved.resolveId, JobOptions(outputDir = outDir.toString(), overwrite = true, concurrency = 3)))

        val request = fakes.downloads.requests.single()
        assertTrue(request.overwrite)
        assertEquals(3, request.concurrency)
    }

    @Test
    fun ranksRestrictTheJobForRetries() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        client.startJob(JobRequest(resolved.resolveId, options(), ranks = listOf(3)))

        assertEquals(listOf(3), fakes.downloads.requests.single().items.map { it.rank })
    }

    @Test
    fun aSingleVideoTakesTheRequestedRank() = testApplication {
        val fakes = TestServices(resolver = FakeResolver(response = sampleVideo()))
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        client.startJob(JobRequest(resolved.resolveId, options(singleRank = 42)))

        assertEquals(listOf(42), fakes.downloads.requests.single().items.map { it.rank })
    }

    @Test
    fun singleRankOutsideTheRangeIsRejected() = testApplication {
        val fakes = TestServices(resolver = FakeResolver(response = sampleVideo()))
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        assertEquals(HttpStatusCode.BadRequest, client.startJob(JobRequest(resolved.resolveId, options(singleRank = 0))).status)
        assertEquals(HttpStatusCode.BadRequest, client.startJob(JobRequest(resolved.resolveId, options(singleRank = 1000))).status)
        assertTrue(fakes.downloads.requests.isEmpty())
    }

    @Test
    fun unknownResolveIdsAreNotFound() = testApplication {
        installServer(TestServices().services)

        val response = apiClient().startJob(JobRequest("does-not-exist", options()))

        assertEquals(HttpStatusCode.NotFound, response.status)
    }

    @Test
    fun localModeNeedsAnAbsoluteOutputFolder() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services)
        val client = apiClient()
        val id = client.resolve().resolveId

        assertEquals(HttpStatusCode.BadRequest, client.startJob(JobRequest(id, JobOptions(outputDir = null))).status)
        assertEquals(HttpStatusCode.BadRequest, client.startJob(JobRequest(id, JobOptions(outputDir = "  "))).status)
        assertEquals(HttpStatusCode.BadRequest, client.startJob(JobRequest(id, JobOptions(outputDir = "relative/dir"))).status)
        assertTrue(fakes.downloads.requests.isEmpty())
    }

    @Test
    fun hostedModeNeverAcceptsServerPaths() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services, ServerMode.HOSTED)
        val client = apiClient()
        val id = client.resolve().resolveId

        assertEquals(HttpStatusCode.BadRequest, client.startJob(JobRequest(id, options())).status)
        assertEquals(HttpStatusCode.NotImplemented, client.startJob(JobRequest(id, JobOptions())).status)
        assertTrue(fakes.downloads.requests.isEmpty())
    }

    @Test
    fun emptySelectionsAreRejected() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services)
        val client = apiClient()
        val id = client.resolve().resolveId

        // rank 2 exists but is unavailable
        assertEquals(HttpStatusCode.BadRequest, client.startJob(JobRequest(id, options(), ranks = listOf(2))).status)
        assertEquals(HttpStatusCode.BadRequest, client.startJob(JobRequest(id, options(), ranks = emptyList())).status)
    }

    // ---- events and cancel -----------------------------------------------------------------

    private val finished = listOf(
        JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"),
        JobEvent.Progress(1, Stage.DOWNLOADING, 50.0),
        JobEvent.ItemDone(1, "001 A - One.mp3"),
        JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)),
    )

    private suspend fun HttpClient.collectEvents(jobId: String): List<Pair<String?, String?>> {
        val received = mutableListOf<Pair<String?, String?>>()
        sse("/jobs/$jobId/events") { incoming.collect { received += it.event to it.data } }
        return received
    }

    @Test
    fun eventsAreStreamedInOrderUntilTheJobEnds() = testApplication {
        val fakes = TestServices()
        fakes.downloads.queued = finished
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()
        val jobId = client.startJob(JobRequest(resolved.resolveId, options())).body<JobCreated>().jobId

        val received = client.collectEvents(jobId)

        assertEquals(listOf("item-started", "progress", "item-done", "job-done"), received.map { it.first })
        val decoded = received.map { ApiJson.instance.decodeFromString(JobEvent.serializer(), it.second!!) }
        assertEquals(finished, decoded)
    }

    @Test
    fun unknownJobsGetAnErrorEvent() = testApplication {
        installServer(TestServices().services)

        val received = apiClient().collectEvents("nope")

        assertEquals("error", received.single().first)
        assertTrue(received.single().second!!.contains("찾을 수 없습니다"))
    }

    @Test
    fun cancellingAJobCancelsItsHandle() = testApplication {
        val fakes = TestServices()
        fakes.downloads.closeAfterQueued = false
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()
        val jobId = client.startJob(JobRequest(resolved.resolveId, options())).body<JobCreated>().jobId

        val response = client.delete("/jobs/$jobId")

        assertEquals(HttpStatusCode.NoContent, response.status)
        assertTrue(fakes.downloads.jobs.single().isCancelled)
    }

    @Test
    fun cancellingAnUnknownJobIsNotFound() = testApplication {
        installServer(TestServices().services)

        assertEquals(HttpStatusCode.NotFound, apiClient().delete("/jobs/nope").status)
    }

    // ---- tools -----------------------------------------------------------------------------

    @Test
    fun toolInstallAndUpdateAreForwarded() = testApplication {
        installServer(TestServices().services)
        val client = apiClient()

        assertEquals(ActionResult("installed"), client.post("/tools/yt-dlp/install").body<ActionResult>())
        assertEquals(ActionResult("updated"), client.post("/tools/yt-dlp/update").body<ActionResult>())
    }

    @Test
    fun toolFailuresBecomeUnprocessableEntity() = testApplication {
        val fakes = TestServices()
        fakes.tools.installError = ToolException("오프라인")
        installServer(fakes.services)

        val response = apiClient().post("/tools/yt-dlp/install")

        assertEquals(HttpStatusCode.UnprocessableEntity, response.status)
        assertEquals("오프라인", response.bodyAsText().let { ApiJson.instance.decodeFromString(ErrorResponse.serializer(), it).message })
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :server:test`
Expected: FAIL — `Unresolved reference 'module'`.

- [ ] **Step 3: Write the implementations**

`Registries.kt`:

```kotlin
package com.xgetsongs.server

import com.xgetsongs.engine.JobHandle
import com.xgetsongs.shared.api.ResolveResponse
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean

/** Remembers recent resolve results so `POST /jobs` can refer to them by ID instead of trusting client data. */
class ResolveCache(private val maxEntries: Int = 20) {
    private val entries = object : LinkedHashMap<String, ResolveResponse>(16, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, ResolveResponse>): Boolean =
            size > maxEntries
    }

    /** Stores [response] and returns it with a freshly assigned `resolveId`. */
    fun put(response: ResolveResponse): ResolveResponse {
        val stored = response.copy(resolveId = UUID.randomUUID().toString())
        synchronized(entries) { entries[stored.resolveId] = stored }
        return stored
    }

    fun get(id: String): ResolveResponse? = synchronized(entries) { entries[id] }
}

/** Running jobs by ID. A job's event stream can be claimed by one reader only. */
class JobRegistry {
    private class Entry(val handle: JobHandle) {
        val claimed = AtomicBoolean(false)
    }

    private val jobs = ConcurrentHashMap<String, Entry>()

    fun register(handle: JobHandle): String {
        val id = UUID.randomUUID().toString()
        jobs[id] = Entry(handle)
        return id
    }

    fun exists(id: String): Boolean = jobs.containsKey(id)

    /** Returns the handle to the first caller and null to everyone after (or when the job is unknown). */
    fun claim(id: String): JobHandle? = jobs[id]?.takeIf { it.claimed.compareAndSet(false, true) }?.handle

    fun cancel(id: String): Boolean {
        val entry = jobs[id] ?: return false
        entry.handle.cancel()
        return true
    }

    fun remove(id: String) {
        jobs.remove(id)
    }
}
```

`Application.kt`:

```kotlin
package com.xgetsongs.server

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.ToolException
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.output.OutputSink
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.ErrorResponse
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ResolveRequest
import com.xgetsongs.shared.api.sseName
import com.xgetsongs.shared.filename.FilenameFormatter
import io.ktor.http.HttpStatusCode
import io.ktor.serialization.kotlinx.json.json
import io.ktor.server.application.Application
import io.ktor.server.application.install
import io.ktor.server.plugins.BadRequestException
import io.ktor.server.plugins.contentnegotiation.ContentNegotiation
import io.ktor.server.plugins.statuspages.StatusPages
import io.ktor.server.request.receive
import io.ktor.server.response.respond
import io.ktor.server.routing.delete
import io.ktor.server.routing.get
import io.ktor.server.routing.post
import io.ktor.server.routing.routing
import io.ktor.server.sse.SSE
import io.ktor.server.sse.sse
import io.ktor.sse.ServerSentEvent
import java.io.IOException
import java.nio.file.InvalidPathException
import java.nio.file.Path

/** A client mistake that maps straight to an HTTP status with a user-facing message. */
class ApiException(val status: HttpStatusCode, message: String) : Exception(message)

fun Application.module(services: Services, config: ServerConfig) {
    val json = ApiJson.instance
    val resolveCache = ResolveCache()
    val jobs = JobRegistry()

    install(ContentNegotiation) { json(json) }
    install(SSE)
    install(StatusPages) {
        exception<ApiException> { call, e -> call.respond(e.status, ErrorResponse(e.message.orEmpty())) }
        exception<ResolveException> { call, e ->
            call.respond(HttpStatusCode.UnprocessableEntity, ErrorResponse(e.message.orEmpty()))
        }
        exception<ToolException> { call, e ->
            call.respond(HttpStatusCode.UnprocessableEntity, ErrorResponse(e.message.orEmpty()))
        }
        exception<BadRequestException> { call, _ ->
            call.respond(HttpStatusCode.BadRequest, ErrorResponse("요청 형식이 올바르지 않습니다."))
        }
    }
    installLocalGuard(config)

    routing {
        get("/tools") { call.respond(services.tools.status()) }
        post("/tools/yt-dlp/install") { call.respond(services.tools.installYtDlp()) }
        post("/tools/yt-dlp/update") { call.respond(services.tools.updateYtDlp()) }

        post("/resolve") {
            val request = call.receive<ResolveRequest>()
            call.respond(resolveCache.put(services.resolver.resolve(request.input)))
        }

        post("/jobs") {
            val request = call.receive<JobRequest>()
            val resolved = resolveCache.get(request.resolveId)
                ?: throw ApiException(HttpStatusCode.NotFound, "조회 결과가 만료되었습니다. 다시 조회하세요.")
            val options = request.options
            val isVideo = resolved.kind == InputKind.VIDEO
            if (isVideo && options.singleRank !in FilenameFormatter.MIN_RANK..FilenameFormatter.MAX_RANK) {
                throw ApiException(HttpStatusCode.BadRequest, "순위 번호는 1~999 사이여야 합니다.")
            }
            val sink = sinkFor(config, options)
            val items = resolved.items
                .filter { it.available && (request.ranks == null || it.rank in request.ranks!!) }
                .map { if (isVideo) it.copy(rank = options.singleRank) else it }
            if (items.isEmpty()) throw ApiException(HttpStatusCode.BadRequest, "다운로드할 항목이 없습니다.")

            val handle = services.downloads.start(DownloadRequest(items, sink, options.overwrite, options.concurrency))
            call.respond(HttpStatusCode.Created, JobCreated(jobs.register(handle)))
        }

        sse("/jobs/{id}/events") {
            val id = call.parameters["id"].orEmpty()
            val handle = jobs.claim(id)
            if (handle == null) {
                val message = if (jobs.exists(id)) "이미 다른 곳에서 이 작업의 이벤트를 받고 있습니다." else "작업을 찾을 수 없습니다."
                send(ServerSentEvent(data = json.encodeToString(ErrorResponse.serializer(), ErrorResponse(message)), event = "error"))
                return@sse
            }
            for (event in handle.events) {
                send(ServerSentEvent(data = json.encodeToString(JobEvent.serializer(), event), event = event.sseName))
            }
            // Only reached when the job ended; a dropped connection leaves the job cancellable.
            jobs.remove(id)
        }

        delete("/jobs/{id}") {
            val id = call.parameters["id"].orEmpty()
            if (!jobs.cancel(id)) throw ApiException(HttpStatusCode.NotFound, "작업을 찾을 수 없습니다.")
            call.respond(HttpStatusCode.NoContent)
        }
    }
}

private fun sinkFor(config: ServerConfig, options: JobOptions): OutputSink = when (config.mode) {
    ServerMode.LOCAL -> {
        val dir = options.outputDir?.takeIf { it.isNotBlank() }
            ?: throw ApiException(HttpStatusCode.BadRequest, "출력 폴더를 지정하세요.")
        val path = try {
            Path.of(dir)
        } catch (e: InvalidPathException) {
            throw ApiException(HttpStatusCode.BadRequest, "출력 폴더 경로가 올바르지 않습니다.")
        }
        if (!path.isAbsolute) throw ApiException(HttpStatusCode.BadRequest, "출력 폴더는 절대 경로여야 합니다.")
        try {
            LocalFolderSink(path)
        } catch (e: IOException) {
            throw ApiException(HttpStatusCode.BadRequest, "출력 폴더를 만들 수 없습니다: ${e.message}")
        }
    }
    ServerMode.HOSTED -> {
        if (options.outputDir != null) {
            throw ApiException(HttpStatusCode.BadRequest, "이 서버에서는 출력 폴더를 지정할 수 없습니다.")
        }
        throw ApiException(HttpStatusCode.NotImplemented, "웹 배포용 출력은 아직 지원하지 않습니다.")
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :server:test`
Expected: `BUILD SUCCESSFUL`; `RoutesTest` 18 tests and `GuardTest` 6 tests, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add server
git commit -m "feat(server): add routes, resolve cache and job registry" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 14: `LocalServer` (real Netty server)

**Files:**
- Create: `server/src/main/kotlin/com/xgetsongs/server/LocalServer.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/LocalServerTest.kt`

**Interfaces:**
- Consumes: everything in `engine` plus `Services`, `module`, `ServerConfig`, `ServerMode`; test helpers from Task 13.
- Produces: `fun createServices(appDataDir: Path, scope: CoroutineScope): Services` (wires the real implementations; deletes `<appDataDir>/work` left over from a previous run; yt-dlp installs go to `<appDataDir>/bin`); `class LocalServer { val port: Int; val token: String; fun stop(); companion object { fun start(appDataDir: Path): LocalServer; fun start(services: Services, scope: CoroutineScope = …): LocalServer } }` — binds `127.0.0.1` on a random port with a fresh random token, in `LOCAL` mode.

- [ ] **Step 1: Write the failing test**

These tests run the real Netty server and a real CIO client. They cover what the in-memory test host cannot: live SSE streaming, the single-reader rule, and a spoofed `Host` header (sent over a raw socket).

```kotlin
package com.xgetsongs.server

import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolveRequest
import com.xgetsongs.shared.api.ResolveResponse
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.plugins.sse.SSE
import io.ktor.client.plugins.sse.sse
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.http.contentType
import io.ktor.serialization.kotlinx.json.json
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.net.ConnectException
import java.net.Socket
import java.nio.file.Files
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

/** Runs the real Netty server on a random loopback port, which the in-memory test host cannot do. */
class LocalServerTest {
    private val fakes = TestServices()
    private lateinit var server: LocalServer
    private val outDir = Files.createTempDirectory("xgs-local").resolve("out")

    @BeforeTest
    fun start() {
        server = LocalServer.start(fakes.services)
    }

    @AfterTest
    fun stop() {
        server.stop()
    }

    private fun client(token: String? = server.token) = HttpClient(CIO) {
        install(ContentNegotiation) { json(ApiJson.instance) }
        install(SSE)
        defaultRequest {
            url("http://127.0.0.1:${server.port}")
            if (token != null) header(ApiHeaders.TOKEN, token)
        }
    }

    private suspend fun HttpClient.startJob(): String {
        val resolved = post("/resolve") {
            contentType(ContentType.Application.Json)
            setBody(ResolveRequest("PLabcdefghijkl"))
        }.body<ResolveResponse>()
        return post("/jobs") {
            contentType(ContentType.Application.Json)
            setBody(JobRequest(resolved.resolveId, JobOptions(outputDir = outDir.toString())))
        }.body<JobCreated>().jobId
    }

    @Test
    fun theServerListensOnLoopbackAndNeedsTheToken() = runBlocking {
        client().use { assertEquals(HttpStatusCode.OK, it.get("/tools").status) }
        client(token = null).use { assertEquals(HttpStatusCode.Unauthorized, it.get("/tools").status) }
        client(token = "wrong").use { assertEquals(HttpStatusCode.Unauthorized, it.get("/tools").status) }
    }

    @Test
    fun browserOriginsAreRefused() = runBlocking {
        client().use {
            val response = it.get("/tools") { header(HttpHeaders.Origin, "http://evil.example") }
            assertEquals(HttpStatusCode.Forbidden, response.status)
        }
    }

    @Test
    fun foreignHostHeadersAreRefused() {
        Socket("127.0.0.1", server.port).use { socket ->
            val request = "GET /tools HTTP/1.1\r\nHost: evil.example\r\n${ApiHeaders.TOKEN}: ${server.token}\r\nConnection: close\r\n\r\n"
            socket.getOutputStream().write(request.toByteArray())
            val statusLine = socket.getInputStream().bufferedReader().readLine()
            assertTrue(statusLine.contains("403"), statusLine)
        }
    }

    @Test
    fun eventsAreForwardedLiveWhileTheJobRuns() = runBlocking {
        fakes.downloads.queued = listOf(JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"))
        fakes.downloads.closeAfterQueued = false
        client().use { client ->
            withTimeout(30_000) {
                val jobId = client.startJob()
                val firstSeen = CompletableDeferred<Unit>()
                val received = mutableListOf<String?>()
                coroutineScope {
                    val reader = launch {
                        client.sse("/jobs/$jobId/events") {
                            incoming.collect {
                                received += it.event
                                if (it.event == "item-started") firstSeen.complete(Unit)
                            }
                        }
                    }
                    firstSeen.await() // arrived while the job is still running
                    fakes.downloads.channel!!.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)))
                    fakes.downloads.channel!!.close()
                    reader.join()
                }
                assertEquals(listOf<String?>("item-started", "job-done"), received)
            }
        }
    }

    @Test
    fun onlyTheFirstReaderOwnsTheEventStream() = runBlocking {
        fakes.downloads.queued = listOf(JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"))
        fakes.downloads.closeAfterQueued = false
        client().use { client ->
            withTimeout(30_000) {
                val jobId = client.startJob()
                val firstSeen = CompletableDeferred<Unit>()
                coroutineScope {
                    val first = launch {
                        client.sse("/jobs/$jobId/events") {
                            incoming.collect { if (it.event == "item-started") firstSeen.complete(Unit) }
                        }
                    }
                    firstSeen.await()

                    val second = mutableListOf<String?>()
                    client.sse("/jobs/$jobId/events") { incoming.collect { second += it.event } }

                    assertEquals(listOf<String?>("error"), second)
                    fakes.downloads.channel!!.close()
                    first.join()
                }
            }
        }
    }

    @Test
    fun stoppingTheServerClosesThePort() {
        val port = server.port
        server.stop()

        assertFailsWith<ConnectException> { Socket("127.0.0.1", port).close() }

        server = LocalServer.start(fakes.services) // so @AfterTest has something to stop
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :server:test`
Expected: FAIL — `Unresolved reference 'LocalServer'`.

- [ ] **Step 3: Write the implementation**

```kotlin
package com.xgetsongs.server

import com.xgetsongs.engine.job.DefaultDownloadService
import com.xgetsongs.engine.job.ItemDownloader
import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tools.DefaultToolManager
import com.xgetsongs.engine.tools.ToolLocator
import com.xgetsongs.engine.ytdlp.YtDlpResolver
import io.ktor.server.engine.embeddedServer
import io.ktor.server.netty.Netty
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import java.nio.file.Path
import java.security.SecureRandom
import java.util.Base64

/** Wires the real engine implementations. Leftover work files from a previous run are removed. */
fun createServices(appDataDir: Path, scope: CoroutineScope): Services {
    val binDir = appDataDir.resolve("bin")
    val workDir = appDataDir.resolve("work")
    workDir.toFile().deleteRecursively()

    val runner = SystemProcessRunner()
    val locator = ToolLocator(appBinDir = binDir)
    val resolver = YtDlpResolver(runner, locator)
    val downloader = ItemDownloader(runner, locator, resolver)
    return Services(
        resolver = resolver,
        downloads = DefaultDownloadService(downloader, workDir, scope),
        tools = DefaultToolManager(locator, runner, binDir),
    )
}

/** The desktop app's embedded server: loopback only, on a random port, guarded by a random token. */
class LocalServer private constructor(
    val port: Int,
    val token: String,
    private val server: io.ktor.server.engine.EmbeddedServer<*, *>,
    private val scope: CoroutineScope,
) {
    fun stop() {
        scope.cancel()
        server.stop(gracePeriodMillis = 200, timeoutMillis = 2_000)
    }

    companion object {
        fun start(appDataDir: Path): LocalServer {
            val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
            return start(createServices(appDataDir, scope), scope)
        }

        fun start(services: Services, scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)): LocalServer {
            val token = newToken()
            val server = embeddedServer(Netty, port = 0, host = "127.0.0.1") {
                module(services, ServerConfig(token = token, mode = ServerMode.LOCAL))
            }
            server.start(wait = false)
            val port = runBlocking { server.engine.resolvedConnectors().first().port }
            return LocalServer(port, token, server, scope)
        }

        private fun newToken(): String {
            val bytes = ByteArray(32).also { SecureRandom().nextBytes(it) }
            return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
        }
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :server:test`
Expected: `BUILD SUCCESSFUL`; `LocalServerTest` 6 tests; the server module has 30 tests in total, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add server
git commit -m "feat(server): add LocalServer and service wiring" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 15: `app` module and `HttpXgsApi`

The first build of this module downloads Compose Multiplatform and Skiko (a few hundred MB); allow several minutes.

**Files:**
- Modify: `settings.gradle.kts` (add `include(":app")` after `include(":server")`)
- Create: `app/build.gradle.kts`
- Create: `app/src/commonMain/kotlin/com/xgetsongs/app/api/XgsApi.kt`
- Create: `app/src/desktopTest/kotlin/com/xgetsongs/app/api/ContractSupport.kt`
- Test: `app/src/desktopTest/kotlin/com/xgetsongs/app/api/HttpXgsApiTest.kt`

**Interfaces:**
- Consumes: shared API models; the real server routes (`module`, `Services`, `ServerConfig`) and engine interfaces in the desktop test.
- Produces (package `com.xgetsongs.app.api`, in `commonMain`): `class ApiError(message): Exception`; `interface XgsApi { suspend fun tools(): ToolsStatus; suspend fun installYtDlp(): ActionResult; suspend fun updateYtDlp(): ActionResult; suspend fun resolve(input: String): ResolveResponse; suspend fun startJob(request: JobRequest): JobCreated; fun events(jobId: String): Flow<JobEvent>; suspend fun cancel(jobId: String) }`; `fun HttpClientConfig<*>.configureXgs(token: String, baseUrl: String = "")` (JSON, SSE, base URL and token header); `class HttpXgsApi(client: HttpClient) : XgsApi`. Non-2xx answers become `ApiError(server message)`; an `error` SSE event also becomes an `ApiError`, thrown after the SSE block.
- Test helpers (package `com.xgetsongs.app.api`, `desktopTest`): `CONTRACT_TOKEN`, `class FakeEngine` (`outputDir`, `item`, `playlist`, `finishedEvents`, `jobs`, `resolveError`, `services()`), `fun ApplicationTestBuilder.apiFor(engine, clientToken = CONTRACT_TOKEN): HttpXgsApi`.

- [ ] **Step 1: Add the module**

Edit `settings.gradle.kts` so the last lines read:

```kotlin
include(":shared")
include(":engine")
include(":server")
include(":app")
```

Create `app/build.gradle.kts`:

```kotlin
import org.jetbrains.compose.desktop.application.dsl.TargetFormat

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.compose.multiplatform)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)
    jvm("desktop")

    sourceSets {
        commonMain.dependencies {
            implementation(project(":shared"))
            implementation(libs.kotlinx.coroutines.core)
            implementation(libs.kotlinx.serialization.json)
            implementation(libs.compose.runtime)
            implementation(libs.compose.foundation)
            implementation(libs.compose.material3)
            implementation(libs.compose.ui)
            implementation(libs.ktor.client.core)
            implementation(libs.ktor.client.content.negotiation)
            implementation(libs.ktor.serialization.kotlinx.json)
        }
        commonTest.dependencies {
            implementation(kotlin("test"))
            implementation(libs.kotlinx.coroutines.test)
        }
        val desktopMain by getting {
            dependencies {
                implementation(compose.desktop.currentOs)
                implementation(project(":server"))
                implementation(libs.ktor.client.cio)
                implementation(libs.kotlinx.coroutines.swing)
            }
        }
        val desktopTest by getting {
            dependencies {
                implementation(libs.ktor.server.test.host)
                implementation(libs.ktor.client.cio)
            }
        }
    }
}

compose.desktop {
    application {
        mainClass = "com.xgetsongs.app.MainKt"
        nativeDistributions {
            targetFormats(TargetFormat.Msi)
            packageName = "xGetSongs"
            packageVersion = "1.0.0"
        }
    }
}
```

- [ ] **Step 2: Write the test helpers and the failing test**

`ContractSupport.kt`:

```kotlin
package com.xgetsongs.app.api

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.DownloadService
import com.xgetsongs.engine.JobHandle
import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.Resolver
import com.xgetsongs.engine.ToolManager
import com.xgetsongs.server.ServerConfig
import com.xgetsongs.server.Services
import com.xgetsongs.server.module
import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.ToolInfo
import com.xgetsongs.shared.api.ToolsStatus
import io.ktor.server.testing.ApplicationTestBuilder
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import java.nio.file.Files

const val CONTRACT_TOKEN = "contract-token"

/** A fake engine whose jobs finish immediately; the real server routes run in front of it. */
class FakeEngine {
    val outputDir: String = Files.createTempDirectory("xgs-contract").resolve("out").toString()

    val item = ResolvedItem(
        rank = 1, videoId = "vid00000001", title = "A - One", artist = "A", track = "One",
        expectedFileName = "001 A - One.mp3",
    )
    val playlist = ResolveResponse(kind = InputKind.PLAYLIST, playlistTitle = "Sample", items = listOf(item))

    val finishedEvents = listOf(
        JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"),
        JobEvent.ItemDone(1, "001 A - One.mp3"),
        JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)),
    )

    val jobs = mutableListOf<Job>()
    var resolveError: ResolveException? = null

    private val resolver = object : Resolver {
        override suspend fun resolve(input: String): ResolveResponse {
            resolveError?.let { throw it }
            return playlist
        }
    }

    private val downloads = object : DownloadService {
        override fun start(request: DownloadRequest): JobHandle {
            val events = Channel<JobEvent>(Channel.UNLIMITED)
            finishedEvents.forEach { events.trySend(it) }
            events.close()
            return JobHandle(events, Job().also { jobs += it })
        }
    }

    private val tools = object : ToolManager {
        override suspend fun status() = ToolsStatus(ToolInfo(true, "1"), ToolInfo(true, "2"), ToolInfo(false))
        override suspend fun installYtDlp() = ActionResult("installed")
        override suspend fun updateYtDlp() = ActionResult("updated")
    }

    fun services() = Services(resolver, downloads, tools)
}

/** Starts the real server routes in the test host and returns an [HttpXgsApi] wired to them. */
fun ApplicationTestBuilder.apiFor(engine: FakeEngine, clientToken: String = CONTRACT_TOKEN): HttpXgsApi {
    application { module(engine.services(), ServerConfig(CONTRACT_TOKEN)) }
    return HttpXgsApi(createClient { configureXgs(clientToken) })
}
```

`HttpXgsApiTest.kt`:

```kotlin
package com.xgetsongs.app.api

import com.xgetsongs.engine.ResolveException
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ToolInfo
import io.ktor.server.testing.testApplication
import kotlinx.coroutines.flow.toList
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

/** Runs [HttpXgsApi] against the real server routes with a fake engine behind them. */
class HttpXgsApiTest {
    private val engine = FakeEngine()

    @Test
    fun readsToolStatusAndRunsToolActions() = testApplication {
        val api = apiFor(engine)

        assertEquals(ToolInfo(false), api.tools().jsRuntime)
        assertEquals("installed", api.installYtDlp().message)
        assertEquals("updated", api.updateYtDlp().message)
    }

    @Test
    fun resolveReturnsItemsWithAnId() = testApplication {
        val response = apiFor(engine).resolve("PLabcdefghijkl")

        assertTrue(response.resolveId.isNotBlank())
        assertEquals(listOf(1), response.items.map { it.rank })
    }

    @Test
    fun serverErrorsBecomeApiErrorsWithTheServerMessage() = testApplication {
        val api = apiFor(engine)
        engine.resolveError = ResolveException("비공개 재생목록")

        val error = assertFailsWith<ApiError> { api.resolve("PLabcdefghijkl") }

        assertEquals("비공개 재생목록", error.message)
    }

    @Test
    fun aWrongTokenIsReportedAsAnApiError() = testApplication {
        val error = assertFailsWith<ApiError> { apiFor(engine, clientToken = "wrong").tools() }

        assertTrue(error.message!!.contains("토큰"))
    }

    @Test
    fun jobEventsArriveInOrder() = testApplication {
        val api = apiFor(engine)
        val resolveId = api.resolve("PLabcdefghijkl").resolveId
        val jobId = api.startJob(JobRequest(resolveId, JobOptions(outputDir = engine.outputDir))).jobId

        val events = api.events(jobId).toList()

        assertEquals(engine.finishedEvents, events)
    }

    @Test
    fun eventsOfAnUnknownJobFailWithAnApiError() = testApplication {
        val error = assertFailsWith<ApiError> { apiFor(engine).events("nope").toList() }

        assertTrue(error.message!!.contains("찾을 수 없습니다"))
    }

    @Test
    fun cancelReachesTheJob() = testApplication {
        val api = apiFor(engine)
        val resolveId = api.resolve("PLabcdefghijkl").resolveId
        val jobId = api.startJob(JobRequest(resolveId, JobOptions(outputDir = engine.outputDir))).jobId

        api.cancel(jobId)

        assertTrue(engine.jobs.single().isCancelled)
        assertFailsWith<ApiError> { api.cancel("nope") }
    }
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `.\gradlew.bat :app:desktopTest`
Expected: FAIL — `Unresolved reference 'HttpXgsApi'`, `'configureXgs'`, `'ApiError'`.

- [ ] **Step 4: Write the implementation**

```kotlin
package com.xgetsongs.app.api

import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.ErrorResponse
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ResolveRequest
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ToolsStatus
import io.ktor.client.HttpClient
import io.ktor.client.HttpClientConfig
import io.ktor.client.call.body
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.plugins.sse.SSE
import io.ktor.client.plugins.sse.sse
import io.ktor.client.request.delete
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.client.request.url
import io.ktor.client.statement.HttpResponse
import io.ktor.http.ContentType
import io.ktor.http.contentType
import io.ktor.http.isSuccess
import io.ktor.serialization.kotlinx.json.json
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow

/** A failure reported by the server (or a lost connection) with a message fit for the user. */
class ApiError(message: String) : Exception(message)

/** Everything the UI needs from the server. Faked in tests. */
interface XgsApi {
    suspend fun tools(): ToolsStatus
    suspend fun installYtDlp(): ActionResult
    suspend fun updateYtDlp(): ActionResult
    suspend fun resolve(input: String): ResolveResponse
    suspend fun startJob(request: JobRequest): JobCreated

    /** Emits the job's events and completes when the stream ends; throws [ApiError] on a server-side error event. */
    fun events(jobId: String): Flow<JobEvent>

    suspend fun cancel(jobId: String)
}

/** Installs JSON, SSE and the auth token. [baseUrl] is empty when the client already points at the server. */
fun HttpClientConfig<*>.configureXgs(token: String, baseUrl: String = "") {
    install(ContentNegotiation) { json(ApiJson.instance) }
    install(SSE)
    defaultRequest {
        if (baseUrl.isNotEmpty()) url(baseUrl)
        header(ApiHeaders.TOKEN, token)
    }
}

class HttpXgsApi(private val client: HttpClient) : XgsApi {
    private val json = ApiJson.instance

    override suspend fun tools(): ToolsStatus = client.get("/tools").checked().body()

    override suspend fun installYtDlp(): ActionResult = client.post("/tools/yt-dlp/install").checked().body()

    override suspend fun updateYtDlp(): ActionResult = client.post("/tools/yt-dlp/update").checked().body()

    override suspend fun resolve(input: String): ResolveResponse =
        client.post("/resolve") {
            contentType(ContentType.Application.Json)
            setBody(ResolveRequest(input))
        }.checked().body()

    override suspend fun startJob(request: JobRequest): JobCreated =
        client.post("/jobs") {
            contentType(ContentType.Application.Json)
            setBody(request)
        }.checked().body()

    override fun events(jobId: String): Flow<JobEvent> = channelFlow {
        // The SSE client wraps anything thrown inside its block, so remember the error and throw it afterwards.
        var serverError: String? = null
        client.sse("/jobs/$jobId/events") {
            incoming.collect { event ->
                val data = event.data.orEmpty()
                if (event.event == "error") {
                    serverError = json.decodeFromString(ErrorResponse.serializer(), data).message
                } else {
                    send(json.decodeFromString(JobEvent.serializer(), data))
                }
            }
        }
        serverError?.let { throw ApiError(it) }
    }

    override suspend fun cancel(jobId: String) {
        client.delete("/jobs/$jobId").checked()
    }

    private suspend fun HttpResponse.checked(): HttpResponse {
        if (status.isSuccess()) return this
        val message = try {
            body<ErrorResponse>().message
        } catch (e: Exception) {
            "서버 오류 (${status.value})"
        }
        throw ApiError(message)
    }
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `.\gradlew.bat :app:desktopTest`
Expected: `BUILD SUCCESSFUL`; `HttpXgsApiTest` 7 tests, 0 failures (reports in `app\build\test-results\desktopTest`).

- [ ] **Step 6: Commit**

```powershell
git add settings.gradle.kts app
git commit -m "feat(app): add module and HttpXgsApi" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 16: UI state model and labels

**Files:**
- Create: `app/src/commonMain/kotlin/com/xgetsongs/app/state/UiState.kt`
- Create: `app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt`
- Test: `app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt`

**Interfaces:**
- Consumes: shared API models.
- Produces (package `com.xgetsongs.app.state`): `sealed interface ItemStatus` = `Ready`, `Waiting`, `Downloading(percent: Double?)`, `Converting`, `Done`, `Skipped(reason)`, `Failed(message)`; `data class ItemRow(item: ResolvedItem, fileName: String?, status: ItemStatus)`; `enum class Phase { IDLE, RESOLVING, PREVIEW, RUNNING, FINISHED }`; `data class UiState(phase, input, outputDir, overwrite, concurrency, singleRank, resolved, rows, error, jobStatus, summary, tools, toolBusy, toolMessage)` with `val failedRanks: List<Int>` and `val canResolve: Boolean`; `fun statusLabel(status): String`, `fun summaryText(state): String?`, `fun rankLabel(rank): String`.

- [ ] **Step 1: Write the failing test**

```kotlin
package com.xgetsongs.app.state

import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class LabelsTest {
    @Test
    fun statusLabels() {
        assertEquals("준비됨", statusLabel(ItemStatus.Ready))
        assertEquals("대기 중", statusLabel(ItemStatus.Waiting))
        assertEquals("다운로드 중…", statusLabel(ItemStatus.Downloading(null)))
        assertEquals("다운로드 42%", statusLabel(ItemStatus.Downloading(42.9)))
        assertEquals("mp3 변환 중…", statusLabel(ItemStatus.Converting))
        assertEquals("완료", statusLabel(ItemStatus.Done))
        assertEquals("건너뜀: 비공개 영상", statusLabel(ItemStatus.Skipped("비공개 영상")))
        assertEquals("실패: boom", statusLabel(ItemStatus.Failed("boom")))
    }

    @Test
    fun summaryTextByJobStatus() {
        val counts = JobSummary(2, 1, 3)
        assertEquals("완료 — 성공 2 · 건너뜀 1 · 실패 3", summaryText(UiState(jobStatus = JobStatus.COMPLETED, summary = counts)))
        assertEquals("취소됨 — 성공 2 · 건너뜀 1 · 실패 3", summaryText(UiState(jobStatus = JobStatus.CANCELLED, summary = counts)))
        assertEquals("중단됨 — 성공 2 · 건너뜀 1 · 실패 3", summaryText(UiState(jobStatus = JobStatus.FAILED, summary = counts)))
    }

    @Test
    fun noSummaryUntilAJobEnds() {
        assertNull(summaryText(UiState()))
        assertNull(summaryText(UiState(summary = JobSummary(1, 0, 0), jobStatus = null)))
    }

    @Test
    fun rankIsZeroPadded() {
        assertEquals("007", rankLabel(7))
        assertEquals("999", rankLabel(999))
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `.\gradlew.bat :app:desktopTest`
Expected: FAIL — `Unresolved reference 'statusLabel'`, `'UiState'`, ...

- [ ] **Step 3: Write the implementations**

`UiState.kt`:

```kotlin
package com.xgetsongs.app.state

import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.ToolsStatus

sealed interface ItemStatus {
    /** Shown in the preview before a download starts. */
    data object Ready : ItemStatus

    /** Part of a running job but not started yet. */
    data object Waiting : ItemStatus

    data class Downloading(val percent: Double?) : ItemStatus
    data object Converting : ItemStatus
    data object Done : ItemStatus
    data class Skipped(val reason: String) : ItemStatus
    data class Failed(val message: String) : ItemStatus
}

data class ItemRow(
    val item: ResolvedItem,
    /** The file name shown to the user: the preview's guess, replaced by the real one once known. */
    val fileName: String?,
    val status: ItemStatus,
)

enum class Phase { IDLE, RESOLVING, PREVIEW, RUNNING, FINISHED }

data class UiState(
    val phase: Phase = Phase.IDLE,
    val input: String = "",
    val outputDir: String = "",
    val overwrite: Boolean = false,
    val concurrency: Int = 2,
    val singleRank: Int = 1,
    val resolved: ResolveResponse? = null,
    val rows: List<ItemRow> = emptyList(),
    val error: String? = null,
    val jobStatus: JobStatus? = null,
    val summary: JobSummary? = null,
    val tools: ToolsStatus? = null,
    val toolBusy: Boolean = false,
    val toolMessage: String? = null,
) {
    val failedRanks: List<Int> get() = rows.filter { it.status is ItemStatus.Failed }.map { it.item.rank }
    val canResolve: Boolean get() = phase != Phase.RESOLVING && phase != Phase.RUNNING
}
```

`Labels.kt`:

```kotlin
package com.xgetsongs.app.state

import com.xgetsongs.shared.api.JobStatus

/** The text shown in an item's status cell. */
fun statusLabel(status: ItemStatus): String = when (status) {
    ItemStatus.Ready -> "준비됨"
    ItemStatus.Waiting -> "대기 중"
    is ItemStatus.Downloading -> status.percent?.let { "다운로드 ${it.toInt()}%" } ?: "다운로드 중…"
    ItemStatus.Converting -> "mp3 변환 중…"
    ItemStatus.Done -> "완료"
    is ItemStatus.Skipped -> "건너뜀: ${status.reason}"
    is ItemStatus.Failed -> "실패: ${status.message}"
}

/** The one-line result shown after a job ends; null while there is nothing to report. */
fun summaryText(state: UiState): String? {
    val summary = state.summary ?: return null
    val counts = "성공 ${summary.succeeded} · 건너뜀 ${summary.skipped} · 실패 ${summary.failed}"
    return when (state.jobStatus) {
        JobStatus.COMPLETED -> "완료 — $counts"
        JobStatus.CANCELLED -> "취소됨 — $counts"
        JobStatus.FAILED -> "중단됨 — $counts"
        null -> null
    }
}

/** The zero-padded rank shown in the list, e.g. `007`. */
fun rankLabel(rank: Int): String = rank.toString().padStart(3, '0')
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `.\gradlew.bat :app:desktopTest`
Expected: `BUILD SUCCESSFUL`; `LabelsTest` 4 tests, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add app
git commit -m "feat(app): add UI state model and labels" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 17: `AppStateHolder`

All screen logic lives here and is tested without Compose.

**Files:**
- Create: `app/src/commonMain/kotlin/com/xgetsongs/app/state/AppStateHolder.kt`
- Create: `app/src/commonTest/kotlin/com/xgetsongs/app/state/FakeApi.kt`
- Test: `app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt`
- Test: `app/src/desktopTest/kotlin/com/xgetsongs/app/api/EndToEndTest.kt`

**Interfaces:**
- Consumes: `XgsApi`, `ApiError` (Task 15); `UiState`, `ItemRow`, `ItemStatus`, `Phase` (Task 16); `InputClassifier`, `ParsedInput`, `FilenameFormatter`, API models (shared).
- Produces: `class AppStateHolder(api: XgsApi, scope: CoroutineScope, defaultOutputDir: String) { val state: StateFlow<UiState>; fun onInput(text); fun onOutputDir(dir); fun onOverwrite(value); fun onConcurrency(value); fun onSingleRank(value); fun dismissError(); fun reset(); fun refreshTools(); fun installYtDlp(); fun updateYtDlp(); fun resolve(); fun switchToVideoOnly(); fun startDownload(); fun retryFailed(); fun cancel() }`.
- Test helper: `class FakeApi : XgsApi` (`resolveResponse`, `resolveError`, `startError`, `jobRequests`, `cancelled`, `eventChannel`, ...) with `FakeApi.playlist()`, `FakeApi.video()`, `FakeApi.item(...)`.

Behaviour to keep: invalid input is rejected locally before any request; the preview shows unavailable items as `Skipped`; `retryFailed` sends only the failed ranks (a single video is re-run whole); a job that cannot start returns to the screen it came from; an event stream that ends without `JobDone` shows "서버와의 연결이 끊어졌습니다."

- [ ] **Step 1: Write the test helpers and the failing tests**

`FakeApi.kt`:

```kotlin
package com.xgetsongs.app.state

import com.xgetsongs.app.api.ApiError
import com.xgetsongs.app.api.XgsApi
import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.ToolInfo
import com.xgetsongs.shared.api.ToolsStatus
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.receiveAsFlow

class FakeApi : XgsApi {
    var toolsStatus = ToolsStatus(ToolInfo(true, "1"), ToolInfo(true, "1"), ToolInfo(true, "24"))
    var resolveResponse: ResolveResponse = playlist()
    var resolveError: ApiError? = null
    var startError: ApiError? = null
    var installMessage = "installed"

    val resolveInputs = mutableListOf<String>()
    val jobRequests = mutableListOf<JobRequest>()
    val cancelled = mutableListOf<String>()
    var installCalls = 0

    /** What the "server" sends for the running job. Close it to end the stream. */
    var eventChannel = Channel<JobEvent>(Channel.UNLIMITED)

    override suspend fun tools(): ToolsStatus = toolsStatus

    override suspend fun installYtDlp(): ActionResult {
        installCalls++
        return ActionResult(installMessage)
    }

    override suspend fun updateYtDlp(): ActionResult = ActionResult("updated")

    override suspend fun resolve(input: String): ResolveResponse {
        resolveInputs += input
        resolveError?.let { throw it }
        return resolveResponse
    }

    override suspend fun startJob(request: JobRequest): JobCreated {
        startError?.let { throw it }
        jobRequests += request
        eventChannel = Channel(Channel.UNLIMITED)
        return JobCreated("job-${jobRequests.size}")
    }

    override fun events(jobId: String): Flow<JobEvent> = eventChannel.receiveAsFlow()

    override suspend fun cancel(jobId: String) {
        cancelled += jobId
    }

    companion object {
        fun item(rank: Int, artist: String = "A$rank", track: String = "T$rank") = ResolvedItem(
            rank = rank,
            videoId = "vid${rank.toString().padStart(8, '0')}",
            title = "$artist - $track",
            artist = artist,
            track = track,
            expectedFileName = "${rank.toString().padStart(3, '0')} $artist - $track.mp3",
        )

        fun playlist(alsoVideoId: String? = null) = ResolveResponse(
            resolveId = "resolve-1",
            kind = InputKind.PLAYLIST,
            playlistTitle = "Sample",
            items = listOf(
                item(1),
                ResolvedItem(rank = 2, videoId = "vid00000002", title = "[Private video]", available = false, unavailableReason = "비공개 영상"),
                item(3).copy(lowConfidence = true),
            ),
            alsoVideoId = alsoVideoId,
        )

        fun video() = ResolveResponse(resolveId = "resolve-2", kind = InputKind.VIDEO, items = listOf(item(1, "IU", "Love")))
    }
}
```

`AppStateHolderTest.kt` (note `runCurrent()`, not `advanceUntilIdle()`, because the holder runs in `backgroundScope`):

```kotlin
@file:OptIn(ExperimentalCoroutinesApi::class)

package com.xgetsongs.app.state

import com.xgetsongs.app.api.ApiError
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.input.RejectReason
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class AppStateHolderTest {
    private val playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"

    private fun TestScope.holder(api: FakeApi = FakeApi()) =
        api to AppStateHolder(api, backgroundScope, defaultOutputDir = "C:/Music/xGetSongs")

    private fun AppStateHolder.row(rank: Int) = state.value.rows.first { it.item.rank == rank }

    private suspend fun TestScope.resolved(api: FakeApi = FakeApi()): Pair<FakeApi, AppStateHolder> {
        val (fake, holder) = holder(api)
        holder.onInput(playlistId)
        holder.resolve()
        runCurrent()
        return fake to holder
    }

    // ---- resolving ------------------------------------------------------------------------

    @Test
    fun startsIdleWithTheDefaultOutputFolder() = runTest {
        val (_, holder) = holder()

        assertEquals(Phase.IDLE, holder.state.value.phase)
        assertEquals("C:/Music/xGetSongs", holder.state.value.outputDir)
    }

    @Test
    fun resolveShowsAPreviewOfEveryItem() = runTest {
        val (api, holder) = resolved()

        val state = holder.state.value
        assertEquals(Phase.PREVIEW, state.phase)
        assertEquals(listOf(playlistId), api.resolveInputs)
        assertEquals(listOf(1, 2, 3), state.rows.map { it.item.rank })
        assertEquals(ItemStatus.Ready, holder.row(1).status)
        assertEquals(ItemStatus.Skipped("비공개 영상"), holder.row(2).status)
        assertEquals("001 A1 - T1.mp3", holder.row(1).fileName)
    }

    @Test
    fun invalidInputIsRejectedBeforeAnyRequest() = runTest {
        val (api, holder) = holder()
        holder.onInput("https://evil.com/x")

        holder.resolve()
        runCurrent()

        assertEquals(RejectReason.UNSUPPORTED_HOST.message, holder.state.value.error)
        assertTrue(api.resolveInputs.isEmpty())
        assertEquals(Phase.IDLE, holder.state.value.phase)
    }

    @Test
    fun serverErrorsAreShownAndTheScreenStaysUsable() = runTest {
        val api = FakeApi().apply { resolveError = ApiError("비공개 재생목록") }
        val (_, holder) = resolved(api)

        assertEquals("비공개 재생목록", holder.state.value.error)
        assertEquals(Phase.IDLE, holder.state.value.phase)
    }

    @Test
    fun switchingToTheVideoOnlyResolvesTheVideoUrl() = runTest {
        val api = FakeApi().apply { resolveResponse = FakeApi.playlist(alsoVideoId = "dQw4w9WgXcQ") }
        val (_, holder) = resolved(api)

        holder.switchToVideoOnly()
        runCurrent()

        assertEquals("https://www.youtube.com/watch?v=dQw4w9WgXcQ", api.resolveInputs.last())
    }

    // ---- single video rank ----------------------------------------------------------------

    @Test
    fun changingTheRankRewritesTheSingleVideoFileName() = runTest {
        val api = FakeApi().apply { resolveResponse = FakeApi.video() }
        val (_, holder) = resolved(api)

        holder.onSingleRank(42)

        assertEquals("042 IU - Love.mp3", holder.row(42).fileName)
        assertEquals(42, holder.state.value.singleRank)
    }

    @Test
    fun rankIsClampedToOneThrough999() = runTest {
        val (_, holder) = holder()

        holder.onSingleRank(0)
        assertEquals(1, holder.state.value.singleRank)
        holder.onSingleRank(5000)
        assertEquals(999, holder.state.value.singleRank)
    }

    // ---- downloading ----------------------------------------------------------------------

    @Test
    fun startingSendsTheOptionsAndMarksItemsWaiting() = runTest {
        val (api, holder) = resolved()
        holder.onOverwrite(true)
        holder.onConcurrency(3)
        holder.onOutputDir("D:/Songs")

        holder.startDownload()
        runCurrent()

        val request = api.jobRequests.single()
        assertEquals("resolve-1", request.resolveId)
        assertEquals("D:/Songs", request.options.outputDir)
        assertTrue(request.options.overwrite)
        assertEquals(3, request.options.concurrency)
        assertNull(request.ranks)
        assertEquals(Phase.RUNNING, holder.state.value.phase)
        assertEquals(ItemStatus.Waiting, holder.row(1).status)
        assertEquals(ItemStatus.Skipped("비공개 영상"), holder.row(2).status)
    }

    @Test
    fun jobEventsDriveTheRowsAndTheSummary() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()

        api.eventChannel.trySend(JobEvent.ItemStarted(1, "vid00000001", "001 Real - Name.mp3"))
        runCurrent()
        assertEquals(ItemStatus.Downloading(null), holder.row(1).status)
        assertEquals("001 Real - Name.mp3", holder.row(1).fileName)

        api.eventChannel.trySend(JobEvent.Progress(1, Stage.DOWNLOADING, 40.0))
        runCurrent()
        assertEquals(ItemStatus.Downloading(40.0), holder.row(1).status)

        api.eventChannel.trySend(JobEvent.Progress(1, Stage.CONVERTING))
        runCurrent()
        assertEquals(ItemStatus.Converting, holder.row(1).status)

        api.eventChannel.trySend(JobEvent.ItemDone(1, "001 Real - Name.mp3"))
        api.eventChannel.trySend(JobEvent.ItemFailed(3, "boom"))
        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)))
        api.eventChannel.close()
        runCurrent()

        val state = holder.state.value
        assertEquals(ItemStatus.Done, holder.row(1).status)
        assertEquals(ItemStatus.Failed("boom"), holder.row(3).status)
        assertEquals(Phase.FINISHED, state.phase)
        assertEquals(JobSummary(1, 0, 1), state.summary)
        assertEquals(JobStatus.COMPLETED, state.jobStatus)
        assertEquals(listOf(3), state.failedRanks)
    }

    @Test
    fun skippedItemsShowTheirReason() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()

        api.eventChannel.trySend(JobEvent.ItemSkipped(1, "이미 존재"))
        runCurrent()

        assertEquals(ItemStatus.Skipped("이미 존재"), holder.row(1).status)
    }

    @Test
    fun retryRunsOnlyTheFailedRanksAndKeepsTheOtherResults() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemDone(1, "001 A1 - T1.mp3"))
        api.eventChannel.trySend(JobEvent.ItemFailed(3, "boom"))
        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)))
        api.eventChannel.close()
        runCurrent()

        holder.retryFailed()
        runCurrent()

        assertEquals(listOf(3), api.jobRequests.last().ranks)
        assertEquals(Phase.RUNNING, holder.state.value.phase)
        assertEquals(ItemStatus.Done, holder.row(1).status)
        assertEquals(ItemStatus.Waiting, holder.row(3).status)
    }

    @Test
    fun retryOfASingleVideoRerunsTheWholeVideo() = runTest {
        val api = FakeApi().apply { resolveResponse = FakeApi.video() }
        val (_, holder) = resolved(api)
        holder.onSingleRank(7)
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemFailed(7, "boom"))
        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(0, 0, 1)))
        api.eventChannel.close()
        runCurrent()

        holder.retryFailed()
        runCurrent()

        assertNull(api.jobRequests.last().ranks)
        assertEquals(7, api.jobRequests.last().options.singleRank)
    }

    @Test
    fun cancelAsksTheServerToStopTheJob() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()

        holder.cancel()
        runCurrent()
        assertEquals(listOf("job-1"), api.cancelled)

        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.CANCELLED, JobSummary(0, 0, 0)))
        api.eventChannel.close()
        runCurrent()

        assertEquals(Phase.FINISHED, holder.state.value.phase)
        assertEquals(JobStatus.CANCELLED, holder.state.value.jobStatus)
        assertEquals(ItemStatus.Ready, holder.row(1).status)
    }

    @Test
    fun aStreamThatEndsWithoutJobDoneIsReportedAsALostConnection() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()

        api.eventChannel.close()
        runCurrent()

        assertEquals(Phase.FINISHED, holder.state.value.phase)
        assertEquals("서버와의 연결이 끊어졌습니다.", holder.state.value.error)
    }

    @Test
    fun aJobThatCannotStartReturnsToThePreview() = runTest {
        val (api, holder) = resolved()
        api.startError = ApiError("출력 폴더를 만들 수 없습니다")

        holder.startDownload()
        runCurrent()

        assertEquals(Phase.PREVIEW, holder.state.value.phase)
        assertEquals("출력 폴더를 만들 수 없습니다", holder.state.value.error)
        assertEquals(ItemStatus.Ready, holder.row(1).status)
    }

    @Test
    fun startingWithoutAnOutputFolderIsRefused() = runTest {
        val (api, holder) = resolved()
        holder.onOutputDir("  ")

        holder.startDownload()
        runCurrent()

        assertTrue(api.jobRequests.isEmpty())
        assertEquals("출력 폴더를 지정하세요.", holder.state.value.error)
    }

    @Test
    fun resetReturnsToAnEmptyScreenButKeepsSettings() = runTest {
        val (_, holder) = resolved()
        holder.onOutputDir("D:/Songs")

        holder.reset()

        val state = holder.state.value
        assertEquals(Phase.IDLE, state.phase)
        assertTrue(state.rows.isEmpty())
        assertEquals("D:/Songs", state.outputDir)
    }

    // ---- tools ----------------------------------------------------------------------------

    @Test
    fun refreshToolsLoadsTheStatus() = runTest {
        val (api, holder) = holder()

        holder.refreshTools()
        runCurrent()

        assertEquals(api.toolsStatus, holder.state.value.tools)
    }

    @Test
    fun installingYtDlpReportsTheResultAndRefreshesTheStatus() = runTest {
        val (api, holder) = holder()

        holder.installYtDlp()
        runCurrent()

        assertEquals(1, api.installCalls)
        assertEquals("installed", holder.state.value.toolMessage)
        assertEquals(false, holder.state.value.toolBusy)
        assertEquals(api.toolsStatus, holder.state.value.tools)
    }
}
```

`EndToEndTest.kt` (the holder, `HttpXgsApi` and the real server routes together; the engine behind them is fake):

```kotlin
package com.xgetsongs.app.api

import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.state.ItemStatus
import com.xgetsongs.app.state.Phase
import com.xgetsongs.shared.api.JobSummary
import io.ktor.server.testing.testApplication
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.first
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class EndToEndTest {
    @Test
    fun theScreenLogicWorksAgainstTheRealServerRoutes() = testApplication {
        val engine = FakeEngine()
        val api = apiFor(engine)
        coroutineScope {
            val holder = AppStateHolder(api, this, defaultOutputDir = engine.outputDir)
            holder.onInput("PLabcdefghijkl")
            holder.resolve()
            holder.state.first { it.phase == Phase.PREVIEW }

            holder.startDownload()
            val state = holder.state.first { it.phase == Phase.FINISHED }

            assertEquals(ItemStatus.Done, state.rows.single().status)
            assertEquals(JobSummary(1, 0, 0), state.summary)
            assertNull(state.error)
        }
    }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.\gradlew.bat :app:desktopTest`
Expected: FAIL — `Unresolved reference 'AppStateHolder'`.

- [ ] **Step 3: Write the implementation**

```kotlin
package com.xgetsongs.app.state

import com.xgetsongs.app.api.ApiError
import com.xgetsongs.app.api.XgsApi
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.filename.FilenameFormatter
import com.xgetsongs.shared.input.ClassifyResult
import com.xgetsongs.shared.input.InputClassifier
import com.xgetsongs.shared.input.ParsedInput
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** All screen logic. The composables only render [state] and call these functions. */
class AppStateHolder(
    private val api: XgsApi,
    private val scope: CoroutineScope,
    defaultOutputDir: String,
) {
    private val _state = MutableStateFlow(UiState(outputDir = defaultOutputDir))
    val state: StateFlow<UiState> = _state.asStateFlow()

    private var jobId: String? = null
    private var jobTask: Job? = null

    // ---- simple setters -----------------------------------------------------------------

    fun onInput(text: String) = _state.update { it.copy(input = text) }

    fun onOutputDir(dir: String) = _state.update { it.copy(outputDir = dir) }

    fun onOverwrite(value: Boolean) = _state.update { it.copy(overwrite = value) }

    fun onConcurrency(value: Int) = _state.update { it.copy(concurrency = value.coerceIn(1, 4)) }

    /** Changing the rank of a single video rewrites its file name in the preview. */
    fun onSingleRank(value: Int) = _state.update { state ->
        val rank = value.coerceIn(FilenameFormatter.MIN_RANK, FilenameFormatter.MAX_RANK)
        if (state.resolved?.kind != InputKind.VIDEO) {
            state.copy(singleRank = rank)
        } else {
            state.copy(singleRank = rank, rows = state.rows.map { it.withRank(rank) })
        }
    }

    fun dismissError() = _state.update { it.copy(error = null) }

    fun reset() {
        jobTask?.cancel()
        jobId = null
        _state.update {
            UiState(
                outputDir = it.outputDir, overwrite = it.overwrite, concurrency = it.concurrency,
                tools = it.tools,
            )
        }
    }

    // ---- tools ----------------------------------------------------------------------------

    fun refreshTools() {
        scope.launch {
            try {
                val tools = api.tools()
                _state.update { it.copy(tools = tools) }
            } catch (e: ApiError) {
                _state.update { it.copy(error = e.message) }
            }
        }
    }

    fun installYtDlp() = runToolAction { api.installYtDlp().message }

    fun updateYtDlp() = runToolAction { api.updateYtDlp().message }

    private fun runToolAction(action: suspend () -> String) {
        _state.update { it.copy(toolBusy = true, toolMessage = null) }
        scope.launch {
            try {
                val message = action()
                val tools = api.tools()
                _state.update { it.copy(toolBusy = false, toolMessage = message, tools = tools) }
            } catch (e: ApiError) {
                _state.update { it.copy(toolBusy = false, toolMessage = e.message) }
            }
        }
    }

    // ---- resolving ------------------------------------------------------------------------

    fun resolve() {
        val current = _state.value
        if (!current.canResolve) return
        when (val result = InputClassifier.classify(current.input)) {
            is ClassifyResult.Rejected -> _state.update { it.copy(error = result.reason.message) }
            is ClassifyResult.Ok -> doResolve(current.input)
        }
    }

    /** For a watch URL that also carried a playlist: look up just that video instead. */
    fun switchToVideoOnly() {
        val videoId = _state.value.resolved?.alsoVideoId ?: return
        val url = ParsedInput.Video(videoId).canonicalUrl
        _state.update { it.copy(input = url) }
        doResolve(url)
    }

    private fun doResolve(input: String) {
        _state.update { it.copy(phase = Phase.RESOLVING, error = null) }
        scope.launch {
            try {
                val response = api.resolve(input)
                _state.update { state ->
                    state.copy(
                        phase = Phase.PREVIEW,
                        resolved = response,
                        rows = response.items.map { previewRow(it, state.singleRank, response.kind) },
                        summary = null,
                        jobStatus = null,
                    )
                }
            } catch (e: ApiError) {
                _state.update { it.copy(phase = Phase.IDLE, error = e.message) }
            }
        }
    }

    private fun previewRow(item: ResolvedItem, singleRank: Int, kind: InputKind): ItemRow {
        val row = ItemRow(
            item = item,
            fileName = item.expectedFileName,
            status = if (item.available) ItemStatus.Ready else ItemStatus.Skipped(item.unavailableReason.orEmpty()),
        )
        return if (kind == InputKind.VIDEO) row.withRank(singleRank) else row
    }

    private fun ItemRow.withRank(rank: Int): ItemRow {
        if (!item.available) return this
        return copy(
            item = item.copy(rank = rank),
            fileName = FilenameFormatter.format(rank, item.artist, item.track),
        )
    }

    // ---- downloading ----------------------------------------------------------------------

    fun startDownload() {
        val state = _state.value
        val resolved = state.resolved ?: return
        if (state.phase != Phase.PREVIEW && state.phase != Phase.FINISHED) return
        if (state.outputDir.isBlank()) {
            _state.update { it.copy(error = "출력 폴더를 지정하세요.") }
            return
        }
        _state.update { s ->
            s.copy(
                phase = Phase.RUNNING, error = null, summary = null, jobStatus = null,
                rows = s.rows.map { if (it.item.available) it.copy(status = ItemStatus.Waiting) else it },
            )
        }
        runJob(JobRequest(resolved.resolveId, jobOptions(state)), fallbackPhase = state.phase)
    }

    /** Runs a new job for just the items that failed last time. */
    fun retryFailed() {
        val state = _state.value
        val resolved = state.resolved ?: return
        val ranks = state.failedRanks
        if (ranks.isEmpty() || state.phase != Phase.FINISHED) return
        _state.update { s ->
            s.copy(
                phase = Phase.RUNNING, error = null, summary = null, jobStatus = null,
                rows = s.rows.map { if (it.item.rank in ranks) it.copy(status = ItemStatus.Waiting) else it },
            )
        }
        // A single video is always re-run as a whole; its rank is chosen by the user, not by the list.
        val retryRanks = ranks.takeIf { resolved.kind != InputKind.VIDEO }
        runJob(JobRequest(resolved.resolveId, jobOptions(state), ranks = retryRanks), fallbackPhase = state.phase)
    }

    fun cancel() {
        val id = jobId ?: return
        scope.launch {
            try {
                api.cancel(id)
            } catch (e: ApiError) {
                _state.update { it.copy(error = e.message) }
            }
        }
    }

    private fun jobOptions(state: UiState) = JobOptions(
        outputDir = state.outputDir,
        overwrite = state.overwrite,
        singleRank = state.singleRank,
        concurrency = state.concurrency,
    )

    /** [fallbackPhase] is where the screen returns to when the job cannot even be started. */
    private fun runJob(request: JobRequest, fallbackPhase: Phase) {
        jobTask = scope.launch {
            var started = false
            try {
                val created = api.startJob(request)
                started = true
                jobId = created.jobId
                api.events(created.jobId).collect { event -> _state.update { apply(it, event) } }
                _state.update { state ->
                    if (state.phase == Phase.RUNNING) {
                        state.copy(phase = Phase.FINISHED, error = "서버와의 연결이 끊어졌습니다.")
                    } else {
                        state
                    }
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: ApiError) {
                _state.update { state ->
                    state.copy(
                        phase = if (started) Phase.FINISHED else fallbackPhase,
                        error = e.message,
                        rows = state.rows.map(::resetWaiting),
                    )
                }
            } catch (e: Exception) {
                _state.update { it.copy(phase = Phase.FINISHED, error = "서버와 통신 중 오류가 발생했습니다: ${e.message}") }
            }
        }
    }

    private fun resetWaiting(row: ItemRow): ItemRow =
        if (row.status is ItemStatus.Waiting) row.copy(status = ItemStatus.Ready) else row

    private fun apply(state: UiState, event: JobEvent): UiState = when (event) {
        is JobEvent.ItemStarted ->
            state.updateRow(event.rank) { it.copy(fileName = event.fileName, status = ItemStatus.Downloading(null)) }
        is JobEvent.Progress -> state.updateRow(event.rank) {
            it.copy(
                status = when (event.stage) {
                    Stage.DOWNLOADING -> ItemStatus.Downloading(event.percent)
                    Stage.CONVERTING -> ItemStatus.Converting
                },
            )
        }
        is JobEvent.ItemDone -> state.updateRow(event.rank) { it.copy(fileName = event.fileName, status = ItemStatus.Done) }
        is JobEvent.ItemSkipped -> state.updateRow(event.rank) { it.copy(status = ItemStatus.Skipped(event.reason)) }
        is JobEvent.ItemFailed -> state.updateRow(event.rank) { it.copy(status = ItemStatus.Failed(event.message)) }
        is JobEvent.JobDone -> state.copy(
            phase = Phase.FINISHED,
            jobStatus = event.status,
            summary = event.summary,
            rows = state.rows.map(::resetWaiting),
        )
    }

    private fun UiState.updateRow(rank: Int, change: (ItemRow) -> ItemRow): UiState =
        copy(rows = rows.map { if (it.item.rank == rank && it.item.available) change(it) else it })
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.\gradlew.bat :app:desktopTest`
Expected: `BUILD SUCCESSFUL`; `AppStateHolderTest` 19, `EndToEndTest` 1, `HttpXgsApiTest` 7, `LabelsTest` 4 tests; 31 in the app module, 0 failures.

- [ ] **Step 5: Commit**

```powershell
git add app
git commit -m "feat(app): add AppStateHolder" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 18: Compose screens and the desktop entry point

The screens only render `UiState` and call `AppStateHolder`; there are no automated UI tests. Verification is a compile plus looking at the real window.

**Files:**
- Create: `app/src/commonMain/kotlin/com/xgetsongs/app/ui/App.kt`, `ToolsPanel.kt`, `InputPanel.kt`, `OptionsPanel.kt`, `PreviewList.kt`, `ActionBar.kt`
- Create: `app/src/desktopMain/kotlin/com/xgetsongs/app/Main.kt`, `FolderPicker.kt`

**Interfaces:**
- Consumes: `AppStateHolder`, `UiState`, `statusLabel`, `summaryText`, `rankLabel` (Tasks 16–17); `HttpXgsApi`, `configureXgs` (Task 15); `LocalServer` (Task 14).
- Produces: `@Composable fun App(holder: AppStateHolder, pickFolder: suspend (initial: String) -> String?)`; `suspend fun pickFolder(initial: String): String?` (Swing `JFileChooser`, directories only); `fun main()`, which starts `LocalServer.start(appDataDir)`, builds a CIO client with `requestTimeout = 0`, and shows the window "xGetSongs" (1000 × 760). The app's own data lives in `%APPDATA%\xGetSongs` (`bin` for yt-dlp, `work` for temp files); the default output folder is `~/Music/xGetSongs`.

- [ ] **Step 1: Write the screens**

`App.kt` (root layout and the `pickFolder` hook):

```kotlin
package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.AppStateHolder

/**
 * The whole screen. [pickFolder] opens a platform folder chooser with the current folder preselected
 * and returns the chosen path, or null when the user cancels.
 */
@Composable
fun App(holder: AppStateHolder, pickFolder: suspend (initial: String) -> String?) {
    val state by holder.state.collectAsState()
    LaunchedEffect(Unit) { holder.refreshTools() }

    MaterialTheme {
        Surface(modifier = Modifier.fillMaxSize()) {
            Column(
                modifier = Modifier.fillMaxSize().padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                ToolsPanel(state, onInstall = holder::installYtDlp, onUpdate = holder::updateYtDlp)
                InputPanel(state, onInput = holder::onInput, onResolve = holder::resolve)
                state.error?.let { ErrorBanner(it, onDismiss = holder::dismissError) }

                if (state.resolved != null) {
                    ResolveInfo(state, onSwitchToVideoOnly = holder::switchToVideoOnly)
                    OptionsPanel(state, holder, pickFolder)
                    PreviewList(state.rows, modifier = Modifier.weight(1f))
                    ActionBar(state, holder)
                } else {
                    Spacer(Modifier.weight(1f))
                }
            }
        }
    }
}
```

`ToolsPanel.kt` (tool status, install consent dialog, update):

```kotlin
package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.UiState
import com.xgetsongs.shared.api.ToolInfo

/** Shows which external tools are available and offers to install or update yt-dlp. */
@Composable
fun ToolsPanel(state: UiState, onInstall: () -> Unit, onUpdate: () -> Unit) {
    var confirmInstall by remember { mutableStateOf(false) }
    val tools = state.tools

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (tools == null) {
                Text("도구 확인 중…", style = MaterialTheme.typography.bodyMedium)
            } else {
                Row(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    ToolStatus("yt-dlp", tools.ytDlp)
                    ToolStatus("ffmpeg", tools.ffmpeg)
                    ToolStatus("JS 런타임", tools.jsRuntime)
                }
                if (!tools.ytDlp.found) {
                    Button(enabled = !state.toolBusy, onClick = { confirmInstall = true }) { Text("yt-dlp 설치") }
                } else {
                    OutlinedButton(enabled = !state.toolBusy, onClick = onUpdate) { Text("yt-dlp 업데이트") }
                }
                if (!tools.ffmpeg.found) {
                    Text(
                        "ffmpeg가 필요합니다. 예: winget install Gyan.FFmpeg",
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
                if (!tools.jsRuntime.found) {
                    Text(
                        "YouTube를 읽으려면 Node.js 22 이상 또는 Deno 2.3 이상이 필요합니다.",
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
            if (state.toolBusy) Text("작업 중…", style = MaterialTheme.typography.bodySmall)
            state.toolMessage?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
        }
    }

    if (confirmInstall) {
        AlertDialog(
            onDismissRequest = { confirmInstall = false },
            title = { Text("yt-dlp 설치") },
            text = {
                Text("GitHub(github.com/yt-dlp/yt-dlp)에서 yt-dlp.exe를 내려받아 이 앱 전용 폴더에 저장합니다. 계속할까요?")
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmInstall = false
                    onInstall()
                }) { Text("내려받기") }
            },
            dismissButton = { TextButton(onClick = { confirmInstall = false }) { Text("취소") } },
        )
    }
}

@Composable
private fun ToolStatus(name: String, info: ToolInfo) {
    val text = if (info.found) "$name ${info.version.orEmpty()} ✓" else "$name ✗"
    Text(
        text,
        style = MaterialTheme.typography.bodyMedium,
        color = if (info.found) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.error,
    )
}
```

`InputPanel.kt` (input field, error banner, playlist info and the "이 영상만 받기" shortcut):

```kotlin
package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.UiState
import com.xgetsongs.shared.api.InputKind

@Composable
fun InputPanel(state: UiState, onInput: (String) -> Unit, onResolve: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        OutlinedTextField(
            value = state.input,
            onValueChange = onInput,
            label = { Text("재생목록 ID 또는 영상 주소") },
            singleLine = true,
            modifier = Modifier.weight(1f),
        )
        Button(enabled = state.canResolve && state.input.isNotBlank(), onClick = onResolve) { Text("조회") }
    }
}

@Composable
fun ErrorBanner(message: String, onDismiss: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.weight(1f))
        TextButton(onClick = onDismiss) { Text("닫기") }
    }
}

/** Playlist title, warnings, and the "this video only" shortcut. */
@Composable
fun ResolveInfo(state: UiState, onSwitchToVideoOnly: () -> Unit) {
    val resolved = state.resolved ?: return
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val title = resolved.playlistTitle ?: "단일 영상"
        val count = if (resolved.kind == InputKind.PLAYLIST) " · ${resolved.items.size}개" else ""
        Text(title + count, style = MaterialTheme.typography.titleMedium)
        if (resolved.truncated) {
            Text("999개를 넘어 앞 999개만 표시합니다.", color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(start = 4.dp))
        }
        if (resolved.alsoVideoId != null && state.canResolve) {
            TextButton(onClick = onSwitchToVideoOnly) { Text("이 영상만 받기") }
        }
    }
}
```

`OptionsPanel.kt` (output folder, overwrite, concurrency, single-video rank):

```kotlin
package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilterChip
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.state.Phase
import com.xgetsongs.app.state.UiState
import com.xgetsongs.shared.api.InputKind
import kotlinx.coroutines.launch

@Composable
fun OptionsPanel(state: UiState, holder: AppStateHolder, pickFolder: suspend (String) -> String?) {
    val scope = rememberCoroutineScope()
    val enabled = state.phase != Phase.RUNNING

    Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OutlinedTextField(
                value = state.outputDir,
                onValueChange = holder::onOutputDir,
                label = { Text("출력 폴더") },
                singleLine = true,
                enabled = enabled,
                modifier = Modifier.weight(1f),
            )
            OutlinedButton(
                enabled = enabled,
                onClick = { scope.launch { pickFolder(state.outputDir)?.let(holder::onOutputDir) } },
            ) { Text("폴더 선택") }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Checkbox(checked = state.overwrite, onCheckedChange = holder::onOverwrite, enabled = enabled)
            Text("기존 파일 덮어쓰기")
            Text("동시 다운로드", modifier = Modifier.width(100.dp))
            (1..4).forEach { n ->
                FilterChip(
                    selected = state.concurrency == n,
                    onClick = { holder.onConcurrency(n) },
                    label = { Text("$n") },
                    enabled = enabled,
                )
            }
            if (state.resolved?.kind == InputKind.VIDEO) {
                OutlinedTextField(
                    value = state.singleRank.toString(),
                    onValueChange = { holder.onSingleRank(it.filter(Char::isDigit).toIntOrNull() ?: 1) },
                    label = { Text("순위 번호") },
                    singleLine = true,
                    enabled = enabled,
                    modifier = Modifier.width(120.dp),
                )
            }
        }
    }
}
```

`PreviewList.kt` (the list with ⚠ marks and progress bars):

```kotlin
package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.ItemRow
import com.xgetsongs.app.state.ItemStatus
import com.xgetsongs.app.state.rankLabel
import com.xgetsongs.app.state.statusLabel

@Composable
fun PreviewList(rows: List<ItemRow>, modifier: Modifier = Modifier) {
    LazyColumn(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        items(rows, key = { "${it.item.rank}-${it.item.videoId}" }) { row -> PreviewRow(row) }
    }
}

@Composable
private fun PreviewRow(row: ItemRow) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(rankLabel(row.item.rank), fontFamily = FontFamily.Monospace, modifier = Modifier.width(40.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                row.fileName ?: row.item.title,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                color = if (row.item.available) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.outline,
            )
            if (row.item.available && row.item.lowConfidence) {
                Text(
                    "⚠ 가수명을 채널명에서 추정했습니다",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.tertiary,
                )
            }
        }
        StatusCell(row.status, modifier = Modifier.width(220.dp))
    }
}

@Composable
private fun StatusCell(status: ItemStatus, modifier: Modifier = Modifier) {
    Column(modifier = modifier) {
        Text(
            statusLabel(status),
            style = MaterialTheme.typography.bodySmall,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            color = if (status is ItemStatus.Failed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface,
        )
        if (status is ItemStatus.Downloading && status.percent != null) {
            LinearProgressIndicator(
                progress = { (status.percent / 100.0).toFloat() },
                modifier = Modifier.fillMaxWidth().padding(top = 2.dp),
            )
        }
    }
}
```

`ActionBar.kt` (start / cancel / retry / reset):

```kotlin
package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.state.Phase
import com.xgetsongs.app.state.UiState
import com.xgetsongs.app.state.summaryText

@Composable
fun ActionBar(state: UiState, holder: AppStateHolder) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        when (state.phase) {
            Phase.PREVIEW -> {
                Button(
                    enabled = state.rows.any { it.item.available },
                    onClick = holder::startDownload,
                ) { Text("다운로드 시작") }
                OutlinedButton(onClick = holder::reset) { Text("새로 시작") }
            }
            Phase.RUNNING -> Button(onClick = holder::cancel) { Text("취소") }
            Phase.FINISHED -> {
                summaryText(state)?.let { Text(it) }
                if (state.failedRanks.isNotEmpty()) {
                    Button(onClick = holder::retryFailed) { Text("실패 항목 재시도") }
                }
                OutlinedButton(onClick = holder::startDownload) { Text("다시 다운로드") }
                OutlinedButton(onClick = holder::reset) { Text("새로 시작") }
            }
            Phase.IDLE, Phase.RESOLVING -> Unit
        }
    }
}
```

- [ ] **Step 2: Write the desktop entry point**

`Main.kt`:

```kotlin
package com.xgetsongs.app

import androidx.compose.runtime.remember
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.xgetsongs.app.api.HttpXgsApi
import com.xgetsongs.app.api.configureXgs
import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.ui.App
import com.xgetsongs.server.LocalServer
import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import java.nio.file.Path

fun main() {
    val server = LocalServer.start(appDataDirectory())
    val http = HttpClient(CIO) {
        configureXgs(token = server.token, baseUrl = "http://127.0.0.1:${server.port}")
        // No request timeout: resolving a big playlist and the SSE stream can legitimately be silent for a while.
        engine { requestTimeout = 0 }
    }
    val uiScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    application {
        val holder = remember { AppStateHolder(HttpXgsApi(http), uiScope, defaultOutputDirectory().toString()) }
        Window(
            onCloseRequest = {
                uiScope.cancel()
                http.close()
                server.stop()
                exitApplication()
            },
            title = "xGetSongs",
            state = rememberWindowState(width = 1000.dp, height = 760.dp),
        ) {
            App(holder, pickFolder = ::pickFolder)
        }
    }
}

/** Where the app keeps its own files: the yt-dlp it installed and temporary download folders. */
internal fun appDataDirectory(): Path {
    val appData = System.getenv("APPDATA")
    return if (appData != null) Path.of(appData, "xGetSongs") else Path.of(System.getProperty("user.home"), ".xgetsongs")
}

internal fun defaultOutputDirectory(): Path = Path.of(System.getProperty("user.home"), "Music", "xGetSongs")
```

`FolderPicker.kt`:

```kotlin
package com.xgetsongs.app

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.swing.Swing
import kotlinx.coroutines.withContext
import java.io.File
import javax.swing.JFileChooser

/** Opens Swing's folder chooser on the UI thread. Returns the chosen absolute path or null if cancelled. */
suspend fun pickFolder(initial: String): String? = withContext(Dispatchers.Swing) {
    val chooser = JFileChooser(File(initial).takeIf { it.isDirectory }).apply {
        fileSelectionMode = JFileChooser.DIRECTORIES_ONLY
        dialogTitle = "출력 폴더 선택"
    }
    if (chooser.showOpenDialog(null) == JFileChooser.APPROVE_OPTION) chooser.selectedFile.absolutePath else null
}
```

- [ ] **Step 3: Compile**

Run: `.\gradlew.bat :app:compileKotlinDesktop`
Expected: `BUILD SUCCESSFUL`, no `e:` lines. (`Dispatchers.Swing` needs `import kotlinx.coroutines.swing.Swing`.)

- [ ] **Step 4: Look at the real window**

Run: `.\gradlew.bat :app:run` (it blocks while the window is open).
Expected on the machine this plan was verified on: a window titled "xGetSongs" with a tool panel reading `yt-dlp ✗  ffmpeg <version> ✓  JS 런타임 <version> ✓` and a **yt-dlp 설치** button, an input field labelled "재생목록 ID 또는 영상 주소", and a disabled **조회** button; Korean text renders correctly. Typing `hello` and pressing 조회 must show the red message "재생목록 ID 또는 영상 주소로 인식할 수 없습니다." without any network traffic. Close the window (this also stops the embedded server).

- [ ] **Step 5: Run every check**

Run: `.\gradlew.bat check`
Expected: `BUILD SUCCESSFUL`; 194 tests in total across the four modules, 0 failures.

- [ ] **Step 6: Commit**

```powershell
git add app
git commit -m "feat(app): add Compose screens and desktop entry point" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 19: Real yt-dlp: integration tests, verification of the unverified assumptions, README

This is the first contact with the real tool. **It downloads yt-dlp from GitHub and then downloads one 19-second public video from YouTube. Ask the user for permission before Step 3** (the app's own consent dialog covers the yt-dlp download as well). If the user declines, finish Steps 1, 2 and 6 and report that Steps 3–5 were not run.

**Files:**
- Modify: `engine/build.gradle.kts` (add the `integrationTest` task and exclude the tag from `test`)
- Create: `engine/src/test/kotlin/com/xgetsongs/engine/integration/RealYtDlpIntegrationTest.kt`
- Create: `README.md`

**Interfaces:**
- Consumes: everything in `engine`.
- Produces: Gradle task `:engine:integrationTest` (JUnit tag `integration`); `README.md`.

- [ ] **Step 1: Add the integration test and its Gradle task**

Replace the `tasks.test { … }` block of `engine/build.gradle.kts` so that the whole file reads:

```kotlin
plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    api(project(":shared"))
    api(libs.kotlinx.coroutines.core)
    implementation(libs.kotlinx.serialization.json)

    testImplementation(kotlin("test"))
    testImplementation(libs.kotlinx.coroutines.test)
}

tasks.test {
    // Tests that need the real yt-dlp and the network run only through integrationTest.
    useJUnitPlatform { excludeTags("integration") }
}

val integrationTest by tasks.registering(Test::class) {
    description = "Runs the tests that talk to the real YouTube with the real yt-dlp and ffmpeg."
    group = "verification"
    testClassesDirs = sourceSets.test.get().output.classesDirs
    classpath = sourceSets.test.get().runtimeClasspath
    useJUnitPlatform { includeTags("integration") }
    testLogging {
        showStandardStreams = true
        events("passed", "skipped", "failed")
    }
}
```

`engine/src/test/kotlin/com/xgetsongs/engine/integration/RealYtDlpIntegrationTest.kt` (skipped, not failed, when yt-dlp, ffmpeg or a JS runtime is missing):

```kotlin
package com.xgetsongs.engine.integration

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.job.DefaultDownloadService
import com.xgetsongs.engine.job.ItemDownloader
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tools.ToolLocator
import com.xgetsongs.engine.ytdlp.YtDlpResolver
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * Talks to the real YouTube through the real yt-dlp and ffmpeg, so it is excluded from `test` and
 * only runs through `integrationTest`. It is skipped (not failed) when a tool is missing.
 */
@Tag("integration")
class RealYtDlpIntegrationTest {
    private val appData = System.getenv("APPDATA")?.let { Path.of(it, "xGetSongs") }
        ?: Path.of(System.getProperty("user.home"), ".xgetsongs")
    private val locator = ToolLocator(appBinDir = appData.resolve("bin"))
    private val runner = SystemProcessRunner()

    @BeforeTest
    fun requireTools() {
        val tools = locator.current()
        assumeTrue(
            tools.ytDlp != null && tools.ffmpeg != null && tools.jsRuntime != null,
            "yt-dlp, ffmpeg and Node 22+/Deno 2.3+ must be installed",
        )
    }

    @Test
    fun resolvesTheReferencePlaylistWithoutDownloadingAnything() = runBlocking {
        val response = YtDlpResolver(runner, locator)
            .resolve("https://www.youtube.com/playlist?list=PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV")

        println("playlist '${response.playlistTitle}' has ${response.items.size} items")
        response.items.take(25).forEach {
            println("%03d %-5s %s".format(it.rank, if (it.lowConfidence) "WARN" else "ok", it.expectedFileName ?: "(${it.unavailableReason})"))
        }

        assertTrue(response.items.size > 10, "expected a long chart playlist")
        assertEquals(response.items.indices.map { it + 1 }, response.items.map { it.rank })
        val available = response.items.filter { it.available }
        assertTrue(available.isNotEmpty())
        assertTrue(available.all { Regex("""\d{3} .+ - .+\.mp3""").matches(it.expectedFileName!!) })
    }

    @Test
    fun downloadsAShortVideoAsMp3() = runBlocking {
        // "Me at the zoo": the first video ever uploaded to YouTube, 19 seconds long.
        val resolver = YtDlpResolver(runner, locator)
        val resolved = resolver.resolve("https://www.youtube.com/watch?v=jNQXAC9IVRw")
        val root = Files.createTempDirectory("xgs-integration")
        val outDir = root.resolve("out")

        val service = DefaultDownloadService(ItemDownloader(runner, locator, resolver), root.resolve("work"), this)
        val events = service
            .start(DownloadRequest(resolved.items, LocalFolderSink(outDir), overwrite = false, concurrency = 1))
            .events.receiveAsFlow().toList()

        val done = events.last() as JobEvent.JobDone
        assertEquals(JobStatus.COMPLETED, done.status, events.toString())
        assertEquals(1, done.summary.succeeded, events.toString())
        val file = Files.list(outDir).use { it.toList().single() }
        println("downloaded: ${file.fileName} (${Files.size(file)} bytes)")
        assertTrue(Regex("""001 .+ - .+\.mp3""").matches(file.fileName.toString()), file.fileName.toString())
        assertTrue(Files.size(file) > 50_000)
        root.toFile().deleteRecursively()
    }
}
```

- [ ] **Step 2: Verify the wiring without yt-dlp**

Run: `.\gradlew.bat check`
Expected: `BUILD SUCCESSFUL`; the integration tests do not run (194 tests).

Run: `.\gradlew.bat :engine:integrationTest`
Expected: `BUILD SUCCESSFUL` with both tests reported `SKIPPED` while yt-dlp is not installed.

- [ ] **Step 3: Install yt-dlp (needs the user's permission)**

Ask the user first. Either use the app's **yt-dlp 설치** button (`.\gradlew.bat :app:run`, press the button, confirm the dialog), or run `winget install yt-dlp.yt-dlp`. Then check:

```powershell
yt-dlp --version
```

or, for the app-managed copy, `& "$env:APPDATA\xGetSongs\bin\yt-dlp.exe" --version`. Expected: a version string such as `2026.xx.xx`. ffmpeg and Node 22+/Deno 2.3+ must already be installed (the app's tool panel shows all three).

- [ ] **Step 4: Compare the real playlist output with the assumptions**

Capture the real flat listing of the reference playlist and look at the entries. In particular check how a deleted or private video appears (title, `availability`), and which of `channel`/`uploader` is filled:

```powershell
$yt = "$env:APPDATA\xGetSongs\bin\yt-dlp.exe"   # or just yt-dlp when installed with winget
& $yt --ignore-config --flat-playlist -J --encoding utf-8 "https://www.youtube.com/playlist?list=PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV" | python -c "import json,sys; d=json.load(sys.stdin); print(d.get('title'), len(d['entries'])); print(d['entries'][0]); [print(e) for e in d['entries'] if (e.get('title') or '').startswith('[')]"
```

Expected (what `YtDlpResolver` assumes): `entries` is a list of objects with `id` and `title`, usually `channel` and/or `uploader`; unavailable videos keep their position and have the titles `[Private video]` / `[Deleted video]`. If the real output differs — for example unavailable entries are missing from the list (which would shift every later rank), or other `availability` values appear — change `YtDlpResolver.unavailableReason` / `toItem`, update the matching fixture in `YtDlpResolverTest` to the real shape, and rerun `.\gradlew.bat :engine:test`.

- [ ] **Step 5: Run the integration tests**

Run: `.\gradlew.bat :engine:integrationTest`
Expected: `BUILD SUCCESSFUL`, both tests `PASSED`. The output prints the first 25 planned file names of the reference playlist — read them: every available entry should look like `NNN <artist> - <title>.mp3`, and entries marked `WARN` are the ones whose artist fell back to the channel name. The second test prints the downloaded file name and size (about `001 jawed - Me at the zoo.mp3`, some hundred KB). If a file name is wrong, add the real title as a fixture to `TitleParserTest`, fix `TitleParser`, and rerun both `check` and `integrationTest`. If YouTube answers "Sign in to confirm you're not a bot", stop and report it; do not work around it.

- [ ] **Step 6: Write the README and commit**

`README.md`:

````markdown
# xGetSongs

YouTube 재생목록 ID를 입력하면 목록의 모든 영상에서 오디오를 추출해 mp3로 저장하고, 영상 주소 1개를 입력하면 그 영상의 오디오만 mp3로 저장하는 Windows 데스크톱 앱입니다. 나중에 웹으로 옮길 수 있게 설계했습니다.

파일명 형식: `{순위번호 001~999} {가수명} - {제목}.mp3`

예: `001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).mp3`

설계 문서: [docs/superpowers/specs/2026-10-04-xgetsongs-design.md](docs/superpowers/specs/2026-10-04-xgetsongs-design.md)

## 필요한 것

| 도구 | 용도 | 설치 |
|---|---|---|
| JDK 21 | 빌드와 실행 | |
| yt-dlp | 영상에서 오디오 추출 | 앱의 "yt-dlp 설치" 버튼 또는 `winget install yt-dlp.yt-dlp` |
| ffmpeg | mp3 변환 | `winget install Gyan.FFmpeg` |
| Node.js 22+ 또는 Deno 2.3+ | yt-dlp가 YouTube를 읽는 데 필요 | `winget install OpenJS.NodeJS` |

앱 첫 화면 위쪽에서 세 도구의 설치 여부와 버전을 확인할 수 있습니다.

## 실행

```powershell
.\gradlew.bat :app:run
```

## 테스트

```powershell
.\gradlew.bat check                    # 전체 단위 테스트
.\gradlew.bat :engine:integrationTest  # 실제 yt-dlp와 YouTube를 쓰는 통합 테스트 (도구가 없으면 건너뜀)
```

## 구조

```
shared/   순수 Kotlin: 입력 판별, 제목 파싱, 파일명 생성, API 모델 (웹/Wasm에서도 재사용)
engine/   yt-dlp·ffmpeg 호출, 재생목록 조회, 다운로드 작업 관리, 출력 저장소
server/   Ktor API 서버 (데스크톱 앱이 127.0.0.1에 내장 실행)
app/      Compose Multiplatform UI (지금은 desktop 타깃만)
```

## 주의

- 내려받은 콘텐츠의 저작권과 YouTube 서비스 약관은 사용자의 책임입니다. 본인이 사용할 목적으로만 쓰세요.
- 내장 서버는 `127.0.0.1`에만 열리며, 실행할 때마다 새로 만든 토큰이 있어야 접근할 수 있습니다.
````

```powershell
git add engine README.md
git commit -m "test(engine): add real yt-dlp integration tests and README" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 20: Bring the design spec in line with the plan

Decisions made while planning and verifying differ from, or add to, the spec. Apply the twelve edits below to `docs/superpowers/specs/2026-10-04-xgetsongs-design.md`. Each `old` text appears exactly once in that file (this was checked by applying them to a copy); use it as the `old_string` of an Edit and the `new` text as the `new_string`.

**Files:**
- Modify: `docs/superpowers/specs/2026-10-04-xgetsongs-design.md`

- [ ] **Step 1: Apply the edits**

**Edit 1 — §4 table, engine row**

old:
```text
| 다운로드 엔진 | `yt-dlp` + `ffmpeg` (외부 프로세스 호출) |
```
new:
```text
| 다운로드 엔진 | `yt-dlp` + `ffmpeg` (외부 프로세스 호출). YouTube를 읽으려면 yt-dlp가 JavaScript 런타임(Deno 2.3+ 또는 Node 22+)도 필요로 한다. |
```

**Edit 2 — §6.2 step 1, noise list**

old:
```text
`Official MV`, `Official Video`
```
new:
```text
`Official MV`, `Official M/V`, `Official Video`
```

**Edit 3 — §6.2 step 3**

old:
```text
3. **메타데이터 보완:** 분리에 실패했을 때만 yt-dlp의 `artist`/`track`을 쓴다. 제목이 곡명뿐인 YouTube Music Topic 채널이 해당한다.
```
new:
```text
3. **메타데이터 보완:** 분리에 실패했을 때만 yt-dlp의 `artist`/`track`을 쓴다. 제목이 곡명뿐인 YouTube Music Topic 채널이 해당한다. 채널명이 ` - Topic`으로 끝나는 자동 생성 채널의 제목은 ` - `가 있어도 분리하지 않는다(곡명의 일부이기 때문).
```

**Edit 4 — §6.2 step 4**

old:
```text
신뢰도를 낮음으로 표시하고 미리보기에 ⚠를 보인다.
```
new:
```text
채널명도 없으면 가수명은 `Unknown Artist`다. 신뢰도를 낮음으로 표시하고 미리보기에 ⚠를 보인다. 다운로드 시작 시 yt-dlp의 전체 메타데이터로 한 번 더 파싱하므로 최종 파일명이 미리보기와 달라질 수 있다.
```

**Edit 5 — §6.3 length bullet**

old:
```text
- **길이:** 확장자를 뺀 이름을 180자로 제한한다. 넘으면 순위와 가수를 보존하고 제목 끝을 `…`으로 줄인다.
```
new:
```text
- **길이:** 확장자를 뺀 이름을 180자로 제한한다. 넘으면 순위와 가수를 보존하고 제목 끝을 `…`으로 줄인다. 가수명 자체가 80자를 넘으면 가수명도 `…`으로 줄인다. 제목이 비어 있으면 `untitled`를 쓴다.
```

**Edit 6 — §7 API table, `POST /jobs` row**

old:
```text
| `POST /jobs` | 다운로드 시작. 옵션은 출력 폴더, 덮어쓰기 여부, 단일 영상의 순위 번호. `jobId` 반환 |
```
new:
```text
| `POST /jobs` | 다운로드 시작. 서버는 `POST /resolve` 결과를 `resolveId`로 최근 20개까지 보관하고, 이 요청은 `resolveId`, 옵션(출력 폴더, 덮어쓰기, 단일 영상의 순위 번호, 동시 개수)과 선택적 `ranks`(실패 항목 재시도용)를 받는다. `jobId` 반환 |
```

**Edit 7 — §7 API table, events row**

old:
```text
| `GET /jobs/{id}/events` | SSE 진행 이벤트: `item-started`, `progress`, `item-done`, `item-failed`, `job-done` |
```
new:
```text
| `GET /jobs/{id}/events` | SSE 진행 이벤트: `item-started`, `progress`, `item-done`, `item-skipped`, `item-failed`, `job-done` |
```

**Edit 8 — §7 API table, tools row (adds two rows)**

old:
```text
| `GET /tools` | yt-dlp와 ffmpeg의 존재 여부와 버전 |
```
new:
```text
| `GET /tools` | yt-dlp, ffmpeg, JS 런타임의 존재 여부와 버전 |
| `POST /tools/yt-dlp/install` | yt-dlp를 앱 전용 폴더에 내려받는다(UI가 먼저 사용자 동의를 받는다) |
| `POST /tools/yt-dlp/update` | `yt-dlp -U` 실행 |
```

**Edit 9 — §7 behaviour rules (adds a bullet)**

old:
```text
- 미리보기의 파일명은 resolve 시점의 예상값이다. 다운로드 시점에 얻은 메타데이터로 이름이 달라지면 `item-started` 이벤트로 최종 이름을 UI에 알린다.
```
new:
```text
- 미리보기의 파일명은 resolve 시점의 예상값이다. 다운로드 시점에 얻은 메타데이터로 이름이 달라지면 `item-started` 이벤트로 최종 이름을 UI에 알린다.
- 작업의 이벤트 스트림은 구독자 한 명만 가질 수 있다. 연결이 끊기면 같은 작업에 다시 붙을 수 없고 취소만 가능하다(재연결 지원은 5단계에서 검토).
```

**Edit 10 — §8 table, last row**

old:
```text
| yt-dlp나 ffmpeg 없음 | 시작 시 `/tools`로 점검하고 설치 안내 표시 |
```
new:
```text
| yt-dlp, ffmpeg 또는 JS 런타임 없음 | 시작 시 `/tools`로 점검하고 설치 안내 표시 |
```

**Edit 11 — §10.2 first bullet**

old:
```text
- `shared`에는 `java.*`를 쓰지 않고 순수 Kotlin만 사용한다.
```
new:
```text
- `shared`에는 `java.*`를 쓰지 않고 순수 Kotlin만 사용한다. `jvm()` 타깃 하나뿐일 때는 컴파일러가 이를 막아 주지 않으므로 Gradle 태스크 `checkCommonPurity`가 검사한다.
```

**Edit 12 — §11 first bullet**

old:
```text
- yt-dlp가 아직 설치되어 있지 않아 `--flat-playlist -J` 출력의 필드명(`title`, `channel`/`uploader`, `availability` 등)은 구현 초기에 실제 출력으로 확인한다.
```
new:
```text
- `--flat-playlist -J` 출력의 필드명(`title`, `channel`/`uploader`, `availability` 등)과 삭제·비공개 항목의 표현은 구현 계획 Task 19에서 실제 yt-dlp 출력으로 확인한다. 확인 결과는 이 항목을 갱신해 기록한다.
```

- [ ] **Step 2: Check the result**

Run:

```powershell
Select-String -Path docs\superpowers\specs\2026-10-04-xgetsongs-design.md -Pattern 'item-skipped','Official M/V','Unknown Artist','checkCommonPurity','JS 런타임' | Select-Object -ExpandProperty Line
```

Expected: at least one line for each of the five search terms.

- [ ] **Step 3: Commit**

```powershell
git add docs/superpowers/specs/2026-10-04-xgetsongs-design.md
git commit -m "docs: sync design spec with the implementation plan" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review against the design spec

| Spec section | Task |
|---|---|
| §2 decisions, §4 stack | Global Constraints; Tasks 1, 5, 12, 15 |
| §5 modules | File Structure; Tasks 1, 5, 12, 15 |
| §6.1 input classification | Task 1 |
| §6.2 title parsing | Task 3 |
| §6.3 file names | Task 2 |
| §7 job and API (states, endpoints, concurrency, progress, summary, retry, final name) | Tasks 4, 7, 9, 10, 13, 14, 17 |
| §8 errors (invalid input, private/unavailable, transient retry, yt-dlp failure, fatal, tools missing, temp folders, yt-dlp update with consent) | Tasks 7, 10, 11, 17, 18 |
| §9 tests (shared, engine, server, UI state, integration) | every task; integration in Task 19 |
| §10.2 rules (pure shared, UI only via API, loopback + token + Origin, LOCAL/HOSTED) | Tasks 1, 12, 13, 14, 15–18 |
| §11 to verify | Task 19 (yt-dlp output), Task 20 (spec updated) |
| §10.1 stages 2–5, §10.3 web risks | out of scope here (later stages) |

Out of scope on purpose (design spec §3): ID3 tags and cover art, editing names in the preview, choosing single items, Android/iOS, resuming jobs after a restart, a public web service.
