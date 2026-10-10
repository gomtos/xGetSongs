package com.xgetsongs.engine.job

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.JobHandle
import com.xgetsongs.engine.lyrics.LyricsProvider
import com.xgetsongs.engine.lyrics.LyricsQuery
import com.xgetsongs.engine.lyrics.NoLyricsProvider
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.output.OutputSink
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.testutil.FakeLyricsProvider
import com.xgetsongs.engine.testutil.FakeProcessRunner
import com.xgetsongs.engine.testutil.Id3v2Tag
import com.xgetsongs.engine.testutil.TEST_TOOLS
import com.xgetsongs.engine.testutil.downloadRunner
import com.xgetsongs.engine.testutil.endsWithFakeAudio
import com.xgetsongs.engine.testutil.ffmetadataTextOf
import com.xgetsongs.engine.testutil.ffmpegCommands
import com.xgetsongs.engine.testutil.isFfmpegCommand
import com.xgetsongs.engine.testutil.outputDirOf
import com.xgetsongs.engine.testutil.toolsOf
import com.xgetsongs.engine.testutil.writeFakeCover
import com.xgetsongs.engine.testutil.writeFakeInfo
import com.xgetsongs.engine.testutil.writeFakeMp3
import com.xgetsongs.engine.testutil.writeFakeTagged
import com.xgetsongs.engine.testutil.ytDlpCommands
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.ytdlp.VideoMeta
import com.xgetsongs.engine.ytdlp.VideoMetadataSource
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.LyricsOutcome
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.filename.FilenameFormatter
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.JsonPrimitive
import java.io.IOException
import java.nio.file.AccessDeniedException
import java.nio.file.FileAlreadyExistsException
import java.nio.file.FileSystemException
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class DefaultDownloadServiceTest {
    private val root: Path = Files.createTempDirectory("xgs-service")
    private val outDir = root.resolve("out")
    private val tempRoot = root.resolve("work")

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private fun item(rank: Int, artist: String = "A$rank", track: String = "T$rank", lowConfidence: Boolean = false) =
        ResolvedItem(
            rank = rank,
            videoId = "vid%08d".format(rank),
            title = "$artist - $track",
            channel = "ch",
            artist = artist,
            track = track,
            lowConfidence = lowConfidence,
            expectedFileName = FilenameFormatter.format(rank, artist, track),
        )

    private fun TestScope.service(
        runner: ProcessRunner,
        tools: ToolPathProvider = toolsOf(),
        metadata: VideoMetadataSource = VideoMetadataSource { null },
        workDir: Path = tempRoot,
        lyrics: LyricsProvider = NoLyricsProvider,
    ) = DefaultDownloadService(ItemDownloader(runner, tools, metadata, lyrics), workDir, this)

    private fun request(
        vararg items: ResolvedItem,
        overwrite: Boolean = false,
        concurrency: Int = 1,
        sink: OutputSink = LocalFolderSink(outDir),
        album: String? = null,
        includeRank: Boolean = true,
        searchLyricsOnline: Boolean = false,
        albumOverride: String? = null,
    ) = DownloadRequest(items.toList(), sink, overwrite, concurrency, album, includeRank, searchLyricsOnline, albumOverride)

    private suspend fun JobHandle.collect(): List<JobEvent> = events.receiveAsFlow().toList()

    private val succeeding = downloadRunner { command, onStdout, _ ->
        onStdout("XGSP|downloading|50|100|NA")
        onStdout("XGSPP|started|ExtractAudio")
        writeFakeMp3(command)
        0
    }

    private fun failingWith(vararg stderr: String) = FakeProcessRunner { _, _, onStderr ->
        stderr.forEach(onStderr)
        1
    }

    private fun done(events: List<JobEvent>) = events.last() as JobEvent.JobDone

    @Test
    fun downloadsEveryItemAndMovesFilesIntoTheSink() = runTest {
        val handle = service(succeeding).start(request(item(1), item(2)))

        val events = handle.collect()

        assertTrue(Files.exists(outDir.resolve("001 A1 - T1.mp3")))
        assertTrue(Files.exists(outDir.resolve("002 A2 - T2.mp3")))
        assertEquals(setOf(1, 2), events.filterIsInstance<JobEvent.ItemStarted>().map { it.rank }.toSet())
        assertEquals(setOf("001 A1 - T1.mp3", "002 A2 - T2.mp3"), events.filterIsInstance<JobEvent.ItemDone>().map { it.fileName }.toSet())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(2, 0, 0)), done(events))
    }

    @Test
    fun reportsDownloadAndConvertingProgress() = runTest {
        val events = service(succeeding).start(request(item(1))).collect()

        val progress = events.filterIsInstance<JobEvent.Progress>()
        assertEquals(JobEvent.Progress(1, Stage.DOWNLOADING, 50.0), progress.first())
        assertEquals(JobEvent.Progress(1, Stage.FINISHING, null), progress.last())
    }

    @Test
    fun progressEventsAreThrottledToWholePercents() = runTest {
        val noisy = downloadRunner { command, onStdout, _ ->
            repeat(1000) { onStdout("XGSP|downloading|$it|1000|NA") }
            writeFakeMp3(command)
            0
        }

        val events = service(noisy).start(request(item(1))).collect()

        assertEquals(100, events.filterIsInstance<JobEvent.Progress>().size)
    }

    @Test
    fun existingFilesAreSkippedUnlessOverwriteIsOn() = runTest {
        Files.createDirectories(outDir)
        Files.writeString(outDir.resolve("001 A1 - T1.mp3"), "old")

        val skipped = service(succeeding).start(request(item(1))).collect()
        assertEquals(listOf(JobEvent.ItemSkipped(1, "이미 존재")), skipped.filterIsInstance<JobEvent.ItemSkipped>())
        assertEquals("old", Files.readString(outDir.resolve("001 A1 - T1.mp3")))
        assertEquals(1, done(skipped).summary.skipped)

        service(succeeding).start(request(item(1), overwrite = true)).collect()
        assertTrue(endsWithFakeAudio(outDir.resolve("001 A1 - T1.mp3")), "the old file must be replaced by the tagged one")
    }

    @Test
    fun transientFailuresAreRetriedWithBackoff() = runTest {
        val calls = AtomicInteger()
        val flaky = downloadRunner { command, _, onStderr ->
            if (calls.incrementAndGet() == 1) {
                onStderr("ERROR: unable to download video data: HTTP Error 503: Service Unavailable")
                1
            } else {
                writeFakeMp3(command)
                0
            }
        }

        val events = service(flaky).start(request(item(1))).collect()

        assertEquals(2, flaky.ytDlpCommands.size)
        assertEquals(1, flaky.ffmpegCommands.size, "only the successful attempt is tagged")
        assertTrue(events.none { it is JobEvent.ItemFailed })
        assertEquals(JobSummary(1, 0, 0), done(events).summary)
    }

    @Test
    fun givesUpAfterTwoRetries() = runTest {
        val runner = failingWith("ERROR: HTTP Error 429: Too Many Requests")

        val events = service(runner).start(request(item(1))).collect()

        assertEquals(3, runner.commands.size)
        assertEquals(1, events.filterIsInstance<JobEvent.ItemFailed>().size)
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(0, 0, 1)), done(events))
    }

    @Test
    fun unavailableVideosAreSkippedWithoutRetry() = runTest {
        val runner = failingWith("ERROR: [youtube] x: Private video. Sign in if you've been granted access")

        val events = service(runner).start(request(item(1))).collect()

        assertEquals(1, runner.commands.size)
        assertEquals(listOf(JobEvent.ItemSkipped(1, "비공개 영상")), events.filterIsInstance<JobEvent.ItemSkipped>())
        assertEquals(JobSummary(0, 1, 0), done(events).summary)
    }

    @Test
    fun otherFailuresDoNotStopTheRemainingItems() = runTest {
        val runner = downloadRunner { command, _, onStderr ->
            if (command.any { it.contains("vid00000001") }) {
                onStderr("ERROR: something odd")
                1
            } else {
                writeFakeMp3(command)
                0
            }
        }

        val events = service(runner).start(request(item(1), item(2))).collect()

        assertEquals(listOf(1), events.filterIsInstance<JobEvent.ItemFailed>().map { it.rank })
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)), done(events))
    }

    @Test
    fun fatalErrorAbortsTheWholeJob() = runTest {
        val runner = failingWith("OSError: [Errno 28] No space left on device")

        val events = service(runner).start(request(item(1), item(2), item(3))).collect()

        assertEquals(JobStatus.FAILED, done(events).status)
        assertEquals(listOf(1), events.filterIsInstance<JobEvent.ItemStarted>().map { it.rank })
        assertEquals(1, runner.commands.size)
    }

    @Test
    fun missingYtDlpAbortsTheJob() = runTest {
        val events = service(succeeding, tools = toolsOf(TEST_TOOLS.copy(ytDlp = null))).start(request(item(1))).collect()

        assertEquals(JobStatus.FAILED, done(events).status)
        assertTrue(events.filterIsInstance<JobEvent.ItemFailed>().single().message.contains("yt-dlp"))
    }

    @Test
    fun cancellingStopsRunningDownloadsAndReportsCancelled() = runTest {
        val started = CompletableDeferred<Unit>()
        val hanging = FakeProcessRunner { _, _, _ ->
            started.complete(Unit)
            awaitCancellation()
        }
        val handle = service(hanging).start(request(item(1)))
        started.await()

        handle.cancel()
        val events = handle.collect()

        assertEquals(JobStatus.CANCELLED, done(events).status)
        assertEquals(1, hanging.commands.size)
    }

    @Test
    fun workDirectoriesAreRemovedAfterTheJob() = runTest {
        service(succeeding).start(request(item(1), item(2))).collect()

        Files.list(tempRoot).use { assertEquals(0, it.count()) }
    }

    @Test
    fun cancellingAlsoRemovesWorkDirectories() = runTest {
        val started = CompletableDeferred<Unit>()
        val hanging = FakeProcessRunner { command, _, _ ->
            writeFakeMp3(command)
            started.complete(Unit)
            awaitCancellation()
        }
        val handle = service(hanging).start(request(item(1)))
        started.await()

        handle.cancel()
        handle.collect()

        Files.list(tempRoot).use { assertEquals(0, it.count()) }
    }

    /** Waits in real time until [count] downloads have started, then gives an unwanted extra one time to show up. */
    private suspend fun awaitStarted(runner: FakeProcessRunner, count: Int) = withContext(Dispatchers.Default) {
        withTimeout(10_000) { while (runner.commands.size < count) delay(10) }
        delay(200)
    }

    @Test
    fun concurrencyIsLimitedToTheRequestedNumber() = runTest {
        val gate = CompletableDeferred<Unit>()
        val held = downloadRunner { command, _, _ ->
            gate.await()
            writeFakeMp3(command)
            0
        }
        val handle = service(held).start(request(*(1..6).map { item(it) }.toTypedArray(), concurrency = 2))

        awaitStarted(held, 2)
        assertEquals(2, held.commands.size, "the other four items must wait for a free slot")
        gate.complete(Unit)
        handle.collect()

        assertEquals(2, held.maxActive.get())
        assertEquals(6, held.ytDlpCommands.size)
        assertEquals(6, held.ffmpegCommands.size)
    }

    @Test
    fun theServiceSetsNoUpperLimitOfItsOwn() = runTest {
        val gate = CompletableDeferred<Unit>()
        val held = downloadRunner { command, _, _ ->
            gate.await()
            writeFakeMp3(command)
            0
        }
        val handle = service(held).start(request(*(1..10).map { item(it) }.toTypedArray(), concurrency = 8))

        awaitStarted(held, 8)
        assertEquals(8, held.commands.size, "eight parallel downloads were asked for, and the other two wait")
        gate.complete(Unit)
        handle.collect()

        assertEquals(8, held.maxActive.get())
        assertEquals(10, held.ytDlpCommands.size)
    }

    @Test
    fun aConcurrencyBelowOneRunsOneAtATime() = runTest {
        val events = service(succeeding).start(request(item(1), item(2), item(3), concurrency = 0)).collect()

        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(3, 0, 0)), done(events))
    }

    @Test
    fun lowConfidenceItemsAreRenamedFromFullMetadata() = runTest {
        val metadata = VideoMetadataSource { VideoMeta("Dynamite", "BTS - Topic", "BTS", "Dynamite") }
        val lowConfidence = item(1, artist = "BTS - Topic", track = "Dynamite", lowConfidence = true)

        val events = service(succeeding, metadata = metadata).start(request(lowConfidence)).collect()

        assertEquals("001 BTS - Dynamite.mp3", events.filterIsInstance<JobEvent.ItemStarted>().single().fileName)
        assertTrue(Files.exists(outDir.resolve("001 BTS - Dynamite.mp3")))
    }

    @Test
    fun sinkFailuresAreReportedPerItem() = runTest {
        val brokenSink = object : OutputSink {
            override suspend fun exists(fileName: String) = false
            override suspend fun put(fileName: String, source: Path, overwrite: Boolean) {
                throw IOException("disk on fire")
            }
        }

        val events = service(succeeding).start(request(item(1), sink = brokenSink)).collect()

        val failed = events.filterIsInstance<JobEvent.ItemFailed>().single()
        assertTrue(failed.message.contains("disk on fire"))
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(0, 0, 1)), done(events))
    }

    private fun failingSink(error: IOException) = object : OutputSink {
        override suspend fun exists(fileName: String) = false
        override suspend fun put(fileName: String, source: Path, overwrite: Boolean) {
            throw error
        }
    }

    @Test
    fun cancellingRightAfterStartStillEndsWithJobDoneCancelled() = runTest {
        val handle = service(succeeding).start(request(item(1)))
        handle.cancel()

        val events = handle.collect()

        assertEquals(JobStatus.CANCELLED, done(events).status)
        assertFalse(Files.exists(tempRoot), "no work folder is created when the job is cancelled before it starts")
    }

    @Test
    fun anUnusableWorkFolderEndsTheJobWithJobDoneFailed() = runTest {
        val blocker = Files.writeString(root.resolve("blocker"), "a file, not a folder")

        val events = service(succeeding, workDir = blocker.resolve("work")).start(request(item(1))).collect()

        assertEquals(JobEvent.JobDone(JobStatus.FAILED, JobSummary(0, 0, 0)), done(events))
        assertEquals(0, succeeding.commands.size)
    }

    @Test
    fun sameVideoTwiceUsesSeparateWorkFolders() = runTest {
        val first = item(1)
        val second = item(2).copy(videoId = first.videoId)

        val events = service(succeeding).start(request(first, second, concurrency = 2)).collect()

        assertEquals(setOf(1, 2), events.filterIsInstance<JobEvent.ItemDone>().map { it.rank }.toSet())
        assertTrue(Files.exists(outDir.resolve("001 A1 - T1.mp3")))
        assertTrue(Files.exists(outDir.resolve("002 A2 - T2.mp3")))
        assertEquals(2, succeeding.ytDlpCommands.map { outputDirOf(it) }.distinct().size)
    }

    @Test
    fun accessDeniedFromTheSinkAbortsTheJob() = runTest {
        val sink = failingSink(AccessDeniedException("x"))

        val events = service(succeeding).start(request(item(1), item(2), item(3), sink = sink)).collect()

        assertEquals(JobStatus.FAILED, done(events).status)
        assertEquals(1, events.filterIsInstance<JobEvent.ItemFailed>().size)
        assertEquals(1, succeeding.ytDlpCommands.size)
    }

    @Test
    fun diskFullFromTheSinkAbortsTheJob() = runTest {
        val sink = failingSink(IOException("No space left on device"))

        val events = service(succeeding).start(request(item(1), item(2), item(3), sink = sink)).collect()

        assertEquals(JobStatus.FAILED, done(events).status)
        assertEquals(1, events.filterIsInstance<JobEvent.ItemFailed>().size)
        assertEquals(1, succeeding.ytDlpCommands.size)
    }

    @Test
    fun jobDoneIsSentExactlyOnce() = runTest {
        val completed = service(succeeding).start(request(item(1), item(2))).collect()
        // A second sink: the first job already put "001 A1 - T1.mp3" into outDir, so the items would be skipped there.
        val aborted = service(failingWith("OSError: [Errno 28] No space left on device"))
            .start(request(item(1), item(2), sink = LocalFolderSink(root.resolve("out-aborted"))))
            .collect()

        assertEquals(JobStatus.COMPLETED, done(completed).status)
        assertEquals(JobStatus.FAILED, done(aborted).status)
        for (events in listOf(completed, aborted)) {
            assertEquals(1, events.count { it is JobEvent.JobDone })
            assertTrue(events.last() is JobEvent.JobDone)
        }
    }

    @Test
    fun aFileNamedLikeADiskFullErrorDoesNotAbortTheJob() = runTest {
        // FileAlreadyExistsException's message is just the path: a file name containing disk-full words is not a full disk.
        val sink = failingSink(FileAlreadyExistsException("C:/out/001 A - Disk Full No Space Left.mp3"))

        val events = service(succeeding).start(request(item(1), item(2), sink = sink)).collect()

        assertEquals(2, events.filterIsInstance<JobEvent.ItemFailed>().size)
        assertEquals(JobStatus.COMPLETED, done(events).status)
        assertEquals(2, succeeding.ytDlpCommands.size)
    }

    @Test
    fun aRealDiskFullReasonInsideAFileSystemExceptionAbortsTheJob() = runTest {
        val sink = failingSink(FileSystemException("C:/out/x.mp3", null, "No space left on device"))

        val events = service(succeeding).start(request(item(1), item(2), item(3), sink = sink)).collect()

        assertEquals(JobStatus.FAILED, done(events).status)
        assertEquals(1, succeeding.ytDlpCommands.size)
    }

    // ---- ID3 tagging ----

    /** A runner whose yt-dlp writes the mp3 (and a cover when [withCover]) and whose ffmpeg records its ffmetadata. */
    private fun taggingRunner(metadataTexts: MutableList<String>, withCover: Boolean = false) =
        FakeProcessRunner { command, _, _ ->
            if (isFfmpegCommand(command)) {
                metadataTexts += ffmetadataTextOf(command)
                writeFakeTagged(command)
            } else {
                writeFakeMp3(command)
                if (withCover) writeFakeCover(command)
            }
            0
        }

    @Test
    fun taggingRunsOncePerItemRightAfterItsDownload() = runTest {
        val runner = taggingRunner(mutableListOf())

        service(runner).start(request(item(1), item(2))).collect()

        assertEquals(listOf(false, true, false, true), runner.commands.map { isFfmpegCommand(it) })
        assertEquals(2, runner.ffmpegCommands.size)
        val tagged = runner.ffmpegCommands.map { command -> Path.of(command[command.indexOf("-i") + 1]).fileName.toString() }
        assertEquals(listOf("vid00000001.mp3", "vid00000002.mp3"), tagged)
    }

    @Test
    fun theTaggedFileWithItsCommentFrameIsWhatReachesTheSink() = runTest {
        service(succeeding).start(request(item(1))).collect()

        val delivered = outDir.resolve("001 A1 - T1.mp3")
        assertTrue(endsWithFakeAudio(delivered))
        val tag = Id3v2Tag.read(delivered)
        assertEquals(listOf("TIT2", "COMM"), tag.ids)
        assertEquals("https://www.youtube.com/watch?v=vid00000001", tag.comments().single().text)
    }

    @Test
    fun thumbnailConversionBeforeTheDownloadDoesNotShowAsConverting() = runTest {
        val runner = downloadRunner { command, onStdout, _ ->
            // yt-dlp converts the thumbnail first, then downloads, then extracts the audio.
            onStdout("XGSPP|started|ThumbnailsConvertor")
            onStdout("XGSPP|finished|ThumbnailsConvertor")
            onStdout("XGSP|downloading|50|100|NA")
            onStdout("XGSPP|started|ExtractAudio")
            writeFakeMp3(command)
            0
        }

        val events = service(runner).start(request(item(1))).collect()

        assertEquals(
            listOf(
                JobEvent.Progress(1, Stage.DOWNLOADING, 50.0),
                JobEvent.Progress(1, Stage.FINISHING, null),
            ),
            events.filterIsInstance<JobEvent.Progress>(),
        )
    }

    @Test
    fun theMetadataFileCarriesTheParsedArtistTitleTrackAndAlbum() = runTest {
        val texts = mutableListOf<String>()
        val runner = taggingRunner(texts)

        service(runner).start(request(item(5, artist = "Artist Five", track = "Song Five"), album = "My List")).collect()

        assertEquals(
            listOf(
                ";FFMETADATA1",
                "title=Song Five",
                "artist=Artist Five",
                "album_artist=Various Artists",
                "album=My List",
                "track=5",
            ),
            texts.single().removeSuffix("\n").split("\n"),
        )
    }

    @Test
    fun theUrlReachesTheFileAsACommFrameAndNotThroughTheMetadataFile() = runTest {
        val texts = mutableListOf<String>()
        val runner = taggingRunner(texts)

        service(runner).start(request(item(5))).collect()

        assertTrue("youtube" !in texts.single(), "ffmpeg would store the comment as TXXX: ${texts.single()}")
        val comment = Id3v2Tag.read(outDir.resolve("005 A5 - T5.mp3")).comments().single()
        assertEquals("https://www.youtube.com/watch?v=vid00000005", comment.text)
    }

    @Test
    fun noAlbumLineIsWrittenWhenTheRequestHasNoAlbum() = runTest {
        val texts = mutableListOf<String>()

        service(taggingRunner(texts)).start(request(item(1))).collect()

        val lines = texts.single().lines()
        assertTrue(lines.none { it.startsWith("album=") }, texts.single())
        assertTrue("track=1" in lines)
    }

    @Test
    fun tagsKeepTheOriginalTextEvenWhenTheFileNameIsSanitized() = runTest {
        val texts = mutableListOf<String>()

        val events = service(taggingRunner(texts))
            .start(request(item(1, artist = "AC/DC", track = "Who Made Who?")))
            .collect()

        val lines = texts.single().lines()
        assertTrue("title=Who Made Who?" in lines, texts.single())
        assertTrue("artist=AC/DC" in lines, texts.single())
        val fileName = events.filterIsInstance<JobEvent.ItemStarted>().single().fileName
        assertEquals(FilenameFormatter.format(1, "AC/DC", "Who Made Who?"), fileName)
        assertFalse('/' in fileName || '?' in fileName, fileName)
    }

    @Test
    fun lowConfidenceItemsAreTaggedWithTheSecondParse() = runTest {
        val texts = mutableListOf<String>()
        val metadata = VideoMetadataSource { VideoMeta("Dynamite", "BTS - Topic", "BTS", "Dynamite") }
        val lowConfidence = item(1, artist = "BTS - Topic", track = "Dynamite (Official)", lowConfidence = true)

        service(taggingRunner(texts), metadata = metadata).start(request(lowConfidence)).collect()

        val lines = texts.single().lines()
        assertTrue("title=Dynamite" in lines, texts.single())
        assertTrue("artist=BTS" in lines, texts.single())
        assertTrue("album_artist=Various Artists" in lines, texts.single())
    }

    @Test
    fun aThumbnailLeftByYtDlpBecomesTheCover() = runTest {
        val runner = taggingRunner(mutableListOf(), withCover = true)

        service(runner).start(request(item(1))).collect()

        val command = runner.ffmpegCommands.single()
        val inputs = command.indices.filter { command[it] == "-i" }.map { Path.of(command[it + 1]).fileName.toString() }
        assertEquals(listOf("vid00000001.mp3", "vid00000001.jpg", "vid00000001.ffmeta"), inputs)
        assertTrue("attached_pic" in command)
    }

    @Test
    fun withoutAThumbnailTheTagsAreWrittenWithoutACover() = runTest {
        val runner = taggingRunner(mutableListOf(), withCover = false)

        val events = service(runner).start(request(item(1))).collect()

        val command = runner.ffmpegCommands.single()
        assertEquals(2, command.count { it == "-i" })
        assertFalse("-vf" in command)
        assertEquals(JobSummary(1, 0, 0), done(events).summary)
    }

    @Test
    fun aTaggingFailureFailsOnlyThatItem() = runTest {
        val runner = FakeProcessRunner { command, _, onStderr ->
            when {
                !isFfmpegCommand(command) -> {
                    writeFakeMp3(command)
                    0
                }
                command.any { it.contains("vid00000001") } -> {
                    onStderr("Error while writing the tag")
                    1
                }
                else -> {
                    writeFakeTagged(command)
                    0
                }
            }
        }

        val events = service(runner).start(request(item(1), item(2))).collect()

        assertEquals(
            listOf(JobEvent.ItemFailed(1, "ID3 태그를 쓰지 못했습니다: Error while writing the tag")),
            events.filterIsInstance<JobEvent.ItemFailed>(),
        )
        assertEquals(listOf(2), events.filterIsInstance<JobEvent.ItemDone>().map { it.rank })
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 1)), done(events))
        assertFalse(Files.exists(outDir.resolve("001 A1 - T1.mp3")), "an untagged file must not reach the sink")
        assertTrue(Files.exists(outDir.resolve("002 A2 - T2.mp3")))
        assertEquals(2, runner.ytDlpCommands.size, "a tagging failure is not retried")
    }

    @Test
    fun missingFfmpegAbortsTheJobBeforeAnyDownloadStarts() = runTest {
        val runner = taggingRunner(mutableListOf())

        val events = service(runner, tools = toolsOf(TEST_TOOLS.copy(ffmpeg = null))).start(request(item(1), item(2), item(3))).collect()

        assertEquals(JobStatus.FAILED, done(events).status)
        assertEquals(
            listOf(JobEvent.ItemFailed(1, "ffmpeg를 찾을 수 없습니다.")),
            events.filterIsInstance<JobEvent.ItemFailed>(),
            "like a missing yt-dlp: one failed item, the others are never started",
        )
        assertEquals(emptyList<List<String>>(), runner.commands.toList(), "no process may be started without ffmpeg")
        assertEquals(JobSummary(0, 0, 1), done(events).summary)
    }

    // ---- the album tag: the request's album (the playlist title), else the video's own album ----

    /** Like [taggingRunner], and yt-dlp also writes the video's info file holding [infoJson] (no file when null). */
    private fun infoRunner(metadataTexts: MutableList<String>, infoJson: String?) =
        FakeProcessRunner { command, _, _ ->
            if (isFfmpegCommand(command)) {
                metadataTexts += ffmetadataTextOf(command)
                writeFakeTagged(command)
            } else {
                writeFakeMp3(command)
                if (infoJson != null) writeFakeInfo(command, infoJson)
            }
            0
        }

    private fun albumLines(metadataText: String) = metadataText.lines().filter { it.startsWith("album=") }

    @Test
    fun thePlaylistTitleBeatsTheVideosOwnAlbum() = runTest {
        val texts = mutableListOf<String>()
        val runner = infoRunner(texts, """{"id":"vid00000001","album":"Palette"}""")

        val events = service(runner).start(request(item(1), album = "My List")).collect()

        assertEquals(listOf("album=My List"), albumLines(texts.single()), texts.single())
        assertTrue("album_artist=Various Artists" in texts.single().lines(), "the album artist is always Various Artists: ${texts.single()}")
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)), done(events))
        assertEquals(listOf("001 A1 - T1.mp3"), filesIn(outDir), "the info file stays in the work folder")
    }

    @Test
    fun withoutAnInfoFileTheAlbumIsThePlaylistTitle() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, infoJson = null)).start(request(item(1), album = "My List")).collect()

        assertEquals(listOf("album=My List"), albumLines(texts.single()), texts.single())
    }

    @Test
    fun anInfoFileWithoutAnAlbumStillGetsThePlaylistTitle() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, """{"id":"vid00000001","title":"x","artist":"A1"}""")).start(request(item(1), album = "My List")).collect()

        assertEquals(listOf("album=My List"), albumLines(texts.single()), texts.single())
    }

    @Test
    fun aBlankInfoAlbumStillGetsThePlaylistTitle() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, """{"album":"   "}""")).start(request(item(1), album = "My List")).collect()

        assertEquals(listOf("album=My List"), albumLines(texts.single()), texts.single())
    }

    @Test
    fun aBlankPlaylistTitleLeavesTheVideosOwnAlbum() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, """{"album":"Palette"}""")).start(request(item(1), album = "  ")).collect()

        assertEquals(listOf("album=Palette"), albumLines(texts.single()), texts.single())
    }

    @Test
    fun aSingleVideoWithAnInfoAlbumGetsThatAlbum() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, """{"album":"Palette"}""")).start(request(item(1), album = null)).collect()

        assertEquals(listOf("album=Palette"), albumLines(texts.single()), texts.single())
    }

    @Test
    fun aSingleVideoWithoutAnyAlbumGetsNoAlbumLine() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, """{"id":"vid00000001"}""")).start(request(item(1), album = null)).collect()

        assertEquals(emptyList<String>(), albumLines(texts.single()), texts.single())
        assertTrue("track=1" in texts.single().lines(), texts.single())
    }

    @Test
    fun anAlbumNameTheUserTypedBeatsTheVideosOwnAlbumAndThePlaylistTitle() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, """{"album":"Palette"}""")).start(request(item(1), album = "My List", albumOverride = "내 앨범")).collect()

        assertEquals(listOf("album=내 앨범"), albumLines(texts.single()), texts.single())
        assertTrue("album_artist=Various Artists" in texts.single().lines(), "the album artist is always Various Artists: ${texts.single()}")
    }

    @Test
    fun anAlbumNameTheUserTypedGivesASingleVideoWithoutAnyAlbumAnAlbumLine() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, """{"id":"vid00000001"}""")).start(request(item(1), album = null, albumOverride = "내 앨범")).collect()

        assertEquals(listOf("album=내 앨범"), albumLines(texts.single()), texts.single())
    }

    @Test
    fun aBlankAlbumNameTheUserTypedChangesNothing() = runTest {
        val texts = mutableListOf<String>()

        service(infoRunner(texts, """{"album":"Palette"}""")).start(request(item(1), album = "My List", albumOverride = "   ")).collect()

        assertEquals(listOf("album=My List"), albumLines(texts.single()), texts.single())
    }

    @Test
    fun aCorruptInfoFileFallsBackAndTheItemStillSucceeds() = runTest {
        val texts = mutableListOf<String>()

        val events = service(infoRunner(texts, """{"album":"Pal""")).start(request(item(1), album = "My List")).collect()

        assertEquals(listOf("album=My List"), albumLines(texts.single()), texts.single())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)), done(events))
        assertTrue(events.none { it is JobEvent.ItemFailed }, events.toString())
        assertTrue(Files.exists(outDir.resolve("001 A1 - T1.mp3")))
    }

    // ---- lyrics: the description in the info file ----

    /** An info file whose `description` is [description] (JSON-escaped here, so the tests can use plain text). */
    private fun infoWithDescription(description: String, album: String? = null): String {
        val albumPart = if (album != null) "\"album\":${JsonPrimitive(album)}," else ""
        return "{\"id\":\"vid00000001\",$albumPart\"description\":${JsonPrimitive(description)}}"
    }

    /** A made-up description: a heading, separators around the block, then links, a copyright line and hashtags. */
    private val describedLyrics = "Example Artist - Example Song\n=======\n[Lyrics]\n=======\n첫 번째 줄\n두 번째 줄\n\nLa la la\n=======\n" +
        "Instagram: https://example.invalid/artist\n© 2024 Example Label\n#example"

    /** Downloads item 1 with the info file [infoJson] and returns the tag of the file that reached the sink. */
    private suspend fun TestScope.deliveredTag(
        infoJson: String?,
        texts: MutableList<String> = mutableListOf(),
        album: String? = "My List",
    ): Id3v2Tag {
        val events = service(infoRunner(texts, infoJson)).start(request(item(1), album = album)).collect()

        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)), done(events), events.toString())
        assertTrue(events.none { it is JobEvent.ItemFailed }, events.toString())
        return Id3v2Tag.read(outDir.resolve("001 A1 - T1.mp3"))
    }

    @Test
    fun lyricsFromTheInfoDescriptionReachTheFileAsAUsltFrameAndNotTheMetadataFile() = runTest {
        val texts = mutableListOf<String>()

        val tag = deliveredTag(infoWithDescription(describedLyrics), texts)

        assertEquals(listOf("TIT2", "COMM", "USLT"), tag.ids)
        assertEquals(listOf(Id3v2Tag.Lyrics(3, "kor", "", "첫 번째 줄\r\n두 번째 줄\r\n\r\nLa la la")), tag.lyrics())
        assertTrue("첫 번째" !in texts.single() && "lyrics" !in texts.single(), "ffmpeg would store the lyrics as a TXXX frame: ${texts.single()}")
        assertEquals("https://www.youtube.com/watch?v=vid00000001", tag.comments().single().text, "the comment is unchanged")
    }

    @Test
    fun aSingleVideosAlbumAndLyricsComeFromTheSameInfoFile() = runTest {
        val texts = mutableListOf<String>()

        val tag = deliveredTag(infoWithDescription("Heading\nLyrics:\nLine one\nLine two\nLine three", album = "Palette"), texts, album = null)

        assertEquals(listOf("album=Palette"), albumLines(texts.single()), texts.single())
        assertEquals(listOf(Id3v2Tag.Lyrics(3, "eng", "", "Line one\r\nLine two\r\nLine three")), tag.lyrics())
    }

    @Test
    fun withoutAnInfoFileThereIsNoLyricsFrame() = runTest {
        assertEquals(listOf("TIT2", "COMM"), deliveredTag(infoJson = null).ids)
    }

    @Test
    fun anInfoFileWithoutADescriptionGivesNoLyricsFrame() = runTest {
        assertEquals(listOf("TIT2", "COMM"), deliveredTag("""{"id":"vid00000001","album":"Palette"}""").ids)
    }

    @Test
    fun aDescriptionThatIsNotAStringGivesNoLyricsFrame() = runTest {
        assertEquals(listOf("TIT2", "COMM"), deliveredTag("""{"id":"vid00000001","description":["[Lyrics]","a","b","c"]}""").ids)
    }

    @Test
    fun aDescriptionWithoutALyricsSectionGivesNoLyricsFrame() = runTest {
        val description = "Example Artist - Example Song\nListen everywhere\nhttps://example.invalid\n\n#example"

        assertEquals(listOf("TIT2", "COMM"), deliveredTag(infoWithDescription(description)).ids)
    }

    @Test
    fun aLyricsSectionWithFewerThanThreeLinesGivesNoLyricsFrame() = runTest {
        val description = "[Lyrics]\nLine one\nLine two\n=======\nhttps://example.invalid"

        assertEquals(listOf("TIT2", "COMM"), deliveredTag(infoWithDescription(description)).ids)
    }

    @Test
    fun aCorruptInfoFileGivesNoLyricsFrameAndTheItemStillSucceeds() = runTest {
        // Cut off inside the description: the lyrics section itself is complete, but the file is not valid JSON.
        val tag = deliveredTag("""{"description":"[Lyrics]\nLine one\nLine two\nLine three""")

        assertEquals(listOf("TIT2", "COMM"), tag.ids)
    }

    // ---- lyrics: the internet lookup when the description has none ----

    /** A made-up answer of the lookup: Korean and English lines, so the language of the frame is `kor`. */
    private val onlineLyrics = "온라인 첫 줄\nLa la online\n온라인 셋째 줄"
    private val onlineLyricsInTheFile = "온라인 첫 줄\r\nLa la online\r\n온라인 셋째 줄"

    /** A description that has no lyrics section: a title, a link and a hashtag. */
    private val descriptionWithoutLyrics = "Example Artist - Example Song\nListen everywhere\nhttps://example.invalid\n\n#example"

    /**
     * Downloads [item] with [provider], the info file [infoJson] and the lookup switched [online]; the item must succeed.
     * Returns the tag of the file that reached the sink.
     */
    private suspend fun TestScope.deliveredWith(
        provider: LyricsProvider,
        infoJson: String?,
        online: Boolean = true,
        item: ResolvedItem = item(1),
        album: String? = "My List",
        texts: MutableList<String> = mutableListOf(),
        metadata: VideoMetadataSource = VideoMetadataSource { null },
    ): Id3v2Tag {
        val events = service(infoRunner(texts, infoJson), lyrics = provider, metadata = metadata)
            .start(request(item, album = album, searchLyricsOnline = online))
            .collect()

        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)), done(events), events.toString())
        assertTrue(events.none { it is JobEvent.ItemFailed }, events.toString())
        return Id3v2Tag.read(outDir.resolve(events.filterIsInstance<JobEvent.ItemStarted>().single().fileName))
    }

    @Test
    fun withoutLyricsInTheDescriptionAndTheOptionOnTheProviderIsAskedOnceAndItsLyricsReachTheFile() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }
        val texts = mutableListOf<String>()

        val tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), texts = texts)

        assertEquals(1, provider.queries.size)
        assertEquals(listOf("TIT2", "COMM", "USLT"), tag.ids)
        assertEquals(listOf(Id3v2Tag.Lyrics(3, "kor", "", onlineLyricsInTheFile)), tag.lyrics())
        assertTrue("온라인" !in texts.single() && "lyrics" !in texts.single(), "ffmpeg would store the lyrics as a TXXX frame: ${texts.single()}")
    }

    @Test
    fun theQueryHoldsTheParsedArtistAndTitleTheTagAlbumAndTheLengthFromTheInfoFile() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }
        val json = """{"id":"vid00000007","album":"Palette","duration":258.4,"description":${JsonPrimitive(descriptionWithoutLyrics)}}"""

        deliveredWith(provider, json, item = item(7, artist = "Artist Seven", track = "Song Seven"), album = "My List")

        assertEquals(listOf(LyricsQuery("Artist Seven", "Song Seven", "Palette", 258)), provider.queries.toList())
    }

    @Test
    fun theQueryAlbumIsTheOneTheTagGetsSoThePlaylistTitleWhenTheVideoHasNone() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), album = "My List")
        deliveredWith(provider, """{"album":"  ","duration":200}""", album = "My List", item = item(2))

        assertEquals(listOf("My List", "My List"), provider.queries.map { it.album })
    }

    @Test
    fun anAlbumNameTheUserTypedIsInTheTagButTheQueryKeepsTheAlbumTheTagWouldHaveHadWithoutIt() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }
        val texts = mutableListOf<String>()
        val json = """{"id":"vid00000001","album":"Palette","duration":200,"description":${JsonPrimitive(descriptionWithoutLyrics)}}"""

        val events = service(infoRunner(texts, json), lyrics = provider)
            .start(request(item(1), album = "My List", searchLyricsOnline = true, albumOverride = "내 앨범"))
            .collect()

        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)), done(events), events.toString())
        assertEquals(listOf("album=내 앨범"), albumLines(texts.single()), texts.single())
        assertEquals(listOf("Palette"), provider.queries.map { it.album }, "a name made up by the user would only hurt the match")
    }

    @Test
    fun aSingleVideoWithoutAnAlbumIsQueriedWithoutOne() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), album = null)

        assertEquals(listOf(null), provider.queries.map { it.album })
    }

    @Test
    fun withoutAnInfoFileOrADurationTheQueryHasNoLength() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        val tag = deliveredWith(provider, infoJson = null, album = null)
        deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), item = item(2))

        assertEquals(listOf(null, null), provider.queries.map { it.durationSeconds })
        assertEquals(listOf("TIT2", "COMM", "USLT"), tag.ids, "a missing info file still lets the lookup run")
    }

    @Test
    fun lowConfidenceItemsAreQueriedWithTheSecondParse() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }
        val metadata = VideoMetadataSource { VideoMeta("Dynamite", "BTS - Topic", "BTS", "Dynamite") }
        val lowConfidence = item(1, artist = "BTS - Topic", track = "Dynamite (Official)", lowConfidence = true)

        deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), item = lowConfidence, metadata = metadata)

        assertEquals(listOf("BTS" to "Dynamite"), provider.queries.map { it.artist to it.title })
    }

    @Test
    fun lyricsInTheDescriptionAlwaysWinAndTheProviderIsNotCalled() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        val tag = deliveredWith(provider, infoWithDescription(describedLyrics))

        assertEquals(emptyList(), provider.queries.toList(), "the description has lyrics, so nobody is asked")
        assertEquals(listOf(Id3v2Tag.Lyrics(3, "kor", "", "첫 번째 줄\r\n두 번째 줄\r\n\r\nLa la la")), tag.lyrics())
    }

    @Test
    fun withTheOptionOffTheProviderIsNotCalledAndThereAreNoLyrics() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        val tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), online = false)

        assertEquals(emptyList(), provider.queries.toList())
        assertEquals(listOf("TIT2", "COMM"), tag.ids)
    }

    @Test
    fun theOptionIsOffUnlessTheRequestSaysOtherwise() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }
        val events = service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)), lyrics = provider)
            .start(DownloadRequest(listOf(item(1)), LocalFolderSink(outDir), overwrite = false, concurrency = 1))
            .collect()

        assertEquals(JobSummary(1, 0, 0), done(events).summary)
        assertEquals(emptyList(), provider.queries.toList())
        assertFalse(DownloadRequest(emptyList(), LocalFolderSink(outDir), false, 1).searchLyricsOnline)
    }

    @Test
    fun withoutAProviderTheOptionChangesNothing() = runTest {
        service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)))
            .start(request(item(1), searchLyricsOnline = true))
            .collect()

        assertEquals(listOf("TIT2", "COMM"), Id3v2Tag.read(outDir.resolve("001 A1 - T1.mp3")).ids, "the engine's default provider finds nothing")
    }

    @Test
    fun aProviderThatFindsNothingLeavesNoLyricsFrame() = runTest {
        val provider = FakeLyricsProvider { null }

        val tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics))

        assertEquals(1, provider.queries.size)
        assertEquals(listOf("TIT2", "COMM"), tag.ids)
    }

    @Test
    fun aBlankAnswerWritesNoLyricsFrame() = runTest {
        val tag = deliveredWith(FakeLyricsProvider { "  \n " }, infoWithDescription(descriptionWithoutLyrics))

        assertEquals(listOf("TIT2", "COMM"), tag.ids)
    }

    @Test
    fun aProviderThatThrowsGivesNoLyricsFrameAndASuccessfulItem() = runTest {
        val failures = listOf(RuntimeException("boom"), IllegalStateException("bad state"), IOException("offline"), UnsupportedOperationException())
        for ((index, failure) in failures.withIndex()) {
            val provider = FakeLyricsProvider { throw failure }

            val tag = deliveredWith(provider, infoWithDescription(descriptionWithoutLyrics), item = item(index + 1))

            assertEquals(1, provider.queries.size, "$failure")
            assertEquals(listOf("TIT2", "COMM"), tag.ids, "$failure")
        }
    }

    @Test
    fun aFailingLookupDoesNotAffectTheOtherItems() = runTest {
        val provider = FakeLyricsProvider { query -> if (query.title == "T1") throw RuntimeException("boom") else onlineLyrics }

        val events = service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)), lyrics = provider)
            .start(request(item(1), item(2), searchLyricsOnline = true))
            .collect()

        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(2, 0, 0)), done(events))
        assertEquals(listOf("TIT2", "COMM"), Id3v2Tag.read(outDir.resolve("001 A1 - T1.mp3")).ids)
        assertEquals(listOf("TIT2", "COMM", "USLT"), Id3v2Tag.read(outDir.resolve("002 A2 - T2.mp3")).ids)
    }

    @Test
    fun aCancellationExceptionFromTheProviderIsNotSwallowed() = runTest {
        val provider = FakeLyricsProvider { throw CancellationException("cancelled in the lookup") }

        val events = service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)), lyrics = provider)
            .start(request(item(1), searchLyricsOnline = true))
            .collect()

        assertEquals(1, provider.queries.size)
        assertTrue(events.none { it is JobEvent.ItemDone || it is JobEvent.ItemFailed }, "the item is cancelled, not done and not failed: $events")
        assertFalse(Files.exists(outDir.resolve("001 A1 - T1.mp3")), "a cancelled item delivers nothing")
    }

    @Test
    fun cancellingTheJobWhileTheLookupRunsCancelsTheLookupAndReportsCancelled() = runTest {
        val inLookup = CompletableDeferred<Unit>()
        var cancelledInside = false
        val provider = FakeLyricsProvider {
            inLookup.complete(Unit)
            try {
                awaitCancellation()
            } finally {
                cancelledInside = true
            }
        }
        val handle = service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)), lyrics = provider)
            .start(request(item(1), searchLyricsOnline = true))
        inLookup.await()

        handle.cancel()
        val events = handle.collect()

        assertEquals(JobStatus.CANCELLED, done(events).status)
        assertTrue(cancelledInside, "the lookup must be cancelled with the job")
        assertTrue(events.none { it is JobEvent.ItemFailed || it is JobEvent.ItemDone }, events.toString())
        Files.list(tempRoot).use { assertEquals(0, it.count()) }
    }

    @Test
    fun noLookupIsMadeWhenTheDownloadFails() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        val events = service(failingWith("ERROR: something odd"), lyrics = provider)
            .start(request(item(1), searchLyricsOnline = true))
            .collect()

        assertEquals(emptyList(), provider.queries.toList())
        assertEquals(JobSummary(0, 0, 1), done(events).summary)
    }

    @Test
    fun theOptionIsPerRequest() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }
        val service = service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)), lyrics = provider)

        service.start(request(item(1), searchLyricsOnline = false)).collect()
        service.start(request(item(2), searchLyricsOnline = true)).collect()

        assertEquals(listOf("T2"), provider.queries.map { it.title })
    }

    @Test
    fun prepareCarriesTheOption() = runTest {
        val downloader = ItemDownloader(succeeding, toolsOf(), VideoMetadataSource { null })

        assertFalse(downloader.prepare(item(1)).searchLyricsOnline, "off by default: the engine never looks anything up unasked")
        assertTrue(downloader.prepare(item(1), "My List", true, searchLyricsOnline = true).searchLyricsOnline)
        assertFalse(downloader.prepare(item(1), "My List", true, searchLyricsOnline = false).searchLyricsOnline)
    }

    // ---- lyrics: the outcome reported with every finished item ----

    /** The events of a job over item 1 (a made-up description decides what the info file holds) with [provider] and the lookup [online]. */
    private suspend fun TestScope.eventsWith(provider: LyricsProvider, infoJson: String?, online: Boolean): List<JobEvent> =
        service(infoRunner(mutableListOf(), infoJson), lyrics = provider)
            .start(request(item(1), searchLyricsOnline = online))
            .collect()

    private fun itemDone(events: List<JobEvent>) = events.filterIsInstance<JobEvent.ItemDone>().single()

    /**
     * Like [infoRunner]; [interceptor] may answer a command first (with an exit code, after writing to stderr) and returns
     * null to leave the command to the usual handling.
     */
    private fun infoRunnerWith(infoJson: String?, interceptor: (List<String>, (String) -> Unit) -> Int?) =
        FakeProcessRunner { command, _, onStderr ->
            interceptor(command, onStderr) ?: run {
                if (isFfmpegCommand(command)) {
                    writeFakeTagged(command)
                } else {
                    writeFakeMp3(command)
                    if (infoJson != null) writeFakeInfo(command, infoJson)
                }
                0
            }
        }

    @Test
    fun lyricsFromTheDescriptionAreReportedAsDescriptionAndTheProviderIsNotAsked() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        val events = eventsWith(provider, infoWithDescription(describedLyrics), online = true)

        assertEquals(JobEvent.ItemDone(1, "001 A1 - T1.mp3", LyricsOutcome.DESCRIPTION), itemDone(events))
        assertEquals(emptyList(), provider.queries.toList())
        assertEquals(JobSummary(1, 0, 0), done(events).summary)
    }

    @Test
    fun lyricsFoundByTheProviderAreReportedAsOnline() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        val events = eventsWith(provider, infoWithDescription(descriptionWithoutLyrics), online = true)

        assertEquals(JobEvent.ItemDone(1, "001 A1 - T1.mp3", LyricsOutcome.ONLINE), itemDone(events))
        assertEquals(1, provider.queries.size)
    }

    @Test
    fun aMissingOrCorruptInfoFileStillLetsTheLookupReportOnline() = runTest {
        for (infoJson in listOf(null, """{"description":"[Lyrics]\nLine one""")) {
            val events = eventsWith(FakeLyricsProvider { onlineLyrics }, infoJson, online = true)

            assertEquals(LyricsOutcome.ONLINE, itemDone(events).lyrics, "$infoJson")
            Files.deleteIfExists(outDir.resolve("001 A1 - T1.mp3"))
        }
    }

    @Test
    fun aProviderThatFindsNothingIsReportedAsNotFound() = runTest {
        val provider = FakeLyricsProvider { null }

        val events = eventsWith(provider, infoWithDescription(descriptionWithoutLyrics), online = true)

        assertEquals(JobEvent.ItemDone(1, "001 A1 - T1.mp3", LyricsOutcome.NOT_FOUND), itemDone(events))
        assertEquals(1, provider.queries.size)
    }

    @Test
    fun aBlankAnswerIsReportedAsNotFound() = runTest {
        val events = eventsWith(FakeLyricsProvider { "  \n " }, infoWithDescription(descriptionWithoutLyrics), online = true)

        assertEquals(LyricsOutcome.NOT_FOUND, itemDone(events).lyrics)
    }

    @Test
    fun withTheLookupOnAndTheEnginesDefaultProviderTheOutcomeIsNotFound() = runTest {
        val events = service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)))
            .start(request(item(1), searchLyricsOnline = true))
            .collect()

        assertEquals(LyricsOutcome.NOT_FOUND, itemDone(events).lyrics, "a lookup that ran and found nothing is not 'search off'")
    }

    @Test
    fun aProviderThatThrowsIsReportedAsNotFoundAndTheItemStillSucceeds() = runTest {
        val failures = listOf(RuntimeException("boom"), IllegalStateException("bad state"), IOException("offline"), UnsupportedOperationException())
        for (failure in failures) {
            val events = eventsWith(FakeLyricsProvider { throw failure }, infoWithDescription(descriptionWithoutLyrics), online = true)

            assertEquals(JobEvent.ItemDone(1, "001 A1 - T1.mp3", LyricsOutcome.NOT_FOUND), itemDone(events), "$failure")
            assertEquals(JobSummary(1, 0, 0), done(events).summary, "$failure")
            assertTrue(events.none { it is JobEvent.ItemFailed }, "$failure")
            Files.deleteIfExists(outDir.resolve("001 A1 - T1.mp3"))
        }
    }

    @Test
    fun withTheLookupOffAndNoLyricsInTheDescriptionTheOutcomeIsSearchOff() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }
        val infos = listOf(infoWithDescription(descriptionWithoutLyrics), null, """{"id":"vid00000001"}""", """{"description":"[Lyrics]\nLine one""")
        for (infoJson in infos) {
            val events = eventsWith(provider, infoJson, online = false)

            assertEquals(JobEvent.ItemDone(1, "001 A1 - T1.mp3", LyricsOutcome.SEARCH_OFF), itemDone(events), "$infoJson")
            Files.deleteIfExists(outDir.resolve("001 A1 - T1.mp3"))
        }
        assertEquals(emptyList(), provider.queries.toList(), "with the lookup off nobody is asked")
    }

    @Test
    fun withTheLookupOffButLyricsInTheDescriptionTheOutcomeIsDescription() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }

        val events = eventsWith(provider, infoWithDescription(describedLyrics), online = false)

        assertEquals(JobEvent.ItemDone(1, "001 A1 - T1.mp3", LyricsOutcome.DESCRIPTION), itemDone(events))
        assertEquals(emptyList(), provider.queries.toList())
    }

    @Test
    fun theRequestsOptionIsOffByDefaultSoTheOutcomeIsSearchOff() = runTest {
        val events = service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)), lyrics = FakeLyricsProvider { onlineLyrics })
            .start(DownloadRequest(listOf(item(1)), LocalFolderSink(outDir), overwrite = false, concurrency = 1))
            .collect()

        assertEquals(LyricsOutcome.SEARCH_OFF, itemDone(events).lyrics)
    }

    @Test
    fun everyItemOfAJobGetsItsOwnOutcome() = runTest {
        val provider = FakeLyricsProvider { query -> if (query.title == "T1") null else onlineLyrics }

        val events = service(infoRunner(mutableListOf(), infoWithDescription(descriptionWithoutLyrics)), lyrics = provider)
            .start(request(item(1), item(2), searchLyricsOnline = true))
            .collect()

        assertEquals(
            mapOf(1 to LyricsOutcome.NOT_FOUND, 2 to LyricsOutcome.ONLINE),
            events.filterIsInstance<JobEvent.ItemDone>().associate { it.rank to it.lyrics },
        )
    }

    @Test
    fun aRetriedItemReportsTheOutcomeOfTheAttemptThatSucceeded() = runTest {
        val provider = FakeLyricsProvider { onlineLyrics }
        val calls = AtomicInteger()
        val flaky = infoRunnerWith(infoWithDescription(descriptionWithoutLyrics)) { command, onStderr ->
            if (!isFfmpegCommand(command) && calls.incrementAndGet() == 1) {
                onStderr("ERROR: unable to download video data: HTTP Error 503: Service Unavailable")
                1
            } else {
                null
            }
        }

        val events = service(flaky, lyrics = provider).start(request(item(1), searchLyricsOnline = true)).collect()

        assertEquals(2, flaky.ytDlpCommands.size)
        assertEquals(1, provider.queries.size, "the failed attempt looked nothing up")
        assertEquals(JobEvent.ItemDone(1, "001 A1 - T1.mp3", LyricsOutcome.ONLINE), itemDone(events))
    }

    @Test
    fun aTaggingFailureFailsTheItemAndReportsNoOutcome() = runTest {
        // Lyrics are in the description, so a finished item would say DESCRIPTION: a failed one must say nothing.
        val runner = infoRunnerWith(infoWithDescription(describedLyrics)) { command, onStderr ->
            if (isFfmpegCommand(command) && command.any { it.contains("vid00000001") }) {
                onStderr("Error while writing the tag")
                1
            } else {
                null
            }
        }

        val events = service(runner).start(request(item(1), item(2))).collect()

        assertEquals(
            listOf(JobEvent.ItemFailed(1, "ID3 태그를 쓰지 못했습니다: Error while writing the tag")),
            events.filterIsInstance<JobEvent.ItemFailed>(),
        )
        assertEquals(
            listOf(JobEvent.ItemDone(2, "002 A2 - T2.mp3", LyricsOutcome.DESCRIPTION)),
            events.filterIsInstance<JobEvent.ItemDone>(),
            "no item-done at all for the item whose tag could not be written",
        )
        assertEquals(JobSummary(1, 0, 1), done(events).summary)
    }

    @Test
    fun aFailureWhileAddingTheLyricsFrameFailsTheItemAndReportsNoOutcome() = runTest {
        // ffmpeg "succeeds" but leaves a tag of another version, which the engine does not extend with its USLT frame.
        val runner = FakeProcessRunner { command, _, _ ->
            if (isFfmpegCommand(command)) {
                Files.write(Path.of(command.last()), "ID3".toByteArray(Charsets.ISO_8859_1) + byteArrayOf(3, 0, 0, 0, 0, 0, 0) + "audio".toByteArray())
            } else {
                writeFakeMp3(command)
                writeFakeInfo(command, infoWithDescription(describedLyrics))
            }
            0
        }

        val events = service(runner).start(request(item(1))).collect()

        val failed = events.filterIsInstance<JobEvent.ItemFailed>().single()
        assertTrue(failed.message.startsWith("ID3 태그를 쓰지 못했습니다"), failed.message)
        assertTrue(events.none { it is JobEvent.ItemDone }, events.toString())
        assertFalse(Files.exists(outDir.resolve("001 A1 - T1.mp3")), "an untagged file must not reach the sink")
        assertEquals(JobSummary(0, 0, 1), done(events).summary)
    }

    @Test
    fun anExistingFileIsSkippedWithoutAnOutcomeAndWithoutALookup() = runTest {
        Files.createDirectories(outDir)
        Files.writeString(outDir.resolve("001 A1 - T1.mp3"), "old")
        val provider = FakeLyricsProvider { onlineLyrics }

        val events = service(infoRunner(mutableListOf(), infoWithDescription(describedLyrics)), lyrics = provider)
            .start(request(item(1), searchLyricsOnline = true))
            .collect()

        assertEquals(listOf(JobEvent.ItemSkipped(1, "이미 존재")), events.filterIsInstance<JobEvent.ItemSkipped>())
        assertTrue(events.none { it is JobEvent.ItemDone }, events.toString())
        assertEquals(emptyList(), provider.queries.toList())
        assertEquals("old", Files.readString(outDir.resolve("001 A1 - T1.mp3")))
    }

    @Test
    fun aFailedDownloadReportsNoOutcome() = runTest {
        val events = service(failingWith("ERROR: something odd"), lyrics = FakeLyricsProvider { onlineLyrics })
            .start(request(item(1), searchLyricsOnline = true))
            .collect()

        assertTrue(events.none { it is JobEvent.ItemDone }, events.toString())
        assertEquals(1, events.filterIsInstance<JobEvent.ItemFailed>().size)
    }

    private class DownloaderCase(val answer: String?, val infoJson: String, val online: Boolean, val expected: LyricsOutcome)

    @Test
    fun theDownloaderReturnsTheOutcomeWithTheFile() = runTest {
        val cases = listOf(
            DownloaderCase(onlineLyrics, infoWithDescription(describedLyrics), online = true, expected = LyricsOutcome.DESCRIPTION),
            DownloaderCase(onlineLyrics, infoWithDescription(descriptionWithoutLyrics), online = true, expected = LyricsOutcome.ONLINE),
            DownloaderCase(null, infoWithDescription(descriptionWithoutLyrics), online = true, expected = LyricsOutcome.NOT_FOUND),
            DownloaderCase(onlineLyrics, infoWithDescription(descriptionWithoutLyrics), online = false, expected = LyricsOutcome.SEARCH_OFF),
        )
        for ((index, case) in cases.withIndex()) {
            val provider = FakeLyricsProvider { case.answer }
            val downloader = ItemDownloader(infoRunner(mutableListOf(), case.infoJson), toolsOf(), VideoMetadataSource { null }, provider)
            val workDir = Files.createDirectories(tempRoot.resolve("direct-$index"))

            val result = downloader.download(downloader.prepare(item(1), searchLyricsOnline = case.online), workDir) { }

            val downloaded = result as DownloadResult.Downloaded
            assertEquals(case.expected, downloaded.lyrics, "case $index")
            assertTrue(Files.isRegularFile(downloaded.file), "case $index")
        }
    }

    // ---- file names without the rank ----

    private fun filesIn(dir: Path): List<String> =
        if (Files.isDirectory(dir)) Files.list(dir).use { files -> files.map { it.fileName.toString() }.sorted().toList() } else emptyList()

    @Test
    fun withoutTheRankTheFileIsNamedArtistDashTitleButTheTagKeepsTheRank() = runTest {
        val texts = mutableListOf<String>()
        val recorded = mutableListOf<String>()
        val recordingSink = object : OutputSink {
            private val inner = LocalFolderSink(outDir)
            override suspend fun exists(fileName: String): Boolean {
                recorded += "exists:$fileName"
                return inner.exists(fileName)
            }

            override suspend fun put(fileName: String, source: Path, overwrite: Boolean) {
                recorded += "put:$fileName"
                inner.put(fileName, source, overwrite)
            }
        }

        val events = service(taggingRunner(texts))
            .start(request(item(5, artist = "Artist Five", track = "Song Five"), sink = recordingSink, album = "My List", includeRank = false))
            .collect()

        val name = "Artist Five - Song Five.mp3"
        assertEquals(listOf("exists:$name", "put:$name"), recorded)
        assertEquals(listOf(name), filesIn(outDir))
        assertEquals(JobEvent.ItemStarted(5, "vid00000005", name), events.filterIsInstance<JobEvent.ItemStarted>().single())
        assertEquals(JobEvent.ItemDone(5, name, LyricsOutcome.SEARCH_OFF), events.filterIsInstance<JobEvent.ItemDone>().single())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)), done(events))
        val lines = texts.single().lines()
        assertTrue("track=5" in lines, "the track number is the rank whatever the file name says: ${texts.single()}")
        assertTrue("title=Song Five" in lines, texts.single())
        assertTrue("artist=Artist Five" in lines, texts.single())
    }

    @Test
    fun withoutTheRankLowConfidenceItemsAreRenamedFromFullMetadataToo() = runTest {
        val metadata = VideoMetadataSource { VideoMeta("Dynamite", "BTS - Topic", "BTS", "Dynamite") }
        val lowConfidence = item(1, artist = "BTS - Topic", track = "Dynamite", lowConfidence = true)

        val events = service(succeeding, metadata = metadata).start(request(lowConfidence, includeRank = false)).collect()

        assertEquals("BTS - Dynamite.mp3", events.filterIsInstance<JobEvent.ItemStarted>().single().fileName)
        assertEquals(listOf("BTS - Dynamite.mp3"), filesIn(outDir))
    }

    @Test
    fun withTheRankTurnedOnTheNameKeepsTheRankPrefix() = runTest {
        val events = service(succeeding).start(request(item(5, artist = "Artist Five", track = "Song Five"), includeRank = true)).collect()

        assertEquals("005 Artist Five - Song Five.mp3", events.filterIsInstance<JobEvent.ItemStarted>().single().fileName)
        assertEquals(listOf("005 Artist Five - Song Five.mp3"), filesIn(outDir))
    }

    // ---- two items of one job with the same file name ----

    /** Two entries that parse to the same artist and title: without the rank their file names are equal. */
    private val firstTwin = item(1, artist = "Same", track = "Song")
    private val secondTwin = item(2, artist = "Same", track = "Song")

    /** A runner whose yt-dlp calls all wait for [gate], so every item has passed its existence check before any file is put. */
    private fun heldRunner(gate: CompletableDeferred<Unit>) = downloadRunner { command, _, _ ->
        gate.await()
        writeFakeMp3(command)
        0
    }

    @Test
    fun twoItemsWithTheSameNameOneAfterAnotherWriteOneFileAndSkipTheSecond() = runTest {
        val runner = taggingRunner(mutableListOf())

        val events = service(runner).start(request(firstTwin, secondTwin, concurrency = 1, includeRank = false)).collect()

        assertEquals(listOf("Same - Song.mp3"), filesIn(outDir))
        assertEquals(listOf(JobEvent.ItemDone(1, "Same - Song.mp3", LyricsOutcome.SEARCH_OFF)), events.filterIsInstance<JobEvent.ItemDone>())
        assertEquals(listOf(JobEvent.ItemSkipped(2, "이미 존재")), events.filterIsInstance<JobEvent.ItemSkipped>())
        assertTrue(events.none { it is JobEvent.ItemFailed }, events.toString())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 1, 0)), done(events))
    }

    @Test
    fun twoItemsWithTheSameNameDownloadedAtTheSameTimeWriteOneFileAndSkipTheOther() = runTest {
        val gate = CompletableDeferred<Unit>()
        val held = heldRunner(gate)
        val handle = service(held).start(request(firstTwin, secondTwin, concurrency = 2, includeRank = false))
        awaitStarted(held, 2)
        gate.complete(Unit)

        val events = handle.collect()

        assertEquals(listOf("Same - Song.mp3"), filesIn(outDir))
        assertTrue(endsWithFakeAudio(outDir.resolve("Same - Song.mp3")), "the delivered file is the tagged one")
        assertEquals(1, events.count { it is JobEvent.ItemDone }, events.toString())
        assertEquals(1, events.filterIsInstance<JobEvent.ItemSkipped>().size, events.toString())
        assertEquals("이미 존재", events.filterIsInstance<JobEvent.ItemSkipped>().single().reason)
        assertTrue(events.none { it is JobEvent.ItemFailed }, events.toString())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 1, 0)), done(events))
    }

    @Test
    fun withOverwriteTwoItemsWithTheSameNameOneAfterAnotherBothFinishAndOneFileRemains() = runTest {
        val events = service(succeeding).start(request(firstTwin, secondTwin, overwrite = true, concurrency = 1, includeRank = false)).collect()

        assertEquals(listOf("Same - Song.mp3"), filesIn(outDir))
        assertTrue(endsWithFakeAudio(outDir.resolve("Same - Song.mp3")))
        assertEquals(
            listOf(JobEvent.ItemDone(1, "Same - Song.mp3", LyricsOutcome.SEARCH_OFF), JobEvent.ItemDone(2, "Same - Song.mp3", LyricsOutcome.SEARCH_OFF)),
            events.filterIsInstance<JobEvent.ItemDone>().sortedBy { it.rank },
            events.toString(),
        )
        assertTrue(events.none { it is JobEvent.ItemSkipped || it is JobEvent.ItemFailed }, events.toString())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(2, 0, 0)), done(events))
    }

    @Test
    fun withOverwriteTwoItemsWithTheSameNameDownloadedAtTheSameTimeBothFinishAndOneFileRemains() = runTest {
        val gate = CompletableDeferred<Unit>()
        val held = heldRunner(gate)
        val handle = service(held).start(request(firstTwin, secondTwin, overwrite = true, concurrency = 2, includeRank = false))
        awaitStarted(held, 2)
        gate.complete(Unit)

        val events = handle.collect()

        assertEquals(listOf("Same - Song.mp3"), filesIn(outDir))
        assertTrue(endsWithFakeAudio(outDir.resolve("Same - Song.mp3")))
        assertEquals(
            listOf(JobEvent.ItemDone(1, "Same - Song.mp3", LyricsOutcome.SEARCH_OFF), JobEvent.ItemDone(2, "Same - Song.mp3", LyricsOutcome.SEARCH_OFF)),
            events.filterIsInstance<JobEvent.ItemDone>().sortedBy { it.rank },
            events.toString(),
        )
        assertTrue(events.none { it is JobEvent.ItemSkipped || it is JobEvent.ItemFailed }, events.toString())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(2, 0, 0)), done(events))
    }

    @Test
    fun aNameTakenBetweenTheCheckAndThePutIsSkippedNotFailed() = runTest {
        // What another item of the job, or another program, causes: the check says free, the put finds the name taken.
        val racedSink = object : OutputSink {
            private var taken = false

            override suspend fun exists(fileName: String) = taken

            override suspend fun put(fileName: String, source: Path, overwrite: Boolean) {
                taken = true
                throw FileAlreadyExistsException(outDir.resolve(fileName).toString())
            }
        }

        val events = service(succeeding).start(request(item(1), sink = racedSink)).collect()

        assertEquals(listOf(JobEvent.ItemSkipped(1, "이미 존재")), events.filterIsInstance<JobEvent.ItemSkipped>())
        assertTrue(events.none { it is JobEvent.ItemFailed || it is JobEvent.ItemDone }, events.toString())
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(0, 1, 0)), done(events))
    }

    @Test
    fun withOverwriteAFileThatCannotBeReplacedStillFailsTheItem() = runTest {
        val sink = object : OutputSink {
            override suspend fun exists(fileName: String) = true
            override suspend fun put(fileName: String, source: Path, overwrite: Boolean) {
                throw FileAlreadyExistsException(outDir.resolve(fileName).toString())
            }
        }

        val events = service(succeeding).start(request(item(1), overwrite = true, sink = sink)).collect()

        assertEquals(listOf(1), events.filterIsInstance<JobEvent.ItemFailed>().map { it.rank })
        assertEquals(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(0, 0, 1)), done(events))
    }
}
