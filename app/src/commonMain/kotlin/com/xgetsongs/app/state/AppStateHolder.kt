package com.xgetsongs.app.state

import com.xgetsongs.app.api.ApiError
import com.xgetsongs.app.api.XgsApi
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.filename.FilenameFormatter
import com.xgetsongs.shared.input.ClassifyResult
import com.xgetsongs.shared.input.InputClassifier
import com.xgetsongs.shared.input.ParsedInput
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** All screen logic. The composables only render [state] and call these functions. */
class AppStateHolder(
    private val api: XgsApi,
    private val scope: CoroutineScope,
    defaultOutputDir: String,
) {
    private val _state = MutableStateFlow(UiState(outputDir = defaultOutputDir))
    val state: StateFlow<UiState> = _state.asStateFlow()

    private var jobId: String? = null
    private var jobTask: Job? = null

    // ---- simple setters -----------------------------------------------------------------

    fun onInput(text: String) = _state.update { it.copy(input = text) }

    fun onOutputDir(dir: String) = _state.update { it.copy(outputDir = dir) }

    fun onOverwrite(value: Boolean) = _state.update { it.copy(overwrite = value) }

    fun onConcurrency(value: Int) = _state.update { it.copy(concurrency = value.coerceIn(1, 4)) }

    /** Changing the rank of a single video rewrites its file name in the preview. */
    fun onSingleRank(value: Int) = _state.update { state ->
        val rank = value.coerceIn(FilenameFormatter.MIN_RANK, FilenameFormatter.MAX_RANK)
        if (state.resolved?.kind != InputKind.VIDEO) {
            state.copy(singleRank = rank)
        } else {
            state.copy(singleRank = rank, rows = state.rows.map { it.withRank(rank) })
        }
    }

    fun dismissError() = _state.update { it.copy(error = null) }

    fun reset() {
        jobTask?.cancel()
        jobId = null
        _state.update {
            UiState(
                outputDir = it.outputDir, overwrite = it.overwrite, concurrency = it.concurrency,
                tools = it.tools,
            )
        }
    }

    // ---- tools ----------------------------------------------------------------------------

    fun refreshTools() {
        scope.launch {
            try {
                val tools = api.tools()
                _state.update { it.copy(tools = tools) }
            } catch (e: ApiError) {
                _state.update { it.copy(error = e.message) }
            }
        }
    }

    fun installYtDlp() = runToolAction { api.installYtDlp().message }

    fun updateYtDlp() = runToolAction { api.updateYtDlp().message }

    private fun runToolAction(action: suspend () -> String) {
        _state.update { it.copy(toolBusy = true, toolMessage = null) }
        scope.launch {
            try {
                val message = action()
                val tools = api.tools()
                _state.update { it.copy(toolBusy = false, toolMessage = message, tools = tools) }
            } catch (e: ApiError) {
                _state.update { it.copy(toolBusy = false, toolMessage = e.message) }
            }
        }
    }

    // ---- resolving ------------------------------------------------------------------------

    fun resolve() {
        val current = _state.value
        if (!current.canResolve) return
        when (val result = InputClassifier.classify(current.input)) {
            is ClassifyResult.Rejected -> _state.update { it.copy(error = result.reason.message) }
            is ClassifyResult.Ok -> doResolve(current.input)
        }
    }

    /** For a watch URL that also carried a playlist: look up just that video instead. */
    fun switchToVideoOnly() {
        val videoId = _state.value.resolved?.alsoVideoId ?: return
        val url = ParsedInput.Video(videoId).canonicalUrl
        _state.update { it.copy(input = url) }
        doResolve(url)
    }

    private fun doResolve(input: String) {
        _state.update { it.copy(phase = Phase.RESOLVING, error = null) }
        scope.launch {
            try {
                val response = api.resolve(input)
                _state.update { state ->
                    state.copy(
                        phase = Phase.PREVIEW,
                        resolved = response,
                        rows = response.items.map { previewRow(it, state.singleRank, response.kind) },
                        summary = null,
                        jobStatus = null,
                    )
                }
            } catch (e: ApiError) {
                _state.update { it.copy(phase = Phase.IDLE, error = e.message) }
            }
        }
    }

    private fun previewRow(item: ResolvedItem, singleRank: Int, kind: InputKind): ItemRow {
        val row = ItemRow(
            item = item,
            fileName = item.expectedFileName,
            status = if (item.available) ItemStatus.Ready else ItemStatus.Skipped(item.unavailableReason.orEmpty()),
        )
        return if (kind == InputKind.VIDEO) row.withRank(singleRank) else row
    }

    private fun ItemRow.withRank(rank: Int): ItemRow {
        if (!item.available) return this
        return copy(
            item = item.copy(rank = rank),
            fileName = FilenameFormatter.format(rank, item.artist, item.track),
        )
    }

    // ---- downloading ----------------------------------------------------------------------

    fun startDownload() {
        val state = _state.value
        val resolved = state.resolved ?: return
        if (state.phase != Phase.PREVIEW && state.phase != Phase.FINISHED) return
        if (state.outputDir.isBlank()) {
            _state.update { it.copy(error = "출력 폴더를 지정하세요.") }
            return
        }
        _state.update { s ->
            s.copy(
                phase = Phase.RUNNING, error = null, summary = null, jobStatus = null,
                rows = s.rows.map { if (it.item.available) it.copy(status = ItemStatus.Waiting) else it },
            )
        }
        runJob(JobRequest(resolved.resolveId, jobOptions(state)), fallbackPhase = state.phase)
    }

    /** Runs a new job for just the items that failed last time. */
    fun retryFailed() {
        val state = _state.value
        val resolved = state.resolved ?: return
        val ranks = state.failedRanks
        if (ranks.isEmpty() || state.phase != Phase.FINISHED) return
        _state.update { s ->
            s.copy(
                phase = Phase.RUNNING, error = null, summary = null, jobStatus = null,
                rows = s.rows.map { if (it.item.rank in ranks) it.copy(status = ItemStatus.Waiting) else it },
            )
        }
        // A single video is always re-run as a whole; its rank is chosen by the user, not by the list.
        val retryRanks = ranks.takeIf { resolved.kind != InputKind.VIDEO }
        runJob(JobRequest(resolved.resolveId, jobOptions(state), ranks = retryRanks), fallbackPhase = state.phase)
    }

    fun cancel() {
        val id = jobId ?: return
        scope.launch {
            try {
                api.cancel(id)
            } catch (e: ApiError) {
                _state.update { it.copy(error = e.message) }
            }
        }
    }

    private fun jobOptions(state: UiState) = JobOptions(
        outputDir = state.outputDir,
        overwrite = state.overwrite,
        singleRank = state.singleRank,
        concurrency = state.concurrency,
    )

    /** [fallbackPhase] is where the screen returns to when the job cannot even be started. */
    private fun runJob(request: JobRequest, fallbackPhase: Phase) {
        jobTask = scope.launch {
            var started = false
            try {
                val created = api.startJob(request)
                started = true
                jobId = created.jobId
                api.events(created.jobId).collect { event -> _state.update { apply(it, event) } }
                _state.update { state ->
                    if (state.phase == Phase.RUNNING) {
                        state.copy(phase = Phase.FINISHED, error = "서버와의 연결이 끊어졌습니다.")
                    } else {
                        state
                    }
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: ApiError) {
                _state.update { state ->
                    state.copy(
                        phase = if (started) Phase.FINISHED else fallbackPhase,
                        error = e.message,
                        rows = state.rows.map(::resetWaiting),
                    )
                }
            } catch (e: Exception) {
                _state.update { it.copy(phase = Phase.FINISHED, error = "서버와 통신 중 오류가 발생했습니다: ${e.message}") }
            }
        }
    }

    private fun resetWaiting(row: ItemRow): ItemRow =
        if (row.status is ItemStatus.Waiting) row.copy(status = ItemStatus.Ready) else row

    private fun apply(state: UiState, event: JobEvent): UiState = when (event) {
        is JobEvent.ItemStarted ->
            state.updateRow(event.rank) { it.copy(fileName = event.fileName, status = ItemStatus.Downloading(null)) }
        is JobEvent.Progress -> state.updateRow(event.rank) {
            it.copy(
                status = when (event.stage) {
                    Stage.DOWNLOADING -> ItemStatus.Downloading(event.percent)
                    Stage.CONVERTING -> ItemStatus.Converting
                },
            )
        }
        is JobEvent.ItemDone -> state.updateRow(event.rank) { it.copy(fileName = event.fileName, status = ItemStatus.Done) }
        is JobEvent.ItemSkipped -> state.updateRow(event.rank) { it.copy(status = ItemStatus.Skipped(event.reason)) }
        is JobEvent.ItemFailed -> state.updateRow(event.rank) { it.copy(status = ItemStatus.Failed(event.message)) }
        is JobEvent.JobDone -> state.copy(
            phase = Phase.FINISHED,
            jobStatus = event.status,
            summary = event.summary,
            rows = state.rows.map(::resetWaiting),
        )
    }

    private fun UiState.updateRow(rank: Int, change: (ItemRow) -> ItemRow): UiState =
        copy(rows = rows.map { if (it.item.rank == rank && it.item.available) change(it) else it })
}
