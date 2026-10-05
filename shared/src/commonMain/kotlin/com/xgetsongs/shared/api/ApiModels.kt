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
    /** Whether the file name starts with the rank (`001 `). The ID3 track number is the rank either way. */
    val includeRank: Boolean = true,
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
