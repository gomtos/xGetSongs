@file:OptIn(ExperimentalCoroutinesApi::class)

package com.xgetsongs.app.state

import com.xgetsongs.app.api.ApiError
import com.xgetsongs.app.settings.UserSettings
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.input.RejectReason
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class AppStateHolderTest {
    private val playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"

    private fun TestScope.holder(api: FakeApi = FakeApi()) =
        api to AppStateHolder(api, backgroundScope, defaultOutputDir = "C:/Music/xGetSongs")

    private fun AppStateHolder.row(rank: Int) = state.value.rows.first { it.item.rank == rank }

    private suspend fun TestScope.resolved(api: FakeApi = FakeApi()): Pair<FakeApi, AppStateHolder> {
        val (fake, holder) = holder(api)
        holder.onInput(playlistId)
        holder.resolve()
        runCurrent()
        return fake to holder
    }

    // ---- resolving ------------------------------------------------------------------------

    @Test
    fun startsIdleWithTheDefaultOutputFolder() = runTest {
        val (_, holder) = holder()

        assertEquals(Phase.IDLE, holder.state.value.phase)
        assertEquals("C:/Music/xGetSongs", holder.state.value.outputDir)
    }

    @Test
    fun resolveShowsAPreviewOfEveryItem() = runTest {
        val (api, holder) = resolved()

        val state = holder.state.value
        assertEquals(Phase.PREVIEW, state.phase)
        assertEquals(listOf(playlistId), api.resolveInputs)
        assertEquals(listOf(1, 2, 3), state.rows.map { it.item.rank })
        assertEquals(ItemStatus.Ready, holder.row(1).status)
        assertEquals(ItemStatus.Skipped("비공개 영상"), holder.row(2).status)
        assertEquals("001 A1 - T1.mp3", holder.row(1).fileName)
    }

    @Test
    fun invalidInputIsRejectedBeforeAnyRequest() = runTest {
        val (api, holder) = holder()
        holder.onInput("https://evil.com/x")

        holder.resolve()
        runCurrent()

        assertEquals(RejectReason.UNSUPPORTED_HOST.message, holder.state.value.error)
        assertTrue(api.resolveInputs.isEmpty())
        assertEquals(Phase.IDLE, holder.state.value.phase)
    }

    @Test
    fun serverErrorsAreShownAndTheScreenStaysUsable() = runTest {
        val api = FakeApi().apply { resolveError = ApiError("비공개 재생목록") }
        val (_, holder) = resolved(api)

        assertEquals("비공개 재생목록", holder.state.value.error)
        assertEquals(Phase.IDLE, holder.state.value.phase)
    }

    @Test
    fun switchingToTheVideoOnlyResolvesTheVideoUrl() = runTest {
        val api = FakeApi().apply { resolveResponse = FakeApi.playlist(alsoVideoId = "dQw4w9WgXcQ") }
        val (_, holder) = resolved(api)

        holder.switchToVideoOnly()
        runCurrent()

        assertEquals("https://www.youtube.com/watch?v=dQw4w9WgXcQ", api.resolveInputs.last())
    }

    // ---- single video rank ----------------------------------------------------------------

    @Test
    fun changingTheRankRewritesTheSingleVideoFileName() = runTest {
        val api = FakeApi().apply { resolveResponse = FakeApi.video() }
        val (_, holder) = resolved(api)

        holder.onSingleRank(42)

        assertEquals("042 IU - Love.mp3", holder.row(42).fileName)
        assertEquals(42, holder.state.value.singleRank)
    }

    @Test
    fun rankIsClampedToOneThrough999() = runTest {
        val (_, holder) = holder()

        holder.onSingleRank(0)
        assertEquals(1, holder.state.value.singleRank)
        holder.onSingleRank(5000)
        assertEquals(999, holder.state.value.singleRank)
    }

    // ---- rank in the file name ------------------------------------------------------------

    @Test
    fun theRankIsPartOfTheFileNameUnlessTheUserTurnsItOff() = runTest {
        val (_, holder) = holder()

        assertTrue(holder.state.value.includeRank)
        holder.onIncludeRank(false)
        assertEquals(false, holder.state.value.includeRank)
        holder.onIncludeRank(true)
        assertTrue(holder.state.value.includeRank)
    }

    @Test
    fun turningTheRankOffRewritesTheFileNamesOfThePreview() = runTest {
        val (_, holder) = resolved()

        holder.onIncludeRank(false)

        assertEquals(Phase.PREVIEW, holder.state.value.phase)
        assertEquals("A1 - T1.mp3", holder.row(1).fileName)
        assertEquals("A3 - T3.mp3", holder.row(3).fileName)
        assertEquals(listOf(1, 2, 3), holder.state.value.rows.map { it.item.rank }, "the position column stays")
        assertEquals(ItemStatus.Ready, holder.row(1).status)
    }

    @Test
    fun turningTheRankBackOnRestoresTheRankedFileNames() = runTest {
        val (_, holder) = resolved()

        holder.onIncludeRank(false)
        holder.onIncludeRank(true)

        assertEquals("001 A1 - T1.mp3", holder.row(1).fileName)
        assertEquals("003 A3 - T3.mp3", holder.row(3).fileName)
    }

    @Test
    fun unavailableRowsKeepTheirTextWhenTheOptionChanges() = runTest {
        val unavailable = ResolvedItem(
            rank = 2, videoId = "vid00000002", title = "[Private video]", available = false,
            unavailableReason = "비공개 영상", expectedFileName = "kept as it was.mp3",
        )
        val api = FakeApi().apply { resolveResponse = FakeApi.playlist().copy(items = listOf(FakeApi.item(1), unavailable)) }
        val (_, holder) = resolved(api)

        holder.onIncludeRank(false)

        assertEquals("kept as it was.mp3", holder.row(2).fileName)
        assertEquals(ItemStatus.Skipped("비공개 영상"), holder.row(2).status)
        assertEquals("A1 - T1.mp3", holder.row(1).fileName)
    }

    @Test
    fun resolvingWithTheRankOffShowsFileNamesWithoutTheRank() = runTest {
        val (_, holder) = holder()
        holder.onIncludeRank(false)
        holder.onInput(playlistId)

        holder.resolve()
        runCurrent()

        assertEquals("A1 - T1.mp3", holder.row(1).fileName)
        assertEquals("A3 - T3.mp3", holder.row(3).fileName)
        assertNull(holder.row(2).fileName)
    }

    @Test
    fun aVideoResolvedWithTheRankOffShowsAFileNameWithoutTheRank() = runTest {
        val api = FakeApi().apply { resolveResponse = FakeApi.video() }
        val (_, holder) = holder(api)
        holder.onIncludeRank(false)
        holder.onSingleRank(42)
        holder.onInput("https://www.youtube.com/watch?v=dQw4w9WgXcQ")

        holder.resolve()
        runCurrent()

        assertEquals("IU - Love.mp3", holder.row(42).fileName)
    }

    @Test
    fun changingTheSingleVideoRankKeepsTheOption() = runTest {
        val api = FakeApi().apply { resolveResponse = FakeApi.video() }
        val (_, holder) = resolved(api)
        holder.onIncludeRank(false)

        holder.onSingleRank(42)

        assertEquals("IU - Love.mp3", holder.row(42).fileName)
        assertEquals(42, holder.state.value.singleRank)
        holder.onIncludeRank(true)
        assertEquals("042 IU - Love.mp3", holder.row(42).fileName, "the rank typed by the user is what comes back")
    }

    @Test
    fun theOptionChosenWhileResolvingAppliesToTheResult() = runTest {
        val (api, holder) = holder()
        holder.onInput(playlistId)
        holder.resolve()
        holder.onIncludeRank(false) // the lookup has not finished yet: there is nothing to rewrite
        runCurrent()

        assertEquals(Phase.PREVIEW, holder.state.value.phase)
        assertEquals(listOf(playlistId), api.resolveInputs)
        assertEquals("A1 - T1.mp3", holder.row(1).fileName)
    }

    @Test
    fun startingSendsTheIncludeRankOption() = runTest {
        val (api, holder) = resolved()

        holder.startDownload()
        runCurrent()
        assertTrue(api.jobRequests.single().options.includeRank, "on by default")
    }

    @Test
    fun startingWithTheRankOffSendsIt() = runTest {
        val (api, holder) = resolved()
        holder.onIncludeRank(false)

        holder.startDownload()
        runCurrent()

        assertEquals(false, api.jobRequests.single().options.includeRank)
    }

    @Test
    fun aRetrySendsTheOptionToo() = runTest {
        val (api, holder) = finishedWithOneFailure()
        holder.onIncludeRank(false)

        holder.retryFailed()
        runCurrent()

        assertEquals(listOf(3), api.jobRequests.last().ranks)
        assertEquals(false, api.jobRequests.last().options.includeRank)
    }

    @Test
    fun turningTheRankOffWhileAJobRunsLeavesTheRowsOfThatJobAlone() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemStarted(1, "vid00000001", "001 Real - Name.mp3"))
        runCurrent()

        holder.onIncludeRank(false)

        assertEquals(Phase.RUNNING, holder.state.value.phase)
        assertEquals(false, holder.state.value.includeRank)
        assertEquals("001 Real - Name.mp3", holder.row(1).fileName)
        assertEquals("003 A3 - T3.mp3", holder.row(3).fileName)
        assertEquals(ItemStatus.Downloading(null), holder.row(1).status)
    }

    @Test
    fun turningTheRankOffAfterAJobFinishedLeavesTheRealFileNamesAlone() = runTest {
        val (_, holder) = finishedWithOneFailure()

        holder.onIncludeRank(false)

        assertEquals(Phase.FINISHED, holder.state.value.phase)
        assertEquals("001 A1 - T1.mp3", holder.row(1).fileName)
        assertEquals("003 A3 - T3.mp3", holder.row(3).fileName)
        assertEquals(false, holder.state.value.includeRank)
    }

    @Test
    fun resetKeepsTheIncludeRankOption() = runTest {
        val (_, holder) = resolved()
        holder.onIncludeRank(false)

        holder.reset()

        assertEquals(Phase.IDLE, holder.state.value.phase)
        assertEquals(false, holder.state.value.includeRank)
    }

    // ---- remembered options ---------------------------------------------------------------

    private fun TestScope.holderWith(store: FakeSettingsStore) =
        AppStateHolder(FakeApi(), backgroundScope, defaultOutputDir = "C:/Music/xGetSongs", settings = store)

    /** What the holder saves when only the folder differs from the defaults of a fresh [FakeSettingsStore]. */
    private fun defaultsWith(
        outputDir: String = "C:/Music/xGetSongs",
        overwrite: Boolean = false,
        includeRank: Boolean = true,
        concurrency: Int = 2,
    ) = UserSettings(outputDir, overwrite, includeRank, concurrency)

    private fun TestScope.assertSavedOnlyAfterTheQuietPeriod(store: FakeSettingsStore, expected: UserSettings) {
        advanceTimeBy(399)
        runCurrent()
        assertEquals(emptyList(), store.saved, "nothing is saved before 400 ms have passed")
        advanceTimeBy(1)
        runCurrent()
        assertEquals(listOf(expected), store.saved)
    }

    @Test
    fun theOptionsStartFromTheStoredSettings() = runTest {
        val store = FakeSettingsStore(UserSettings(outputDir = "D:/음악", overwrite = true, includeRank = false, concurrency = 4))

        val holder = holderWith(store)

        val state = holder.state.value
        assertEquals("D:/음악", state.outputDir)
        assertTrue(state.overwrite)
        assertEquals(false, state.includeRank)
        assertEquals(4, state.concurrency)
        assertEquals(Phase.IDLE, state.phase)
        assertEquals(1, store.loadCalls)
    }

    @Test
    fun aMissingOrBlankStoredFolderFallsBackToTheDefaultFolder() = runTest {
        for (stored in listOf(null, "", "   ")) {
            val holder = holderWith(FakeSettingsStore(UserSettings(outputDir = stored, overwrite = true)))

            assertEquals("C:/Music/xGetSongs", holder.state.value.outputDir, "stored folder: $stored")
            assertTrue(holder.state.value.overwrite, "the other options are still taken from the store")
        }
    }

    @Test
    fun aStoredConcurrencyOutsideOneToFourIsClamped() = runTest {
        assertEquals(4, holderWith(FakeSettingsStore(UserSettings(concurrency = 9))).state.value.concurrency)
        assertEquals(1, holderWith(FakeSettingsStore(UserSettings(concurrency = 0))).state.value.concurrency)
    }

    @Test
    fun changingTheOutputFolderSavesTheValuesAfterAQuietPeriod() = runTest {
        val store = FakeSettingsStore()
        val holder = holderWith(store)

        holder.onOutputDir("D:/Songs")

        assertSavedOnlyAfterTheQuietPeriod(store, defaultsWith(outputDir = "D:/Songs"))
    }

    @Test
    fun changingOverwriteSavesTheValuesAfterAQuietPeriod() = runTest {
        val store = FakeSettingsStore()
        val holder = holderWith(store)

        holder.onOverwrite(true)

        assertSavedOnlyAfterTheQuietPeriod(store, defaultsWith(overwrite = true))
    }

    @Test
    fun changingTheRankOptionSavesTheValuesAfterAQuietPeriod() = runTest {
        val store = FakeSettingsStore()
        val holder = holderWith(store)

        holder.onIncludeRank(false)

        assertSavedOnlyAfterTheQuietPeriod(store, defaultsWith(includeRank = false))
    }

    @Test
    fun changingTheConcurrencySavesTheValuesAfterAQuietPeriod() = runTest {
        val store = FakeSettingsStore()
        val holder = holderWith(store)

        holder.onConcurrency(3)

        assertSavedOnlyAfterTheQuietPeriod(store, defaultsWith(concurrency = 3))
    }

    @Test
    fun theRankOptionIsSavedWhileAJobRunsToo() = runTest {
        val store = FakeSettingsStore()
        val holder = AppStateHolder(FakeApi(), backgroundScope, defaultOutputDir = "C:/Music/xGetSongs", settings = store)
        holder.onInput(playlistId)
        holder.resolve()
        runCurrent()
        holder.startDownload()
        runCurrent()

        holder.onIncludeRank(false)

        assertSavedOnlyAfterTheQuietPeriod(store, defaultsWith(includeRank = false))
    }

    @Test
    fun quickChangesAreSavedOnceWithTheLastValues() = runTest {
        val store = FakeSettingsStore()
        val holder = holderWith(store)

        holder.onOutputDir("D:/S")
        advanceTimeBy(300)
        holder.onOutputDir("D:/Songs")
        advanceTimeBy(300)
        holder.onConcurrency(3)
        advanceTimeBy(399)
        runCurrent()
        assertEquals(emptyList(), store.saved, "every change starts the quiet period again")
        advanceTimeBy(1)
        runCurrent()

        assertEquals(listOf(defaultsWith(outputDir = "D:/Songs", concurrency = 3)), store.saved)
        advanceTimeBy(5_000)
        runCurrent()
        assertEquals(1, store.saved.size, "no second save follows")
    }

    @Test
    fun settingAValueThatIsAlreadySetSavesNothing() = runTest {
        val store = FakeSettingsStore(UserSettings(concurrency = 4))
        val holder = holderWith(store)

        holder.onOverwrite(false)
        holder.onIncludeRank(true)
        holder.onConcurrency(4)
        holder.onConcurrency(9) // clamped to 4, which is what it already is
        holder.onOutputDir("C:/Music/xGetSongs")
        advanceTimeBy(5_000)
        runCurrent()

        assertEquals(emptyList(), store.saved)
    }

    @Test
    fun repeatingAValueDoesNotRestartTheQuietPeriod() = runTest {
        val store = FakeSettingsStore()
        val holder = holderWith(store)

        holder.onOverwrite(true)
        advanceTimeBy(300)
        holder.onOverwrite(true)
        advanceTimeBy(99)
        runCurrent()
        assertEquals(emptyList(), store.saved)
        advanceTimeBy(1)
        runCurrent()

        assertEquals(listOf(defaultsWith(overwrite = true)), store.saved)
    }

    @Test
    fun flushSettingsSavesRightAwayAndCancelsThePendingSave() = runTest {
        val store = FakeSettingsStore()
        val holder = holderWith(store)
        holder.onOverwrite(true)
        advanceTimeBy(100)
        runCurrent()
        assertEquals(emptyList(), store.saved)

        holder.flushSettings()

        assertEquals(listOf(defaultsWith(overwrite = true)), store.saved, "saved at once, without waiting")
        advanceTimeBy(5_000)
        runCurrent()
        assertEquals(1, store.saved.size, "the pending save was cancelled, so there is no second one")
    }

    @Test
    fun flushSettingsSavesTheCurrentValuesEvenWhenNothingWasChanged() = runTest {
        val store = FakeSettingsStore(UserSettings(outputDir = "D:/Songs", includeRank = false))
        val holder = holderWith(store)

        holder.flushSettings()

        assertEquals(listOf(defaultsWith(outputDir = "D:/Songs", includeRank = false)), store.saved)
    }

    @Test
    fun nothingIsSavedAtStartup() = runTest {
        val store = FakeSettingsStore(UserSettings(outputDir = " ", concurrency = 9))

        holderWith(store)
        advanceTimeBy(5_000)
        runCurrent()

        assertEquals(emptyList(), store.saved)
    }

    @Test
    fun resetKeepsTheOptionsAndSavesNothing() = runTest {
        val store = FakeSettingsStore(UserSettings(outputDir = "D:/Songs", overwrite = true, includeRank = false, concurrency = 3))
        val holder = holderWith(store)
        holder.onInput(playlistId)
        holder.resolve()
        runCurrent()

        holder.reset()
        advanceTimeBy(5_000)
        runCurrent()

        val state = holder.state.value
        assertEquals(Phase.IDLE, state.phase)
        assertEquals("D:/Songs", state.outputDir)
        assertTrue(state.overwrite)
        assertEquals(false, state.includeRank)
        assertEquals(3, state.concurrency)
        assertEquals(emptyList(), store.saved)
    }

    @Test
    fun theTypedInputAndTheSingleVideoRankAreNeverRemembered() = runTest {
        val store = FakeSettingsStore()
        val holder = holderWith(store)

        holder.onInput(playlistId)
        holder.onSingleRank(42)
        advanceTimeBy(5_000)
        runCurrent()
        assertEquals(emptyList(), store.saved, "neither of them is an option that is saved")

        holder.onOverwrite(true)
        advanceTimeBy(400)
        runCurrent()
        assertEquals(listOf(defaultsWith(overwrite = true)), store.saved)
    }

    // ---- downloading ----------------------------------------------------------------------

    @Test
    fun startingSendsTheOptionsAndMarksItemsWaiting() = runTest {
        val (api, holder) = resolved()
        holder.onOverwrite(true)
        holder.onConcurrency(3)
        holder.onOutputDir("D:/Songs")

        holder.startDownload()
        runCurrent()

        val request = api.jobRequests.single()
        assertEquals("resolve-1", request.resolveId)
        assertEquals("D:/Songs", request.options.outputDir)
        assertTrue(request.options.overwrite)
        assertEquals(3, request.options.concurrency)
        assertNull(request.ranks)
        assertEquals(Phase.RUNNING, holder.state.value.phase)
        assertEquals(ItemStatus.Waiting, holder.row(1).status)
        assertEquals(ItemStatus.Skipped("비공개 영상"), holder.row(2).status)
    }

    @Test
    fun jobEventsDriveTheRowsAndTheSummary() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()

        api.eventChannel.trySend(JobEvent.ItemStarted(1, "vid00000001", "001 Real - Name.mp3"))
        runCurrent()
        assertEquals(ItemStatus.Downloading(null), holder.row(1).status)
        assertEquals("001 Real - Name.mp3", holder.row(1).fileName)

        api.eventChannel.trySend(JobEvent.Progress(1, Stage.DOWNLOADING, 40.0))
        runCurrent()
        assertEquals(ItemStatus.Downloading(40.0), holder.row(1).status)

        api.eventChannel.trySend(JobEvent.Progress(1, Stage.CONVERTING))
        runCurrent()
        assertEquals(ItemStatus.Converting, holder.row(1).status)

        api.eventChannel.trySend(JobEvent.ItemDone(1, "001 Real - Name.mp3"))
        api.eventChannel.trySend(JobEvent.ItemFailed(3, "boom"))
        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)))
        api.eventChannel.close()
        runCurrent()

        val state = holder.state.value
        assertEquals(ItemStatus.Done, holder.row(1).status)
        assertEquals(ItemStatus.Failed("boom"), holder.row(3).status)
        assertEquals(Phase.FINISHED, state.phase)
        assertEquals(JobSummary(1, 0, 1), state.summary)
        assertEquals(JobStatus.COMPLETED, state.jobStatus)
        assertEquals(listOf(3), state.failedRanks)
    }

    @Test
    fun skippedItemsShowTheirReason() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()

        api.eventChannel.trySend(JobEvent.ItemSkipped(1, "이미 존재"))
        runCurrent()

        assertEquals(ItemStatus.Skipped("이미 존재"), holder.row(1).status)
    }

    @Test
    fun retryRunsOnlyTheFailedRanksAndKeepsTheOtherResults() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemDone(1, "001 A1 - T1.mp3"))
        api.eventChannel.trySend(JobEvent.ItemFailed(3, "boom"))
        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)))
        api.eventChannel.close()
        runCurrent()

        holder.retryFailed()
        runCurrent()

        assertEquals(listOf(3), api.jobRequests.last().ranks)
        assertEquals(Phase.RUNNING, holder.state.value.phase)
        assertEquals(ItemStatus.Done, holder.row(1).status)
        assertEquals(ItemStatus.Waiting, holder.row(3).status)
    }

    @Test
    fun retryOfASingleVideoRerunsTheWholeVideo() = runTest {
        val api = FakeApi().apply { resolveResponse = FakeApi.video() }
        val (_, holder) = resolved(api)
        holder.onSingleRank(7)
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemFailed(7, "boom"))
        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(0, 0, 1)))
        api.eventChannel.close()
        runCurrent()

        holder.retryFailed()
        runCurrent()

        assertNull(api.jobRequests.last().ranks)
        assertEquals(7, api.jobRequests.last().options.singleRank)
    }

    @Test
    fun cancelAsksTheServerToStopTheJob() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()

        holder.cancel()
        runCurrent()
        assertEquals(listOf("job-1"), api.cancelled)

        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.CANCELLED, JobSummary(0, 0, 0)))
        api.eventChannel.close()
        runCurrent()

        assertEquals(Phase.FINISHED, holder.state.value.phase)
        assertEquals(JobStatus.CANCELLED, holder.state.value.jobStatus)
        assertEquals(ItemStatus.Ready, holder.row(1).status)
    }

    @Test
    fun aStreamThatEndsWithoutJobDoneIsReportedAsALostConnection() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()

        api.eventChannel.close()
        runCurrent()

        assertEquals(Phase.FINISHED, holder.state.value.phase)
        assertEquals("서버와의 연결이 끊어졌습니다.", holder.state.value.error)
    }

    @Test
    fun aJobThatCannotStartReturnsToThePreview() = runTest {
        val (api, holder) = resolved()
        api.startError = ApiError("출력 폴더를 만들 수 없습니다")

        holder.startDownload()
        runCurrent()

        assertEquals(Phase.PREVIEW, holder.state.value.phase)
        assertEquals("출력 폴더를 만들 수 없습니다", holder.state.value.error)
        assertEquals(ItemStatus.Ready, holder.row(1).status)
    }

    @Test
    fun startingWithoutAnOutputFolderIsRefused() = runTest {
        val (api, holder) = resolved()
        holder.onOutputDir("  ")

        holder.startDownload()
        runCurrent()

        assertTrue(api.jobRequests.isEmpty())
        assertEquals("출력 폴더를 지정하세요.", holder.state.value.error)
    }

    @Test
    fun resetReturnsToAnEmptyScreenButKeepsSettings() = runTest {
        val (_, holder) = resolved()
        holder.onOutputDir("D:/Songs")

        holder.reset()

        val state = holder.state.value
        assertEquals(Phase.IDLE, state.phase)
        assertTrue(state.rows.isEmpty())
        assertEquals("D:/Songs", state.outputDir)
    }

    // ---- tools ----------------------------------------------------------------------------

    @Test
    fun refreshToolsLoadsTheStatus() = runTest {
        val (api, holder) = holder()

        holder.refreshTools()
        runCurrent()

        assertEquals(api.toolsStatus, holder.state.value.tools)
    }

    @Test
    fun installingYtDlpReportsTheResultAndRefreshesTheStatus() = runTest {
        val (api, holder) = holder()

        holder.installYtDlp()
        runCurrent()

        assertEquals(1, api.installCalls)
        assertEquals("installed", holder.state.value.toolMessage)
        assertEquals(false, holder.state.value.toolBusy)
        assertEquals(api.toolsStatus, holder.state.value.tools)
    }

    // ---- job endings and failures ---------------------------------------------------------

    private suspend fun TestScope.finishedWithOneFailure(): Pair<FakeApi, AppStateHolder> {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemDone(1, "001 A1 - T1.mp3"))
        api.eventChannel.trySend(JobEvent.ItemFailed(3, "boom"))
        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)))
        api.eventChannel.close()
        runCurrent()
        return api to holder
    }

    @Test
    fun rowsInFlightGoBackToReadyWhenACancelledJobEnds() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemStarted(1, "vid00000001", "001 A1 - T1.mp3"))
        api.eventChannel.trySend(JobEvent.Progress(1, Stage.DOWNLOADING, 40.0))
        runCurrent()
        assertEquals(ItemStatus.Downloading(40.0), holder.row(1).status)

        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.CANCELLED, JobSummary(0, 0, 0)))
        api.eventChannel.close()
        runCurrent()

        assertEquals(ItemStatus.Ready, holder.row(1).status)
        assertEquals(Phase.FINISHED, holder.state.value.phase)
        assertEquals(JobStatus.CANCELLED, holder.state.value.jobStatus)
    }

    @Test
    fun rowsInFlightGoBackToReadyWhenTheStreamBreaks() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemStarted(1, "vid00000001", "001 A1 - T1.mp3"))
        api.eventChannel.trySend(JobEvent.Progress(1, Stage.CONVERTING))
        runCurrent()
        assertEquals(ItemStatus.Converting, holder.row(1).status)

        api.eventChannel.close()
        runCurrent()

        assertEquals(Phase.FINISHED, holder.state.value.phase)
        assertEquals("\uc11c\ubc84\uc640\uc758 \uc5f0\uacb0\uc774 \ub04a\uc5b4\uc84c\uc2b5\ub2c8\ub2e4.", holder.state.value.error)
        assertEquals(ItemStatus.Ready, holder.row(1).status)
    }

    @Test
    fun aRawExceptionDuringResolveDoesNotLeaveTheScreenResolving() = runTest {
        val api = FakeApi().apply { resolveError = IllegalStateException("boom") }
        val (_, holder) = resolved(api)

        assertEquals(Phase.IDLE, holder.state.value.phase)
        assertTrue(holder.state.value.error.orEmpty().contains("boom"))
    }

    @Test
    fun aRawExceptionFromToolActionsClearsTheBusyFlag() = runTest {
        val api = FakeApi().apply { toolsError = IllegalStateException("boom") }
        val (_, holder) = holder(api)

        holder.installYtDlp()
        runCurrent()

        assertEquals(false, holder.state.value.toolBusy)
        assertTrue(holder.state.value.toolMessage.orEmpty().contains("boom"))

        holder.refreshTools()
        runCurrent()

        assertTrue(holder.state.value.error.orEmpty().contains("boom"))
    }

    @Test
    fun aFailedStartFromFinishedKeepsThePreviousResults() = runTest {
        val (api, holder) = finishedWithOneFailure()
        api.startError = ApiError("\ucd9c\ub825 \ud3f4\ub354\ub97c \ub9cc\ub4e4 \uc218 \uc5c6\uc2b5\ub2c8\ub2e4")

        holder.retryFailed()
        runCurrent()

        val state = holder.state.value
        assertEquals(Phase.FINISHED, state.phase)
        assertEquals(ItemStatus.Failed("boom"), holder.row(3).status)
        assertEquals(ItemStatus.Done, holder.row(1).status)
        assertEquals(JobSummary(1, 0, 1), state.summary)
        assertEquals(JobStatus.COMPLETED, state.jobStatus)
        assertEquals(listOf(3), state.failedRanks)
        assertEquals("\ucd9c\ub825 \ud3f4\ub354\ub97c \ub9cc\ub4e4 \uc218 \uc5c6\uc2b5\ub2c8\ub2e4", state.error)
    }

    @Test
    fun aRawExceptionWhileStartingARetryRestoresThePreviousScreen() = runTest {
        val (api, holder) = finishedWithOneFailure()
        api.startError = IllegalStateException("boom")

        holder.retryFailed()
        runCurrent()

        val state = holder.state.value
        assertEquals(Phase.FINISHED, state.phase)
        assertEquals(listOf(3), state.failedRanks)
        assertTrue(state.error.orEmpty().contains("boom"))
    }

    @Test
    fun aFailedSecondLookupKeepsThePreviewUsable() = runTest {
        val (api, holder) = resolved()
        val rowsBefore = holder.state.value.rows
        api.resolveError = ApiError("x")

        holder.onInput(playlistId)
        holder.resolve()
        runCurrent()

        val state = holder.state.value
        assertEquals(Phase.PREVIEW, state.phase)
        assertEquals(rowsBefore.map { it.item.rank }, state.rows.map { it.item.rank })
        assertEquals(rowsBefore.map { it.status }, state.rows.map { it.status })
        assertEquals("x", state.error)
    }

    @Test
    fun aFailedSecondLookupShowsTheOldRowsWithTheOptionAsItIsNow() = runTest {
        val (api, holder) = resolved()
        assertEquals("001 A1 - T1.mp3", holder.row(1).fileName)
        api.resolveGate = CompletableDeferred()
        api.resolveError = ApiError("x")
        holder.onInput(playlistId)
        holder.resolve()
        runCurrent()
        assertEquals(Phase.RESOLVING, holder.state.value.phase)

        holder.onIncludeRank(false) // while the lookup is held: nothing is rewritten yet
        api.resolveGate?.complete(Unit)
        runCurrent()

        val state = holder.state.value
        assertEquals(Phase.PREVIEW, state.phase)
        assertEquals("x", state.error)
        assertEquals(false, state.includeRank)
        assertEquals("A1 - T1.mp3", holder.row(1).fileName)
        assertEquals("A3 - T3.mp3", holder.row(3).fileName)
        assertEquals(listOf(1, 2, 3), state.rows.map { it.item.rank })
        assertEquals(ItemStatus.Ready, holder.row(1).status)
    }

    @Test
    fun aFailedLookupAfterAFinishedJobKeepsTheResults() = runTest {
        val (api, holder) = resolved()
        holder.startDownload()
        runCurrent()
        api.eventChannel.trySend(JobEvent.ItemDone(1, "001 A1 - T1.mp3"))
        api.eventChannel.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)))
        api.eventChannel.close()
        runCurrent()
        api.resolveError = ApiError("x")

        holder.resolve()
        runCurrent()

        val state = holder.state.value
        assertEquals(Phase.FINISHED, state.phase)
        assertEquals(JobSummary(1, 0, 0), state.summary)
        assertEquals(ItemStatus.Done, holder.row(1).status)
        assertEquals("x", state.error)
    }
}
