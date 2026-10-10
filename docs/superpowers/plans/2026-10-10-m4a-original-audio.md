# 출력 형식 mp3 → m4a 원본 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** YouTube의 AAC 오디오(itag 140)를 재인코딩 없이 `.m4a`로 저장하고, ffmpeg가 MP4 태그와 커버를 한 번에 쓴다. mp3 출력과 직접 만든 ID3 코드는 없앤다.

**Architecture:** yt-dlp는 `-f bestaudio[ext=m4a] --fixup never`로 조각난 DASH m4a를 그대로 받고, `M4aTagger`가 ffmpeg 스트림 복사 한 번으로 태그·커버·코멘트·가사를 쓰면서 컨테이너를 일반 M4A로 다시 쓴다(오디오 MD5는 그대로). 진행 단계는 yt-dlp의 후처리 줄 대신 `ItemDownloader`가 yt-dlp 종료 직후 직접 `Stage.FINISHING`을 보낸다.

**Tech Stack:** Kotlin 2.4.20 / JDK 21 / Gradle Kotlin DSL, kotlin.test, 실제 yt-dlp·ffmpeg 8.x·ffprobe(통합 테스트).

설계 문서: [2026-10-10-m4a-original-audio-design.md](../specs/2026-10-10-m4a-original-audio-design.md)

## Global Constraints

- yt-dlp 명령은 `-f bestaudio[ext=m4a]`와 `--fixup never`를 쓰고 `-x`, `--extract-audio`, `--audio-format`, `--audio-quality`는 쓰지 않는다. 오디오는 재인코딩하지 않는다(ffmpeg도 `-c:a copy`).
- 파일 확장자는 `.m4a`다. `FfmpegCommands.tag`의 출력은 `.m4a`로 끝나야 한다(ffmpeg가 확장자로 muxer를 고른다). 출력 폴더에 이미 있는 `.mp3`는 건드리지 않는다.
- 진행 단계 이름: `Stage.CONVERTING` → `Stage.FINISHING`, `ItemStatus.Converting` → `ItemStatus.Finishing`, 화면 문구 "mp3 변환 중…" → "마무리 중…".
- 사용자에게 보이는 문구: 형식 없음 실패 "m4a 오디오 형식이 없습니다."(`FailureKind.OTHER`), 태그 실패 "태그를 쓰지 못했습니다: …", 파일 없음 "받은 m4a 파일을 찾을 수 없습니다.".
- 태그 값의 규칙은 그대로다: 앨범은 폴더 이름의 근거와 같고, 앨범 아티스트는 모든 곡에서 `Various Artists`, 트랙 번호는 순위, 코멘트는 영상 URL. 가사의 줄바꿈은 LF다(CRLF 변환 없음). 값 끝의 `\`를 전각 `＼`로 바꾸고 제어 문자를 지우는 `Ffmetadata`의 규칙도 그대로다.
- 새 옵션, API 모델, 설정, 화면은 추가하지 않는다. Opus, mp3 토글, 형식 선택, 기존 mp3 변환은 범위 밖이다.
- Gradle은 항상 `--no-daemon`으로 돌린다(공유 데몬이 죽으면 사용자의 앱이 같이 죽은 적이 있다). 명령은 저장소 루트에서 Git Bash로 `./gradlew --no-daemon --console=plain …`처럼 쓴다. 파일 안의 문자열 치환은 `sed -i`(GNU sed)를 쓰고, 모든 소스는 LF 줄바꿈이다.
- 코드 주석은 영어(기존 코드와 같다), 문서는 한국어. 커밋은 main에 바로 하고 메시지는 한국어, 끝에 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 줄을 붙인다. push는 Task 5 끝에서 한 번에 한다(Task 3 전에는 앱이 mp3, Task 3 뒤에는 m4a를 만들므로 중간 상태를 올리지 않는다).
- `docs/xGetSongs 작업 메모.txt`는 사용자의 작업 메모다. 건드리지 않는다. 추적 중인 파일만 올리는 `git add -u`와 새 파일을 이름으로 지정한 `git add`만 쓴다(`git add .`, `git add -A` 금지).
- 테스트 결과는 XML 리포트로 개수를 확인할 수 있다: `grep -h "<testsuite " engine/build/test-results/test/*.xml | sed 's/ timestamp.*//'`

## 파일 구조

| 파일 | 역할 | Task |
|---|---|---|
| `shared/src/commonMain/.../api/ApiModels.kt` (수정) | `Stage.FINISHING`, KDoc | 1, 3 |
| `app/src/commonMain/.../state/{UiState,Labels,AppStateHolder}.kt` (수정) | `ItemStatus.Finishing`, "마무리 중…" | 1 |
| `engine/src/main/.../ytdlp/ProgressParser.kt` (수정) | 다운로드 줄만 해석 | 2 |
| `engine/src/main/.../ytdlp/YtDlpCommands.kt` (수정) | 후처리 템플릿 삭제(2), `-f bestaudio[ext=m4a] --fixup never`(3) | 2, 3 |
| `engine/src/main/.../job/ItemDownloader.kt` (수정) | 직접 `FINISHING` 보고(2), m4a 파일·`M4aTagger`(3) | 2, 3 |
| `shared/src/commonMain/.../filename/FilenameFormatter.kt` (수정) | 확장자 `.m4a` | 3 |
| `engine/src/main/.../tags/Ffmetadata.kt` (수정) | 코멘트·가사도 렌더링 | 3 |
| `engine/src/main/.../tags/TrackTags.kt` (수정) | KDoc을 MP4 항목 이름으로 | 3 |
| `engine/src/main/.../ytdlp/FfmpegCommands.kt` (수정) | `.m4a` 출력, `-id3v2_version` 삭제 | 3 |
| `engine/src/main/.../tags/M4aTagger.kt` (`Id3Tagger.kt`에서 이름 변경) | 한 번의 ffmpeg 복사로 태그 쓰기 | 3 |
| `engine/src/main/.../tags/Id3Frames.kt` (삭제) | 직접 만든 COMM·USLT 삽입 코드 | 3 |
| `engine/src/main/.../ytdlp/ErrorClassifier.kt` (수정) | 형식 없음 분류 | 3 |
| `engine/src/test/.../testutil/{Fakes.kt,FfmetadataReader.kt,FfmetadataReaderTest.kt}` | 가짜 m4a, ffmetadata 읽기 도구 | 3 |
| `engine/src/test/.../testutil/Id3v2Tag.kt`, `tags/Id3FramesTest.kt` (삭제) | | 3 |
| `engine/src/test/.../tags/{FfmetadataTest,M4aTaggerTest}.kt`, `ytdlp/{FfmpegCommandsTest,YtDlpCommandsTest,ErrorClassifierTest,ProgressParserTest,YtDlpResolverTest}.kt`, `job/DefaultDownloadServiceTest.kt` | 단위 테스트 | 2, 3 |
| `engine/src/test/.../integration/{Ffprobe.kt,RealFfmpegTaggingIntegrationTest.kt,RealYtDlpIntegrationTest.kt}` | 실제 도구 통합 테스트 | 3, 4 |
| `shared/.../FilenameFormatterTest.kt`, `app/.../{AppStateHolderTest,FakeApi,LabelsTest}.kt` 외 | 기대값 | 1, 3 |
| `README.md`, `docs/superpowers/specs/2026-10-04-xgetsongs-design.md` | 문서 | 5 |

---

### Task 1: 진행 단계 이름을 FINISHING으로 바꾸기

동작은 그대로이고 이름과 문구만 바꾼다(`CONVERTING` → `FINISHING`, "mp3 변환 중…" → "마무리 중…").

**Files:**
- Modify: `shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`
- Modify: `app/src/commonMain/kotlin/com/xgetsongs/app/state/{UiState,Labels,AppStateHolder}.kt`
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt`
- Modify (tests): `app/.../AppStateHolderTest.kt`, `app/.../LabelsTest.kt`, `shared/.../ApiModelsTest.kt`, `server/.../JobLogTest.kt`, `engine/.../DefaultDownloadServiceTest.kt`, `engine/.../RealYtDlpIntegrationTest.kt`

**Interfaces:**
- Produces: `Stage.FINISHING`(shared), `ItemStatus.Finishing`(app). 이후 Task 2·3이 이 이름을 쓴다.

- [ ] **Step 1: 테스트의 이름과 기대 문구를 먼저 바꾼다**

```bash
cd /c/Projects/xGetSongs
sed -i 's/Stage\.CONVERTING/Stage.FINISHING/g; s/ItemStatus\.Converting/ItemStatus.Finishing/g' \
  app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt \
  app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt \
  shared/src/commonTest/kotlin/com/xgetsongs/shared/api/ApiModelsTest.kt \
  server/src/test/kotlin/com/xgetsongs/server/JobLogTest.kt \
  engine/src/test/kotlin/com/xgetsongs/engine/job/DefaultDownloadServiceTest.kt \
  engine/src/test/kotlin/com/xgetsongs/engine/integration/RealYtDlpIntegrationTest.kt
sed -i 's/"mp3 변환 중…"/"마무리 중…"/' app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt
grep -n "Finishing\|FINISHING\|마무리" app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt
```

Expected: `LabelsTest.kt`에 `assertEquals("마무리 중…", statusLabel(ItemStatus.Finishing))` 한 줄.

- [ ] **Step 2: 실패를 확인한다**

Run: `./gradlew --no-daemon --console=plain :shared:jvmTest`
Expected: FAIL — `Unresolved reference 'FINISHING'`(컴파일 오류).

- [ ] **Step 3: 본 코드의 이름을 바꾼다**

```bash
cd /c/Projects/xGetSongs
sed -i 's/Stage\.CONVERTING/Stage.FINISHING/g; s/ItemStatus\.Converting/ItemStatus.Finishing/g' \
  engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt \
  app/src/commonMain/kotlin/com/xgetsongs/app/state/AppStateHolder.kt \
  app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt
sed -i 's/"mp3 변환 중…"/"마무리 중…"/' app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt
sed -i 's/data object Converting : ItemStatus/data object Finishing : ItemStatus/' app/src/commonMain/kotlin/com/xgetsongs/app/state/UiState.kt
```

`shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`에서 아래를 바꾼다(Edit).

old:
```kotlin
@Serializable
enum class Stage { DOWNLOADING, CONVERTING }
```
new:
```kotlin
/** What an item is doing: [DOWNLOADING] the audio, then [FINISHING] it (writing the tags, the cover and the lyrics). */
@Serializable
enum class Stage { DOWNLOADING, FINISHING }
```

- [ ] **Step 4: 전체 단위 테스트가 통과하는지 본다**

Run: `./gradlew --no-daemon --console=plain :shared:jvmTest :engine:test :server:test :app:desktopTest`
Expected: `BUILD SUCCESSFUL`. 남은 `Converting`/`CONVERTING`은 `ProgressUpdate.Converting`(Task 2에서 삭제)과 테스트 이름뿐이다:
`grep -rn "Converting\|CONVERTING" --include=*.kt . | grep -v "/build/"`

- [ ] **Step 5: 커밋**

```bash
cd /c/Projects/xGetSongs
git add -u
git commit -q -m "$(cat <<'EOF'
refactor: 진행 단계 이름을 CONVERTING에서 FINISHING으로 바꾼다

mp3 변환이 없어지므로 단계 이름과 화면 문구("마무리 중…")를 태그·커버·가사를
쓰는 구간에 맞게 바꾼다. 동작은 그대로다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
EOF
)"
git status --short
```

Expected: `git status`에는 `?? "docs/xGetSongs …메모.txt"`만 남는다.

---

### Task 2: yt-dlp 후처리 줄 대신 직접 FINISHING 보고

지금의 "변환 중"은 yt-dlp의 `XGSPP` 후처리 시작 줄을 신호로 쓴다. 변환이 없어지면 `MoveFiles` 같은 무관한 후처리에 걸리고, 태그·가사 구간에는 표시가 없다. `ItemDownloader`가 yt-dlp 종료 직후 직접 보낸다.

**Files:**
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/ProgressParser.kt`
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/YtDlpCommands.kt`
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/ProgressParserTest.kt`, `YtDlpCommandsTest.kt`, `engine/src/test/kotlin/com/xgetsongs/engine/job/DefaultDownloadServiceTest.kt`

