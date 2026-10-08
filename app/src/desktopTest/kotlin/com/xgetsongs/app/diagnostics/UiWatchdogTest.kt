package com.xgetsongs.app.diagnostics

import ch.qos.logback.classic.Level
import java.nio.file.Files
import java.nio.file.Path
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

// Time is a variable here and the heartbeat is a queue the test empties when it wants the "UI thread" to answer, so the
// loop body (tick) is called directly and nothing sleeps. Only the last tests start the real thread.
class UiWatchdogTest {
    private val dir: Path = Files.createTempDirectory("xgs-watchdog")
    private val capture = LogCapture(UiWatchdog::class.java)

    private var now = 0L
    private var wallTime = LocalDateTime.of(2026, 10, 7, 21, 3, 11)
    private var uiAnswers = false
    private val queuedHeartbeats = mutableListOf<Runnable>()
    private var posts = 0
    private val dumps = mutableListOf<String>()

    @AfterTest
    fun cleanUp() {
        capture.close()
        dir.toFile().deleteRecursively()
    }

    private fun syntheticDump(silentMs: Long, reason: String): String {
        val awt = Thread(Runnable { }, "AWT-EventQueue-0")
        val main = Thread(Runnable { }, "main")
        val frame = arrayOf(StackTraceElement("com.example.Stuck", "wait", "Stuck.kt", 7))
        // Put the AWT thread last in the map: the rendering has to bring it to the front.
        val stacks = linkedMapOf(main to frame, awt to frame)
        return ThreadDump.render(wallTime, silentMs, reason, stacks, emptyMap(), MemorySnapshot(1, 2, 3), emptyList())
    }

    private fun watchdog(
        logDir: Path = dir,
        maxDumpFiles: Int = 20,
        maxDumpFileBytes: Long = 5L * 1024 * 1024,
        dump: (Long, String) -> String = { silentMs, reason -> syntheticDump(silentMs, reason).also { dumps += it } },
    ) = UiWatchdog(
        logDir = logDir,
        postToUi = { heartbeat ->
            posts++
            if (uiAnswers) heartbeat.run() else queuedHeartbeats += heartbeat
        },
        dump = dump,
        clockMs = { now },
        wallClock = { wallTime },
        maxDumpFiles = maxDumpFiles,
        maxDumpFileBytes = maxDumpFileBytes,
    )

    /** One second passes, then the watchdog thread's loop body runs; [count] times. */
    private fun UiWatchdog.ticks(count: Int) = repeat(count) {
        now += 1_000
        tick()
    }

    /** The UI thread gets to its queue: every heartbeat posted so far runs, at the current time. */
    private fun answerQueuedHeartbeats() {
        queuedHeartbeats.toList().forEach { it.run() }
        queuedHeartbeats.clear()
    }

    private fun dumpFiles(): List<String> =
        Files.newDirectoryStream(dir, "ui-hang-*.txt").use { stream -> stream.map { it.fileName.toString() }.sorted() }

    private fun messages(level: Level) = capture.at(level).map { it.formattedMessage }

    @Test
    fun aFrozenUiGetsOneDumpFileAndOneErrorRecord() {
        val watchdog = watchdog()

        watchdog.ticks(5)
        assertEquals(0, dumps.size, "5 s without an answer is not a hang yet")
        assertEquals(emptyList(), dumpFiles())

        watchdog.ticks(5)

        assertEquals(1, dumps.size, "one dump, not one per tick")
        assertEquals(listOf("ui-hang-20261007-210311.txt"), dumpFiles())
        val text = Files.readString(dir.resolve("ui-hang-20261007-210311.txt"))
        assertEquals(dumps.single(), text, "the file holds the dump")
        assertTrue(text.indexOf("AWT-EventQueue-0") in 0 until text.indexOf("\"main\""), "the event queue thread comes first")
        val errors = messages(Level.ERROR)
        assertEquals(1, errors.size)
        assertTrue("UI 스레드가 6초 동안 응답하지 않음" in errors.single(), errors.single())
        assertTrue(dumps.single() in errors.single(), "the log record holds the dump too")
        assertEquals(1, queuedHeartbeats.size, "only one heartbeat waits in the queue of the blocked UI thread")
    }

