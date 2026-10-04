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

    /** The events the fake job sends before it closes its channel. */
    var eventsToSend: List<JobEvent> = finishedEvents

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
            eventsToSend.forEach { events.trySend(it) }
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
