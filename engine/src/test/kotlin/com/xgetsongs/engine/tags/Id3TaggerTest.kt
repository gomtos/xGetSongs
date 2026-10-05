package com.xgetsongs.engine.tags

import com.xgetsongs.engine.testutil.FAKE_TAGGED_MP3
import com.xgetsongs.engine.testutil.FakeProcessRunner
import com.xgetsongs.engine.testutil.TEST_TOOLS
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
        assertEquals(FAKE_TAGGED_MP3, Files.readString(file))
        assertEquals(listOf("vid00000001.mp3"), filesInDir())
        assertEquals(Ffmetadata.render(tags), metadataText)
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
        assertTrue("Golden" !in commandLine && "아티스트" !in commandLine && "My List" !in commandLine, commandLine)
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
