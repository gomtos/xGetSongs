package com.xgetsongs.server

import ch.qos.logback.classic.Level
import ch.qos.logback.classic.spi.ILoggingEvent
import com.xgetsongs.engine.job.DownloadConcurrency
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.LyricsOutcome
import com.xgetsongs.shared.api.ResolveRequest
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.Stage
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.plugins.sse.sse
import io.ktor.client.request.delete
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.http.ContentType
import io.ktor.http.contentType
import io.ktor.server.testing.testApplication
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

// The names of the files and titles in these events are recognisable markers: none of them may ever show up in a log line.
class JobLogTest {
    private val outDir: Path = Files.createTempDirectory("xgs-joblog").resolve("out")

    // ---- the lines -------------------------------------------------------------------------

    @Test
    fun anItemThatStartsIsADebugLineWithRankAndVideoIdOnly() {
        val line = assertNotNull(JobLog.describe(JobEvent.ItemStarted(7, "vid00000007", "FILENAME-MARKER.mp3")))

        assertEquals(JobLogLevel.DEBUG, line.level)
        assertTrue("순위 7" in line.text && "vid00000007" in line.text, line.text)
        assertFalse("FILENAME-MARKER" in line.text, line.text)
    }

    @Test
    fun progressIsNotLogged() {
        assertNull(JobLog.describe(JobEvent.Progress(1, Stage.DOWNLOADING, 42.0)))
        assertNull(JobLog.describe(JobEvent.Progress(1, Stage.CONVERTING, null)))
    }

    @Test
    fun aFinishedItemIsAnInfoLineWithTheRankOnly() {
        val line = assertNotNull(JobLog.describe(JobEvent.ItemDone(12, "FILENAME-MARKER.mp3")))

        assertEquals(JobLogLevel.INFO, line.level)
        assertEquals("항목 완료: 순위 12", line.text)
    }

    @Test
    fun aFinishedItemNamesItsLyricsOutcomeButNotItsFileName() {
        for (outcome in LyricsOutcome.entries) {
            val line = assertNotNull(JobLog.describe(JobEvent.ItemDone(3, "FILENAME-MARKER.mp3", outcome)))

            assertEquals(JobLogLevel.INFO, line.level)
            assertEquals("항목 완료: 순위 3, 가사 ${outcome.name}", line.text)
            assertFalse("FILENAME-MARKER" in line.text, line.text)
        }
        assertEquals("항목 완료: 순위 3, 가사 ONLINE", JobLog.describe(JobEvent.ItemDone(3, "x.mp3", LyricsOutcome.ONLINE))?.text)
    }

    @Test
    fun aFinishedItemWithoutAKnownOutcomeAddsNothingToTheLine() {
        val line = assertNotNull(JobLog.describe(JobEvent.ItemDone(3, "FILENAME-MARKER.mp3", lyrics = null)))

        assertEquals(JobLogLevel.INFO, line.level)
        assertEquals("항목 완료: 순위 3", line.text)
    }

    @Test
    fun aSkippedItemIsAnInfoLineWithRankAndReason() {
        val line = assertNotNull(JobLog.describe(JobEvent.ItemSkipped(2, "이미 파일이 있음")))

        assertEquals(JobLogLevel.INFO, line.level)
        assertEquals("항목 건너뜀: 순위 2, 사유: 이미 파일이 있음", line.text)
    }

    @Test
    fun aFailedItemIsAWarningWithRankAndReason() {
        val line = assertNotNull(JobLog.describe(JobEvent.ItemFailed(3, "yt-dlp가 비정상 종료했습니다 (코드 1)")))

        assertEquals(JobLogLevel.WARN, line.level)
        assertEquals("항목 실패: 순위 3, 사유: yt-dlp가 비정상 종료했습니다 (코드 1)", line.text)
    }

    @Test
    fun theSummaryIsAnInfoLineWithTheStatusAndTheThreeCounts() {
        val line = assertNotNull(JobLog.describe(JobEvent.JobDone(JobStatus.CANCELLED, JobSummary(succeeded = 5, skipped = 2, failed = 1))))

        assertEquals(JobLogLevel.INFO, line.level)
        assertEquals("작업 종료: 상태=CANCELLED, 성공 5, 건너뜀 2, 실패 1", line.text)
    }