**Interfaces:**
- Consumes: `Stage.FINISHING`(Task 1).
- Produces: `ProgressParser.parse(line: String): ProgressUpdate.Downloading?`, `YtDlpCommands.PROGRESS_PREFIX`만 남고 `POSTPROCESS_PREFIX`는 없어진다. `ItemDownloader.download`는 yt-dlp가 성공(종료 코드 0)으로 끝나면 `JobEvent.Progress(rank, Stage.FINISHING, null)`을 한 번 보낸다.

- [ ] **Step 1: ProgressParserTest를 새로 쓴다**

`engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/ProgressParserTest.kt` 전체를 아래로 바꾼다.

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
        val update = ProgressParser.parse("XGSP|downloading|250|NA|1000")!!
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

    // yt-dlp's post-processor lines are not read any more: the downloader reports the finishing stage itself.
    @Test
    fun postprocessorLinesAreIgnored() {
        assertNull(ProgressParser.parse("XGSPP|started|ExtractAudio"))
        assertNull(ProgressParser.parse("XGSPP|started|MoveFiles"))
        assertNull(ProgressParser.parse("XGSPP|finished|ThumbnailsConvertor"))
    }

    @Test
    fun otherLinesAreIgnored() {
        assertNull(ProgressParser.parse("[youtube] Extracting URL"))
        assertNull(ProgressParser.parse(""))
    }
}
```

- [ ] **Step 2: YtDlpCommandsTest의 진행 템플릿 기대를 바꾼다**

`YtDlpCommandsTest.kt`의 `downloadExtractsMp3IntoTheOutputDirectory` 안에서 아래 한 줄을 (Edit)

old:
```kotlin
        assertEquals(2, command.count { it == "--progress-template" })
```
new:
```kotlin
        assertEquals(1, command.count { it == "--progress-template" })
        assertFalse(command.any { it.startsWith("postprocess:") }, "yt-dlp's post-processor lines are not read any more")
```

- [ ] **Step 3: DefaultDownloadServiceTest의 진행 테스트를 바꾼다**

(a) `succeeding` 러너에서 이 줄을 지운다: `        onStdout("XGSPP|started|ExtractAudio")`

(b) `reportsDownloadAndConvertingProgress`(Task 1 뒤에도 이름은 그대로)를 아래로 바꾼다.

```kotlin
    @Test
    fun reportsDownloadAndFinishingProgress() = runTest {
        val events = service(succeeding).start(request(item(1))).collect()

        val progress = events.filterIsInstance<JobEvent.Progress>()
        assertEquals(JobEvent.Progress(1, Stage.DOWNLOADING, 50.0), progress.first())
        assertEquals(JobEvent.Progress(1, Stage.FINISHING, null), progress.last())
    }
```

(c) `progressEventsAreThrottledToWholePercents`의 마지막 단정 한 줄 `assertEquals(100, events.filterIsInstance<JobEvent.Progress>().size)`을 아래로 바꾼다.

```kotlin
        val progress = events.filterIsInstance<JobEvent.Progress>()
        assertEquals(100, progress.count { it.stage == Stage.DOWNLOADING })
        assertEquals(1, progress.count { it.stage == Stage.FINISHING })
```

(d) `thumbnailConversionBeforeTheDownloadDoesNotShowAsConverting` 테스트 전체를 아래 두 테스트로 바꾼다.

```kotlin
    @Test
    fun finishingIsReportedOnceAfterTheDownloadWhateverYtDlpPrintsAroundIt() = runTest {
        val runner = downloadRunner { command, onStdout, _ ->
            onStdout("XGSPP|started|ThumbnailsConvertor") // yt-dlp's post-processor lines are not read any more
            onStdout("XGSP|downloading|50|100|NA")
            onStdout("XGSPP|started|MoveFiles")
            writeFakeMp3(command)
            0
        }

        val events = service(runner).start(request(item(1))).collect()

        assertEquals(
            listOf(
                JobEvent.Progress(1, Stage.DOWNLOADING, 50.0),
                JobEvent.Progress(1, Stage.FINISHING, null),
            ),
            events.filterIsInstance<JobEvent.Progress>(),
        )
    }

    @Test
    fun aFailedDownloadReportsNoFinishingStage() = runTest {
        val events = service(failingWith("ERROR: something odd")).start(request(item(1))).collect()

        assertEquals(emptyList(), events.filterIsInstance<JobEvent.Progress>())
    }
```

- [ ] **Step 4: 실패를 확인한다**

Run: `./gradlew --no-daemon --console=plain :engine:test --tests "*ProgressParserTest" --tests "*YtDlpCommandsTest" --tests "*DefaultDownloadServiceTest"`
Expected: FAIL — `postprocessorLinesAreIgnored`(아직 `Converting`을 돌려준다), `downloadExtractsMp3…`(템플릿 개수 2), `reportsDownloadAndFinishingProgress`와 `finishingIsReportedOnce…`(FINISHING 없음).

- [ ] **Step 5: ProgressParser를 새로 쓴다**

`engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/ProgressParser.kt` 전체를 아래로 바꾼다.

```kotlin
package com.xgetsongs.engine.ytdlp

sealed interface ProgressUpdate {
    /** [percent] is null when yt-dlp does not know the total size yet. */
    data class Downloading(val percent: Double?) : ProgressUpdate
}

/** Parses the lines produced by the `--progress-template` in [YtDlpCommands.download]. */
object ProgressParser {
    fun parse(line: String): ProgressUpdate.Downloading? {
        val text = line.trim()
        return if (text.startsWith("${YtDlpCommands.PROGRESS_PREFIX}|")) parseDownload(text) else null
    }

    private fun parseDownload(text: String): ProgressUpdate.Downloading? {
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
}
```

- [ ] **Step 6: YtDlpCommands에서 후처리 템플릿을 지운다**

`YtDlpCommands.kt`에서 (Edit 세 곳)

1. `    const val POSTPROCESS_PREFIX = "XGSPP"` 줄을 지운다(앞의 `PROGRESS_PREFIX` 줄은 남긴다).
2. `        val postprocess = "postprocess:$POSTPROCESS_PREFIX|%(progress.status)s|%(progress.postprocessor)s"` 줄을 지운다.
3. `            "--progress-template", postprocess,` 줄을 지운다.

- [ ] **Step 7: ItemDownloader가 직접 FINISHING을 보낸다**

`ItemDownloader.kt`에서 (Edit 세 곳)

1. 진행 줄 처리 부분

old:
```kotlin
            onStdout = { line ->
                ProgressParser.parse(line)?.let { update ->
                    throttle.accept(update)?.let { emit(JobEvent.Progress(rank, it.first, it.second)) }
                }
            },
```
new:
```kotlin
            onStdout = { line ->
                ProgressParser.parse(line)?.let { update ->
                    if (throttle.accept(update)) emit(JobEvent.Progress(rank, Stage.DOWNLOADING, update.percent))
                }
            },
```

2. yt-dlp 실패 처리 바로 뒤에 보고를 넣는다.

old:
```kotlin
        if (exitCode != 0) {
            return DownloadResult.Failed(ErrorClassifier.classify(synchronized(stderr) { stderr.toList() }))
        }
```
new:
```kotlin
        if (exitCode != 0) {
            return DownloadResult.Failed(ErrorClassifier.classify(synchronized(stderr) { stderr.toList() }))
        }
        // From here on the work is ours (tags, cover, lyrics), so we say so ourselves instead of reading yt-dlp's output.
        emit(JobEvent.Progress(rank, Stage.FINISHING, null))
```

3. 파일 끝의 `private class ProgressThrottle { … }` 블록 전체(바로 위 KDoc `/** Lets a progress update through only …` 줄부터 닫는 `}`까지)를 아래로 바꾼다.

```kotlin
    /** Lets a download update through only when it is the first one or the whole percent advances. */
    private class ProgressThrottle {
        private var reported = false
        private var lastPercent = -1

