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
