package com.xgetsongs.app.state

import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
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
    data object Done : ItemStatus
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
    /** [outputDir], [overwrite], [includeRank] and [concurrency] are the options the app remembers between runs. */
    val outputDir: String = "",
    val overwrite: Boolean = false,
    /** Whether file names start with the rank (`001 `). Sent with the job; the preview names follow it. */
    val includeRank: Boolean = true,
    val concurrency: Int = 2,
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
