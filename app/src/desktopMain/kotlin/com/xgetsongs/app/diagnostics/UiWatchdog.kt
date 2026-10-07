package com.xgetsongs.app.diagnostics

import org.slf4j.Logger
import org.slf4j.LoggerFactory
import java.io.IOException
import java.nio.charset.StandardCharsets.UTF_8
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardOpenOption.APPEND
import java.nio.file.StandardOpenOption.CREATE
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/** Milliseconds from a clock that only goes forward, which is what a duration needs (not the wall clock). */
internal fun monotonicMillis(): Long = System.nanoTime() / 1_000_000

/**
 * Notices when the UI thread stops answering and leaves a record of what every thread was doing. Once per [intervalMs] a
 * daemon thread named `xgs-ui-watchdog` posts a heartbeat to the UI thread through [postToUi] (in the app that is
 * `EventQueue::invokeLater`: the Compose Desktop UI runs on the AWT event queue thread) and asks a [HangDetector] what it
 * makes of the beats so far.
 *
 * When the UI thread has not answered for [thresholdMs] the thread dump from [dump] goes to the log as an ERROR and into a
 * new `ui-hang-yyyyMMdd-HHmmss.txt` in [logDir]. While the hang lasts another section is appended to that file every
 * [repeatMs] (the log gets a line without the dump, or it would fill up with them); when the UI thread answers again the
 * log says how long it took. Only the newest [maxDumpFiles] of those files are kept.
 *
 * All the time-dependent parts are injected, and the loop body is the single function [tick], so tests drive it
 * directly and never wait. [tick] never throws.
 */
internal class UiWatchdog(
    private val logDir: Path,
    private val postToUi: (Runnable) -> Unit,
    private val dump: (silentMs: Long, reason: String) -> String = { silentMs, reason -> ThreadDump.capture(silentMs, reason) },
    private val clockMs: () -> Long = ::monotonicMillis,
    private val wallClock: () -> LocalDateTime = LocalDateTime::now,
    private val intervalMs: Long = 1_000,
    thresholdMs: Long = 5_000,
    repeatMs: Long = 30_000,
    maxTickGapMs: Long = 4 * intervalMs,
    private val maxDumpFiles: Int = 20,
) {
    private val log: Logger = LoggerFactory.getLogger(UiWatchdog::class.java)
    private val detector = HangDetector(clockMs(), thresholdMs, repeatMs, maxTickGapMs)

    @Volatile
    private var thread: Thread? = null

    // Touched only by the watchdog thread (or by whoever calls tick in a test).
    private var currentFile: Path? = null
    private var failedTicks = 0

    /** Starts the thread; a watchdog runs once. */
    @Synchronized
    fun start() {
        check(thread == null) { "The watchdog is already running" }
        thread = Thread(::loop, THREAD_NAME).apply {
            isDaemon = true
            start()
        }
    }

    /** Ends the thread. It is asleep nearly all the time, so this returns at once (the wait is only a safety net). */
    @Synchronized
    fun stop() {
        val running = thread ?: return
        thread = null
        running.interrupt()
        running.join(JOIN_MS)
    }

    private fun loop() {
        while (!Thread.currentThread().isInterrupted) {
            tick()
            try {
                Thread.sleep(intervalMs)
            } catch (e: InterruptedException) {
                return
            }
        }
    }

    /** One round of the loop: post a heartbeat, then check the beats so far. */
    fun tick() {
        try {
            val now = clockMs()
            postToUi(Runnable { detector.onUiBeat(clockMs()) })
            val gapsBefore = detector.gapsIgnored
            val event = detector.onTick(now)
            failedTicks = 0
            if (detector.gapsIgnored != gapsBefore) {
                log.info("UI 감시: 확인 간격이 크게 벌어져 이번 판정은 건너뜀 (절전이거나 감시 스레드가 지연됨, 누적 {}회)", detector.gapsIgnored)
            }
            when (event) {
                is HangEvent.Hung -> onHung(event)
                is HangEvent.Recovered -> onRecovered(event)
                null -> Unit
            }
        } catch (e: Exception) {
            // The first failure is logged, then a reminder every REMINDER_EVERY ticks: a broken heartbeat repeats every second.
            failedTicks++
            if (failedTicks == 1 || failedTicks % REMINDER_EVERY == 0) log.warn("UI 감시 중 오류: {}", e.toString())
        }
    }

    private fun onHung(event: HangEvent.Hung) {
        val seconds = event.silentMs / 1_000
        val reason = if (event.repeat) "UI 스레드 무응답 계속" else "UI 스레드 무응답"
        val text = try {
            dump(event.silentMs, reason)
        } catch (e: Exception) {
            log.warn("스레드 덤프를 만들지 못함: {}", e.toString())
            null
        }
        val file = text?.let { writeDump(it, repeat = event.repeat) }
        when {
            text == null -> log.error("UI 스레드가 {}초 동안 응답하지 않음 (덤프 없음)", seconds)
            event.repeat -> log.error("UI 스레드가 {}초 동안 응답하지 않음 (계속, 덤프: {})", seconds, file?.fileName ?: "파일 없음")
            else -> log.error("UI 스레드가 {}초 동안 응답하지 않음\n{}", seconds, text)
        }
    }

    private fun onRecovered(event: HangEvent.Recovered) {
        currentFile = null
        log.warn("UI 응답 회복 (총 {}초)", event.totalMs / 1_000)
    }

    /** Writes [text] as a new file, or as one more section of the file of the hang that is going on. Null when it cannot. */
    private fun writeDump(text: String, repeat: Boolean): Path? = try {
        Files.createDirectories(logDir)
        val file = currentFile?.takeIf { repeat && Files.exists(it) }
            ?: logDir.resolve("ui-hang-${fileNameTime.format(wallClock())}.txt")
        Files.writeString(file, text, UTF_8, CREATE, APPEND)
        currentFile = file
        deleteOldFiles(keep = file)
        file
    } catch (e: IOException) {
        log.warn("스레드 덤프 파일을 쓰지 못함: {}", e.toString())
        null
    }

    /** The names carry the time, so by name is by age. */
    private fun deleteOldFiles(keep: Path) {
        try {
            val dumps = Files.newDirectoryStream(logDir, "ui-hang-*.txt").use { stream -> stream.toList() }
            for (old in dumps.sortedByDescending { it.fileName.toString() }.drop(maxDumpFiles)) {
                if (old != keep) Files.deleteIfExists(old)
            }
        } catch (e: IOException) {
            log.warn("오래된 덤프 파일을 지우지 못함: {}", e.toString())
        }
    }

    private companion object {
        const val THREAD_NAME = "xgs-ui-watchdog"
        const val JOIN_MS = 500L
        const val REMINDER_EVERY = 60
        val fileNameTime: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss")
    }
}