    @Test
    fun aReasonIsKeptOnOneLineAndCutWhenItIsLong() {
        val multiLine = assertNotNull(JobLog.describe(JobEvent.ItemFailed(1, "first line\r\nsecond line\n\n\tthird\u0000line")))
        assertEquals("항목 실패: 순위 1, 사유: first line second line third line", multiLine.text)

        val long = assertNotNull(JobLog.describe(JobEvent.ItemFailed(1, "x".repeat(2_000))))
        val reason = long.text.substringAfter("사유: ")
        assertEquals(JobLog.MAX_REASON_LENGTH + 1, reason.length)
        assertTrue(reason.endsWith("…"))
    }

    // ---- paths and file names --------------------------------------------------------------

    @Test
    fun aNoSuchFileExceptionShapedReasonLosesBothPathsWhatEverTheSpacesInThem() {
        val reason = "NoSuchFileException: C:\\Users\\Some User\\AppData\\Roaming\\xGetSongs\\work\\job-1\\001 A - B.f251.webm -> " +
            "D:\\Music\\My Playlist\\001 A - B.mp3"

        assertEquals("NoSuchFileException: <경로> -> <경로>", JobLog.redact(reason, emptyList()))
    }

    @Test
    fun aFileSystemExceptionKeepsItsSystemReason() {
        val reason = "FileSystemException: C:\\w\\job 1\\001 A - B.mp3 -> D:\\Music\\My Playlist\\001 A - B.mp3: " +
            "The process cannot access the file because it is being used by another process"

        assertEquals(
            "FileSystemException: <경로> -> <경로>: The process cannot access the file because it is being used by another process",
            JobLog.redact(reason, emptyList()),
        )
    }

    @Test
    fun aUncPathIsRedactedToo() {
        assertEquals("AccessDeniedException: <경로>: Access is denied", JobLog.redact("AccessDeniedException: \\\\NAS\\Music Share\\Lists\\a.mp3: Access is denied", emptyList()))
    }

    @Test
    fun theKnownFileNameIsReplacedWhereverItAppearsIgnoringCase() {
        val redacted = JobLog.redact("yt-dlp could not write 001 A - B.MP3 (while merging)", listOf("001 A - B.mp3"))

        assertEquals("yt-dlp could not write <파일명> (while merging)", redacted)
    }

    @Test
    fun textWithoutPathsOrKnownNamesIsUnchanged() {
        for (text in listOf("연결이 끊어졌습니다", "HTTP 429: Too Many Requests", "ERROR: [youtube] abc: Video unavailable", "")) {
            assertEquals(text, JobLog.redact(text, listOf("001 A - B.mp3")), text)
        }
    }

    @Test
    fun aFailedItemLosesPathsAndTheFileNameOfItsRank() {
        val event = JobEvent.ItemFailed(
            3,
            "FileSystemException: C:\\Users\\me\\Music\\MARKER-SONG.mp3 -> D:\\Out\\MARKER-SONG.mp3: Access is denied (MARKER-SONG.mp3)",
        )

        val line = assertNotNull(JobLog.describe(event, mapOf(3 to "MARKER-SONG.mp3")))

        assertEquals(JobLogLevel.WARN, line.level)
        assertEquals("항목 실패: 순위 3, 사유: FileSystemException: <경로> -> <경로>: Access is denied (<파일명>)", line.text)
    }

    @Test
    fun aSkippedItemLosesThemToo() {
        val line = assertNotNull(JobLog.describe(JobEvent.ItemSkipped(2, "이미 있음: D:\\Out\\MARKER-SONG.mp3"), mapOf(2 to "MARKER-SONG.mp3")))

        assertEquals("항목 건너뜀: 순위 2, 사유: 이미 있음: <경로>", line.text)
    }

    @Test
    fun theFileNameOfAnotherRankIsNotTouched() {
        val line = assertNotNull(JobLog.describe(JobEvent.ItemFailed(3, "failed: other-name.mp3"), mapOf(4 to "other-name.mp3")))

        assertEquals("항목 실패: 순위 3, 사유: failed: other-name.mp3", line.text)
    }

    @Test
    fun aPathIsRedactedBeforeTheReasonIsCutSoNoPartOfItSurvives() {
        val longPath = "C:\\" + "very long folder name\\".repeat(40) + "file.mp3"

        val line = assertNotNull(JobLog.describe(JobEvent.ItemFailed(1, "failed: $longPath"), emptyMap()))

        assertEquals("항목 실패: 순위 1, 사유: failed: <경로>", line.text)
    }

