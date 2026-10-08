package com.xgetsongs.app.state

import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.LyricsOutcome
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.ToolsStatus

sealed interface ItemStatus {
    /** Shown in the preview before a download starts. */
    data object Ready : ItemStatus

    /** Part of a running job but not started yet. */
    data object Waiting : ItemStatus

    data class Downloading(val percent: Double?) : ItemStatus
    data object Converting : ItemStatus

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
    /** [outputDir], [overwrite], [includeRank], [concurrency] and [searchLyricsOnline] are the options the app remembers between runs. */
    val outputDir: String = "",
    val overwrite: Boolean = false,
    /** Whether file names start with the rank (`001 `). Sent with the job; the preview names follow it. */
    val includeRank: Boolean = true,
    val concurrency: Int = 2,
    /** Whether a song whose description has no lyrics is looked up on the internet. Sent with the job; the preview does not change. */
    val searchLyricsOnline: Boolean = true,
    /** Not remembered: it belongs to the video that is on screen. */
    val singleRank: Int = 1,
    val resolved: ResolveResponse? = null,
    val rows: List<ItemRow> = emptyList(),
    val error: String? = null,
    val jobStatus: JobStatus? = null,
    val summary: JobSummary? = null,
    val tools: ToolsStatus? = null,
    val toolBusy: Boolean = false,
    val toolMessage: String? = null,
) {
    val failedRanks: List<Int> get() = rows.filter { it.status is ItemStatus.Failed }.map { it.item.rank }
    val canResolve: Boolean get() = phase != Phase.RESOLVING && phase != Phase.RUNNING
}
