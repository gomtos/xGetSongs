package com.xgetsongs.engine.ytdlp

import com.xgetsongs.engine.testutil.TEST_TOOLS
import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertContains
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class YtDlpCommandsTest {
    private val url = "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

    @Test
    fun playlistListingIsFlatAndPutsTheUrlAfterDoubleDash() {
        val command = YtDlpCommands.resolvePlaylist(TEST_TOOLS, "https://www.youtube.com/playlist?list=PLabcdefghijkl")
        assertEquals(TEST_TOOLS.ytDlp.toString(), command.first())
        assertContains(command, "--flat-playlist")
        assertContains(command, "--ignore-config")
        assertEquals(listOf("--", "https://www.youtube.com/playlist?list=PLabcdefghijkl"), command.takeLast(2))
    }

    @Test
    fun videoMetadataUsesTheJsRuntime() {
        val command = YtDlpCommands.resolveVideo(TEST_TOOLS, url)
        val i = command.indexOf("--js-runtimes")
        assertEquals("node:${TEST_TOOLS.jsRuntime}", command[i + 1])
        assertEquals(listOf("--", url), command.takeLast(2))
    }

    @Test
    fun downloadSavesTheAacStreamAsItIsIntoTheOutputDirectory() {
        val command = YtDlpCommands.download(TEST_TOOLS, url, Path.of("C:/work/job-1"), "dQw4w9WgXcQ")
        assertEquals("bestaudio[ext=m4a]", command[command.indexOf("-f") + 1])
        assertEquals("never", command[command.indexOf("--fixup") + 1])
        for (reencoding in listOf("-x", "--extract-audio", "--audio-format", "--audio-quality")) {
            assertFalse(reencoding in command, "the audio must not be re-encoded: $reencoding")
        }
        assertTrue(command[command.indexOf("-o") + 1].endsWith("dQw4w9WgXcQ.%(ext)s"))
        assertEquals(TEST_TOOLS.ffmpeg.toString(), command[command.indexOf("--ffmpeg-location") + 1])
        assertEquals(1, command.count { it == "--progress-template" })
        assertEquals(listOf("--", url), command.takeLast(2))
    }

    @Test
    fun downloadSavesTheThumbnailAsJpgNextToTheAudio() {
        val command = YtDlpCommands.download(TEST_TOOLS, url, Path.of("C:/work/job-1"), "dQw4w9WgXcQ")
        assertContains(command, "--write-thumbnail")
        assertEquals("jpg", command[command.indexOf("--convert-thumbnails") + 1])
        assertEquals(1, command.count { it == "--write-thumbnail" })
        assertEquals(1, command.count { it == "--convert-thumbnails" })
    }

    @Test
    fun thumbnailOptionsComeBeforeTheOutputTemplate() {
        val command = YtDlpCommands.download(TEST_TOOLS, url, Path.of("C:/work/job-1"), "dQw4w9WgXcQ")
        assertTrue(command.indexOf("--write-thumbnail") < command.indexOf("-o"))
        assertTrue(command.indexOf("--convert-thumbnails") < command.indexOf("-o"))
    }

    @Test
    fun downloadWritesTheInfoJsonBeforeTheOutputTemplate() {
        val command = YtDlpCommands.download(TEST_TOOLS, url, Path.of("C:/work/job-1"), "dQw4w9WgXcQ")
        assertEquals(1, command.count { it == "--write-info-json" })
        assertTrue(command.indexOf("--write-info-json") < command.indexOf("-o"))
        assertContains(command, "--no-playlist") // keeps it to one video, so one info file
    }

    @Test
    fun onlyTheDownloadCommandWritesAnInfoJson() {
        assertFalse(YtDlpCommands.resolveVideo(TEST_TOOLS, url).contains("--write-info-json"))
        assertFalse(YtDlpCommands.resolvePlaylist(TEST_TOOLS, url).contains("--write-info-json"))
    }

    @Test
    fun denoIsPassedWithItsPath() {
        val tools = TEST_TOOLS.copy(jsRuntime = Path.of("C:/tools/deno.exe"))
        val command = YtDlpCommands.resolveVideo(tools, url)
        assertEquals("deno:${tools.jsRuntime}", command[command.indexOf("--js-runtimes") + 1])
    }

    @Test
    fun noJsRuntimeFlagWhenNoneIsInstalled() {
        val command = YtDlpCommands.download(TEST_TOOLS.copy(jsRuntime = null, ffmpeg = null), url, Path.of("C:/w"), "id")
        assertFalse(command.contains("--js-runtimes"))
        assertFalse(command.contains("--ffmpeg-location"))
    }

    @Test
    fun ytDlpPathIsRequired() {
        assertFailsWith<IllegalArgumentException> { YtDlpCommands.resolveVideo(TEST_TOOLS.copy(ytDlp = null), url) }
    }
}
