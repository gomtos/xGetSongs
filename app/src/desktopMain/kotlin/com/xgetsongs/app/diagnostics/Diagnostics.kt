package com.xgetsongs.app.diagnostics

import org.slf4j.LoggerFactory
import java.awt.EventQueue
import java.awt.GraphicsEnvironment
import java.nio.file.Files
import java.nio.file.Path
import kotlin.time.Duration.Companion.nanoseconds

/**
 * The system property that tells logback.xml where the log files go. `main` sets it before anything asks for a logger:
 * logback reads its configuration, and with it this property, the first time that happens.
 */
internal const val LOG_DIR_PROPERTY = "xgs.logDir"

/**
 * Everything that helps to find out afterwards why the app vanished or froze: unhandled exceptions in the log, a record of
 * how the JVM was asked to exit, and a thread dump when the UI thread stops answering. [start] is called once, first thing
 * in `main` (after [LOG_DIR_PROPERTY] is set).
 */
internal object Diagnostics {
    private val log = LoggerFactory.getLogger(Diagnostics::class.java)

    /**
     * Prepares [logDir] (a failure is only reported on stderr), hooks the uncaught-exception handler, writes the startup
     * record, registers the exit logger through [registerHook] and starts the watchdog, which posts its heartbeats with
     * [postToUi].
     */
    fun start(
        logDir: Path,
        postToUi: (Runnable) -> Unit = EventQueue::invokeLater,
        registerHook: (Thread) -> Unit = Runtime.getRuntime()::addShutdownHook,
    ): DiagnosticsHandle {
        ensureLogDirectory(logDir)
        Thread.setDefaultUncaughtExceptionHandler(UncaughtLogger(Thread.getDefaultUncaughtExceptionHandler()))
        log.info(
            startupRecord(
                appVersion = Diagnostics::class.java.`package`?.implementationVersion ?: System.getProperty("jpackage.app-version"),
                property = System::getProperty,
                processors = Runtime.getRuntime().availableProcessors(),
                maxHeapMb = Runtime.getRuntime().maxMemory() / (1024 * 1024),
                headless = GraphicsEnvironment.isHeadless(),
                workingDir = Path.of("").toAbsolutePath().toString(),
                logDir = logDir,
            ),
        )

        val startedAt = System.nanoTime()
        val exitLogger = ExitLogger(runningJobs = { -1 }, uptime = { (System.nanoTime() - startedAt).nanoseconds })
        exitLogger.register(registerHook)
        val watchdog = UiWatchdog(logDir, postToUi)
        watchdog.start()
        return DiagnosticsHandle(exitLogger, watchdog)
    }
}

/** What `main` keeps from [Diagnostics.start]. */
internal class DiagnosticsHandle(private val exitLogger: ExitLogger, private val watchdog: UiWatchdog) {
    private val log = LoggerFactory.getLogger(DiagnosticsHandle::class.java)

    /** The number of running downloads, for the exit record: -1 (unknown) until the server is up and `main` says how to ask it. */
    var runningJobs: () -> Int
        get() = exitLogger.runningJobs
        set(value) {
            exitLogger.runningJobs = value
        }

    /** The window's close handler calls this: the exit that follows is the user's own, not one from outside. */
    fun markUserExit() {
        log.info("사용자가 창 닫기를 요청함")
        exitLogger.markUserExit()
    }

    /** Ends the watchdog thread. The exit record is still written when the JVM goes down. */
    fun stop() = watchdog.stop()
}

/** The startup record: one block of text, so it is one log record. Only the system properties listed here are read. */
internal fun startupRecord(
    appVersion: String?,
    property: (String) -> String?,
    processors: Int,
    maxHeapMb: Long,
    headless: Boolean,
    workingDir: String,
    logDir: Path,
): String = buildString {
    fun value(key: String) = property(key) ?: "?"
    appendLine("시작 정보")
    appendLine("  앱: xGetSongs${appVersion?.let { " $it" }.orEmpty()}")
    appendLine("  Java: ${value("java.version")} (${value("java.vendor")}), ${value("java.vm.name")}")
    appendLine("  OS: ${value("os.name")} ${value("os.version")} ${value("os.arch")}")
    appendLine("  프로세서 ${processors}개, 최대 힙 ${maxHeapMb}MB")
    appendLine("  작업 폴더: $workingDir")
    appendLine("  로그 폴더: $logDir")
    append("  headless=$headless")
}

/** Creates [logDir] (the log files would be created on first use anyway); a failure is only [report]ed, never thrown. */
internal fun ensureLogDirectory(logDir: Path, report: (String) -> Unit = System.err::println): Boolean = try {
    Files.createDirectories(logDir)
    true
} catch (e: Exception) {
    report("로그 폴더를 만들지 못함: $logDir ($e)")
    false
}

/** Writes what no thread caught to the log, with the thread's name, and then lets the handler that was there before see it. */
internal class UncaughtLogger(private val previous: Thread.UncaughtExceptionHandler?) : Thread.UncaughtExceptionHandler {
    private val log = LoggerFactory.getLogger(UncaughtLogger::class.java)

    override fun uncaughtException(thread: Thread, error: Throwable) {
        try {
            log.error("처리되지 않은 예외: 스레드 \"{}\"", thread.name, error)
        } catch (e: Exception) {
            // Logging is the thing that failed; the previous handler still gets its turn.
        }
        try {
            previous?.uncaughtException(thread, error)
        } catch (e: Exception) {
            // The JVM ignores what a handler throws, and so does this one.
        }
    }
}