    @Test
    fun aNewHeartbeatIsPostedOnlyWhenThePreviousOneHasRun() {
        val watchdog = watchdog()

        watchdog.ticks(10)
        assertEquals(1, posts, "ten ticks, one heartbeat")

        answerQueuedHeartbeats()
        assertEquals(0, queuedHeartbeats.size)
        watchdog.ticks(1)
        assertEquals(2, posts, "the next tick posts again")
        watchdog.ticks(5)
        assertEquals(2, posts)
    }

    @Test
    fun theBeatCountsFromTheTimeTheHeartbeatRunsNotFromTheTimeItWasPosted() {
        val watchdog = watchdog()
        watchdog.ticks(3) // the heartbeat is posted at 1 s and waits in the queue

        now = 4_000
        answerQueuedHeartbeats() // it runs at 4 s: that is the beat
        watchdog.ticks(5) // 5 s to 9 s; the next heartbeat (posted at 5 s) is never run

        assertEquals(0, dumps.size, "9 s after the post but only 5 s after the beat: not more than the threshold")
        watchdog.ticks(1)
        assertEquals(1, dumps.size, "10 s: 6 s since the beat")
    }

    @Test
    fun aHeartbeatThatCouldNotBePostedIsNotLeftPendingForever() {
        var failNext = true
        val delivered = mutableListOf<Runnable>()
        val watchdog = UiWatchdog(
            logDir = dir,
            postToUi = { heartbeat ->
                if (failNext) throw IllegalStateException("no queue")
                delivered += heartbeat
            },
            clockMs = { now },
        )
        watchdog.ticks(2) // both fail

        failNext = false
        watchdog.ticks(2)

        assertEquals(1, delivered.size, "the post that works goes out, and the next waits for it to run")
    }

    @Test
    fun theLogSaysWhenTheUiAnswersAgain() {
        val watchdog = watchdog()
        watchdog.ticks(6)
        assertEquals(1, messages(Level.ERROR).size)
        assertEquals(emptyList(), messages(Level.WARN))

        now += 2_000
        answerQueuedHeartbeats() // the UI thread was blocked until now, 8 s after its last answer
        uiAnswers = true
        watchdog.ticks(1)

        assertEquals(1, messages(Level.WARN).size)
        assertEquals("UI 응답 회복 (총 8초)", messages(Level.WARN).single())
        watchdog.ticks(60)
        assertEquals(1, messages(Level.ERROR).size, "no new report once it answers")
        assertEquals(1, messages(Level.WARN).size, "the recovery is reported once")
        assertEquals(1, dumps.size)
    }

    @Test
    fun aUiThatAnswersImmediatelyNeverGetsADump() {
        uiAnswers = true
        val watchdog = watchdog()

        watchdog.ticks(300)

        assertEquals(0, dumps.size)
        assertEquals(emptyList(), dumpFiles())
        assertEquals(emptyList(), capture.at(Level.ERROR))
        assertEquals(emptyList(), capture.at(Level.WARN))
        assertEquals(emptyList(), queuedHeartbeats)
    }

    @Test
    fun aLastingHangAppendsAnotherSectionToTheSameFileEveryThirtySeconds() {
        val watchdog = watchdog()

        watchdog.ticks(36)

        assertEquals(2, dumps.size)
        assertEquals(listOf("ui-hang-20261007-210311.txt"), dumpFiles())
        val text = Files.readString(dir.resolve("ui-hang-20261007-210311.txt"))
        assertEquals(2, Regex("스레드 덤프").findAll(text).count(), "two sections")
        assertTrue("무응답 6000ms" in dumps[0] && "무응답 36000ms" in dumps[1], "the sections tell how long it has lasted")
        val errors = messages(Level.ERROR)
        assertEquals(2, errors.size)
        assertTrue("UI 스레드가 36초 동안 응답하지 않음" in errors[1] && "계속" in errors[1] && "ui-hang-20261007-210311.txt" in errors[1], errors[1])
        assertFalse(dumps[1] in errors[1], "a repeat puts the dump in the file only: the log would fill up")
    }

