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
 * playlist title written into every file's ID3 tags, or null for a single video.
 */
data class DownloadRequest(
    val items: List<ResolvedItem>,
    val sink: OutputSink,
    val overwrite: Boolean,
    val concurrency: Int,
    val album: String? = null,
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
