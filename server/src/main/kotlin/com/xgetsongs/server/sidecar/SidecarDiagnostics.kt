package com.xgetsongs.server.sidecar

import com.xgetsongs.diagnostics.Diagnostics
import com.xgetsongs.diagnostics.DiagnosticsHandle
import com.xgetsongs.diagnostics.LOG_DIR_PROPERTY
import com.xgetsongs.diagnostics.Processes
import com.xgetsongs.diagnostics.SystemProcesses
import com.xgetsongs.diagnostics.chooseLogDirectory
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import java.nio.file.Path

// No logger is declared in this file: logback reads its configuration the first time anything asks for one, and the log
// folder it needs is set by [SidecarDiagnostics.start] first.

/**
 * What the Compose app's `Main` does around its window, for the sidecar: the log folder is chosen and handed to logback,
 * then the diagnostics (startup record, uncaught exceptions, exit record, `last-run.txt`, hang dumps) are started.
 *
 * The sidecar has no UI thread. What can hang here is the work the server does on `Dispatchers.Default` (downloads, the
 * event streams), so the watchdog's heartbeats go there: when every worker of that dispatcher is stuck the heartbeat does
 * not run, and the thread dump of the stuck workers is written the way the app's UI hang dump is.
 */
internal object SidecarDiagnostics {
    private val heartbeats = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** Call this before anything asks for a logger. */
    fun start(
        args: SidecarArgs,
        tempDir: Path = Path.of(System.getProperty("java.io.tmpdir") ?: "."),
        registerHook: (Thread) -> Unit = Runtime.getRuntime()::addShutdownHook,
        processes: Processes = SystemProcesses,
    ): DiagnosticsHandle {
        val logDir = chooseLogDirectory(sidecarLogCandidates(args, tempDir))
        System.setProperty(LOG_DIR_PROPERTY, logDir.toString())
        return Diagnostics.start(
            logDir,
            postToUi = { heartbeat -> heartbeats.launch { heartbeat.run() } },
            registerHook = registerHook,
            processes = processes,
        )
    }
}