    @Test
    fun aNewHangAfterARecoveryGetsANewFile() {
        val watchdog = watchdog()
        watchdog.ticks(6)
        answerQueuedHeartbeats()
        watchdog.ticks(1)
        assertEquals(1, messages(Level.WARN).size)

        wallTime = LocalDateTime.of(2026, 10, 7, 21, 10, 0)
        watchdog.ticks(8) // nobody answers the new heartbeats

        assertEquals(listOf("ui-hang-20261007-210311.txt", "ui-hang-20261007-211000.txt"), dumpFiles())
        assertEquals(2, dumps.size)
        assertEquals(1, Regex("스레드 덤프").findAll(Files.readString(dir.resolve("ui-hang-20261007-210311.txt"))).count())
        assertEquals(1, Regex("스레드 덤프").findAll(Files.readString(dir.resolve("ui-hang-20261007-211000.txt"))).count())
        assertEquals(2, messages(Level.ERROR).size)
    }

    @Test
    fun onlyTheNewestTwentyDumpFilesAreKept() {
        for (i in 0 until 25) Files.writeString(dir.resolve("ui-hang-20260101-0000%02d.txt".format(i)), "old $i")
        Files.writeString(dir.resolve("xgetsongs.log"), "the log")
        Files.writeString(dir.resolve("notes.txt"), "not a dump")
        val watchdog = watchdog()

        watchdog.ticks(6)

        val kept = dumpFiles()
        assertEquals(20, kept.size, kept.toString())
        assertTrue("ui-hang-20261007-210311.txt" in kept, "the new dump is never the one that goes")
        assertEquals((6 until 25).map { "ui-hang-20260101-0000%02d.txt".format(it) }, kept - "ui-hang-20261007-210311.txt", "the six oldest went")
        assertTrue(Files.exists(dir.resolve("xgetsongs.log")))
        assertTrue(Files.exists(dir.resolve("notes.txt")))
    }

    @Test
    fun theNumberOfKeptFilesCanBeChosen() {
        for (i in 0 until 5) Files.writeString(dir.resolve("ui-hang-20260101-0000%02d.txt".format(i)), "old $i")
        val watchdog = watchdog(maxDumpFiles = 3)

        watchdog.ticks(6)

        assertEquals(listOf("ui-hang-20260101-000003.txt", "ui-hang-20260101-000004.txt", "ui-hang-20261007-210311.txt"), dumpFiles())
    }

    @Test
    fun aFolderThatCannotBeWrittenStillGetsTheDumpInTheLog() {
        val blocker = Files.writeString(dir.resolve("a-file"), "not a folder")
        val watchdog = watchdog(logDir = blocker.resolve("logs"))

        watchdog.ticks(6) // must not throw

        val errors = messages(Level.ERROR)
        assertEquals(1, errors.size)
        assertTrue(dumps.single() in errors.single(), "the log record still holds the dump")
        assertTrue(messages(Level.WARN).any { "덤프 파일" in it }, messages(Level.WARN).toString())
        watchdog.ticks(2) // and it keeps going
    }

    @Test
    fun aDumpThatCannotBeMadeStillLogsTheHangAndDoesNotKillTheTick() {
        var attempts = 0
        val watchdog = watchdog(dump = { _, _ -> attempts++; throw IllegalStateException("boom") })

        watchdog.ticks(8) // must not throw

        assertEquals(1, attempts)
        assertEquals(1, messages(Level.ERROR).size)
        assertTrue("UI 스레드가 6초 동안 응답하지 않음" in messages(Level.ERROR).single())
        assertTrue(messages(Level.WARN).any { "IllegalStateException" in it }, messages(Level.WARN).toString())
        assertEquals(emptyList(), dumpFiles())
    }

    @Test
    fun aHeartbeatThatCannotBePostedDoesNotKillTheTick() {
        val watchdog = UiWatchdog(logDir = dir, postToUi = { throw IllegalStateException("no queue") }, clockMs = { now })

        watchdog.ticks(3) // must not throw

        assertTrue(messages(Level.WARN).any { "IllegalStateException" in it }, messages(Level.WARN).toString())
    }

