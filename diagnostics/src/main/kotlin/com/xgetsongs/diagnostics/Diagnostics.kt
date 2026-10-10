package com.xgetsongs.diagnostics

import com.xgetsongs.shared.log.LogRedaction
import org.slf4j.LoggerFactory
import java.nio.file.Files
import java.nio.file.Path
import java.time.Instant
import java.util.Collections
import java.util.IdentityHashMap
import kotlin.time.Duration.Companion.nanoseconds

/**
 * The system property that tells logback.xml where the log files go. `main` sets it before anything asks for a logger:
 * logback reads its configuration, and with it this property, the first time that happens.
 */
const val LOG_DIR_PROPERTY = "xgs.logDir"

/** What this process and the others on the machine look like: the marker of the last run is judged with it. */
interface Processes {
    val ownPid: Long
    val ownStart: Instant

    /** The start time of the live process with number [pid], or null when there is none (or it will not say). */
    fun startOf(pid: Long): Instant?
}

object SystemProcesses : Processes {
    override val ownPid: Long get() = ProcessHandle.current().pid()
    override val ownStart: Instant by lazy { ProcessHandle.current().info().startInstant().orElseGet { Instant.now() } }
    override fun startOf(pid: Long): Instant? =
        ProcessHandle.of(pid).filter { it.isAlive }.flatMap { it.info().startInstant() }.orElse(null)
}

/**
 * Everything that helps to find out afterwards why the app vanished or froze: unhandled exceptions in the log, a record of
 * how the JVM was asked to exit, a marker file that tells the next run whether this one ended properly, and a thread dump
 * when the UI thread stops answering. [start] is called once, first thing in `main` (after [LOG_DIR_PROPERTY] is set).
 */
object Diagnostics {
    /** The marker of the last run, next to the log. */
    const val MARKER_FILE_NAME = "last-run.txt"

    private val log = LoggerFactory.getLogger(Diagnostics::class.java)

    /** Set by the uncaught-exception handler; the exit record mentions it. */
    @Volatile
    private var uncaughtSeen = false

    /**
     * Prepares [logDir] (a failure is only reported on stderr), hooks the uncaught-exception handler, writes the startup
     * record, tells what the marker of the previous run says and writes the marker of this one, registers the exit logger
     * through [registerHook] and starts the watchdog, which posts its heartbeats with [postToUi].
     *
     * [postToUi] runs a Runnable on the thread whose answers the watchdog waits for (the app: the AWT event queue; the
     * sidecar: a coroutine dispatcher). [headless] is only written into the startup record; null leaves the line out.
     */
    fun start(
        logDir: Path,
        postToUi: (Runnable) -> Unit,
        registerHook: (Thread) -> Unit = Runtime.getRuntime()::addShutdownHook,
        processes: Processes = SystemProcesses,
        headless: Boolean? = null,
    ): DiagnosticsHandle {
        ensureLogDirectory(logDir)
        uncaughtSeen = false
        Thread.setDefaultUncaughtExceptionHandler(
            UncaughtLogger(Thread.getDefaultUncaughtExceptionHandler(), onUncaught = { uncaughtSeen = true }),
        )
        log.info(
            startupRecord(
                appVersion = Diagnostics::class.java.`package`?.implementationVersion ?: System.getProperty("jpackage.app-version"),
                property = System::getProperty,
                processors = Runtime.getRuntime().availableProcessors(),
                maxHeapMb = Runtime.getRuntime().maxMemory() / (1024 * 1024),
                headless = headless,
                workingDir = Path.of("").toAbsolutePath().toString(),
                logDir = logDir,
            ),
        )

        val marker = startMarker(logDir, processes)

        val startedAt = System.nanoTime()
        val exitLogger = ExitLogger(
            runningJobs = { -1 },
            uptime = { (System.nanoTime() - startedAt).nanoseconds },
            uncaughtSeen = { uncaughtSeen },
            onExit = { userRequested -> marker?.markExited(userRequested) },
        )
        exitLogger.register(registerHook)
        val watchdog = UiWatchdog(logDir, postToUi)
        watchdog.start()
        return DiagnosticsHandle(exitLogger, watchdog)
    }

    /**
     * Tells what the marker of the previous run says and writes the marker of this one. A marker that was edited by hand, a
     * process table that fails, a start time that cannot be read: none of that may keep the app from starting, so each step
     * is guarded and a failure is a WARN line. Null when the marker of this run cannot be set up at all (nothing is
     * rewritten at exit then).
     */
    private fun startMarker(logDir: Path, processes: Processes): RunMarkerFile? {
        val marker = try {
            RunMarkerFile(logDir.resolve(MARKER_FILE_NAME), processes.ownPid, processes.ownStart)
        } catch (e: Exception) {
            log.warn("이번 실행 기록을 만들지 못함 ({})", e.javaClass.simpleName)
            return null
        }
        try {
            describePreviousRun(judgePreviousRun(marker.read(), processes::startOf))?.let { note ->
                if (note.warn) log.warn(note.text) else log.info(note.text)
            }
        } catch (e: Exception) {
            log.warn("이전 실행 기록을 확인하지 못함 ({})", e.javaClass.simpleName)
        }
        marker.markRunning()
        return marker
    }
}

/** What `main` keeps from [Diagnostics.start]. */
class DiagnosticsHandle internal constructor(private val exitLogger: ExitLogger, private val watchdog: UiWatchdog) {
    private val log = LoggerFactory.getLogger(DiagnosticsHandle::class.java)

    /** The number of jobs the server has registered, for the exit record: -1 (unknown) until the server is up and `main` says how to ask it. */
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
    headless: Boolean?,
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
    append("  로그 폴더: $logDir")
    if (headless != null) append("\n  headless=$headless")
}