    @Test
    fun theLineForADroppedEventConnectionNamesTheJob() {
        assertEquals("이벤트 연결이 끊어짐 (작업 01234567)", JobLog.eventsDisconnected("0123456789abcdef"))
    }

    @Test
    fun theStartLineHasKindCountAndOptionsAndNoTitles() {
        val options = JobOptions(outputDir = "C:\\Music\\out", overwrite = true, includeRank = false, searchLyricsOnline = false)

        val playlist = JobLog.started("0123456789abcdef", InputKind.PLAYLIST, itemCount = 40, options, concurrency = 3)
        val video = JobLog.started("0123456789abcdef", InputKind.VIDEO, itemCount = 1, options.copy(outputDir = null), concurrency = 3)

        assertEquals(
            "작업 시작: id=0123456789abcdef, 종류=재생목록, 항목=40개, overwrite=true, includeRank=false, concurrency=3, searchLyricsOnline=false, outputDir=C:\\Music\\out",
            playlist,
        )
        assertTrue("종류=영상" in video && "항목=1개" in video && "outputDir=없음" in video, video)
    }

    @Test
    fun theStartLineSaysThatAnAlbumNameWasTypedButNeverWhatItIs() {
        val plain = JobOptions(outputDir = "C:\\Music\\out")
        val typed = plain.copy(albumName = "비밀 앨범")
        val blank = plain.copy(albumName = "  ")

        val line = JobLog.started("0123456789abcdef", InputKind.PLAYLIST, itemCount = 2, typed, concurrency = 3)

        assertTrue(line.endsWith(", outputDir=C:\\Music\\out, albumName=지정"), line)
        assertTrue("비밀" !in line, line)
        assertEquals(JobLog.started("0123456789abcdef", InputKind.PLAYLIST, 2, plain, 3), JobLog.started("0123456789abcdef", InputKind.PLAYLIST, 2, blank, 3))
        assertTrue("albumName" !in JobLog.started("0123456789abcdef", InputKind.PLAYLIST, 2, plain, 3))
    }

    @Test
    fun theShortIdIsTheFirstEightCharacters() {
        assertEquals("01234567", JobLog.shortId("0123456789abcdef"))
        assertEquals("abc", JobLog.shortId("abc"))
    }

    // ---- through the routes ----------------------------------------------------------------

    private suspend fun HttpClient.resolve(): ResolveResponse =
        post("/resolve") {
            contentType(ContentType.Application.Json)
            setBody(ResolveRequest("PLabcdefghijkl"))
        }.body()

    private suspend fun HttpClient.startJob(resolved: ResolveResponse, options: JobOptions): String =
        post("/jobs") {
            contentType(ContentType.Application.Json)
            setBody(JobRequest(resolved.resolveId, options))
        }.body<JobCreated>().jobId

    private suspend fun HttpClient.readEventsToTheEnd(jobId: String) {
        sse("/jobs/$jobId/events") { incoming.collect { } }
    }

    private fun List<ILoggingEvent>.jobs() = filter { it.loggerName == JobLog.LOGGER_NAME }