    @Test
    fun aFailureThatKeepsComingIsLoggedOnceInAWhileNotEverySecond() {
        val watchdog = UiWatchdog(logDir = dir, postToUi = { throw IllegalStateException("no queue") }, clockMs = { now })

        watchdog.ticks(59)
        assertEquals(1, messages(Level.WARN).size, "the first one is logged, the 58 after it are not")

        watchdog.ticks(1)
        assertEquals(2, messages(Level.WARN).size, "and a reminder at the sixtieth")
    }

    @Test
    fun aGapBetweenTicksIsLoggedButNotReportedAsAHang() {
        val watchdog = watchdog()
        watchdog.ticks(3)

        now += 3_600_000 // the machine slept for an hour
        watchdog.tick()

        assertEquals(emptyList(), capture.at(Level.ERROR))
        assertEquals(emptyList(), capture.at(Level.WARN))
        assertEquals(0, dumps.size)
        val info = messages(Level.INFO).single()
        assertTrue("확인 간격" in info && "1회" in info, info)
    }

    @Test
    fun theThreadIsADaemonAndStopEndsIt() {
        val threads = ConcurrentLinkedQueue<Thread>()
        val ticked = CountDownLatch(3)
        val watchdog = UiWatchdog(
            logDir = dir,
            postToUi = { heartbeat ->
                heartbeat.run() // the "UI" answers at once, so the real clock never sees a hang
                threads += Thread.currentThread()
                ticked.countDown()
            },
            intervalMs = 5,
        )

        watchdog.start()
        assertTrue(ticked.await(30, TimeUnit.SECONDS), "the thread ticks")
        watchdog.stop()

        val thread = threads.first()
        assertEquals("xgs-ui-watchdog", thread.name)
        assertTrue(thread.isDaemon)
        thread.join(30_000)
        assertFalse(thread.isAlive, "stop() ended the thread")
        assertEquals(emptyList(), dumpFiles())
    }

    @Test
    fun startingTwiceIsRejectedAndStoppingWithoutAStartIsFine() {
        val watchdog = UiWatchdog(logDir = dir, postToUi = { it.run() }, intervalMs = 5)
        watchdog.stop()

        watchdog.start()
        try {
            assertFailsWith<IllegalStateException> { watchdog.start() }
        } finally {
            watchdog.stop()
        }
    }

    // ---- keeping going after anything ------------------------------------------------------

    @Test
    fun aDumpThatFailsWithAnErrorDoesNotStopTheLaterTicks() {
        var attempts = 0
        val watchdog = watchdog(dump = { _, _ -> attempts++; throw OutOfMemoryError("while building the dump") })

        watchdog.ticks(8) // must not throw

        assertEquals(1, attempts)
        assertEquals(1, messages(Level.ERROR).size, "the hang itself is still logged")
        assertTrue(messages(Level.WARN).any { "OutOfMemoryError" in it }, messages(Level.WARN).toString())
        watchdog.ticks(30) // 38 s: the repeat is due at 36 s
        assertEquals(2, attempts, "it went on ticking and tried again")
    }

    @Test
    fun anErrorFromThePostIsSurvivedToo() {
        val watchdog = UiWatchdog(logDir = dir, postToUi = { throw NoClassDefFoundError("java/awt/EventQueue") }, clockMs = { now })

        watchdog.ticks(3) // must not throw

        assertTrue(messages(Level.WARN).any { "NoClassDefFoundError" in it }, messages(Level.WARN).toString())
    }

    @Test
    fun theThreadKeepsRunningAfterErrors() {
        val calls = AtomicInteger()
        val answered = CountDownLatch(3)
        val watchdog = UiWatchdog(
            logDir = dir,
            postToUi = { heartbeat ->
                if (calls.incrementAndGet() <= 3) throw OutOfMemoryError("simulated")
                heartbeat.run()
                answered.countDown()
            },
            intervalMs = 5,
        )

        watchdog.start()
        try {
            assertTrue(answered.await(30, TimeUnit.SECONDS), "it went on after the three errors")
        } finally {
            watchdog.stop()
        }
    }

    // ---- a slow tick ------------------------------------------------------------------------