/** Creates [logDir] (the log files would be created on first use anyway); a failure is only [report]ed, never thrown. */
internal fun ensureLogDirectory(logDir: Path, report: (String) -> Unit = System.err::println): Boolean = try {
    Files.createDirectories(logDir)
    true
} catch (e: Exception) {
    report("로그 폴더를 만들지 못함: $logDir ($e)")
    false
}

/**
 * The folder for the log files, in the order they are tried: a `log` folder next to the application ([applicationDir]; not
 * known when null), the app's data folder, the temp folder.
 */
fun logDirectoryCandidates(applicationDir: Path?, appDataDir: Path, tempDir: Path): List<Path> =
    listOfNotNull(applicationDir?.resolve("log"), appDataDir.resolve("logs"), tempDir.resolve("xgetsongs-logs"))

/**
 * The folder the application runs from: where the native launcher is when there is one (jpackage sets
 * `jpackage.app-path`); else the folder of the jar, or the classes folder itself when the classes are not in a jar; and
 * when that sits inside a source checkout, the root of the checkout (the first folder above that holds a Gradle settings
 * file, [isProjectRoot]), so that a run from Gradle does not put its `log` folder inside `build`. Null when nothing tells
 * ([property] and [codeSource] are what a test replaces).
 */
fun applicationDirectory(
    property: (String) -> String? = { System.getProperty(it) },
    codeSource: () -> Path? = {
        Diagnostics::class.java.protectionDomain?.codeSource?.location?.toURI()?.let { Path.of(it) }
    },
    isProjectRoot: (Path) -> Boolean = { Files.exists(it.resolve("settings.gradle.kts")) || Files.exists(it.resolve("settings.gradle")) },
): Path? {
    val launcher = property("jpackage.app-path")?.takeIf { it.isNotBlank() }
        ?.let { runCatching { Path.of(it).toAbsolutePath().parent }.getOrNull() }
    if (launcher != null) return launcher
    val source = runCatching(codeSource).getOrNull() ?: return null
    val folder = if (Files.isDirectory(source)) source else source.toAbsolutePath().parent
    return generateSequence(folder) { it.parent }.firstOrNull { runCatching { isProjectRoot(it) }.getOrDefault(false) } ?: folder
}

/**
 * The first of [candidates] that can be created; the last one when none can. `main` calls this before it sets
 * [LOG_DIR_PROPERTY], so logback is never told about a folder that does not exist. It never throws; when even the last
 * candidate cannot be created it is still the answer, and logback then writes to the console only.
 */
fun chooseLogDirectory(candidates: List<Path>, report: (String) -> Unit = System.err::println): Path {
    for ((index, candidate) in candidates.withIndex()) {
        if (ensureLogDirectory(candidate, report)) return candidate
        candidates.getOrNull(index + 1)?.let { report("로그를 대신 이 폴더에 씀: $it") }
    }
    return candidates.last()
}

/**
 * Writes what no thread caught to the log, with the thread's name, and then lets the handler that was there before see it.
 * [onUncaught] is told first. The exception is written out by [render] (by default [describeThrowable]), not handed to
 * logback, so that paths in its message (and in those of its causes) can be removed. Every step is guarded against any
 * [Throwable]: this runs when something has gone wrong already, an [Error] while writing the text out still leaves a line
 * with the thread and the class, and the previous handler always gets its turn.
 */
internal class UncaughtLogger(
    private val previous: Thread.UncaughtExceptionHandler?,
    private val onUncaught: () -> Unit = {},
    private val render: (Throwable) -> String = ::describeThrowable,
) : Thread.UncaughtExceptionHandler {
    private val log = LoggerFactory.getLogger(UncaughtLogger::class.java)

    override fun uncaughtException(thread: Thread, error: Throwable) {
        try {
            onUncaught()
        } catch (e: Throwable) {
            // Only a flag for the exit record.
        }
        try {
            log.error("처리되지 않은 예외: 스레드 \"{}\"\n{}", thread.name, render(error))
        } catch (e: Throwable) {
            try {
                log.error("처리되지 않은 예외: 스레드 \"{}\" ({}, 내용을 기록하지 못함)", thread.name, error.javaClass.name)
            } catch (inner: Throwable) {
                // Logging is the thing that failed; the previous handler still gets its turn.
            }
        }
        try {
            previous?.uncaughtException(thread, error)
        } catch (e: Throwable) {
            // The JVM ignores what a handler throws, and so does this one.
        }
    }
}

private const val MAX_FRAMES_PER_THROWABLE = 100
private const val MAX_CAUSES = 10

/**
 * [error] as a stack trace in text: the class and the message (without paths) of the exception and of each cause, with at
 * most [MAX_FRAMES_PER_THROWABLE] frames each. A cause that was seen already ends the chain (there are cycles).
 */
internal fun describeThrowable(error: Throwable): String {
    val seen = Collections.newSetFromMap(IdentityHashMap<Throwable, Boolean>())
    val text = StringBuilder()
    var current: Throwable? = error
    while (current != null && seen.size < MAX_CAUSES && seen.add(current)) {
        if (seen.size > 1) text.append("Caused by: ")
        text.append(current.javaClass.name)
        current.message?.let { text.append(": ").append(LogRedaction.redactPaths(it)) }
        text.append('\n')
        val frames = current.stackTrace
        for (frame in frames.take(MAX_FRAMES_PER_THROWABLE)) text.append("\tat ").append(frame).append('\n')
        if (frames.size > MAX_FRAMES_PER_THROWABLE) text.append("\t... ${frames.size - MAX_FRAMES_PER_THROWABLE}개 프레임 생략\n")
        current = current.cause
    }
    return text.toString().trimEnd()
}
