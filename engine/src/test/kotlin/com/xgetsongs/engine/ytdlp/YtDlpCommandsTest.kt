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
    fun downloadExtractsMp3IntoTheOutputDirectory() {
        val command = YtDlpCommands.download(TEST_TOOLS, url, Path.of("C:/work/job-1"), "dQw4w9WgXcQ")
        assertContains(command, "-x")
        assertEquals("mp3", command[command.indexOf("--audio-format") + 1])
        assertEquals("0", command[command.indexOf("--audio-quality") + 1])
        assertTrue(command[command.indexOf("-o") + 1].endsWith("dQw4w9WgXcQ.%(ext)s"))
        assertEquals(TEST_TOOLS.ffmpeg.toString(), command[command.indexOf("--ffmpeg-location") + 1])
        assertEquals(2, command.count { it == "--progress-template" })
        assertEquals(listOf("--", url), command.takeLast(2))
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