        fun accept(update: ProgressUpdate.Downloading): Boolean {
            val whole = update.percent?.toInt() ?: -1
            if (reported && whole == lastPercent) return false
            reported = true
            lastPercent = whole
            return true
        }
    }
```

- [ ] **Step 8: 통과를 확인한다**

Run: `./gradlew --no-daemon --console=plain :engine:test`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 9: 커밋**

```bash
cd /c/Projects/xGetSongs
git add -u
git commit -q -m "$(cat <<'EOF'
refactor(engine): 마무리 단계를 yt-dlp 후처리 줄 대신 직접 알린다

mp3 변환이 없어지면 yt-dlp의 후처리 시작 줄은 MoveFiles 같은 무관한 후처리에도
걸리고, 태그 쓰기와 가사 검색 구간에는 표시가 없다. ItemDownloader가 yt-dlp가
성공으로 끝난 직후 FINISHING 단계를 보내고, 후처리 진행 템플릿과 파싱은 지운다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: 오디오 파이프라인을 m4a로 바꾸기

한 번에 바뀌어야 하는 묶음이다: 다운로드 형식, 확장자, 태그 쓰기, 오류 분류, 그리고 그 테스트와 통합 테스트 소스(같은 소스 세트라 컴파일이 같이 되어야 한다). 순서는 테스트와 테스트 도구를 먼저 고쳐 빨간불을 확인하고(컴파일 오류), 본 코드를 고쳐 초록불로 만든다. 커밋은 맨 끝에 한 번 한다.

**Files:** 파일 구조 표의 Task 3 행 전부.

**Interfaces:**
- Consumes: `Stage.FINISHING`, `ProgressParser`(Task 2).
- Produces:
  - `FilenameFormatter.format(...)`가 `….m4a`를 돌려준다.
  - `Ffmetadata.render(tags: TrackTags): String`이 `comment`와 `lyrics`도 쓴다(비어 있으면 생략).
  - `FfmpegCommands.tag(ffmpeg: Path, input: Path, cover: Path?, metadataFile: Path, output: Path): List<String>`의 `output`은 `.m4a`.
  - `class M4aTagger(runner: ProcessRunner, tools: ToolPathProvider) { suspend fun tag(file: Path, cover: Path?, tags: TrackTags): Failure? }`
  - 테스트 도구 `FfmetadataReader.read(text: String): Map<String, String>`, `writeFakeM4a(command)`, `writeFakeTagged(command)`(파일 내용이 정확히 `FAKE_AUDIO`).

#### Part A — 파일 이름 확장자와 그 기대값

- [ ] **Step 1: 확장자를 `.m4a`로 바꾸고 기대값을 따라 바꾼다**

```bash
cd /c/Projects/xGetSongs
sed -i 's/\.mp3/.m4a/g' \
  shared/src/commonMain/kotlin/com/xgetsongs/shared/filename/FilenameFormatter.kt \
  shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt \
  app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt \
  app/src/commonTest/kotlin/com/xgetsongs/app/state/FakeApi.kt \
  engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/YtDlpResolverTest.kt
grep -n 'EXTENSION' shared/src/commonMain/kotlin/com/xgetsongs/shared/filename/FilenameFormatter.kt | head -3
grep -rn "mp3" shared/src/commonMain/kotlin/com/xgetsongs/shared/filename/ app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt app/src/commonTest/kotlin/com/xgetsongs/app/state/FakeApi.kt engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/YtDlpResolverTest.kt || echo "no mp3 left"
```

Expected: `private const val EXTENSION = ".m4a"`, 그리고 `no mp3 left`.

#### Part B — 테스트 도구

- [ ] **Step 2: 가짜 m4a와 가짜 ffmpeg 출력을 바꾼다**

`engine/src/test/kotlin/com/xgetsongs/engine/testutil/Fakes.kt`에서 (Edit 여러 곳)

1. `writeFakeMp3` 함수

old:
```kotlin
/** Pretends yt-dlp finished: creates `<dir>/<id>.mp3` the way the real tool would. */
fun writeFakeMp3(command: List<String>) {
    Files.writeString(outputDirOf(command).resolve("${videoIdOf(command)}.mp3"), "mp3-data")
}
```
new:
```kotlin
/** Pretends yt-dlp finished: creates `<dir>/<id>.m4a` the way the real tool would. */
fun writeFakeM4a(command: List<String>) {
    Files.writeString(outputDirOf(command).resolve("${videoIdOf(command)}.m4a"), "m4a-data")
}
```

2. `FAKE_AUDIO`부터 `endsWithFakeAudio`까지(상수, `fakeTaggedMp3`, `endsWithFakeAudio`) 전체를 아래로 바꾼다.

```kotlin
/** The bytes of what the fake ffmpeg writes. Nothing after ffmpeg may change them. */
const val FAKE_AUDIO = "tagged-m4a-data"

/** True when the file still holds the output of the fake ffmpeg. */
fun endsWithFakeAudio(file: Path): Boolean = String(Files.readAllBytes(file), Charsets.ISO_8859_1).endsWith(FAKE_AUDIO)
```

3. `isFfmpegCommand`의 KDoc `/** True for the ID3 tagging pass …` → `/** True for the tagging pass (the command starts with the ffmpeg path); false for yt-dlp commands. */`

4. `writeFakeTagged`

old:
```kotlin
    Files.write(Path.of(command.last()), fakeTaggedMp3())
```
new:
```kotlin
    Files.write(Path.of(command.last()), FAKE_AUDIO.toByteArray(Charsets.ISO_8859_1))
```

- [ ] **Step 3: ffmetadata를 ffmpeg처럼 읽는 테스트 도구를 만든다**

Create `engine/src/test/kotlin/com/xgetsongs/engine/testutil/FfmetadataReader.kt`:

```kotlin
package com.xgetsongs.engine.testutil

/**
 * Reads an ffmetadata file the way ffmpeg does, so a test can see the values that reach the tags: the first line is the
 * header, a backslash makes the next character part of the text (a line break too), an unescaped line feed ends the
 * entry and the first unescaped `=` splits the key from the value.
 */
object FfmetadataReader {
    fun read(text: String): Map<String, String> {
        val values = linkedMapOf<String, String>()
        var i = text.indexOf('\n') + 1
        while (i < text.length) {
            val key = StringBuilder()
            val value = StringBuilder()
            var inValue = false
            while (i < text.length) {
                val c = text[i++]
                if (c == '\\' && i < text.length) {
                    (if (inValue) value else key).append(text[i++])
                } else if (c == '\n') {
                    break
                } else if (c == '=' && !inValue) {
                    inValue = true
                } else {
                    (if (inValue) value else key).append(c)
                }
            }
            if (inValue) values[key.toString()] = value.toString()
        }
        return values
    }
}
```

Create `engine/src/test/kotlin/com/xgetsongs/engine/testutil/FfmetadataReaderTest.kt`:

```kotlin
package com.xgetsongs.engine.testutil

import com.xgetsongs.engine.tags.Ffmetadata
import com.xgetsongs.engine.tags.TrackTags
import kotlin.test.Test
import kotlin.test.assertEquals

class FfmetadataReaderTest {
    @Test
    fun readsBackWhatFfmetadataRenderWrote() {
        val tags = TrackTags(
            title = "a=b;c#d\\e",
            artist = "방탄소년단",
            album = "100% [x] {y} #1; k=v",
            albumArtist = "Various Artists",
            trackNumber = 7,
            comment = "line one\nline two\r\nline three",
            lyrics = "첫 줄\n\nLa la=la 🎵\n마지막",
        )

        assertEquals(
            mapOf(
                "title" to tags.title,
                "artist" to tags.artist,
                "album_artist" to tags.albumArtist,
                "album" to tags.album,
                "track" to "7",
                "comment" to tags.comment,
                "lyrics" to tags.lyrics,
            ),
            FfmetadataReader.read(Ffmetadata.render(tags)),
        )
    }
}
```

- [ ] **Step 4: 쓰지 않게 되는 ID3 테스트 도구를 지운다**

```bash
cd /c/Projects/xGetSongs
git rm -q engine/src/test/kotlin/com/xgetsongs/engine/testutil/Id3v2Tag.kt engine/src/test/kotlin/com/xgetsongs/engine/tags/Id3FramesTest.kt
```

#### Part C — Ffmetadata

- [ ] **Step 5: FfmetadataTest를 고친다**

`engine/src/test/kotlin/com/xgetsongs/engine/tags/FfmetadataTest.kt`에서 (Edit 세 곳)

1. 도우미의 기본 코멘트를 `null`로 바꾼다(그러면 기존 테스트의 기대 줄 수가 그대로다).

old: `        comment: String? = "note",`
new: `        comment: String? = null,`

2. `theLyricsAreNotRendered` 테스트 전체를 아래 두 테스트로 바꾼다.

```kotlin
    @Test
    fun theLyricsAreRenderedAfterTheCommentWithLineBreaksEscaped() {
        val withLyrics = tags(comment = "note").copy(lyrics = "Line one\nLine two=three\n첫 번째 줄")

        assertEquals(
            ";FFMETADATA1\ntitle=Dynamite\nartist=BTS\nalbum_artist=BTS\nalbum=Best of BTS\ntrack=7\ncomment=note\n" +
                "lyrics=Line one\\\nLine two\\=three\\\n첫 번째 줄\n",
            Ffmetadata.render(withLyrics),
        )
    }

    @Test
    fun blankLyricsAreNotRendered() {
        for (lyrics in listOf(null, "", " \n\t")) {
            assertFalse(Ffmetadata.render(tags().copy(lyrics = lyrics)).contains("lyrics"), "lyrics = [$lyrics]")
        }
    }
```

3. `neverWritesTheCommentBecauseFfmpegWouldStoreItAsTxxx` 테스트 전체를 아래로 바꾼다.

```kotlin
    @Test
    fun theCommentIsRenderedUnlessItIsNullOrBlank() {
        for (comment in listOf(null, "", "\t ")) {
            assertFalse(lines(Ffmetadata.render(tags(comment = comment))).any { it.startsWith("comment") }, "comment = [$comment]")
        }

        val text = Ffmetadata.render(tags(comment = "https://www.youtube.com/watch?v=abc"))

        assertTrue("comment=https://www.youtube.com/watch?v\\=abc" in lines(text), text)
    }
```

#### Part D — FfmpegCommands

- [ ] **Step 6: FfmpegCommandsTest를 새로 쓴다**

`engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/FfmpegCommandsTest.kt` 전체를 아래로 바꾼다.

```kotlin
package com.xgetsongs.engine.ytdlp

import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class FfmpegCommandsTest {
    private val ffmpeg = Path.of("C:/tools/ffmpeg.exe")
    private val input = Path.of("C:/work/1/vid.m4a")
    private val cover = Path.of("C:/work/1/vid.jpg")
    private val metadata = Path.of("C:/work/1/vid.ffmeta")
    private val output = Path.of("C:/work/1/vid.tagged.m4a")

    @Test
    fun withoutACoverCopiesTheAudioAndTagsItFromTheMetadataFile() {
        val command = FfmpegCommands.tag(ffmpeg, input, null, metadata, output)

        assertEquals(
            listOf(
                ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-i", input.toString(), "-f", "ffmetadata", "-i", metadata.toString(),
                "-map", "0:a", "-map_chapters", "-1", "-map_metadata", "1",
                "-c:a", "copy",
                output.toString(),
            ),
            command,
        )
    }

    @Test
    fun withACoverAddsItAsAnAttachedSquareMjpegPicture() {
        val command = FfmpegCommands.tag(ffmpeg, input, cover, metadata, output)

        assertEquals(
            listOf(
                ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-i", input.toString(), "-i", cover.toString(), "-f", "ffmetadata", "-i", metadata.toString(),
                "-map", "0:a", "-map", "1:v", "-map_chapters", "-1", "-map_metadata", "2",
                "-c:a", "copy",
                "-c:v", "mjpeg", "-q:v", "2", "-vf", "crop=min(iw\\,ih):min(iw\\,ih)", "-disposition:v", "attached_pic",
                "-metadata:s:v", "title=Album cover", "-metadata:s:v", "comment=Cover (front)",
                output.toString(),
            ),
            command,
        )
    }

    @Test
    fun theCropFilterIsOneArgumentWithAnEscapedComma() {
        val command = FfmpegCommands.tag(ffmpeg, input, cover, metadata, output)

        assertEquals("""crop=min(iw\,ih):min(iw\,ih)""", command[command.indexOf("-vf") + 1])
    }

    @Test
    fun theOutputMustBeAnM4aSoFfmpegPicksTheMp4Muxer() {
        for (wrong in listOf("vid.tmp", "vid.mp3")) {
            assertFailsWith<IllegalArgumentException>(wrong) {
                FfmpegCommands.tag(ffmpeg, input, null, metadata, Path.of("C:/work/1/$wrong"))
            }
        }
    }
}
```

#### Part E — M4aTagger

- [ ] **Step 7: Id3TaggerTest를 M4aTaggerTest로 바꾸고 새로 쓴다**

```bash
cd /c/Projects/xGetSongs
git mv engine/src/test/kotlin/com/xgetsongs/engine/tags/Id3TaggerTest.kt engine/src/test/kotlin/com/xgetsongs/engine/tags/M4aTaggerTest.kt
```

`M4aTaggerTest.kt` 전체를 아래로 바꾼다.

```kotlin
package com.xgetsongs.engine.tags

import com.xgetsongs.engine.testutil.FakeProcessRunner
import com.xgetsongs.engine.testutil.FfmetadataReader
import com.xgetsongs.engine.testutil.TEST_TOOLS
import com.xgetsongs.engine.testutil.endsWithFakeAudio
import com.xgetsongs.engine.testutil.ffmetadataTextOf
import com.xgetsongs.engine.testutil.toolsOf
import com.xgetsongs.engine.testutil.writeFakeTagged
import com.xgetsongs.engine.tools.ToolPaths
import com.xgetsongs.engine.ytdlp.Failure
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.engine.ytdlp.FfmpegCommands
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class M4aTaggerTest {
    private val dir: Path = Files.createTempDirectory("xgs-m4a")
    private val file = dir.resolve("vid00000001.m4a")
    private val cover = dir.resolve("vid00000001.jpg")
    private val tags = TrackTags(
        title = "\"Golden\" 'x'",
        artist = "아티스트",
        album = "My List",
        albumArtist = "아티스트",
        trackNumber = 3,
        comment = "https://www.youtube.com/watch?v=vid00000001",
    )
    private val lyrics = "첫 번째 줄\nLa la la\n세 번째 줄"

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    private fun tagger(runner: FakeProcessRunner, tools: ToolPaths = TEST_TOOLS) =
        M4aTagger(runner, toolsOf(tools))

    private fun filesInDir(): List<String> = Files.list(dir).use { stream -> stream.map { it.fileName.toString() }.sorted().toList() }

    @BeforeTest
    fun writeOriginal() {
        Files.writeString(file, "original-m4a")
    }

    @Test
    fun replacesTheOriginalWithTheTaggedFileAndRemovesTheTemporaryFiles() = runTest {
        var metadataText: String? = null
        val runner = FakeProcessRunner { command, _, _ ->
            metadataText = ffmetadataTextOf(command)
            writeFakeTagged(command)
            0
        }

        val result = tagger(runner).tag(file, null, tags)

        assertNull(result)
        assertTrue(endsWithFakeAudio(file), "the output of ffmpeg must reach the final file")
        assertEquals(listOf("vid00000001.m4a"), filesInDir())
        assertEquals(Ffmetadata.render(tags), metadataText)
    }

    @Test
    fun theCommentAndTheLyricsReachFfmpegThroughTheMetadataFile() = runTest {
        val withLyrics = tags.copy(lyrics = lyrics)
        var metadataText: String? = null
        val runner = FakeProcessRunner { command, _, _ ->
            metadataText = ffmetadataTextOf(command)
            writeFakeTagged(command)
            0
        }

        val result = tagger(runner).tag(file, null, withLyrics)

        assertNull(result)
        val values = FfmetadataReader.read(metadataText!!)
        assertEquals(tags.comment, values["comment"])
        assertEquals(lyrics, values["lyrics"])
    }

    @Test
    fun aMissingOrBlankCommentAndLyricsWriteNoEntry() = runTest {
        for (blank in listOf(null, "", " \n\t")) {
            var metadataText: String? = null
            val runner = FakeProcessRunner { command, _, _ ->
                metadataText = ffmetadataTextOf(command)
                writeFakeTagged(command)
                0
            }

            val result = tagger(runner).tag(file, null, tags.copy(comment = blank, lyrics = blank))

            assertNull(result)
            val values = FfmetadataReader.read(metadataText!!)
            assertFalse("comment" in values || "lyrics" in values, "comment and lyrics = [$blank]: $values")
        }
    }

    @Test
    fun runsTheFfmpegCommandWithTemporaryFilesNextToTheOriginal() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        tagger(runner).tag(file, null, tags)

        val expected = FfmpegCommands.tag(
            TEST_TOOLS.ffmpeg!!,
            file,
            null,
            dir.resolve("vid00000001.ffmeta"),
            dir.resolve("vid00000001.tagged.m4a"),
        )
        assertEquals(listOf(expected), runner.commands.toList())
    }

    @Test
    fun passesTheCoverAsASecondInput() = runTest {
        Files.writeString(cover, "jpg-data")
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        tagger(runner).tag(file, cover, tags)

        val command = runner.commands.single()
        assertEquals(
            listOf(file.toString(), cover.toString()),
            command.indices.filter { command[it] == "-i" }.take(2).map { command[it + 1] },
        )
        assertEquals(
            listOf("0:a", "1:v"),
            command.indices.filter { command[it] == "-map" }.map { command[it + 1] },
        )
        assertEquals("2", command[command.indexOf("-map_metadata") + 1])
    }

    @Test
    fun noTagTextAppearsOnTheCommandLine() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        tagger(runner).tag(file, null, tags.copy(lyrics = lyrics))

        val commandLine = runner.commands.single().joinToString(" ")
        assertTrue(
            listOf("Golden", "아티스트", "My List", "youtube", "첫 번째", "La la la").none { it in commandLine },
            commandLine,
        )
    }

    @Test
    fun aNonZeroExitCodeFailsWithTheLastStderrLineAndKeepsTheOriginal() = runTest {
        val runner = FakeProcessRunner { command, _, onStderr ->
            writeFakeTagged(command) // a half-written output must not survive
            onStderr("first problem")
            onStderr("Invalid argument")
            onStderr("   ")
            1
        }

        val result = tagger(runner).tag(file, null, tags)

        assertEquals(Failure(FailureKind.OTHER, "태그를 쓰지 못했습니다: Invalid argument"), result)
        assertEquals("original-m4a", Files.readString(file))
        assertEquals(listOf("vid00000001.m4a"), filesInDir())
    }

    @Test
    fun exitCodeZeroWithoutAnOutputFileIsAFailure() = runTest {
        val runner = FakeProcessRunner { _, _, _ -> 0 }

        val result = tagger(runner).tag(file, null, tags)

        assertEquals(Failure(FailureKind.OTHER, "태그를 쓰지 못했습니다: 알 수 없는 오류"), result)
        assertEquals("original-m4a", Files.readString(file))
        assertEquals(listOf("vid00000001.m4a"), filesInDir())
    }

    @Test
    fun theStderrSummaryIsCutToTwoHundredCharacters() = runTest {
        val runner = FakeProcessRunner { _, _, onStderr ->
            onStderr("x".repeat(500))
            1
        }

        val result = tagger(runner).tag(file, null, tags)

        assertEquals(Failure(FailureKind.OTHER, "태그를 쓰지 못했습니다: " + "x".repeat(200)), result)
    }

    @Test
    fun aMissingFfmpegIsFatalAndRunsNothing() = runTest {
        val runner = FakeProcessRunner { _, _, _ -> 0 }

        val result = tagger(runner, TEST_TOOLS.copy(ffmpeg = null)).tag(file, null, tags)

        assertEquals(Failure(FailureKind.FATAL, "ffmpeg를 찾을 수 없습니다."), result)
        assertTrue(runner.commands.isEmpty())
        assertEquals(listOf("vid00000001.m4a"), filesInDir())
    }

    @Test
    fun anIoErrorBecomesAFailureInsteadOfAnException() = runTest {
        val runner = FakeProcessRunner { _, _, _ -> 0 }
        val missingDir = dir.resolve("no-such-dir").resolve("a.m4a")

        val result = tagger(runner).tag(missingDir, null, tags)

        assertEquals(FailureKind.OTHER, result?.kind)
        assertTrue(result!!.message.startsWith("태그를 쓰지 못했습니다: "), result.message)
        assertTrue(runner.commands.isEmpty())
    }

    @Test
    fun cancellingWhileFfmpegRunsPropagatesAndStillRemovesTheTemporaryFiles() = runTest {
        val started = CompletableDeferred<Unit>()
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            started.complete(Unit)
            awaitCancellation()
        }
        val job = launch { tagger(runner).tag(file, null, tags) }
        started.await()

        job.cancelAndJoin()

        assertTrue(job.isCancelled)
        assertEquals(listOf("vid00000001.m4a"), filesInDir())
        assertEquals("original-m4a", Files.readString(file))
    }
}
```

#### Part F — YtDlpCommands 테스트

- [ ] **Step 8: YtDlpCommandsTest를 m4a 기준으로 바꾼다**

`YtDlpCommandsTest.kt`에서 `downloadExtractsMp3IntoTheOutputDirectory`(Task 2 뒤의 본문이 무엇이든) 테스트 전체를 아래로 바꾸고, 이름의 `Mp3`를 지운다.

```kotlin
    @Test
    fun downloadSavesTheAacStreamAsItIsIntoTheOutputDirectory() {
        val command = YtDlpCommands.download(TEST_TOOLS, url, Path.of("C:/work/job-1"), "dQw4w9WgXcQ")
        assertEquals("bestaudio[ext=m4a]", command[command.indexOf("-f") + 1])
        assertEquals("never", command[command.indexOf("--fixup") + 1])
        for (reencoding in listOf("-x", "--extract-audio", "--audio-format", "--audio-quality")) {
            assertFalse(reencoding in command, "the audio must not be re-encoded: $reencoding")
        }
        assertTrue(command[command.indexOf("-o") + 1].endsWith("dQw4w9WgXcQ.%(ext)s"))
        assertEquals(TEST_TOOLS.ffmpeg.toString(), command[command.indexOf("--ffmpeg-location") + 1])
        assertEquals(1, command.count { it == "--progress-template" })
        assertEquals(listOf("--", url), command.takeLast(2))
    }
```

```bash
cd /c/Projects/xGetSongs
sed -i 's/downloadSavesTheThumbnailAsJpgNextToTheMp3/downloadSavesTheThumbnailAsJpgNextToTheAudio/' engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/YtDlpCommandsTest.kt
```

#### Part G — ErrorClassifier 테스트

- [ ] **Step 9: 형식이 없을 때의 분류 테스트를 더한다**

`ErrorClassifierTest.kt`의 `fatalWinsOverTransient` 테스트 바로 앞에 아래 두 테스트를 넣는다.

```kotlin
    @Test
    fun aVideoWithoutAnM4aStreamFailsWithAClearReason() {
        val failure = classify("ERROR: [youtube] abc: Requested format is not available. Use --list-formats for a list of available formats")

        assertEquals(FailureKind.OTHER, failure.kind)
        assertEquals("m4a 오디오 형식이 없습니다.", failure.message)
    }

    @Test
    fun otherClassificationsComeBeforeTheMissingFormat() {
        assertEquals(FailureKind.UNAVAILABLE, classify("ERROR: [youtube] abc: Video unavailable. Requested format is not available").kind)
        assertEquals(
            FailureKind.TRANSIENT,
            classify("ERROR: unable to download video data: HTTP Error 503", "Requested format is not available").kind,
        )
    }
```

#### Part H — DefaultDownloadServiceTest

- [ ] **Step 10: 파일 전체에 적용하는 치환**

```bash
cd /c/Projects/xGetSongs
F=engine/src/test/kotlin/com/xgetsongs/engine/job/DefaultDownloadServiceTest.kt
sed -i 's/writeFakeMp3/writeFakeM4a/g; s/\bmp3\b/m4a/g; s/ID3 태그를/태그를/g; s/ID3 tagging/tagging/; s/NoLyricsFrame/NoLyrics/g' "$F"
```

- [ ] **Step 11: ID3를 읽던 도우미와 테스트를 ffmetadata를 읽도록 고친다 (직접 수정)**

`DefaultDownloadServiceTest.kt`에서 아래를 차례로 바꾼다(함수 이름으로 찾는다).

1. import: `import com.xgetsongs.engine.testutil.Id3v2Tag` 줄을 지우고, `import com.xgetsongs.engine.testutil.FakeProcessRunner` 다음 줄에 `import com.xgetsongs.engine.testutil.FfmetadataReader`를 넣는다. `import kotlin.test.assertFalse` 다음에 `import kotlin.test.assertNotNull`과 `import kotlin.test.assertNull`을 넣는다.

2. `theTaggedFileWithItsCommentFrameIsWhatReachesTheSink`를 아래로 바꾼다.

```kotlin
    @Test
    fun theTaggedFileIsWhatReachesTheSink() = runTest {
        service(succeeding).start(request(item(1))).collect()

        assertTrue(endsWithFakeAudio(outDir.resolve("001 A1 - T1.m4a")))
    }
```

3. `theMetadataFileCarriesTheParsedArtistTitleTrackAndAlbum`의 기대 목록 끝에 코멘트 줄을 더한다.

old:
```kotlin
                "track=5",
            ),
            texts.single().removeSuffix("\n").split("\n"),
```
new:
```kotlin
                "track=5",
                "comment=https://www.youtube.com/watch?v\\=vid00000005",
            ),
            texts.single().removeSuffix("\n").split("\n"),
```

4. `theUrlReachesTheFileAsACommFrameAndNotThroughTheMetadataFile`을 아래로 바꾼다.

```kotlin
    @Test
    fun theUrlIsTheCommentInTheMetadataFileAndNeverOnTheCommandLine() = runTest {
        val texts = mutableListOf<String>()
        val runner = taggingRunner(texts)

        service(runner).start(request(item(5))).collect()

        assertEquals("https://www.youtube.com/watch?v=vid00000005", FfmetadataReader.read(texts.single())["comment"])
        assertTrue(runner.ffmpegCommands.single().none { "youtube" in it }, "tag text must not be on the command line")
    }
```

5. `deliveredTag`를 아래로 바꾼다.

```kotlin
    /** Downloads item 1 with the info file [infoJson] and returns the tag values that were handed to ffmpeg. */
    private suspend fun TestScope.deliveredTag(
        infoJson: String?,
        texts: MutableList<String> = mutableListOf(),
        album: String? = "My List",
    ): Map<String, String> {
        val events = service(infoRunner(texts, infoJson)).start(request(item(1), album = album)).collect()

        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)), done(events), events.toString())
        assertTrue(events.none { it is JobEvent.ItemFailed }, events.toString())
        return FfmetadataReader.read(texts.single())
    }
```

6. `lyricsFromTheInfoDescriptionReachTheFileAsAUsltFrameAndNotTheMetadataFile`을 아래로 바꾼다.

```kotlin
    @Test
    fun lyricsFromTheInfoDescriptionAreInTheMetadataFile() = runTest {
        val tag = deliveredTag(infoWithDescription(describedLyrics))

        assertEquals("첫 번째 줄\n두 번째 줄\n\nLa la la", tag["lyrics"])
        assertEquals("https://www.youtube.com/watch?v=vid00000001", tag["comment"], "the comment is unchanged")
    }
```

7. `aSingleVideosAlbumAndLyricsComeFromTheSameInfoFile`의 가사 단정 한 줄을 바꾼다.

old: `        assertEquals(listOf(Id3v2Tag.Lyrics(3, "eng", "", "Line one\r\nLine two\r\nLine three")), tag.lyrics())`
new: `        assertEquals("Line one\nLine two\nLine three", tag["lyrics"])`

8. `onlineLyricsInTheFile` 값(`private val onlineLyricsInTheFile = …`)과 바로 위 KDoc의 "so the language of the frame is `kor`" 부분을 정리한다: 값 줄을 지우고 KDoc을 `/** A made-up answer of the lookup: Korean and English lines. */`로 바꾼다.

9. `deliveredWith`의 반환 형식과 마지막 줄을 바꾼다.

old:
```kotlin
    ): Id3v2Tag {
        val events = service(infoRunner(texts, infoJson), lyrics = provider, metadata = metadata)
```
new:
```kotlin
    ): Map<String, String> {
        val events = service(infoRunner(texts, infoJson), lyrics = provider, metadata = metadata)
```
old: `        return Id3v2Tag.read(outDir.resolve(events.filterIsInstance<JobEvent.ItemStarted>().single().fileName))`
new: `        return FfmetadataReader.read(texts.single())`
그리고 이 함수의 KDoc 마지막 줄 `Returns the tag of the file that reached the sink.`를 `Returns the tag values that were handed to ffmpeg.`로 바꾼다.

10. `withoutLyricsInTheDescriptionAndTheOptionOnTheProviderIsAskedOnceAndItsLyricsReachTheFile`을 아래로 바꾼다.

```kotlin
    @Test
    fun withoutLyricsInTheDescriptionAndTheOptionOnTheProviderIsAskedOnceAndItsLyricsReachTheFile() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        val tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics))

        assertEquals(1, provider.queries.size)
        assertEquals(onlineLyrics, tag["lyrics"])
    }
```

11. `lyricsInTheDescriptionAlwaysWinAndTheProviderIsNotCalled`의 가사 단정을 바꾼다.

old: `        assertEquals(listOf(Id3v2Tag.Lyrics(3, "kor", "", "첫 번째 줄\r\n두 번째 줄\r\n\r\nLa la la")), tag.lyrics())`
new: `        assertEquals("첫 번째 줄\n두 번째 줄\n\nLa la la", tag["lyrics"])`

12. `withoutAProviderTheOptionChangesNothing`을 아래로 바꾼다.

```kotlin
    @Test
    fun withoutAProviderTheOptionChangesNothing() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, infoWithDescription(descriptionWithoutLyrics)))
            .start(request(item(1), searchLyricsOnline = true))
            .collect()

        assertNull(FfmetadataReader.read(texts.single())["lyrics"], "the engine's default provider finds nothing")
    }
```

13. `aFailingLookupDoesNotAffectTheOtherItems`를 아래로 바꾼다.

```kotlin
    @Test
    fun aFailingLookupDoesNotAffectTheOtherItems() = runTest {
        val provider = FakeLyricsProvider { query -> if (query.title == "T1") throw RuntimeException("boom") else onlineLyrics }
        val texts = mutableListOf<String>()

        val events = service(infoRunner(texts, infoWithDescription(descriptionWithoutLyrics)), lyrics = provider)
            .start(request(item(1), item(2), searchLyricsOnline = true))
            .collect()

        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(2, 0, 0)), done(events))
        assertEquals(
            listOf(null, onlineLyrics),
            texts.map { FfmetadataReader.read(it)["lyrics"] },
            "one metadata file per item, in order (the request runs one item at a time)",
        )
    }
```

14. `aFailureWhileAddingTheLyricsFrameFailsTheItemAndReportsNoOutcome` 테스트 전체를 지운다(ID3 프레임을 더하는 단계가 없어졌다).

- [ ] **Step 12: 남은 `.ids` 단정을 치환한다**

Step 11이 끝난 뒤에 돌린다(남은 형태만 맞는다).

```bash
cd /c/Projects/xGetSongs
F=engine/src/test/kotlin/com/xgetsongs/engine/job/DefaultDownloadServiceTest.kt
sed -i \
  -e 's/assertEquals(listOf("TIT2", "COMM", "USLT"), tag\.ids\(.*\))$/assertNotNull(tag["lyrics"]\1)/' \
  -e 's/assertEquals(listOf("TIT2", "COMM"), tag\.ids\(.*\))$/assertNull(tag["lyrics"]\1)/' \
  -e 's/assertEquals(listOf("TIT2", "COMM"), \(deliveredTag(.*)\)\.ids)$/assertNull(\1["lyrics"])/' \
  "$F"
grep -n "Id3\|\.ids\|TIT2\|USLT\|COMM\|mp3\|ID3\|\\\\r\\\\n" "$F" || echo "no ID3 leftovers"
```

Expected: `no ID3 leftovers`. 남아 있으면 같은 방식으로 직접 고친다(`Id3v2Tag.read(…)`는 `FfmetadataReader.read(texts.single())` 꼴로).

#### Part I — 통합 테스트 소스 (같은 소스 세트라 컴파일이 되어야 한다)

- [ ] **Step 13: Mp3Probe.kt의 이름을 Ffprobe.kt로 바꾼다**

```bash
cd /c/Projects/xGetSongs
git mv engine/src/test/kotlin/com/xgetsongs/engine/integration/Mp3Probe.kt engine/src/test/kotlin/com/xgetsongs/engine/integration/Ffprobe.kt
```

(파일 안의 클래스 `Ffprobe`, `Probed`, `ProbedStream`은 그대로다. 파일 이름이 클래스와 같아질 뿐이다.)

- [ ] **Step 14: RealFfmpegTaggingIntegrationTest를 새로 쓴다**

`engine/src/test/kotlin/com/xgetsongs/engine/integration/RealFfmpegTaggingIntegrationTest.kt` 전체를 아래로 바꾼다.

```kotlin
package com.xgetsongs.engine.integration

import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tags.M4aTagger
import com.xgetsongs.engine.tags.TrackTags
import com.xgetsongs.engine.tools.ToolLocator
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * Runs the real [M4aTagger] with the real ffmpeg and reads the result back with the real ffprobe. It needs no
 * network, but it is excluded from `test` like the other real-tool tests and skipped when a tool is missing.
 */
@Tag("integration")
class RealFfmpegTaggingIntegrationTest {
    private val appData = System.getenv("APPDATA")?.let { Path.of(it, "xGetSongs") }
        ?: Path.of(System.getProperty("user.home"), ".xgetsongs")
    private val locator = ToolLocator(appBinDir = appData.resolve("bin"))
    private val runner = SystemProcessRunner()
    private val root: Path = Files.createTempDirectory("xgs-tagging")
    private lateinit var ffmpeg: Path
    private lateinit var ffprobe: Ffprobe

    @BeforeTest
    fun requireTools() {
        val found = locator.current().ffmpeg
        val probe = Ffprobe.besides(found)
        assumeTrue(found != null && probe != null, "ffmpeg and ffprobe must be installed")
        ffmpeg = found!!
        ffprobe = Ffprobe(probe!!, runner)
    }

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private suspend fun generate(vararg args: String) {
        val stderr = mutableListOf<String>()
        val exitCode = runner.run(
            listOf(ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y") + args,
            onStderr = { synchronized(stderr) { stderr += it } },
        )
        assertEquals(0, exitCode, stderr.toString())
    }

    /** A one-second silent AAC file in a regular MP4 container. */
    private suspend fun silentM4a(): Path = root.resolve("track.m4a").also {
        generate("-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "1", "-c:a", "aac", it.toString())
    }

    /** Like [silentM4a], but fragmented with the `dash` brand, which is how yt-dlp leaves YouTube's itag 140 audio. */
    private suspend fun fragmentedM4a(): Path = root.resolve("track.m4a").also {
        generate(
            "-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "1", "-c:a", "aac",
            "-movflags", "frag_keyframe+empty_moov+default_base_moof", "-brand", "dash", "-f", "mp4", it.toString(),
        )
    }

    /** A 640x360 red picture, like a YouTube thumbnail (16:9). */
    private suspend fun redJpg(): Path = root.resolve("track.jpg").also {
        generate("-f", "lavfi", "-i", "color=c=red:s=640x360", "-frames:v", "1", it.toString())
    }

    /** The MD5 of the audio stream as it is stored: a stream copy must not change it. */
    private suspend fun audioMd5(file: Path): String {
        val stdout = mutableListOf<String>()
        val exitCode = runner.run(
            listOf(ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-i", file.toString(), "-map", "0:a", "-c", "copy", "-f", "md5", "-"),
            onStdout = { synchronized(stdout) { stdout += it } },
        )
        assertEquals(0, exitCode, stdout.toString())
        return stdout.single { it.startsWith("MD5=") }
    }

    private val tags = TrackTags(
        title = "\"Golden\" '골든' (feat. 지민)",
        artist = "방탄소년단",
        album = "My \"Best\" List",
        albumArtist = "방탄소년단",
        trackNumber = 7,
        comment = "https://www.youtube.com/watch?v=jNQXAC9IVRw",
        lyrics = "첫 번째 줄\nLa la la 🎵\n\nSecond line\n마지막 줄",
    )

    private fun assertTags(expected: TrackTags, actual: Map<String, String>) {
        assertEquals(expected.title, actual["title"])
        assertEquals(expected.artist, actual["artist"])
        assertEquals(expected.albumArtist, actual["album_artist"])
        assertEquals(expected.album, actual["album"])
        assertEquals(expected.trackNumber.toString(), actual["track"])
        assertEquals(expected.comment, actual["comment"])
        assertEquals(expected.lyrics, actual["lyrics"])
    }

    // JUnit does not discover test methods with a non-void return type, and the last expression of the runBlocking
    // block is not Unit in general, so the return type is declared explicitly.
    @Test
    fun writesEveryTagAndASquareCoverIntoTheM4aWithoutTouchingTheAudio(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()
            val audioBefore = audioMd5(m4a)

            val failure = M4aTagger(runner, locator).tag(m4a, redJpg(), tags)

            assertNull(failure)
            val probed = ffprobe.probe(m4a)
            assertTags(tags, probed.tags)
            assertEquals("M4A ", probed.tags["major_brand"])
            assertEquals(2, probed.streams.size, probed.toString())
            val audio = probed.streams.single { it.codecType == "audio" }
            assertEquals("aac", audio.codecName)
            assertFalse(audio.attachedPic)
            val picture = probed.streams.single { it.codecType == "video" }
            assertEquals("mjpeg", picture.codecName)
            assertEquals(360, picture.width)
            assertEquals(360, picture.height)
            assertTrue(picture.attachedPic)
            assertEquals(audioBefore, audioMd5(m4a), "the audio must be copied, not re-encoded")
            val left = Files.list(root).use { files -> files.map { it.fileName.toString() }.sorted().toList() }
            assertEquals(listOf("track.jpg", "track.m4a"), left, "no temporary file may stay behind")
        }
    }

    @Test
    fun aFragmentedDashFileBecomesARegularM4aWithTheSameAudio(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = fragmentedM4a()
            assertEquals("dash", ffprobe.probe(m4a).tags["major_brand"], "precondition: the input is a fragmented dash file")
            val audioBefore = audioMd5(m4a)

            assertNull(M4aTagger(runner, locator).tag(m4a, redJpg(), tags))

            val probed = ffprobe.probe(m4a)
            assertEquals("M4A ", probed.tags["major_brand"])
            assertTags(tags, probed.tags)
            assertEquals(audioBefore, audioMd5(m4a), "the audio must be copied, not re-encoded")
        }
    }

    @Test
    fun withoutACoverOnlyTheAudioStreamIsWritten(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()

            val failure = M4aTagger(runner, locator).tag(m4a, null, tags)

            assertNull(failure)
            val probed = ffprobe.probe(m4a)
            assertTags(tags, probed.tags)
            assertEquals(listOf("audio"), probed.streams.map { it.codecType }, probed.toString())
        }
    }

    @Test
    fun aSingleVideoGetsNoAlbumTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()

            assertNull(M4aTagger(runner, locator).tag(m4a, null, tags.copy(album = null)))

            val probed = ffprobe.probe(m4a)
            assertNull(probed.tags["album"])
            assertEquals(tags.title, probed.tags["title"])
        }
    }

    @Test
    fun withoutCommentAndLyricsNeitherTagIsWritten(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()

            assertNull(M4aTagger(runner, locator).tag(m4a, null, tags.copy(comment = null, lyrics = null)))

            val probed = ffprobe.probe(m4a)
            assertNull(probed.tags["comment"])
            assertTrue(probed.tags.keys.none { it.startsWith("lyrics") }, probed.tags.keys.toString())
            assertEquals(tags.title, probed.tags["title"])
        }
    }

    @Test
    fun syntaxCharactersAndLineBreaksSurviveTheRoundTrip(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()
            val nasty = tags.copy(
                title = "a=b;c#d\\e",
                album = "100% [x] {y} #1; k=v",
                comment = "line one\nline two\r\nline three",
                lyrics = "x=y;z\r\nsecond #line\n\nlast \\ line",
            )

            assertNull(M4aTagger(runner, locator).tag(m4a, null, nasty))

            assertTags(nasty, ffprobe.probe(m4a).tags)
        }
    }

    // A value ending in a backslash made ffmpeg swallow the next line of the ffmetadata file (the artist went missing).
    @Test
    fun aTitleEndingInABackslashDoesNotSwallowTheNextTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()
            val trailing = tags.copy(title = "Path to C:\\", artist = "Artist\\\\", albumArtist = "Artist\\\\", album = "Album\\")

            assertNull(M4aTagger(runner, locator).tag(m4a, null, trailing))

            val probed = ffprobe.probe(m4a).tags
            assertEquals("Path to C:\uFF3C", probed["title"])
            assertEquals("Artist\uFF3C\uFF3C", probed["artist"])
            assertEquals("Artist\uFF3C\uFF3C", probed["album_artist"])
            assertEquals("Album\uFF3C", probed["album"])
            assertEquals("7", probed["track"])
            assertEquals(trailing.comment, probed["comment"])
            assertEquals(trailing.lyrics, probed["lyrics"])
        }
    }

    @Test
    fun aNulInATitleCannotInjectAnotherTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()
            val injected = tags.copy(title = "abc\u0000album=Injected", album = "Real Album")

            assertNull(M4aTagger(runner, locator).tag(m4a, null, injected))

            val probed = ffprobe.probe(m4a).tags
            assertEquals("abcalbum=Injected", probed["title"])
            assertEquals("Real Album", probed["album"])
        }
    }
}
```

- [ ] **Step 15: RealYtDlpIntegrationTest를 m4a 기준으로 고친다**

```bash
cd /c/Projects/xGetSongs
F=engine/src/test/kotlin/com/xgetsongs/engine/integration/RealYtDlpIntegrationTest.kt
sed -i 's/\\\.mp3/\\.m4a/; s/NextToTheMp3/NextToTheM4a/; s/\$videoId\.mp3/$videoId.m4a/g; s/the mp3 must be there/the m4a must be there/; s/next to the mp3/next to the m4a/' "$F"
grep -n "mp3" "$F"
```

Expected: 남는 `mp3`는 `downloadsAShortVideoAsMp3` 함수 안뿐이다(아래에서 통째로 바꾼다).

`RealYtDlpIntegrationTest.kt`에서 직접 고친다.

1. import 두 줄을 지운다: `import com.xgetsongs.engine.process.ProcessRunner`, `import com.xgetsongs.engine.testutil.Id3v2Tag`.

2. `ProgressRecordingRunner` 클래스(KDoc `/** Passes everything to the real runner …`부터 닫는 `}`까지)를 지운다.

3. `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`의 `assertEquals(videoId, root.getValue("id").jsonPrimitive.content)` 바로 뒤에 한 줄을 더한다.

```kotlin
                assertEquals("140", root.getValue("format_id").jsonPrimitive.content, "the AAC stream (itag 140) is what is downloaded")
```

4. `downloadsAShortVideoAsMp3` 테스트 전체(`@Test`부터 닫는 `}`까지)를 아래로 바꾼다.

```kotlin
    @Test
    fun downloadsAShortVideoAsM4aWithoutReencoding(): Unit = runBlocking {
        val ffprobePath = Ffprobe.besides(locator.current().ffmpeg)
        assumeTrue(ffprobePath != null, "ffprobe must be installed next to ffmpeg")
        val ffprobe = Ffprobe(ffprobePath!!, runner)
        withTimeout(180_000) {
            // "Me at the zoo": the first video ever uploaded to YouTube, 19 seconds long.
            val resolver = YtDlpResolver(runner, locator)
            val resolved = resolver.resolve("https://www.youtube.com/watch?v=jNQXAC9IVRw")
            val root = Files.createTempDirectory("xgs-integration")
            try {
                val outDir = root.resolve("out")

                // The lyrics lookup is on and the real LRCLIB is asked: this video has no lyrics in its description and none
                // may be found anywhere else, so the file must come out without any lyrics tag.
                val service = DefaultDownloadService(ItemDownloader(runner, locator, resolver, LrclibLyricsProvider()), root.resolve("work"), this)
                val events = service
                    .start(
                        DownloadRequest(
                            resolved.items, LocalFolderSink(outDir), overwrite = false, concurrency = 1, album = "Test Album",
                            searchLyricsOnline = true,
                        ),
                    )
                    .events.receiveAsFlow().toList()

                val done = events.last() as JobEvent.JobDone
                assertEquals(JobStatus.COMPLETED, done.status, events.toString())
                assertEquals(1, done.summary.succeeded, events.toString())
                val file = Files.list(outDir).use { it.toList().single() }
                println("downloaded: ${file.fileName} (${Files.size(file)} bytes)")
                assertTrue(Regex("""001 .+ - .+\.m4a""").matches(file.fileName.toString()), file.fileName.toString())
                assertTrue(Files.size(file) > 50_000)

                val stages = events.filterIsInstance<JobEvent.Progress>().map { it.stage }
                println("reported stages: ${stages.fold(emptyList<Stage>()) { all, next -> if (all.lastOrNull() == next) all else all + next }}")
                assertTrue(Stage.DOWNLOADING in stages, "no download progress was reported: $stages")
                assertEquals(Stage.FINISHING, stages.last(), "the finishing stage comes last: $stages")
                assertEquals(1, stages.count { it == Stage.FINISHING }, stages.toString())

                val probed = ffprobe.probe(file)
                // Never print a lyrics tag: if the lookup wrongly found lyrics, they must not end up in the log.
                println("tags: ${probed.tags.filterKeys { !it.startsWith("lyrics") }}; lyrics tags: ${probed.tags.keys.count { it.startsWith("lyrics") }}; streams: ${probed.streams}")
                assertTrue(probed.tags.keys.none { it.startsWith("lyrics") }, "the video has no lyrics anywhere: ${probed.tags.keys}")
                assertEquals("M4A ", probed.tags["major_brand"])
                assertEquals("Me at the zoo", probed.tags["title"])
                assertEquals("jawed", probed.tags["artist"])
                assertEquals("Various Artists", probed.tags["album_artist"])
                assertEquals("Test Album", probed.tags["album"])
                assertEquals("1", probed.tags["track"])
                assertTrue(probed.tags["comment"].orEmpty().startsWith("https://www.youtube.com/watch?v="), probed.tags.toString())
                assertEquals(listOf("audio", "video"), probed.streams.map { it.codecType }.sorted(), probed.streams.toString())
                assertEquals("aac", probed.streams.single { it.codecType == "audio" }.codecName, "YouTube's AAC stream is copied as it is")
                val cover = probed.streams.single { it.codecType == "video" }
                assertTrue(cover.attachedPic, "the picture must be an attached cover")
                assertNotNull(cover.width)
                assertEquals(cover.width, cover.height, "the cover is cropped to a square")
            } finally {
                root.toFile().deleteRecursively()
            }
        }
    }
```

#### Part J — 본 코드

- [ ] **Step 16: 컴파일 오류(빨간불)를 확인한다**

Run: `./gradlew --no-daemon --console=plain :engine:compileTestKotlin`
Expected: FAIL — `Unresolved reference 'M4aTagger'`, 그리고 `Ffmetadata`/`writeFakeM4a` 관련 오류.

- [ ] **Step 17: YtDlpCommands를 m4a 원본 다운로드로 바꾼다**

`YtDlpCommands.kt`에서 (Edit 두 곳)

1. `download`의 KDoc

old:
```kotlin
    /**
     * Downloads the audio of [url] as `<outputDir>/<videoId>.mp3` and leaves the thumbnail, converted to JPEG, as
     * `<outputDir>/<videoId>.jpg` (the cover for the ID3 tags) and the video's info as `<outputDir>/<videoId>.info.json`
     * (where [VideoInfoFile] reads the album from).
     */
```
new:
```kotlin
    /**
     * Downloads the AAC audio stream of [url] (YouTube's itag 140) as it is, without re-encoding, as
     * `<outputDir>/<videoId>.m4a`, and leaves the thumbnail, converted to JPEG, as `<outputDir>/<videoId>.jpg` (the
     * cover for the tags) and the video's info as `<outputDir>/<videoId>.info.json` (where [VideoInfoFile] reads the
     * album from). The m4a is a fragmented DASH file: `--fixup never` leaves it so, because the tag step rewrites the
     * container anyway.
     */
```

2. 형식 인자

old: `            "-x", "--audio-format", "mp3", "--audio-quality", "0",`
new: `            "-f", "bestaudio[ext=m4a]", "--fixup", "never",`

- [ ] **Step 18: FfmpegCommands를 `.m4a` 출력으로 바꾼다**

`FfmpegCommands.kt`의 `tag` 함수(KDoc 포함, `fun tag`의 닫는 `}`까지)를 아래로 바꾼다.

```kotlin
    /**
     * Copies the audio of [input] into [output] without re-encoding and writes the MP4 tags (the lyrics and the comment
     * too) read from the ffmetadata file [metadataFile]. With a [cover] the picture is cropped to a centered square,
     * re-encoded as JPEG and attached as the front cover. [output] must end in `.m4a`: ffmpeg picks the muxer from the
     * extension. Writing a new container also turns the fragmented DASH file yt-dlp leaves into a regular m4a.
     */
    fun tag(ffmpeg: Path, input: Path, cover: Path?, metadataFile: Path, output: Path): List<String> {
        require(output.fileName.toString().endsWith(".m4a", ignoreCase = true)) { "output must be an .m4a file: $output" }
        val coverInput = cover?.let { listOf("-i", it.toString()) }.orEmpty()
        val coverMap = if (cover != null) listOf("-map", "1:v") else emptyList()
        val coverOutput = if (cover != null) COVER_OUTPUT else emptyList()
        // Inputs are numbered in order: the audio file is 0, the cover (when there is one) is 1, the ffmetadata file is last.
        val metadataIndex = if (cover != null) 2 else 1
        return listOf(ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y") +
            listOf("-i", input.toString()) + coverInput + listOf("-f", "ffmetadata", "-i", metadataFile.toString()) +
            listOf("-map", "0:a") + coverMap +
            listOf("-map_chapters", "-1", "-map_metadata", metadataIndex.toString()) +
            listOf("-c:a", "copy") + coverOutput +
            listOf(output.toString())
    }
```

- [ ] **Step 19: Ffmetadata가 코멘트와 가사도 쓰게 한다**

`Ffmetadata.kt`에서 (Edit 두 곳)

1. 클래스 KDoc 전체

old:
```kotlin
/**
 * Renders [TrackTags] as an ffmetadata file (`-f ffmetadata`), so tag text reaches ffmpeg through a file and never
 * through a command line. Write the result as UTF-8.
 *
 * The comment and the lyrics are not rendered: ffmpeg would store them as `TXXX` frames, so [Id3Tagger] adds a real
 * `COMM` frame and a real `USLT` frame itself.
 */
```
new:
```kotlin
/**
 * Renders [TrackTags] as an ffmetadata file (`-f ffmetadata`), so tag text reaches ffmpeg through a file and never
 * through a command line. Write the result as UTF-8.
 *
 * The MP4 muxer stores `comment` as `©cmt` and `lyrics` as `©lyr`, so every value is rendered. A null or blank album,
 * comment or lyrics text writes no entry.
 */
```

2. `render`

old:
```kotlin
        entry("track", tags.trackNumber.toString())
    }
```
new:
```kotlin
        entry("track", tags.trackNumber.toString())
        tags.comment?.takeIf { it.isNotBlank() }?.let { entry("comment", it) }
        tags.lyrics?.takeIf { it.isNotBlank() }?.let { entry("lyrics", it) }
    }
```

- [ ] **Step 20: TrackTags의 설명을 MP4 항목으로 바꾼다**

`TrackTags.kt` 전체를 아래로 바꾼다.

```kotlin
package com.xgetsongs.engine.tags

/**
 * The tag values of one track, as they are written into the m4a. They are the original text: unlike the file name
 * nothing is sanitized or shortened.
 */
data class TrackTags(
    /** The title (`©nam`). */
    val title: String,
    /** The artist (`©ART`). */
    val artist: String,
    /** The album (`©alb`): the name of the folder (the user's album name, else the playlist title), else the video's own album; null (no tag) if none. */
    val album: String?,
    /** The album artist (`aART`). */
    val albumArtist: String,
    /** The track number (`trkn`): the playlist position, written without leading zeros. */
    val trackNumber: Int,
    /** The comment (`©cmt`): the video URL. A null or blank comment writes no tag. */
    val comment: String?,
    /** The lyrics (`©lyr`) found in the video description or by the lookup, lines separated by `\n`. A null or blank text writes no tag. */
    val lyrics: String? = null,
)
```

- [ ] **Step 21: Id3Tagger를 M4aTagger로 바꾸고 ID3 코드를 지운다**

```bash
cd /c/Projects/xGetSongs
git mv engine/src/main/kotlin/com/xgetsongs/engine/tags/Id3Tagger.kt engine/src/main/kotlin/com/xgetsongs/engine/tags/M4aTagger.kt
git rm -q engine/src/main/kotlin/com/xgetsongs/engine/tags/Id3Frames.kt
```

`M4aTagger.kt` 전체를 아래로 바꾼다.

```kotlin
package com.xgetsongs.engine.tags

import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.ytdlp.Failure
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.engine.ytdlp.FfmpegCommands
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import kotlin.io.path.nameWithoutExtension

/**
 * Writes the tags and the cover into an m4a with one ffmpeg stream-copy pass. The tag text travels in an ffmetadata
 * file next to the m4a, never on the command line. The same pass writes a new container, so the fragmented DASH file
 * yt-dlp leaves behind comes out as a regular m4a.
 */
class M4aTagger(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
) {
    /**
     * Rewrites [file] in place with [tags] and, when given, the picture [cover]. Returns null on success, else the
     * failure; [file] is left untouched then.
     */
    suspend fun tag(file: Path, cover: Path?, tags: TrackTags): Failure? {
        val ffmpeg = tools.current().ffmpeg ?: return Failure(FailureKind.FATAL, "ffmpeg를 찾을 수 없습니다.")
        val name = file.nameWithoutExtension
        val metadataFile = file.resolveSibling("$name.ffmeta")
        val output = file.resolveSibling("$name.tagged.m4a")
        val stderr = mutableListOf<String>()
        try {
            withContext(Dispatchers.IO) { Files.writeString(metadataFile, Ffmetadata.render(tags), Charsets.UTF_8) }
            val exitCode = runner.run(
                FfmpegCommands.tag(ffmpeg, file, cover, metadataFile, output),
                onStderr = { line -> synchronized(stderr) { stderr += line } },
            )
            val written = withContext(Dispatchers.IO) { Files.isRegularFile(output) }
            if (exitCode != 0 || !written) {
                return failure(synchronized(stderr) { stderr.lastOrNull { it.isNotBlank() } }?.trim()?.take(MAX_DETAIL))
            }
            withContext(Dispatchers.IO) { Files.move(output, file, StandardCopyOption.REPLACE_EXISTING) }
            return null
        } catch (e: IOException) {
            return failure(e.message)
        } finally {
            withContext(NonCancellable + Dispatchers.IO) {
                deleteQuietly(metadataFile)
                deleteQuietly(output)
            }
        }
    }

    private fun failure(detail: String?) =
        Failure(FailureKind.OTHER, "태그를 쓰지 못했습니다: ${detail ?: "알 수 없는 오류"}")

    /** A leftover temp file is harmless (the job's work folder is removed), so it must not hide the real result. */
    private fun deleteQuietly(path: Path) {
        try {
            Files.deleteIfExists(path)
        } catch (e: IOException) {
            // Ignored on purpose, see above.
        }
    }

    private companion object {
        const val MAX_DETAIL = 200
    }
}
```

- [ ] **Step 22: ErrorClassifier에 형식 없음 분류를 더한다**

`ErrorClassifier.kt`에서 (Edit 한 곳) `classify` 안의 TRANSIENT 검사 뒤, 마지막 `return` 앞에 넣는다.

old:
```kotlin
        if (TRANSIENT.any { text.contains(it) }) return Failure(FailureKind.TRANSIENT, message)
        return Failure(FailureKind.OTHER, message)
```
new:
```kotlin
        if (TRANSIENT.any { text.contains(it) }) return Failure(FailureKind.TRANSIENT, message)
        // Not "unavailable": it can be a hiccup on YouTube's side, so the item fails and can be retried.
        if (text.contains("requested format is not available")) return Failure(FailureKind.OTHER, "m4a 오디오 형식이 없습니다.")
        return Failure(FailureKind.OTHER, message)
```

- [ ] **Step 23: ItemDownloader를 m4a와 M4aTagger로 바꾼다**

```bash
cd /c/Projects/xGetSongs
sed -i 's/Id3Tagger/M4aTagger/g; s/ID3 tags/tags/g; s/\bmp3\b/m4a/g' engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt
sed -i 's/변환된 m4a 파일을 찾을 수 없습니다\./받은 m4a 파일을 찾을 수 없습니다./' engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt
grep -n "m4a\|M4aTagger" engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt
```

Expected: `import …M4aTagger`, `private val tagger = M4aTagger(runner, tools)`, `workDir.resolve("$videoId.m4a")`, "받은 m4a 파일을 찾을 수 없습니다." 그리고 KDoc의 m4a 몇 줄.

- [ ] **Step 24: 남은 KDoc의 ID3·mp3 표현을 고친다**

```bash
cd /c/Projects/xGetSongs
sed -i 's/ID3 album tag/album tag/; s/the ID3 track number/the track number/' engine/src/main/kotlin/com/xgetsongs/engine/Services.kt
sed -i 's/The ID3 track number/The track number/' shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt
sed -i 's/(the ones the ID3 tags get)/(the ones the tags get)/' engine/src/main/kotlin/com/xgetsongs/engine/lyrics/LyricsProvider.kt
sed -i 's/Where finished mp3 files end up/Where finished m4a files end up/' engine/src/main/kotlin/com/xgetsongs/engine/output/OutputSink.kt
grep -rn "ID3\|Id3\|mp3\|Mp3" --include=*.kt engine/src/main shared/src/commonMain app/src/commonMain server/src/main | grep -v "/build/" || echo "main sources are clean"
```

Expected: `LogRedaction.kt`의 경로 예시 주석(`…\001 Artist - Title.mp3` 같은 것)만 남을 수 있다. 그 외에 남으면 같은 방식으로 고친다.

- [ ] **Step 25: 전체 단위 테스트가 통과하는지 본다**

Run: `./gradlew --no-daemon --console=plain :shared:jvmTest :engine:test :server:test :app:desktopTest`
Expected: `BUILD SUCCESSFUL`. 실패하면 메시지대로 고친다. 자주 나올 수 있는 것:
- `DefaultDownloadServiceTest`에서 `deliveredTag`/`deliveredWith`의 `texts.single()`이 두 번 호출된 테스트: 같은 `texts`를 두 번 넘기지 않았는지 본다.
- 앱 쪽 `AppStateHolderTest`에서 `.mp3`가 남은 곳: Step 1의 치환으로 모두 바뀌었어야 한다.

테스트 개수가 줄었는지(삭제한 `Id3FramesTest` 등)만 확인한다: `grep -h "<testsuite " engine/build/test-results/test/*.xml | sed 's/ timestamp.*//' | head -40`

- [ ] **Step 26: 남은 mp3·ID3 흔적을 확인한다**

```bash
cd /c/Projects/xGetSongs
grep -rln "mp3\|Mp3\|MP3\|ID3\|Id3" --include=*.kt . | grep -v "/build/"
```

Expected: 아래 파일만 나온다(확장자와 상관없는 예시 이름이거나 경로 가리기 테스트다).
`ErrorClassifierTest.kt`, `LocalFolderSinkTest.kt`, `JobLogTest.kt`, `RoutesTest.kt`, `TestSupport.kt`(server), `LocalServerTest.kt`, `ContractSupport.kt`, `DiagnosticsTest.kt`, `ExplorerOpenerTest.kt`, `LogRedaction.kt`, `LogRedactionTest.kt`. 이 밖의 파일이 나오면 고친다.

- [ ] **Step 27: 커밋**

```bash
cd /c/Projects/xGetSongs
git add engine/src/test/kotlin/com/xgetsongs/engine/testutil/FfmetadataReader.kt \
        engine/src/test/kotlin/com/xgetsongs/engine/testutil/FfmetadataReaderTest.kt
git add -u
git status --short
git commit -q -m "$(cat <<'EOF'
feat(engine): 출력 형식을 mp3에서 YouTube AAC 원본 m4a로 바꾼다

yt-dlp가 itag 140을 재인코딩 없이 받고(-f bestaudio[ext=m4a] --fixup never),
M4aTagger가 ffmpeg 스트림 복사 한 번으로 MP4 태그, 커버, 코멘트, 가사를 쓴다.
같은 단계에서 조각난 DASH 컨테이너가 일반 M4A로 다시 쓰이고 오디오 MD5는
그대로다. 직접 만든 ID3 프레임 코드(Id3Frames)는 필요 없어져 지운다.
파일 확장자는 .m4a, m4a 형식이 없는 영상은 "m4a 오디오 형식이 없습니다."로
실패한다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
EOF
)"
git status --short
```

Expected: `git status`에는 사용자의 메모 파일만 `??`로 남는다.

---

### Task 4: 실제 도구로 통합 테스트

실제 yt-dlp, ffmpeg, ffprobe, YouTube로 Task 3의 가정(itag 140 선택, 조각난 파일을 ffmpeg가 일반 M4A로 다시 씀, 오디오 MD5 유지, 단계 보고)을 확인한다. 도구가 없으면 테스트가 건너뛰어진다(여기서는 `%APPDATA%\xGetSongs\bin\yt-dlp.exe`, PATH의 ffmpeg·ffprobe·node가 있다).

**Files:**
- Modify (필요할 때만): Task 3의 통합 테스트 소스

- [ ] **Step 1: 태그 통합 테스트(네트워크 없음)**

Run: `./gradlew --no-daemon --console=plain :engine:integrationTest --tests "*RealFfmpegTaggingIntegrationTest"`
Expected: 모든 테스트 `PASSED`(건너뛰어지면 안 된다). `aFragmentedDashFileBecomesARegularM4aWithTheSameAudio`가 `-brand dash` 입력을 만들지 못하면 ffmpeg 버전의 옵션 차이이므로 `generate`의 인자를 맞춘다(`ffmpeg -h muxer=mov`로 옵션 이름 확인).

- [ ] **Step 2: yt-dlp 통합 테스트(네트워크 필요)**

Run: `./gradlew --no-daemon --console=plain :engine:integrationTest --tests "*RealYtDlpIntegrationTest"`
Expected: 모든 테스트 `PASSED`. 특히 `downloadsAShortVideoAsM4aWithoutReencoding`의 출력에서 `reported stages: [DOWNLOADING, FINISHING]`, `streams`에 aac와 mjpeg가 보인다. `format_id`가 `140`이 아니거나 `Requested format is not available`이 나오면 그 영상은 m4a가 없는 것이므로 결과를 사용자에게 알리고 멈춘다(추측으로 고치지 않는다).

- [ ] **Step 3: 전체 통합 테스트를 한 번 돌려 다른 부분이 깨지지 않았는지 본다**

Run: `./gradlew --no-daemon --console=plain :engine:integrationTest`
Expected: 통과 또는 건너뜀. LRCLIB·구글 가사 카드 테스트는 서비스 상태에 따라 건너뛸 수 있다(이번 변경과 무관). 이번 변경으로 새로 실패한 것이 있으면 고친다.

- [ ] **Step 4: 고친 것이 있으면 커밋**

```bash
cd /c/Projects/xGetSongs
git status --short
# 고친 파일이 있을 때만:
git add -u
git commit -q -m "$(cat <<'EOF'
test(engine): 실제 도구 통합 테스트를 m4a 원본 기준으로 맞춘다

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
EOF
)"
```

고친 것이 없으면 커밋하지 않는다.

---

### Task 5: 문서 갱신과 push

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-10-04-xgetsongs-design.md`

(지난 스펙·계획서 `2026-10-09-id3v24-utf8-*`와 그 밖의 `docs/superpowers/` 이력은 고치지 않는다. `2026-10-10-m4a-original-audio-design.md`가 ID3 쓰기를 대체한다.)

- [ ] **Step 1: README 치환**

```bash
cd /c/Projects/xGetSongs
sed -i 's/\.mp3/.m4a/g; s/mp3로/m4a로/g; s/ID3 트랙 번호/트랙 번호/g' README.md
grep -n "mp3\|ID3\|USLT\|프레임" README.md
```

Expected: 남는 곳은 다음 단계에서 직접 고칠 줄들(`## ID3 태그`, 태그 설명, 가사 줄, 가사 검색 줄, ffmpeg 표 줄, 알려진 제한)이다.

- [ ] **Step 2: README 직접 수정**

`README.md`에서 (Edit)

1. 첫 문단 바로 아래(파일명 형식 줄 앞)에 한 문단을 더한다.

old:
```
파일명 형식: `{순위번호 001~999} {가수명} - {제목}.m4a`
```
new:
```
오디오는 YouTube의 AAC 원본(약 130kbps, itag 140)을 재인코딩 없이 그대로 저장합니다. 음질이 좋아지는 방식은 아니고 원본 그대로라는 뜻입니다.

파일명 형식: `{순위번호 001~999} {가수명} - {제목}.m4a`
```

2. 절 제목과 그 아래 첫 문단의 앞 두 문장 (Edit 두 곳)

old: `## ID3 태그`
new: `## 태그`

old:
```
받은 mp3마다 ID3v2.4 태그를 씁니다(재인코딩 없음, 텍스트는 모두 UTF-8). 이전 버전의 앱이 받은 파일은 ID3v2.3 그대로 두고 다시 쓰지 않습니다.
```
new:
```
받은 m4a마다 MP4 태그를 씁니다(재인코딩 없음, 텍스트는 UTF-8). 이전 버전의 앱이 받은 mp3 파일은 그대로 두고 다시 쓰지 않습니다.
```

3. 가사 항목 (`- 가사: …`)에서 두 군데

old: `그 아래 가사를 가사 프레임(USLT)에 씁니다.`
new: `그 아래 가사를 가사 태그에 씁니다.`

old: ` 한글이 있으면 언어는 `kor`, 아니면 `eng`입니다.`
new: (지운다 — 이 문장 전체를 빈 문자열로)

4. "### 가사 검색" 문단에서

old: `둘 다 못 찾으면 가사 프레임을 쓰지 않으므로 파일에 가사가 남지 않습니다.`
new: `둘 다 못 찾으면 가사 태그를 쓰지 않으므로 파일에 가사가 남지 않습니다.`

5. "필요한 것" 표

old: `| ffmpeg | mp3 변환 | `winget install Gyan.FFmpeg` |`
new: `| ffmpeg | 태그와 커버 쓰기, 썸네일 변환 | `winget install Gyan.FFmpeg` |`

6. "알려진 제한 (1단계)"의 첫 줄

old: `- mp3 품질은 VBR 최고 품질(`--audio-quality 0`)로 고정입니다. ffmpeg는 PATH 또는 `%APPDATA%\xGetSongs\bin`에서 찾으며, 따로 경로를 지정하는 설정은 아직 없습니다.`
new:
```
- 음질은 YouTube의 AAC 원본(약 130kbps)을 그대로 쓰며 고를 수 없습니다. m4a를 읽지 못하는 오래된 기기에서는 재생되지 않고, 예전 버전처럼 mp3로 받는 방법은 없습니다. 이전 버전이 받은 mp3 파일은 건드리지 않으며, 같은 곡을 다시 받으면 덮어쓰지 않고 `.m4a`가 옆에 따로 생깁니다. YouTube가 m4a 오디오를 주지 않는 영상은 "m4a 오디오 형식이 없습니다."로 실패하고 "실패 항목 재시도"로 다시 받을 수 있습니다.
- ffmpeg는 PATH 또는 `%APPDATA%\xGetSongs\bin`에서 찾으며, 따로 경로를 지정하는 설정은 아직 없습니다.
```

- [ ] **Step 3: README 확인**

```bash
cd /c/Projects/xGetSongs
grep -n "mp3\|ID3\|USLT\|프레임\|변환" README.md
```

Expected: `mp3`는 "이전 버전의 앱이 받은 mp3 파일", "예전 버전처럼 mp3로 받는 방법은 없습니다", "이전 버전이 받은 mp3 파일" 세 곳뿐이고 `ID3`, `USLT`, `프레임`은 없다. `변환`은 "썸네일 변환"과 도구 설명에만 남는다.

- [ ] **Step 4: 기존 설계 문서 치환과 직접 수정**

```bash
cd /c/Projects/xGetSongs
D=docs/superpowers/specs/2026-10-04-xgetsongs-design.md
sed -i 's/\.mp3/.m4a/g; s/추출해 mp3로 저장하고/추출해 m4a로 저장하고/; s/오디오만 mp3로 저장하는 앱/오디오만 m4a로 저장하는 앱/; s/ID3 트랙 번호/트랙 번호/g; s/ID3 태그를 쓰지 못/태그를 쓰지 못/g' "$D"
sed -i 's/제목(TIT2)/제목/; s/가수(TPE1)/가수/; s/앨범 아티스트(TPE2)/앨범 아티스트/; s/앨범(TALB)이 된다/앨범이 된다/; s/앨범(TALB)은/앨범은/; s/트랙 번호(TRCK)/트랙 번호/; s/주석(COMM)/주석/; s/가사(USLT)는/가사는/; s/가사 프레임을 쓰지 않는다/가사를 쓰지 않는다/' "$D"
grep -n "mp3\|ID3\|TIT2\|TPE\|TALB\|TRCK\|COMM\|USLT\|APIC\|Converting\|변환" "$D"
```

`$D`에서 직접 고친다(Edit).

1. 6.3의 이름 결정 주체 줄

old: `yt-dlp는 임시 이름(`{영상ID}.m4a`)으로 받고`
(sed로 이미 `.m4a`가 되어 있다. 그대로 둔다.)

2. 6.3의 품질 줄

old: `- **mp3 품질:** VBR 최고 품질(`--audio-quality 0`)로 고정이다. 품질을 고르는 설정은 이후 단계로 미룬다.`
new: `- **오디오:** YouTube의 AAC 원본(itag 140, 약 130kbps)을 재인코딩 없이 그대로 저장한다(`-f bestaudio[ext=m4a]`). 음질을 고르는 설정은 없다. 이전에는 Opus 원본을 mp3 V0로 다시 인코딩했다(2026-10-10에 바꿈, 근거는 [m4a 스펙](2026-10-10-m4a-original-audio-design.md)).`

3. 6.4 제목 `### 6.4 저장 폴더와 ID3 태그` → `### 6.4 저장 폴더와 태그`

4. 6.4의 "태그 쓰기" 줄 전체

old: `- **태그 쓰기:** yt-dlp가 mp3를 만든 뒤 엔진이 ffmpeg로 스트림 복사(재인코딩 없음)하면서 ID3v2.4를 쓴다(제목·가수 같은 텍스트 프레임은 모두 UTF-8이고, 영문뿐인 값도 같다). 값은 명령줄이 아니라 ffmetadata 파일로 넘겨 따옴표 같은 문자가 깨지지 않게 한다. ID3v1은 쓰지 않는다. 태그를 쓰지 못하면 그 항목은 실패로 처리하며 "실패한 것만 다시"로 재시도한다.`
(앞 sed로 `mp3` 글자는 그대로 남아 있다.) new: `- **태그 쓰기:** yt-dlp가 받은 m4a를 엔진이 ffmpeg로 스트림 복사(재인코딩 없음)하면서 MP4 태그와 커버를 쓴다. 이 단계가 조각난 DASH 컨테이너를 일반 M4A로 다시 쓰므로 yt-dlp의 컨테이너 수정은 끈다(`--fixup never`). 값은 명령줄이 아니라 ffmetadata 파일로 넘겨 따옴표 같은 문자가 깨지지 않게 한다. 태그를 쓰지 못하면 그 항목은 실패로 처리하며 "실패한 것만 다시"로 재시도한다.`

5. 6.4 "값" 줄 안의 두 문장

old: `UTF-8로 쓰고 줄바꿈은 CRLF이며, 언어는 한글이 있으면 `kor`, 아니면 `eng`다.`
new: `UTF-8로 쓰고 줄바꿈은 LF다.`

6. 6.4의 "COMM과 USLT" 줄과 "커버" 줄

old: `- **COMM과 USLT:** ffmpeg의 ID3 쓰기는 이 두 프레임을 만들지 못하므로(`comment`는 `TXXX`가 된다) 엔진이 ffmpeg가 끝난 뒤 태그에 직접 이어 붙인다(이 프레임들도 UTF-8이다).`
new: `- **MP4 항목:** ffmpeg의 MP4 쓰기가 제목(`©nam`), 가수(`©ART`), 앨범 아티스트(`aART`), 앨범(`©alb`), 트랙 번호(`trkn`), 주석(`©cmt`), 가사(`©lyr`)로 쓴다. ID3와 달리 주석과 가사도 ffmpeg가 직접 쓴다.`

old: `넣는다(APIC, 앞표지).`
new: `넣는다(MP4 `covr`).`

7. §7 항목 상태와 진행률

old: `- 항목: `Pending → Downloading(%) → Converting → Done(최종 파일명)``
new: `- 항목: `Pending → Downloading(%) → Finishing → Done(최종 파일명)``

old: `ffmpeg 변환 구간은 퍼센트 없이 "변환 중"으로 표시한다.`
new: `다운로드가 끝난 뒤 태그·커버·가사를 쓰는 구간은 퍼센트 없이 "마무리 중"으로 표시한다. 이 단계는 yt-dlp 출력이 아니라 엔진이 yt-dlp 종료 직후 직접 알린다.`

8. §8 오류 표의 "yt-dlp 비정상 종료" 줄 뒤에 한 줄을 더한다.

new row: `| 영상에 m4a 오디오가 없음 | `Failed("m4a 오디오 형식이 없습니다.")`, 건너뜀이 아니라 실패이며 "실패 항목 재시도"로 다시 받는다 |`

9. §1 목표의 끝(`1단계는 Windows 데스크톱 앱, 이후 웹으로 이전한다.` 앞)에 한 줄을 더한다.

new: `- 2026-10-10에 출력 형식을 mp3에서 YouTube AAC 원본 m4a로 바꿨다: [2026-10-10-m4a-original-audio-design.md](2026-10-10-m4a-original-audio-design.md)`

- [ ] **Step 5: 설계 문서 확인**

```bash
cd /c/Projects/xGetSongs
D=docs/superpowers/specs/2026-10-04-xgetsongs-design.md
grep -n "mp3\|ID3\|TIT2\|TPE\|TALB\|TRCK\|COMM\|USLT\|APIC\|Converting" "$D"
```

Expected: `mp3`는 "이전에는 Opus 원본을 mp3 V0로 다시 인코딩했다"와 §1 한 줄의 링크 설명, `ID3`는 "ID3와 달리" 한 곳에만 남고 나머지(`TIT2` 등)는 없다. 남아 있으면 같은 방식으로 고친다.

- [ ] **Step 6: 전체 테스트를 한 번 더 돌린다**

Run: `./gradlew --no-daemon --console=plain check`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 7: 문서 커밋과 push**

```bash
cd /c/Projects/xGetSongs
git add -u
git commit -q -m "$(cat <<'EOF'
문서: README와 설계 문서를 m4a 원본 저장에 맞춘다

출력 형식, 태그(MP4 항목), 진행 단계(마무리 중), m4a 형식이 없을 때의 오류,
이전 mp3 파일과 새 m4a가 나란히 남는다는 점을 적었다. 지난 ID3v2.4 스펙과
계획서는 이력이라 고치지 않는다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
EOF
)"
git push -q origin main
git log --oneline -6
git status --short
```

Expected: 위쪽에 Task 1~5의 커밋이 보이고, `git status`에는 사용자의 메모 파일만 `??`로 남는다.

- [ ] **Step 8: 사용자에게 실제 기기 확인을 부탁한다**

앱(`.\gradlew.bat :app:run`)으로 곡 하나를 받아, 새 `.m4a`가 에뮬레이터나 사용 중인 플레이어에서 재생되고 제목, 한글 태그, 커버가 보이는지 확인해 달라고 한다(가사 표시는 플레이어에 따라 다르다). 결과를 메모리 파일 `project-m4a-original-audio-pending-test.md`에 반영한다.
