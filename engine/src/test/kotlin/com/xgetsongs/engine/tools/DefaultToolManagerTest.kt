package com.xgetsongs.engine.tools

import com.xgetsongs.engine.ToolException
import com.xgetsongs.engine.testutil.FakeProcessRunner
import kotlinx.coroutines.test.runTest
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class DefaultToolManagerTest {
    private val root: Path = Files.createTempDirectory("xgs-manager")
    private val binDir = root.resolve("bin")

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private fun runnerFor(outputs: Map<String, String>, exitCode: Int = 0) =
        FakeProcessRunner { command, onStdout, _ ->
            val tool = Path.of(command.first()).fileName.toString().substringBefore('.')
            outputs[tool]?.lines()?.forEach(onStdout)
            if (outputs.containsKey(tool)) exitCode else 1
        }

    private fun paths(ytDlp: Boolean = true, js: String = "node") = ToolPaths(
        ytDlp = if (ytDlp) Path.of("C:/t/yt-dlp.exe") else null,
        ffmpeg = Path.of("C:/t/ffmpeg.exe"),
        jsRuntime = Path.of("C:/t/$js.exe"),
    )

    @Test
    fun reportsVersionsOfAllTools() = runTest {
        val runner = runnerFor(
            mapOf(
                "yt-dlp" to "2026.10.01",
                "ffmpeg" to "ffmpeg version 8.1-full_build-www.gyan.dev Copyright (c) 2000-2026 the FFmpeg developers",
                "node" to "v24.11.1",
            ),
        )

        val status = DefaultToolManager(ToolPathProvider { paths() }, runner, binDir).status()

        assertTrue(status.ytDlp.found)
        assertEquals("2026.10.01", status.ytDlp.version)
        assertEquals("8.1-full_build-www.gyan.dev", status.ffmpeg.version)
        assertTrue(status.jsRuntime.found)
        assertEquals("24.11.1", status.jsRuntime.version)
    }

    @Test
    fun denoVersionIsParsedFromItsBanner() = runTest {
        val runner = runnerFor(mapOf("yt-dlp" to "1", "ffmpeg" to "ffmpeg version 7", "deno" to "deno 2.5.0 (stable, release, x86_64-pc-windows-msvc)"))

        val status = DefaultToolManager(ToolPathProvider { paths(js = "deno") }, runner, binDir).status()

        assertTrue(status.jsRuntime.found)
        assertEquals("2.5.0", status.jsRuntime.version)
    }

    @Test
    fun oldJsRuntimesAreReportedAsNotUsable() = runTest {
        val old = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(mapOf("yt-dlp" to "1", "ffmpeg" to "ffmpeg version 7", "node" to "v20.11.0")),
            binDir,
        ).status()
        assertFalse(old.jsRuntime.found)
        assertEquals("20.11.0", old.jsRuntime.version)

        val oldDeno = DefaultToolManager(
            ToolPathProvider { paths(js = "deno") },
            runnerFor(mapOf("yt-dlp" to "1", "ffmpeg" to "ffmpeg version 7", "deno" to "deno 2.2.9 (stable)")),
            binDir,
        ).status()
        assertFalse(oldDeno.jsRuntime.found)
    }

    @Test
    fun missingToolsAreReportedNotFound() = runTest {
        val manager = DefaultToolManager(
            ToolPathProvider { ToolPaths(null, null, null) },
            runnerFor(emptyMap()),
            binDir,
        )

        val status = manager.status()

        assertFalse(status.ytDlp.found)
        assertFalse(status.ffmpeg.found)
        assertFalse(status.jsRuntime.found)
    }

    @Test
    fun installDownloadsYtDlpIntoTheAppBinDirectory() = runTest {
        val provider = ToolPathProvider {
            ToolPaths(binDir.resolve("yt-dlp.exe").takeIf { Files.exists(it) }, null, null)
        }
        val fetched = mutableListOf<String>()
        val manager = DefaultToolManager(
            provider,
            runnerFor(mapOf("yt-dlp" to "2026.10.01")),
            binDir,
            fetch = { url -> fetched += url; ByteArray(2_000_000) },
        )

        val result = manager.installYtDlp()

        assertEquals(listOf(DefaultToolManager.YTDLP_URL), fetched)
        assertTrue(Files.exists(binDir.resolve("yt-dlp.exe")))
        assertTrue(result.message.contains("2026.10.01"))
    }

    @Test
    fun installRejectsATooSmallDownload() = runTest {
        val manager = DefaultToolManager(ToolPathProvider { paths() }, runnerFor(emptyMap()), binDir, fetch = { ByteArray(10) })

        assertFailsWith<ToolException> { manager.installYtDlp() }
        assertFalse(Files.exists(binDir.resolve("yt-dlp.exe")))
    }

    @Test
    fun installReportsNetworkFailures() = runTest {
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(emptyMap()),
            binDir,
            fetch = { throw IOException("offline") },
        )

        val error = assertFailsWith<ToolException> { manager.installYtDlp() }

        assertTrue(error.message!!.contains("offline"))
    }

    @Test
    fun installRejectsADownloadThatDoesNotRunAndKeepsTheOldBinary() = runTest {
        Files.createDirectories(binDir)
        Files.writeString(binDir.resolve("yt-dlp.exe"), "old")
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(emptyMap()),
            binDir,
            fetch = { ByteArray(2_000_000) },
        )

        assertFailsWith<ToolException> { manager.installYtDlp() }

        assertEquals("old", Files.readString(binDir.resolve("yt-dlp.exe")))
        assertFalse(Files.exists(binDir.resolve("yt-dlp.new.exe")))
    }

    @Test
    fun installReplacesAnExistingBinaryOnlyAfterTheNewOneRuns() = runTest {
        Files.createDirectories(binDir)
        Files.writeString(binDir.resolve("yt-dlp.exe"), "old")
        var textWhenVerified: String? = null
        val runner = FakeProcessRunner { command, onStdout, _ ->
            if (command.first().endsWith("yt-dlp.new.exe")) {
                textWhenVerified = Files.readString(binDir.resolve("yt-dlp.exe"))
                onStdout("2026.10.01")
                0
            } else {
                1
            }
        }
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            runner,
            binDir,
            fetch = { ByteArray(2_000_000) },
        )

        manager.installYtDlp()

        assertEquals("old", textWhenVerified)
        assertEquals(2_000_000L, Files.size(binDir.resolve("yt-dlp.exe")))
        assertFalse(Files.exists(binDir.resolve("yt-dlp.new.exe")))
        val names = Files.list(binDir).use { stream -> stream.map { it.fileName.toString() }.toList() }
        assertEquals(listOf("yt-dlp.exe"), names)
    }

    @Test
    fun installReportsFileSystemFailuresAsToolExceptions() = runTest {
        val blocker = root.resolve("blocker")
        Files.writeString(blocker, "not a directory")
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(mapOf("yt-dlp" to "2026.10.01")),
            blocker.resolve("bin"),
            fetch = { ByteArray(2_000_000) },
        )

        val error = assertFailsWith<ToolException> { manager.installYtDlp() }

        assertTrue(error.message!!.contains("\uc124\uce58\ud558\uc9c0 \ubabb\ud588\uc2b5\ub2c8\ub2e4"))
    }

    @Test
    fun installReportsAMoveFailureAndKeepsTheOldBinary() = runTest {
        val target = binDir.resolve("yt-dlp.exe")
        Files.createDirectories(target)
        Files.writeString(target.resolve("inner.txt"), "keep")
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(mapOf("yt-dlp" to "2026.10.01")),
            binDir,
            fetch = { ByteArray(2_000_000) },
        )

        val error = assertFailsWith<ToolException> { manager.installYtDlp() }

        assertTrue(error.message!!.contains("\uc124\uce58\ud558\uc9c0 \ubabb\ud588\uc2b5\ub2c8\ub2e4"))
        assertTrue(Files.isDirectory(target))
        assertEquals("keep", Files.readString(target.resolve("inner.txt")))
        assertFalse(Files.exists(binDir.resolve("yt-dlp.new.exe")))
    }

    @Test
    fun updateRunsYtDlpSelfUpdate() = runTest {
        val runner = runnerFor(mapOf("yt-dlp" to "Current version: 2026.10.01\nUpdated yt-dlp to 2026.11.02"))
        val manager = DefaultToolManager(ToolPathProvider { paths() }, runner, binDir)

        val result = manager.updateYtDlp()

        assertTrue(runner.commands.single().contains("-U"))
        assertEquals("Updated yt-dlp to 2026.11.02", result.message)
    }

    @Test
    fun updateFailureIsReported() = runTest {
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            runnerFor(mapOf("yt-dlp" to "ERROR: Unable to update"), exitCode = 1),
            binDir,
        )

        val error = assertFailsWith<ToolException> { manager.updateYtDlp() }

        assertEquals("ERROR: Unable to update", error.message)
    }

    @Test
    fun updateNeedsAnInstalledYtDlp() = runTest {
        val manager = DefaultToolManager(ToolPathProvider { paths(ytDlp = false) }, runnerFor(emptyMap()), binDir)

        assertFailsWith<ToolException> { manager.updateYtDlp() }
    }

    @Test
    fun updateReportsAnExecutableThatCannotBeStarted() = runTest {
        val manager = DefaultToolManager(
            ToolPathProvider { paths() },
            FakeProcessRunner { _, _, _ -> throw IOException("Cannot run program") },
            binDir,
        )

        val error = assertFailsWith<ToolException> { manager.updateYtDlp() }

        assertTrue(error.message!!.contains("\uc2e4\ud589\ud560 \uc218 \uc5c6\uc2b5\ub2c8\ub2e4"))
        assertTrue(error.message!!.contains("Cannot run program"))
    }
}
