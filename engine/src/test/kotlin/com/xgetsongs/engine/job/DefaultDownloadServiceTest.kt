package com.xgetsongs.engine.job

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.JobHandle
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.output.OutputSink
import com.xgetsongs.engine.process.ProcessRunner
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
import com.xgetsongs.engine.testutil.writeFakeMp3
import com.xgetsongs.engine.testutil.writeFakeTagged
import com.xgetsongs.engine.testutil.ytDlpCommands
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.ytdlp.VideoMeta
import com.xgetsongs.engine.ytdlp.VideoMetadataSource
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.filename.FilenameFormatter
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
    ) = DefaultDownloadService(ItemDownloader(runner, tools, metadata), workDir, this)

    private fun request(
        vararg items: ResolvedItem,
        overwrite: Boolean = false,
        concurrency: Int = 1,
        sink: OutputSink = LocalFolderSink(outDir),
        album: String? = null,
    ) = DownloadRequest(items.toList(), sink, overwrite, concurrency, album)

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
        assertEquals(JobEvent.Progress(1, Stage.CONVERTING, null), progress.last())
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
    fun concurrencyIsClampedToFour() = runTest {
        val gate = CompletableDeferred<Unit>()
        val held = downloadRunner { command, _, _ ->
            gate.await()
            writeFakeMp3(command)
            0
        }
        val handle = service(held).start(request(*(1..8).map { item(it) }.toTypedArray(), concurrency = 99))

        awaitStarted(held, 4)
        assertEquals(4, held.commands.size, "99 is clamped to 4 parallel downloads")
        gate.complete(Unit)
        handle.collect()

        assertEquals(4, held.maxActive.get())
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
                JobEvent.Progress(1, Stage.CONVERTING, null),
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
                "album_artist=Artist Five",
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
        assertTrue("album_artist=BTS" in lines, texts.single())
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
}
