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
