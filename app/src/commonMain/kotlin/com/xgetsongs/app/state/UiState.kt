package com.xgetsongs.app.state

import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.LyricsOutcome
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.ToolsStatus
import kotlin.time.Duration

sealed interface ItemStatus {
    /** Shown in the preview before a download starts. */
    data object Ready : ItemStatus

    /** Part of a running job but not started yet. */
    data object Waiting : ItemStatus

    data class Downloading(val percent: Double?) : ItemStatus
    data object Finishing : ItemStatus

    /** [lyrics] is what the finished file got as lyrics (or why it got none); null when the server did not say. */
    data class Done(val lyrics: LyricsOutcome? = null) : ItemStatus
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
    /** [outputDir], [overwrite], [includeRank] and [searchLyricsOnline] are the options the app remembers between runs. */
    val outputDir: String = "",
    val overwrite: Boolean = false,
    /** Whether file names start with the rank (`001 `). Sent with the job; the preview names follow it. */
    val includeRank: Boolean = true,
    /** Whether a song whose description has no lyrics is looked up on the internet. Sent with the job; the preview does not change. */
    val searchLyricsOnline: Boolean = true,
    /** Not remembered: it belongs to the video that is on screen. */
    val singleRank: Int = 1,
    /**
     * The album name the user typed, as typed; it is also the name of the folder the files are saved in. Null means the
     * user has not touched the field: it shows the default (see [albumNameText]) and sends nothing. Once touched, the
     * text stays as it is, and blank means the default again (the playlist title as album and as folder name; for a
     * single video no folder and its own album). Not remembered, like [singleRank]: the next playlist should not
     * inherit the name of this one. [AppStateHolder.reset] clears it.
     */
    val albumName: String? = null,
    val resolved: ResolveResponse? = null,
    val rows: List<ItemRow> = emptyList(),
    val error: String? = null,
    val jobStatus: JobStatus? = null,
    val summary: JobSummary? = null,
    /** How long the job that [summary] belongs to took, from the press of the button to the job-done event; null while there is none. */
    val elapsed: Duration? = null,
    val tools: ToolsStatus? = null,
    val toolBusy: Boolean = false,
    val toolMessage: String? = null,
) {
    val failedRanks: List<Int> get() = rows.filter { it.status is ItemStatus.Failed }.map { it.item.rank }
    val canResolve: Boolean get() = phase != Phase.RESOLVING && phase != Phase.RUNNING
}
