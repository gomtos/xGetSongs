package com.xgetsongs.server.sidecar

import com.xgetsongs.diagnostics.LOG_DIR_PROPERTY
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.joinAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertTrue

class SidecarDiagnosticsTest {
    private val base: Path = Files.createTempDirectory("xgs-sidecar-diag").toAbsolutePath()
    private val previousHandler = Thread.getDefaultUncaughtExceptionHandler()

    @AfterTest
    fun cleanUp() {
        Thread.setDefaultUncaughtExceptionHandler(previousHandler) // Diagnostics.start hooks it
        System.clearProperty(LOG_DIR_PROPERTY)
        base.toFile().deleteRecursively()
    }

    private fun hangFiles(logDir: Path): List<Path> =
        if (!Files.isDirectory(logDir)) emptyList()
        else Files.list(logDir).use { files -> files.filter { it.fileName.toString().startsWith("ui-hang-") }.toList() }

    @Test
    fun aDispatcherThatStopsAnsweringLeavesAThreadDumpInTheLogFolder() = runBlocking {
        val logDir = base.resolve("log")
        val handle = SidecarDiagnostics.start(SidecarArgs(base.resolve("data"), logDir), tempDir = base, registerHook = { })
        try {
            // Every worker of the default dispatcher is busy for longer than the watchdog's limit (5 s): the heartbeats
            // the watchdog posts there cannot run, which is what the server's downloads would look like when stuck.
            val stall = List(Runtime.getRuntime().availableProcessors()) { launch(Dispatchers.Default) { Thread.sleep(9_000) } }
            val deadline = System.nanoTime() + 25_000_000_000L
            while (hangFiles(logDir).isEmpty() && System.nanoTime() < deadline) delay(200)

            val files = hangFiles(logDir)
            assertTrue(files.isNotEmpty(), "a hang dump was written")
            assertTrue("DefaultDispatcher-worker" in Files.readString(files.first()), "the dump shows the stuck workers")
            stall.joinAll()
        } finally {
            handle.stop()
        }
    }
}
