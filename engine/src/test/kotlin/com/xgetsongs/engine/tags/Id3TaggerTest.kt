package com.xgetsongs.engine.tags

import com.xgetsongs.engine.testutil.FakeProcessRunner
import com.xgetsongs.engine.testutil.Id3v2Tag
import com.xgetsongs.engine.testutil.TEST_TOOLS
import com.xgetsongs.engine.testutil.endsWithFakeAudio
import com.xgetsongs.engine.testutil.fakeTaggedMp3
import com.xgetsongs.engine.testutil.ffmetadataTextOf
import com.xgetsongs.engine.testutil.toolsOf
import com.xgetsongs.engine.testutil.writeFakeTagged
import com.xgetsongs.engine.tools.ToolPaths
import com.xgetsongs.engine.ytdlp.Failure
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.engine.ytdlp.FfmpegCommands
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class Id3TaggerTest {
    private val dir: Path = Files.createTempDirectory("xgs-id3")
    private val file = dir.resolve("vid00000001.mp3")
    private val cover = dir.resolve("vid00000001.jpg")
    private val tags = TrackTags(
        title = "\"Golden\" 'x'",
        artist = "아티스트",
        album = "My List",
        albumArtist = "아티스트",
        trackNumber = 3,
        comment = "https://www.youtube.com/watch?v=vid00000001",
    )

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    private fun tagger(runner: FakeProcessRunner, tools: ToolPaths = TEST_TOOLS) =
        Id3Tagger(runner, toolsOf(tools))

    private fun filesInDir(): List<String> = Files.list(dir).use { stream -> stream.map { it.fileName.toString() }.sorted().toList() }

    @BeforeTest
    fun writeOriginal() {
        Files.writeString(file, "original-mp3")
    }

    @Test
    fun replacesTheOriginalWithTheTaggedFileAndRemovesTheTemporaryFiles() = runTest {
        var metadataText: String? = null
        val runner = FakeProcessRunner { command, _, _ ->
            metadataText = ffmetadataTextOf(command)
            writeFakeTagged(command)
            0
        }

        val result = tagger(runner).tag(file, null, tags)

        assertNull(result)
        assertTrue(endsWithFakeAudio(file), "the audio of ffmpeg's output must reach the final file")
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
        assertEquals(Ffmetadata.render(tags), metadataText)
    }

    @Test
    fun addsTheCommentAsACommFrameToFfmpegsOutput() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        val result = tagger(runner).tag(file, null, tags)

        assertNull(result)
        val tag = Id3v2Tag.read(file)
        assertEquals(listOf("TIT2", "COMM"), tag.ids, "ffmpeg's frame stays, the comment frame is added")
        assertEquals(listOf(Id3v2Tag.Comment(0, "eng", "", tags.comment!!)), tag.comments())
    }

    @Test
    fun aMissingOrBlankCommentLeavesFfmpegsOutputUntouched() = runTest {
        for (comment in listOf(null, "", "  \t")) {
            Files.writeString(file, "original-mp3")
            val runner = FakeProcessRunner { command, _, _ ->
                writeFakeTagged(command)
                0
            }

            val result = tagger(runner).tag(file, null, tags.copy(comment = comment))

            assertNull(result)
            assertContentEquals(fakeTaggedMp3(), Files.readAllBytes(file), "comment = [$comment]")
            assertEquals(listOf("vid00000001.mp3"), filesInDir())
        }
    }

    private val lyrics = "첫 번째 줄\nLa la la\n세 번째 줄"
    private val lyricsInTheFile = "첫 번째 줄\r\nLa la la\r\n세 번째 줄"

    @Test
    fun addsTheLyricsAsAUsltFrameAfterTheCommentToFfmpegsOutput() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        val result = tagger(runner).tag(file, null, tags.copy(lyrics = lyrics))

        assertNull(result)
        val tag = Id3v2Tag.read(file)
        assertEquals(listOf("TIT2", "COMM", "USLT"), tag.ids, "ffmpeg's frame stays, COMM and USLT are added in that order")
        assertEquals(listOf(Id3v2Tag.Comment(0, "eng", "", tags.comment!!)), tag.comments())
        assertEquals(listOf(Id3v2Tag.Lyrics(1, "kor", "", lyricsInTheFile)), tag.lyrics())
        assertTrue(endsWithFakeAudio(file), "the audio of ffmpeg's output must reach the final file")
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
    }

    @Test
    fun lyricsWithoutACommentAreStillWritten() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        val result = tagger(runner).tag(file, null, tags.copy(comment = null, lyrics = "Line one\nLine two"))

        assertNull(result)
        val tag = Id3v2Tag.read(file)
        assertEquals(listOf("TIT2", "USLT"), tag.ids)
        assertEquals(listOf(Id3v2Tag.Lyrics(1, "eng", "", "Line one\r\nLine two")), tag.lyrics())
    }

    @Test
    fun missingOrBlankLyricsAndCommentLeaveFfmpegsOutputUntouched() = runTest {
        for (blank in listOf(null, "", " \n\t")) {
            Files.writeString(file, "original-mp3")
            val runner = FakeProcessRunner { command, _, _ ->
                writeFakeTagged(command)
                0
            }

            val result = tagger(runner).tag(file, null, tags.copy(comment = blank, lyrics = blank))

            assertNull(result)
            assertContentEquals(fakeTaggedMp3(), Files.readAllBytes(file), "comment and lyrics = [$blank]")
            assertEquals(listOf("vid00000001.mp3"), filesInDir())
        }
    }

    @Test
    fun noLyricsGivenMeansNoLyricsFrameEvenWhenTheInputFileCarriesOne() = runTest {
        // The mp3 that comes in already has a USLT frame (an earlier tag, or lyrics left by another program).
        Files.write(file, fakeTaggedMp3())
        Id3Frames.add(file, null, "Old made-up line one\nOld line two\nOld line three")
        assertEquals(listOf("TIT2", "USLT"), Id3v2Tag.read(file).ids, "precondition: the input has lyrics")
        var metadataText: String? = null
        // Like the real ffmpeg: the new file is built from the audio and the metadata file, nothing else of the input.
        val runner = FakeProcessRunner { command, _, _ ->
            metadataText = ffmetadataTextOf(command)
            writeFakeTagged(command)
            0
        }

        val result = tagger(runner).tag(file, null, tags.copy(lyrics = null))

        assertNull(result)
        val tag = Id3v2Tag.read(file)
        assertEquals(listOf("TIT2", "COMM"), tag.ids, "no USLT frame: the item has no lyrics, whatever the input had")
        assertEquals(emptyList(), tag.lyrics())
        assertTrue("lyrics" !in metadataText.orEmpty())
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
    }

    @Test
    fun theLyricsReachNeitherTheMetadataFileNorTheCommandLine() = runTest {
        var metadataText: String? = null
        val runner = FakeProcessRunner { command, _, _ ->
            metadataText = ffmetadataTextOf(command)
            writeFakeTagged(command)
            0
        }
        val withLyrics = tags.copy(lyrics = lyrics)

        tagger(runner).tag(file, null, withLyrics)

        assertEquals(Ffmetadata.render(tags), metadataText, "ffmpeg would store the lyrics as a TXXX frame")
        val commandLine = runner.commands.single().joinToString(" ")
        assertTrue(listOf("첫 번째", "La la la", "lyrics").none { it in commandLine }, commandLine)
    }

    @Test
    fun aTagLayoutTheFrameStepCannotRewriteFailsTheItemWhenOnlyLyricsAreGiven() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            Files.write(Path.of(command.last()), "ID3".toByteArray() + byteArrayOf(4, 0, 0, 0, 0, 0, 0) + "audio".toByteArray())
            0
        }

        val result = tagger(runner).tag(file, null, tags.copy(comment = null, lyrics = lyrics))

        assertEquals(Failure(FailureKind.OTHER, "ID3 태그를 쓰지 못했습니다: unsupported ID3 tag layout"), result)
        assertEquals("original-mp3", Files.readString(file))
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
    }

    @Test
    fun aTagLayoutTheCommentStepCannotRewriteFailsTheItemAndKeepsTheOriginal() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            // An ID3v2.4 header: the engine only knows how to extend v2.3 tags.
            Files.write(Path.of(command.last()), "ID3".toByteArray() + byteArrayOf(4, 0, 0, 0, 0, 0, 0) + "audio".toByteArray())
            0
        }

        val result = tagger(runner).tag(file, null, tags)

        assertEquals(Failure(FailureKind.OTHER, "ID3 태그를 쓰지 못했습니다: unsupported ID3 tag layout"), result)
        assertEquals("original-mp3", Files.readString(file))
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
    }

    @Test
    fun runsTheFfmpegCommandWithTemporaryFilesNextToTheOriginal() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        tagger(runner).tag(file, null, tags)

        val expected = FfmpegCommands.tag(
            TEST_TOOLS.ffmpeg!!,
            file,
            null,
            dir.resolve("vid00000001.ffmeta"),
            dir.resolve("vid00000001.tagged.mp3"),
        )
        assertEquals(listOf(expected), runner.commands.toList())
    }

    @Test
    fun passesTheCoverAsASecondInput() = runTest {
        Files.writeString(cover, "jpg-data")
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        tagger(runner).tag(file, cover, tags)

        val command = runner.commands.single()
        assertEquals(
            listOf(file.toString(), cover.toString()),
            command.indices.filter { command[it] == "-i" }.take(2).map { command[it + 1] },
        )
        assertEquals(
            listOf("0:a", "1:v"),
            command.indices.filter { command[it] == "-map" }.map { command[it + 1] },
        )
        assertEquals("2", command[command.indexOf("-map_metadata") + 1])
    }

    @Test
    fun noTagTextAppearsOnTheCommandLine() = runTest {
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            0
        }

        tagger(runner).tag(file, null, tags)

        val commandLine = runner.commands.single().joinToString(" ")
        assertTrue(
            listOf("Golden", "아티스트", "My List", "youtube").none { it in commandLine },
            commandLine,
        )
    }

    @Test
    fun aNonZeroExitCodeFailsWithTheLastStderrLineAndKeepsTheOriginal() = runTest {
        val runner = FakeProcessRunner { command, _, onStderr ->
            writeFakeTagged(command) // a half-written output must not survive
            onStderr("first problem")
            onStderr("Invalid argument")
            onStderr("   ")
            1
        }

        val result = tagger(runner).tag(file, null, tags)

        assertEquals(Failure(FailureKind.OTHER, "ID3 태그를 쓰지 못했습니다: Invalid argument"), result)
        assertEquals("original-mp3", Files.readString(file))
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
    }

    @Test
    fun exitCodeZeroWithoutAnOutputFileIsAFailure() = runTest {
        val runner = FakeProcessRunner { _, _, _ -> 0 }

        val result = tagger(runner).tag(file, null, tags)

        assertEquals(Failure(FailureKind.OTHER, "ID3 태그를 쓰지 못했습니다: 알 수 없는 오류"), result)
        assertEquals("original-mp3", Files.readString(file))
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
    }

    @Test
    fun theStderrSummaryIsCutToTwoHundredCharacters() = runTest {
        val runner = FakeProcessRunner { _, _, onStderr ->
            onStderr("x".repeat(500))
            1
        }

        val result = tagger(runner).tag(file, null, tags)

        assertEquals(Failure(FailureKind.OTHER, "ID3 태그를 쓰지 못했습니다: " + "x".repeat(200)), result)
    }

    @Test
    fun aMissingFfmpegIsFatalAndRunsNothing() = runTest {
        val runner = FakeProcessRunner { _, _, _ -> 0 }

        val result = tagger(runner, TEST_TOOLS.copy(ffmpeg = null)).tag(file, null, tags)

        assertEquals(Failure(FailureKind.FATAL, "ffmpeg를 찾을 수 없습니다."), result)
        assertTrue(runner.commands.isEmpty())
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
    }

    @Test
    fun anIoErrorBecomesAFailureInsteadOfAnException() = runTest {
        val runner = FakeProcessRunner { _, _, _ -> 0 }
        val missingDir = dir.resolve("no-such-dir").resolve("a.mp3")

        val result = tagger(runner).tag(missingDir, null, tags)

        assertEquals(FailureKind.OTHER, result?.kind)
        assertTrue(result!!.message.startsWith("ID3 태그를 쓰지 못했습니다: "), result.message)
        assertTrue(runner.commands.isEmpty())
    }

    @Test
    fun cancellingWhileFfmpegRunsPropagatesAndStillRemovesTheTemporaryFiles() = runTest {
        val started = CompletableDeferred<Unit>()
        val runner = FakeProcessRunner { command, _, _ ->
            writeFakeTagged(command)
            started.complete(Unit)
            awaitCancellation()
        }
        val job = launch { tagger(runner).tag(file, null, tags) }
        started.await()

        job.cancelAndJoin()

        assertTrue(job.isCancelled)
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
        assertEquals("original-mp3", Files.readString(file))
    }
}
