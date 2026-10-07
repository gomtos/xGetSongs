package com.xgetsongs.app.diagnostics

import ch.qos.logback.classic.Level
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class DiagnosticsTest {
    private val dir: Path = Files.createTempDirectory("xgs-diagnostics")
    private val defaultHandlerBefore = Thread.getDefaultUncaughtExceptionHandler()

    @AfterTest
    fun cleanUp() {
        Thread.setDefaultUncaughtExceptionHandler(defaultHandlerBefore)
        dir.toFile().deleteRecursively()
    }

    private val properties = mapOf(
        "java.version" to "21.0.5",
        "java.vendor" to "Eclipse Adoptium",
        "java.vm.name" to "OpenJDK 64-Bit Server VM",
        "os.name" to "Windows 11",
        "os.version" to "10.0",
        "os.arch" to "amd64",
        "user.name" to "SECRET-USER",
        "java.class.path" to "SECRET-CLASSPATH",
    )

    private fun record(appVersion: String? = "1.0.0", asked: MutableList<String> = mutableListOf()): String = startupRecord(
        appVersion = appVersion,
        property = { key -> asked += key; properties[key] },
        processors = 8,
        maxHeapMb = 2_048,
        headless = false,
        workingDir = "C:\\work",
        logDir = Path.of("C:\\logs"),
    )

    // ---- the startup record ----------------------------------------------------------------

    @Test
    fun theStartupRecordHasEverythingTheBriefAsksFor() {
        val text = record()

        for (expected in listOf("xGetSongs 1.0.0", "21.0.5", "Eclipse Adoptium", "OpenJDK 64-Bit Server VM", "Windows 11", "10.0", "amd64")) {
            assertTrue(expected in text, "$expected in $text")
        }
        assertTrue("프로세서 8개" in text, text)
        assertTrue("최대 힙 2048MB" in text, text)
        assertTrue("C:\\work" in text, text)
        assertTrue("C:\\logs" in text, text)
        assertTrue("headless=false" in text, text)
    }

    @Test
    fun theVersionIsLeftOutWhenItIsNotKnown() {
        val text = record(appVersion = null)

        assertTrue("앱: xGetSongs\n" in text + "\n", text)
        assertFalse("null" in text, text)
    }

    @Test
    fun theStartupRecordAsksForNothingBeyondTheListedProperties() {
        val asked = mutableListOf<String>()

        val text = record(asked = asked)

        assertEquals(
            setOf("java.version", "java.vendor", "java.vm.name", "os.name", "os.version", "os.arch"),
            asked.toSet(),
        )
        assertFalse("SECRET" in text, text)
    }

    @Test
    fun aMissingPropertyDoesNotBreakTheStartupRecord() {
        val text = startupRecord(null, { null }, 4, 512, true, "C:\\w", Path.of("C:\\l"))

        assertTrue("프로세서 4개" in text, text)
        assertTrue("headless=true" in text, text)
    }

    // ---- the log folder --------------------------------------------------------------------

    @Test
    fun theLogFolderIsCreatedWithItsParents() {
        val folder = dir.resolve("a").resolve("b").resolve("logs")

        assertTrue(ensureLogDirectory(folder))

        assertTrue(Files.isDirectory(folder))
        assertTrue(ensureLogDirectory(folder), "an existing folder is fine")
    }

    @Test
    fun aLogFolderThatCannotBeCreatedIsOnlyReported() {
        val file = Files.writeString(dir.resolve("a-file"), "not a folder")
        val reports = mutableListOf<String>()

        val created = ensureLogDirectory(file.resolve("logs"), report = { reports += it })

        assertFalse(created)
        assertEquals(1, reports.size)
        assertTrue("a-file" in reports.single(), reports.single())
    }

    // ---- the uncaught exception handler ----------------------------------------------------

    @Test
    fun anUncaughtExceptionIsLoggedWithTheThreadNameAndPassedOn() {
        LogCapture(UncaughtLogger::class.java).use { capture ->
            var passedOn: Pair<Thread, Throwable>? = null
            val handler = UncaughtLogger(Thread.UncaughtExceptionHandler { t, e -> passedOn = t to e })
            val thread = Thread(Runnable { }, "worker-7")
            val error = IllegalStateException("boom")

            handler.uncaughtException(thread, error)

            val record = capture.events.single()
            assertEquals(Level.ERROR, record.level)
            assertTrue("worker-7" in record.formattedMessage, record.formattedMessage)
            assertEquals("java.lang.IllegalStateException", record.throwableProxy.className)
            assertEquals(thread to error, passedOn)
        }
    }

    @Test
    fun anUncaughtExceptionWithoutAPreviousHandlerIsJustLogged() {
        LogCapture(UncaughtLogger::class.java).use { capture ->
            UncaughtLogger(null).uncaughtException(Thread(Runnable { }, "t"), RuntimeException("x"))

            assertEquals(1, capture.at(Level.ERROR).size)
        }
    }

    @Test
    fun aThrowingPreviousHandlerDoesNotLeakOutOfTheHandler() {
        LogCapture(UncaughtLogger::class.java).use { capture ->
            val handler = UncaughtLogger(Thread.UncaughtExceptionHandler { _, _ -> throw IllegalStateException("handler failed") })

            handler.uncaughtException(Thread(Runnable { }, "t"), RuntimeException("x")) // must not throw

            assertEquals(1, capture.at(Level.ERROR).size)
        }
    }


    // ---- start -----------------------------------------------------------------------------

    @Test
    fun startPreparesTheFolderLogsTheStartupRecordAndHooksTheExceptionHandler() {
        val sentinelCalls = mutableListOf<Throwable>()
        Thread.setDefaultUncaughtExceptionHandler { _, e -> sentinelCalls += e }
        val logDir = dir.resolve("logs")

        LogCapture(Diagnostics::class.java).use { capture ->
            val handle = Diagnostics.start(logDir, postToUi = { it.run() }, registerHook = { })
            try {
                assertTrue(Files.isDirectory(logDir))
                val startup = capture.at(Level.INFO).single().formattedMessage
                assertTrue("시작 정보" in startup && "로그 폴더" in startup && logDir.toString() in startup, startup)

                val installed = Thread.getDefaultUncaughtExceptionHandler()
                assertTrue(installed is UncaughtLogger, installed.toString())
                val error = RuntimeException("later")
                installed.uncaughtException(Thread(Runnable { }, "t"), error)
                assertEquals(listOf<Throwable>(error), sentinelCalls, "the handler that was there before still gets it")
            } finally {
                handle.stop()
            }
        }
    }

    @Test
    fun theExitHookKnowsWhetherTheUserClosedTheWindowAndHowManyJobsRun() {
        val hooks = mutableListOf<Thread>()
        LogCapture(ExitLogger::class.java).use { capture ->
            val handle = Diagnostics.start(dir.resolve("logs"), postToUi = { it.run() }, registerHook = { hooks += it })
            try {
                assertEquals(1, hooks.size)
                assertEquals(-1, handle.runningJobs(), "unknown until the server says")
                hooks.single().run()
                handle.runningJobs = { 5 } // set once the server is up
                handle.markUserExit()
                hooks.single().run()

                val (external, user) = capture.events
                assertEquals(Level.WARN, external.level)
                assertTrue("외부 종료 요청" in external.formattedMessage && "진행 중인 작업 수를 알 수 없음" in external.formattedMessage, external.formattedMessage)
                assertEquals(Level.INFO, user.level)
                assertTrue("사용자가 창을 닫음" in user.formattedMessage && "진행 중인 작업 5개" in user.formattedMessage, user.formattedMessage)
            } finally {
                handle.stop()
            }
        }
    }

    @Test
    fun stopEndsTheWatchdogThread() {
        val threads = java.util.concurrent.ConcurrentLinkedQueue<Thread>()
        val handle = Diagnostics.start(
            dir.resolve("logs"),
            postToUi = { it.run(); threads += Thread.currentThread() },
            registerHook = { },
        )
        val deadline = System.nanoTime() + 30_000_000_000L
        while (threads.isEmpty() && System.nanoTime() < deadline) Thread.sleep(10)
        assertTrue(threads.isNotEmpty(), "the watchdog started ticking")

        handle.stop()

        val thread = threads.first()
        thread.join(30_000)
        assertFalse(thread.isAlive)
        assertEquals("xgs-ui-watchdog", thread.name)
    }
}