    @Test
    fun aJobLogsItsStartItsItemsAndItsSummary() = testApplication {
        val fakes = TestServices()
        fakes.downloads.queued = listOf(
            JobEvent.ItemStarted(1, "vid00000001", "TITLE-MARKER-ONE.mp3"),
            JobEvent.Progress(1, Stage.DOWNLOADING, 50.0),
            JobEvent.ItemDone(1, "TITLE-MARKER-ONE.mp3", LyricsOutcome.ONLINE),
            JobEvent.ItemFailed(3, "연결이 끊어졌습니다"),
            JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)),
        )
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        LogCapture().use { capture ->
            val jobId = client.startJob(resolved, JobOptions(outputDir = outDir.toString(), overwrite = true))
            client.readEventsToTheEnd(jobId)

            val tag = "[${jobId.take(8)}]"
            val records = capture.events.jobs()
            val byText = records.associate { it.formattedMessage to it.level }
            val start = records.first()
            assertEquals(Level.INFO, start.level)
            assertTrue(start.formattedMessage.startsWith("$tag 작업 시작: id=$jobId, 종류=재생목록, 항목=2개, overwrite=true, includeRank=true, concurrency=${DownloadConcurrency.automatic()}, searchLyricsOnline=true, outputDir=$outDir"), start.formattedMessage)
            assertEquals(Level.DEBUG, byText["$tag 항목 시작: 순위 1, 영상 vid00000001"])
            assertEquals(Level.INFO, byText["$tag 항목 완료: 순위 1, 가사 ONLINE"])
            assertEquals(Level.WARN, byText["$tag 항목 실패: 순위 3, 사유: 연결이 끊어졌습니다"])
            assertEquals(Level.INFO, byText["$tag 작업 종료: 상태=COMPLETED, 성공 1, 건너뜀 0, 실패 1"])
            assertEquals(5, records.size, "start, started, done, failed, summary: no line for the progress event: ${records.map { it.formattedMessage }}")
            assertEquals(records.map { it.formattedMessage }.last(), "$tag 작업 종료: 상태=COMPLETED, 성공 1, 건너뜀 0, 실패 1")
        }
    }

    @Test
    fun aSingleVideoIsLoggedAsAVideo() = testApplication {
        val fakes = TestServices(resolver = FakeResolver(response = sampleVideo()))
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        LogCapture().use { capture ->
            client.startJob(resolved, JobOptions(outputDir = outDir.toString(), singleRank = 42))

            val start = capture.events.jobs().single().formattedMessage
            assertTrue("종류=영상" in start && "항목=1개" in start, start)
        }
    }

    @Test
    fun noLogLineHoldsATitleAFileNameOrTheToken() = testApplication {
        val fakes = TestServices()
        fakes.downloads.queued = listOf(
            JobEvent.ItemStarted(1, "vid00000001", "TITLE-MARKER-ONE.mp3"),
            JobEvent.ItemDone(1, "TITLE-MARKER-ONE.mp3", LyricsOutcome.DESCRIPTION),
            JobEvent.ItemStarted(3, "vid00000003", "TITLE-MARKER-THREE.mp3"),
            JobEvent.ItemFailed(
                3,
                "FileSystemException: C:\\Users\\Some User\\Music\\TITLE-MARKER-THREE.mp3 -> D:\\Out\\TITLE-MARKER-THREE.mp3: " +
                    "Access is denied while writing title-marker-three.mp3",
            ),
            JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)),
        )
        installServer(fakes.services)
        val client = apiClient() // sends the token header with every request

        LogCapture().use { capture ->
            val resolved = client.resolve()
            val jobId = client.startJob(resolved, JobOptions(outputDir = outDir.toString()))
            client.readEventsToTheEnd(jobId)

            val everything = capture.events.joinToString("\n") { it.formattedMessage + " " + it.throwableProxy?.message }
            assertTrue(capture.events.jobs().isNotEmpty())
            assertFalse(TEST_TOKEN in everything, everything)
            assertFalse("X-XGS-Token" in everything, everything)
            assertFalse("TITLE-MARKER" in everything, everything)
            assertFalse("title-marker" in everything, everything)
            assertFalse("Some User" in everything, "no path: $everything")
            assertFalse("A - One" in everything, "no title of the sample playlist: $everything")
            val failure = capture.events.jobs().single { it.level == Level.WARN }.formattedMessage
            assertTrue(failure.endsWith("항목 실패: 순위 3, 사유: FileSystemException: <경로> -> <경로>: Access is denied while writing <파일명>"), failure)
        }
    }

    @Test
    fun cancellingAJobIsLogged() = testApplication {
        val fakes = TestServices()
        fakes.downloads.closeAfterQueued = false
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        LogCapture().use { capture ->
            val jobId = client.startJob(resolved, JobOptions(outputDir = outDir.toString()))
            client.delete("/jobs/$jobId")

            val cancel = capture.events.jobs().last()
            assertEquals(Level.INFO, cancel.level)
            assertEquals("[${jobId.take(8)}] 취소 요청을 받음", cancel.formattedMessage)
        }
    }

    @Test
    fun cancellingAnUnknownJobLogsNothingAboutAJob() = testApplication {
        installServer(TestServices().services)

        LogCapture().use { capture ->
            apiClient().delete("/jobs/nope")

            assertEquals(emptyList(), capture.events.jobs())
        }
    }
}
