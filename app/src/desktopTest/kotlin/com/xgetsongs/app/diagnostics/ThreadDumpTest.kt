package com.xgetsongs.app.diagnostics

import java.time.LocalDateTime
import java.util.concurrent.CountDownLatch
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ThreadDumpTest {
    private val time = LocalDateTime.of(2026, 10, 7, 21, 3, 11, 123_000_000)

    private fun thread(name: String, daemon: Boolean = false): Thread = Thread(Runnable { }, name).apply { isDaemon = daemon }

    private fun frames(count: Int, owner: String = "com.example.Deep"): Array<StackTraceElement> =
        Array(count) { StackTraceElement(owner, "call$it", "Deep.kt", it + 1) }

    private fun render(
        stacks: Map<Thread, Array<StackTraceElement>>,
        states: Map<Thread, Thread.State> = emptyMap(),
        silentMs: Long = 7_200,
        reason: String = "UI 스레드 무응답",
        memory: MemorySnapshot = MemorySnapshot(usedMb = 123, totalMb = 256, maxMb = 2_048),
        gcs: List<GcSnapshot> = listOf(GcSnapshot("G1 Young Generation", count = 12, timeMs = 345)),
    ): String = ThreadDump.render(time, silentMs, reason, stacks, states, memory, gcs)

    /** The thread header lines, in order: the lines that start with a quote. */
    private fun headers(dump: String): List<String> = dump.lines().filter { it.startsWith("\"") }

    @Test
    fun theAwtEventQueueThreadComesFirst() {
        val stacks = linkedMapOf(
            thread("alpha") to frames(1),
            thread("main") to frames(1),
            thread("AWT-EventQueue-0") to frames(2),
            thread("Zeta") to frames(1),
        )

        val names = headers(render(stacks)).map { it.substringAfter("\"").substringBefore("\"") }

        assertEquals("AWT-EventQueue-0", names.first())
        assertEquals(4, names.size)
    }

    @Test
    fun everyEventQueueThreadComesBeforeTheOthers() {
        // After the queue's thread died and restarted the number goes up; both belong in front.
        val stacks = linkedMapOf(thread("alpha") to frames(1), thread("AWT-EventQueue-1") to frames(1), thread("AWT-EventQueue-0") to frames(1))

        val names = headers(render(stacks)).map { it.substringAfter("\"").substringBefore("\"") }

        assertEquals(listOf("AWT-EventQueue-0", "AWT-EventQueue-1", "alpha"), names)
    }

    @Test
    fun theOtherThreadsAreSortedByNameIgnoringCase() {
        val stacks = linkedMapOf(
            thread("gamma") to frames(1),
            thread("Beta") to frames(1),
            thread("alpha") to frames(1),
            thread("AWT-EventQueue-0") to frames(1),
        )

        val names = headers(render(stacks)).map { it.substringAfter("\"").substringBefore("\"") }

        assertEquals(listOf("AWT-EventQueue-0", "alpha", "Beta", "gamma"), names)
    }

    @Test
    fun framesAreCutAtSixtyPerThread() {
        val stacks = mapOf(thread("deep") to frames(100), thread("shallow") to frames(3))

        val dump = render(stacks)

        val deep = dump.substringAfter("\"deep\"").substringBefore("\"shallow\"")
        assertEquals(60, deep.lines().count { it.startsWith("\tat ") })
        assertTrue("\tat com.example.Deep.call59(Deep.kt:60)" in deep, "the 60th frame is the last one")
        assertFalse("call60" in dump, "the 61st frame is gone")
        assertTrue("40개 프레임 생략" in deep, "the cut says how much is missing")
        val shallow = dump.substringAfter("\"shallow\"")
        assertEquals(3, shallow.lines().count { it.startsWith("\tat ") })
        assertFalse("생략" in shallow, "nothing is cut from a short stack")
    }

    @Test
    fun exactlySixtyFramesAreNotCut() {
        val dump = render(mapOf(thread("t") to frames(60)))

        assertEquals(60, dump.lines().count { it.startsWith("\tat ") })
        assertFalse("생략" in dump)
    }

    @Test
    fun theFramesAreWrittenAsAtLines() {
        val dump = render(mapOf(thread("t") to arrayOf(StackTraceElement("com.example.Foo", "bar", "Foo.kt", 12))))

        assertTrue("\tat com.example.Foo.bar(Foo.kt:12)" in dump.lines())
    }

    @Test
    fun theHeaderHasTheTimeTheSilentDurationAndTheReason() {
        val dump = render(mapOf(thread("t") to frames(1)), silentMs = 7_200, reason = "UI 스레드 무응답")

        val header = dump.lines().first()
        assertTrue("2026-10-07 21:03:11.123" in header, header)
        assertTrue("7200ms" in header, header)
        assertTrue("UI 스레드 무응답" in header, header)
    }

    @Test
    fun theMemoryAndGcLinesAreThere() {
        val dump = render(
            mapOf(thread("t") to frames(1)),
            gcs = listOf(GcSnapshot("G1 Young Generation", 12, 345), GcSnapshot("G1 Old Generation", 1, 67)),
        )

        val lines = dump.lines()
        assertTrue(lines.any { "123MB" in it && "256MB" in it && "2048MB" in it }, "used, total and max heap: $dump")
        assertTrue(lines.any { "G1 Young Generation" in it && "12" in it && "345ms" in it }, dump)
        assertTrue(lines.any { "G1 Old Generation" in it && "1회" in it && "67ms" in it }, dump)
    }

    @Test
    fun theMemoryAndGcLinesComeBeforeTheThreads() {
        val dump = render(mapOf(thread("t") to frames(1)))

        assertTrue(dump.indexOf("123MB") < dump.indexOf("\"t\""))
        assertTrue(dump.indexOf("G1 Young Generation") < dump.indexOf("\"t\""))
    }

    @Test
    fun aMissingGcListIsFine() {
        val dump = render(mapOf(thread("t") to frames(1)), gcs = emptyList())

        assertTrue("\"t\"" in dump)
    }

    @Test
    fun daemonAndStateAreShown() {
        val worker = thread("worker", daemon = true)
        val main = thread("main")
        val waiting = thread("waiter")
        val stacks = linkedMapOf(worker to frames(1), main to frames(1), waiting to frames(1))
        val states = mapOf(worker to Thread.State.RUNNABLE, main to Thread.State.BLOCKED, waiting to Thread.State.TIMED_WAITING)

        val lines = render(stacks, states).lines()

        assertTrue("\"worker\" daemon RUNNABLE" in lines, lines.toString())
        assertTrue("\"main\" BLOCKED" in lines, lines.toString())
        assertTrue("\"waiter\" TIMED_WAITING" in lines, lines.toString())
    }

    @Test
    fun aThreadWithoutAStateEntryShowsItsOwnState() {
        val fresh = thread("fresh") // never started

        val lines = render(mapOf(fresh to frames(0))).lines()

        assertTrue("\"fresh\" NEW" in lines, lines.toString())
    }

    @Test
    fun noEnvironmentVariablesOrSystemPropertiesAreWritten() {
        val dump = render(mapOf(thread("t") to frames(1)))

        for (name in listOf("PATH", "USERNAME", "APPDATA", "JAVA_HOME")) {
            System.getenv(name)?.takeIf { it.length > 3 }?.let { assertFalse(it in dump, "the value of $name") }
        }
        for (key in listOf("java.class.path", "user.name", "user.home", "java.home")) {
            System.getProperty(key)?.takeIf { it.length > 3 }?.let { assertFalse(it in dump, "the value of $key") }
        }
    }

    @Test
    fun aCapturedDumpListsTheRunningThreadsWithTheMemoryAndGcLines() {
        val dump = ThreadDump.capture(silentMs = 6_000, reason = "test", time = time)

        assertTrue("2026-10-07 21:03:11.123" in dump.lines().first())
        assertTrue("\"${Thread.currentThread().name}\" RUNNABLE" in dump.lines(), "this thread is in it")
        assertTrue(dump.lines().any { it.startsWith("메모리:") }, dump)
        assertTrue(dump.lines().any { it.startsWith("GC ") }, dump)
        for (name in listOf("PATH", "APPDATA")) {
            System.getenv(name)?.takeIf { it.length > 3 }?.let { assertFalse(it in dump, "the value of $name") }
        }
        assertFalse(System.getProperty("java.class.path").let { it.length > 3 && it in dump })
    }

    @Test
    fun aCapturedDumpPutsAnEventQueueThreadFirstWhenThereIsOne() {
        val release = CountDownLatch(1)
        val running = Thread({ release.await() }, "AWT-EventQueue-7").apply { isDaemon = true; start() }
        try {
            val names = headers(ThreadDump.capture(silentMs = 6_000, reason = "test", time = time))
                .map { it.substringAfter("\"").substringBefore("\"") }

            val inFront = names.takeWhile { it.startsWith("AWT-EventQueue") }
            assertTrue("AWT-EventQueue-7" in inFront, names.toString())
            assertTrue(names.drop(inFront.size).none { it.startsWith("AWT-EventQueue") }, names.toString())
        } finally {
            release.countDown()
            running.join(5_000)
        }
    }
}
