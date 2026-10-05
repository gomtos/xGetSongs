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
 * [items] must already carry their final ranks. [concurrency] is clamped to 1..4 by the service. [album] is the
 * fallback for the ID3 album tag: a file gets the video's own album when yt-dlp knows one, else this playlist title.
 * It is null for a single video, which then gets no album tag unless it has an album of its own. [includeRank] says
 * whether the file names start with the rank; the ID3 track number is the rank either way. Without the rank two items
 * can end up with the same file name: the first one to finish wins, the other is skipped (or, with [overwrite],
 * replaces it).
 */
data class DownloadRequest(
    val items: List<ResolvedItem>,
    val sink: OutputSink,
    val overwrite: Boolean,
    val concurrency: Int,
    val album: String? = null,
    val includeRank: Boolean = true,
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
