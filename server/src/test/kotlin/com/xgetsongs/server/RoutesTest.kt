package com.xgetsongs.server

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.ToolException
import com.xgetsongs.engine.output.LocalFolderSink
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
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
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

    // ---- playlist folder and album ---------------------------------------------------------

    private fun DownloadRequest.directory(): Path = assertIs<LocalFolderSink>(sink).directory

    @Test
    fun aPlaylistIsSavedInAFolderNamedAfterItWithTheTitleAsAlbum() = testApplication {
        val fakes = TestServices() // the sample playlist is titled "Sample"
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        client.startJob(JobRequest(resolved.resolveId, options()))

        val request = fakes.downloads.requests.single()
        assertEquals(outDir.resolve("Sample"), request.directory())
        assertTrue(Files.isDirectory(outDir.resolve("Sample")), "the playlist folder is created")
        assertEquals("Sample", request.album)
    }

    @Test
    fun theFolderNameIsSanitizedButTheAlbumKeepsTheOriginalTitle() = testApplication {
        val fakes = TestServices(resolver = FakeResolver(response = samplePlaylist().copy(playlistTitle = "Best: Of?")))
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        client.startJob(JobRequest(resolved.resolveId, options()))

        val request = fakes.downloads.requests.single()
        val folder = "Best： Of？" // full-width colon and question mark
        assertEquals(outDir.resolve(folder), request.directory())
        assertTrue(Files.isDirectory(outDir.resolve(folder)))
        assertEquals("Best: Of?", request.album)
    }

    @Test
    fun aPlaylistWithoutATitleGetsTheDefaultFolderAndNoAlbum() = testApplication {
        val fakes = TestServices(resolver = FakeResolver(response = samplePlaylist().copy(playlistTitle = null)))
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        client.startJob(JobRequest(resolved.resolveId, options()))

        val request = fakes.downloads.requests.single()
        assertEquals(outDir.resolve("재생목록"), request.directory())
        assertTrue(Files.isDirectory(outDir.resolve("재생목록")))
        assertNull(request.album)
    }

    @Test
    fun aSingleVideoIsSavedInTheOutputFolderWithoutAnAlbum() = testApplication {
        // A title on a video is ignored: only the kind decides whether there is a sub-folder.
        val fakes = TestServices(resolver = FakeResolver(response = sampleVideo().copy(playlistTitle = "Sample")))
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        client.startJob(JobRequest(resolved.resolveId, options()))

        val request = fakes.downloads.requests.single()
        assertEquals(outDir, request.directory())
        assertTrue(Files.isDirectory(outDir))
        assertFalse(Files.exists(outDir.resolve("Sample")), "no sub-folder for a single video")
        assertNull(request.album)
    }

    @Test
    fun aRetryOfAPlaylistJobUsesTheSameFolderAndAlbum() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()

        client.startJob(JobRequest(resolved.resolveId, options()))
        client.startJob(JobRequest(resolved.resolveId, options(), ranks = listOf(3)))

        val (first, retry) = fakes.downloads.requests.toList()
        assertEquals(listOf(3), retry.items.map { it.rank })
        assertEquals(first.directory(), retry.directory())
        assertEquals(outDir.resolve("Sample"), retry.directory())
        assertEquals(first.album, retry.album)
    }

    @Test
    fun aFileInThePlaceOfThePlaylistFolderIsABadRequest() = testApplication {
        val fakes = TestServices()
        installServer(fakes.services)
        val client = apiClient()
        val resolved = client.resolve()
        Files.createDirectories(outDir)
        Files.writeString(outDir.resolve("Sample"), "not a folder")

        val response = client.startJob(JobRequest(resolved.resolveId, options()))

        assertEquals(HttpStatusCode.BadRequest, response.status)
        assertTrue(response.body<ErrorResponse>().message.startsWith("출력 폴더를 만들 수 없습니다"))
        assertTrue(fakes.downloads.requests.isEmpty())
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
