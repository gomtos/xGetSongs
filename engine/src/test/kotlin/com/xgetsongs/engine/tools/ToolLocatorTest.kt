package com.xgetsongs.engine.tools

import java.io.File
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class ToolLocatorTest {
    private val root: Path = Files.createTempDirectory("xgs-tools")
    private val appBin = Files.createDirectories(root.resolve("app-bin"))
    private val pathDir = Files.createDirectories(root.resolve("path-dir"))

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private fun touch(dir: Path, name: String): Path = Files.writeString(dir.resolve(name), "x")

    private fun locator(ffmpegOverride: Path? = null) =
        ToolLocator(appBin, pathEnv = pathDir.toString() + File.pathSeparator + "Z:\\does\\not\\exist", ffmpegOverride = ffmpegOverride)

    @Test
    fun findsToolsOnPath() {
        touch(pathDir, "yt-dlp.exe")
        touch(pathDir, "ffmpeg.exe")
        touch(pathDir, "node.exe")

        val paths = locator().current()

        assertEquals(pathDir.resolve("yt-dlp.exe"), paths.ytDlp)
        assertEquals(pathDir.resolve("ffmpeg.exe"), paths.ffmpeg)
        assertEquals(pathDir.resolve("node.exe"), paths.jsRuntime)
    }

    @Test
    fun appBinDirectoryWinsOverPath() {
        touch(pathDir, "yt-dlp.exe")
        touch(appBin, "yt-dlp.exe")

        assertEquals(appBin.resolve("yt-dlp.exe"), locator().current().ytDlp)
    }

    @Test
    fun denoIsPreferredOverNode() {
        touch(pathDir, "node.exe")
        touch(pathDir, "deno.exe")

        assertEquals(pathDir.resolve("deno.exe"), locator().current().jsRuntime)
    }

    @Test
    fun missingToolsAreNull() {
        val paths = locator().current()

        assertNull(paths.ytDlp)
        assertNull(paths.ffmpeg)
        assertNull(paths.jsRuntime)
    }

    @Test
    fun ffmpegOverrideIsUsedWhenItExists() {
        touch(pathDir, "ffmpeg.exe")
        val custom = touch(root, "my-ffmpeg.exe")

        assertEquals(custom, locator(ffmpegOverride = custom).current().ffmpeg)
        assertEquals(pathDir.resolve("ffmpeg.exe"), locator(ffmpegOverride = root.resolve("gone.exe")).current().ffmpeg)
    }

    @Test
    fun toolInstalledLaterIsPickedUpWithoutRestart() {
        val locator = locator()
        assertNull(locator.current().ytDlp)

        touch(appBin, "yt-dlp.exe")

        assertEquals(appBin.resolve("yt-dlp.exe"), locator.current().ytDlp)
    }
}