    @Test
    fun aSlowDumpDoesNotMakeTheNextTickAGapThatDropsTheHang() {
        var calls = 0
        val watchdog = watchdog(dump = { silentMs, reason ->
            calls++
            now += 30_000 // building and writing this dump takes half a minute
            syntheticDump(silentMs, reason).also { dumps += it }
        })

        watchdog.ticks(6) // the hang is reported at 6 s; that tick ends at 36 s
        assertEquals(1, calls)

        watchdog.ticks(1) // 37 s: one second after the end of that tick, and the repeat is due

        assertEquals(2, calls, "the hang is still known: no gap came between the ticks")
        assertEquals(emptyList(), capture.at(Level.INFO), "no gap line")
    }

    // ---- backing off ------------------------------------------------------------------------

    @Test
    fun theRepeatsOfALongHangBackOff() {
        val watchdog = watchdog()

        watchdog.ticks(300)
        assertEquals(4, dumps.size, "at 6, 36, 96 and 216 s")

        watchdog.ticks(160) // 460 s: the next one was due at 456 s
        assertEquals(5, dumps.size)
    }

    // ---- the size of the file ---------------------------------------------------------------

    private val notice = "이후 덤프는 생략함(파일 크기 제한)"

    /** A dump of exactly [bytes] bytes, whatever the clock says. */
    private fun fixedDump(bytes: Int = 100) = "X".repeat(bytes - 1) + "\n"

    private fun dumpText(file: String = "ui-hang-20261007-210311.txt") = Files.readString(dir.resolve(file))

    @Test
    fun theDumpFileStopsGrowingAtTheSizeLimitWithOneNoticeAndTheLogGetsOneLines() {
        var calls = 0
        val watchdog = watchdog(maxDumpFileBytes = 250, dump = { _, _ -> calls++; fixedDump() })

        watchdog.ticks(500) // reports at 6, 36, 96, 216 and 456 s

        assertEquals(fixedDump() + fixedDump() + notice + "\n", dumpText(), "two sections fit, the third would make 300 bytes")
        assertEquals(3, calls, "once the file is full the dump is not even made")
        val errors = messages(Level.ERROR)
        assertEquals(5, errors.size)
        assertTrue("계속" in errors[1] && "ui-hang-20261007-210311.txt" in errors[1], errors[1])
        for (i in 2..4) {
            assertTrue("크기 제한" in errors[i] && "ui-hang" !in errors[i] && "XXXX" !in errors[i], errors[i])
        }
        assertEquals(listOf("ui-hang-20261007-210311.txt"), dumpFiles())
    }

    @Test
    fun theLimitIsInBytesNotCharacters() {
        val korean = "가".repeat(30) + "\n" // 31 characters, 91 bytes
        val watchdog = watchdog(maxDumpFileBytes = 150, dump = { _, _ -> korean })

        watchdog.ticks(40) // reports at 6 s and 36 s

        assertEquals(korean + notice + "\n", dumpText(), "a second section of 91 bytes would make 182")
    }

    @Test
    fun aFirstDumpBiggerThanTheLimitIsStillWritten() {
        val watchdog = watchdog(maxDumpFileBytes = 50, dump = { _, _ -> fixedDump() })

        watchdog.ticks(7)
        assertEquals(fixedDump(), dumpText(), "the first section is the main evidence: no limit applies to it")

        watchdog.ticks(30) // the repeat at 36 s
        assertEquals(fixedDump() + notice + "\n", dumpText())
    }

    @Test
    fun aNewHangAfterARecoveryHasAFreshFileAndAFreshLimit() {
        var calls = 0
        val watchdog = watchdog(maxDumpFileBytes = 150, dump = { _, _ -> calls++; fixedDump() })
        watchdog.ticks(40) // 6 s: written; 36 s: would make 200 bytes: notice
        assertEquals(fixedDump() + notice + "\n", dumpText())

        answerQueuedHeartbeats()
        uiAnswers = true
        watchdog.ticks(1) // recovered
        uiAnswers = false
        wallTime = LocalDateTime.of(2026, 10, 7, 21, 10, 0)
        watchdog.ticks(8) // nobody answers: a new hang

        assertEquals(3, calls)
        assertEquals(fixedDump(), dumpText("ui-hang-20261007-211000.txt"), "its first section is written and has no notice")
        assertEquals(fixedDump() + notice + "\n", dumpText(), "the first file is left as it was")
    }
}
