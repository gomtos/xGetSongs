package com.xgetsongs.app.state

import com.xgetsongs.app.api.ApiError
import com.xgetsongs.app.api.XgsApi
import com.xgetsongs.app.settings.NoSettingsStore
import com.xgetsongs.app.settings.SettingsStore
import com.xgetsongs.app.settings.UserSettings
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
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * All screen logic. The composables only render [state] and call these functions.
 *
 * The five options (output folder, overwrite, rank in file names, concurrency, lyrics search on the internet) start from
 * what [settings] holds and are saved again a moment after the user changes one of them; see [flushSettings].
 */
class AppStateHolder(
    private val api: XgsApi,
    private val scope: CoroutineScope,
    defaultOutputDir: String,
    private val settings: SettingsStore = NoSettingsStore,
) {
    private val _state = MutableStateFlow(initialState(defaultOutputDir))
    val state: StateFlow<UiState> = _state.asStateFlow()

    private var jobId: String? = null
    private var jobTask: Job? = null
    private var saveJob: Job? = null

    /**
     * The options as they are known to be stored: what the screen started with (what [settings] holds, with a blank
     * folder replaced by the default and the concurrency brought into range), then whatever was saved last. Nothing is
     * written while the screen still shows exactly these, so a file that could not be read is not replaced by defaults
     * and the default folder does not end up in the file unless the user changes something.
     */
    private var lastKnown: UserSettings = _state.value.options()

    private fun initialState(defaultOutputDir: String): UiState {
        val stored = settings.load()
        return UiState(
            outputDir = stored.outputDir?.takeIf { it.isNotBlank() } ?: defaultOutputDir,
            overwrite = stored.overwrite,
            includeRank = stored.includeRank,
            concurrency = stored.concurrency.coerceIn(UserSettings.MIN_CONCURRENCY, UserSettings.MAX_CONCURRENCY),
            searchLyricsOnline = stored.searchLyricsOnline,
        )
    }

    // ---- simple setters -----------------------------------------------------------------

    fun onInput(text: String) = _state.update { it.copy(input = text) }

    fun onOutputDir(dir: String) = changeOptions { it.copy(outputDir = dir) }

    fun onOverwrite(value: Boolean) = changeOptions { it.copy(overwrite = value) }

    /**
     * Turning the rank in the file name on or off rewrites the names shown in the preview. Rows of a running or finished
     * job are left alone: they show the names the server really used, and the rows of a job that ended before it
     * started them (cancelled or aborted) keep the preview names they had.
     */
    fun onIncludeRank(value: Boolean) = changeOptions { state ->
        if (state.phase != Phase.PREVIEW) {
            state.copy(includeRank = value)
        } else {
            state.copy(includeRank = value, rows = state.rows.map { it.withFileName(value) })
        }
    }

    fun onConcurrency(value: Int) =
        changeOptions { it.copy(concurrency = value.coerceIn(UserSettings.MIN_CONCURRENCY, UserSettings.MAX_CONCURRENCY)) }

    /** Whether a song whose description has no lyrics is looked up on the internet. It only matters when a job starts. */
    fun onSearchLyricsOnline(value: Boolean) = changeOptions { it.copy(searchLyricsOnline = value) }

    /** The album name for the tags of the next job; blank keeps the default. Not remembered (see [UiState.albumName]). */
    fun onAlbumName(text: String) = _state.update { it.copy(albumName = text) }

    /** The name of the folder the next job saves into; blank keeps the default. Not remembered (see [UiState.folderName]). */
    fun onFolderName(text: String) = _state.update { it.copy(folderName = text) }

    /** Changing the rank of a single video rewrites its file name in the preview. */
    fun onSingleRank(value: Int) = _state.update { state ->
        val rank = value.coerceIn(FilenameFormatter.MIN_RANK, FilenameFormatter.MAX_RANK)
        if (state.resolved?.kind != InputKind.VIDEO) {
            state.copy(singleRank = rank)
        } else {
            state.copy(singleRank = rank, rows = state.rows.map { it.withRank(rank, state.includeRank) })
        }
    }

    fun dismissError() = _state.update { it.copy(error = null) }

    // ---- remembered options -----------------------------------------------------------------

    /** Applies [change] and, if it altered one of the remembered options, schedules a save. */
    private fun changeOptions(change: (UiState) -> UiState) {
        var altered = false
        _state.update { old ->
            val new = change(old)
            altered = new.options() != old.options()
            new
        }
        if (altered) scheduleSave()
    }

    /** Each change restarts the wait, so typing a folder name produces one save, with the finished text. */
    private fun scheduleSave() {
        saveJob?.cancel()
        saveJob = scope.launch {
            delay(SAVE_DELAY_MS)
            saveIfChanged()
        }
    }

    /**
     * Saves the options as they are now, at once, and drops a save that is still waiting. Call it when the window
     * closes, because a change made in the last moments would otherwise be lost. Writes nothing when the options are
     * the ones already stored (see [lastKnown]).
     */
    fun flushSettings() {
        saveJob?.cancel()
        saveJob = null
        saveIfChanged()
    }

    private fun saveIfChanged() {
        val current = _state.value.options()
        if (current == lastKnown) return
        settings.save(current)
        lastKnown = current
    }

    /** The five options that are remembered; nothing else on the screen is. */
    private fun UiState.options() = UserSettings(
        outputDir = outputDir, overwrite = overwrite, includeRank = includeRank, concurrency = concurrency,
        searchLyricsOnline = searchLyricsOnline,
    )

    fun reset() {
        jobTask?.cancel()
        jobId = null
        _state.update {
            UiState(
                outputDir = it.outputDir, overwrite = it.overwrite, includeRank = it.includeRank,
                concurrency = it.concurrency, searchLyricsOnline = it.searchLyricsOnline, tools = it.tools,
            )
        }
    }

    // ---- tools ----------------------------------------------------------------------------

    fun refreshTools() {
        scope.launch {
            try {
                val tools = api.tools()
                _state.update { it.copy(tools = tools) }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                _state.update { it.copy(error = failureMessage(e)) }
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
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                _state.update { it.copy(toolBusy = false, toolMessage = failureMessage(e)) }
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
        val before = _state.value
        _state.update { it.copy(phase = Phase.RESOLVING, error = null) }
        scope.launch {
            try {
                val response = api.resolve(input)
                _state.update { state ->
                    state.copy(
                        phase = Phase.PREVIEW,
                        resolved = response,
                        rows = response.items.map { previewRow(it, state.singleRank, response.kind, state.includeRank) },
                        summary = null,
                        jobStatus = null,
                    )
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                // A failed lookup must not strip the buttons from a preview that is still on screen. The user may have
                // changed the rank option while the lookup ran (the rows were not rewritten then), so a restored
                // preview shows names that follow the option as it is now.
                val phase = if (before.resolved != null) before.phase else Phase.IDLE
                _state.update { state ->
                    val rows = if (phase == Phase.PREVIEW) state.rows.map { it.withFileName(state.includeRank) } else state.rows
                    state.copy(phase = phase, rows = rows, error = failureMessage(e))
                }
            }
        }
    }

    /** The server's `expectedFileName` always carries the rank, so the shown name is built here to honour [includeRank]. */
    private fun previewRow(item: ResolvedItem, singleRank: Int, kind: InputKind, includeRank: Boolean): ItemRow {
        val row = ItemRow(
            item = item,
            fileName = item.expectedFileName,
            status = if (item.available) ItemStatus.Ready else ItemStatus.Skipped(item.unavailableReason.orEmpty()),
        )
        return if (kind == InputKind.VIDEO) row.withRank(singleRank, includeRank) else row.withFileName(includeRank)
    }

    private fun ItemRow.withRank(rank: Int, includeRank: Boolean): ItemRow {
        if (!item.available) return this
        return copy(item = item.copy(rank = rank)).withFileName(includeRank)
    }

    private fun ItemRow.withFileName(includeRank: Boolean): ItemRow {
        if (!item.available) return this
        return copy(fileName = FilenameFormatter.format(item.rank, item.artist, item.track, includeRank))
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
        runJob(JobRequest(resolved.resolveId, jobOptions(state)), before = state)
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
        runJob(JobRequest(resolved.resolveId, jobOptions(state), ranks = retryRanks), before = state)
    }

    fun cancel() {
        val id = jobId ?: return
        scope.launch {
            try {
                api.cancel(id)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                _state.update { it.copy(error = failureMessage(e)) }
            }
        }
    }

    private fun jobOptions(state: UiState) = JobOptions(
        outputDir = state.outputDir,
        overwrite = state.overwrite,
        singleRank = state.singleRank,
        concurrency = state.concurrency,
        includeRank = state.includeRank,
        searchLyricsOnline = state.searchLyricsOnline,
        albumName = state.albumName.takeIf { it.isNotBlank() },
        folderName = state.folderName.takeIf { it.isNotBlank() },
    )

    /**
     * [before] is the screen as it was when the user pressed the button. It is restored when the job cannot even be
     * started, so a failed retry does not wipe the results of the previous run.
     */
    private fun runJob(request: JobRequest, before: UiState) {
        jobTask = scope.launch {
            var started = false
            try {
                val created = api.startJob(request)
                started = true
                jobId = created.jobId
                api.events(created.jobId).collect { event -> _state.update { apply(it, event) } }
                _state.update { state ->
                    if (state.phase == Phase.RUNNING) {
                        state.copy(
                            phase = Phase.FINISHED,
                            error = "서버와의 연결이 끊어졌습니다.",
                            rows = state.rows.map(::resetTransient),
                        )
                    } else {
                        state
                    }
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                _state.update { state ->
                    if (started) {
                        state.copy(
                            phase = Phase.FINISHED,
                            error = failureMessage(e),
                            rows = state.rows.map(::resetTransient),
                        )
                    } else {
                        state.copy(
                            phase = before.phase,
                            rows = before.rows,
                            summary = before.summary,
                            jobStatus = before.jobStatus,
                            error = failureMessage(e),
                        )
                    }
                }
            }
        }
    }

    /** What the user sees for a failure: the server's own message, or a generic one for transport/decoding errors. */
    private fun failureMessage(e: Exception): String =
        if (e is ApiError) e.message.orEmpty() else "서버와 통신 중 오류가 발생했습니다: ${e.message}"

    /**
     * A cancelled or aborted job sends no final event for the items that were still waiting or in flight, so those
     * rows go back to [ItemStatus.Ready]. Finished, skipped and failed rows keep their result.
     */
    private fun resetTransient(row: ItemRow): ItemRow = when (row.status) {
        ItemStatus.Waiting, is ItemStatus.Downloading, ItemStatus.Converting -> row.copy(status = ItemStatus.Ready)
        else -> row
    }

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
        is JobEvent.ItemDone ->
            state.updateRow(event.rank) { it.copy(fileName = event.fileName, status = ItemStatus.Done(event.lyrics)) }
        is JobEvent.ItemSkipped -> state.updateRow(event.rank) { it.copy(status = ItemStatus.Skipped(event.reason)) }
        is JobEvent.ItemFailed -> state.updateRow(event.rank) { it.copy(status = ItemStatus.Failed(event.message)) }
        is JobEvent.JobDone -> state.copy(
            phase = Phase.FINISHED,
            jobStatus = event.status,
            summary = event.summary,
            rows = state.rows.map(::resetTransient),
        )
    }

    private fun UiState.updateRow(rank: Int, change: (ItemRow) -> ItemRow): UiState =
        copy(rows = rows.map { if (it.item.rank == rank && it.item.available) change(it) else it })

    private companion object {
        /** How long the options must stay unchanged before they are written. */
        const val SAVE_DELAY_MS = 400L
    }
}
