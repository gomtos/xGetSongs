package com.xgetsongs.app.diagnostics

import ch.qos.logback.classic.Level
import java.nio.file.Files
import java.nio.file.Path
import java.time.Instant
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

    @Test
    fun thePreferredLogFolderIsUsedWhenItCanBeCreated() {
        val preferred = dir.resolve("appdata").resolve("logs")
        val fallback = dir.resolve("temp").resolve("xgetsongs-logs")
        val reports = mutableListOf<String>()

        val chosen = chooseLogDirectory(preferred, fallback, report = { reports += it })

        assertEquals(preferred, chosen)
        assertTrue(Files.isDirectory(preferred))
        assertFalse(Files.exists(fallback), "the fallback is not touched")
        assertEquals(emptyList(), reports)
    }

    @Test
    fun theTempFolderIsUsedWhenThePreferredLogFolderCannotBeCreated() {
        val blocker = Files.writeString(dir.resolve("appdata"), "a file where the app folder should be")
        val fallback = dir.resolve("temp").resolve("xgetsongs-logs")
        val reports = mutableListOf<String>()

        val chosen = chooseLogDirectory(blocker.resolve("logs"), fallback, report = { reports += it })

        assertEquals(fallback, chosen)
        assertTrue(Files.isDirectory(fallback))
        assertTrue(reports.any { "appdata" in it }, reports.toString())
    }

    @Test
    fun whenNeitherFolderCanBeCreatedTheFallbackIsStillTheAnswerAndNothingIsThrown() {
        val blocker = Files.writeString(dir.resolve("a-file"), "not a folder")

        val chosen = chooseLogDirectory(blocker.resolve("one"), blocker.resolve("two"), report = { })

        assertEquals(blocker.resolve("two"), chosen)
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
            assertTrue("java.lang.IllegalStateException: boom" in record.formattedMessage, record.formattedMessage)
            assertTrue("\tat com.xgetsongs.app.diagnostics.DiagnosticsTest" in record.formattedMessage, "the stack is in it")
            assertEquals(thread to error, passedOn)
        }
    }

    @Test
    fun theMessageOfAnUncaughtExceptionLosesItsPaths() {
        LogCapture(UncaughtLogger::class.java).use { capture ->
            val error = RuntimeException(
                "copy failed: C:\\Users\\Some User\\Music\\My Playlist\\001 A - B.mp3 -> D:\\out\\001 A - B.mp3: Access is denied",
                java.io.IOException("cannot read \\\\NAS\\Music Share\\a.mp3"),
            )

            UncaughtLogger(null).uncaughtException(Thread(Runnable { }, "t"), error)

            val text = capture.events.single().formattedMessage
            assertTrue("java.lang.RuntimeException: copy failed: <경로> -> <경로>: Access is denied" in text, text)
            assertTrue("Caused by: java.io.IOException: cannot read <경로>" in text, text)
            for (leaked in listOf("Some User", "My Playlist", "001 A", "NAS", "Users")) assertFalse(leaked in text, "$leaked in $text")
        }
    }

    @Test
    fun anExceptionWithoutAMessageIsJustItsClassName() {
        LogCapture(UncaughtLogger::class.java).use { capture ->
            UncaughtLogger(null).uncaughtException(Thread(Runnable { }, "t"), NullPointerException())

            val text = capture.events.single().formattedMessage
            assertTrue("\njava.lang.NullPointerException\n\tat " in text, text)
        }
    }

    @Test
    fun aVeryDeepStackIsCutAndACauseCycleDoesNotLoopForever() {
        LogCapture(UncaughtLogger::class.java).use { capture ->
            val deep = RuntimeException("deep").apply {
                stackTrace = Array(500) { StackTraceElement("com.example.Deep", "call$it", "Deep.kt", it + 1) }
            }
            val first = RuntimeException("first", deep)
            deep.initCause(first) // a cycle: first -> deep -> first

            UncaughtLogger(null).uncaughtException(Thread(Runnable { }, "t"), first)

            val text = capture.events.single().formattedMessage
            assertTrue(text.lines().count { it.startsWith("\tat com.example.Deep") } <= 100, "the stack is cut")
            assertTrue("개 프레임 생략" in text, text)
            assertEquals(1, Regex("Caused by: ").findAll(text).count(), "each throwable once")
        }
    }

    @Test
    fun theCallbackIsToldAboutEveryUncaughtException() {
        var told = 0
        val handler = UncaughtLogger(null, onUncaught = { told++ })

        handler.uncaughtException(Thread(Runnable { }, "t"), RuntimeException("x"))
        handler.uncaughtException(Thread(Runnable { }, "t"), RuntimeException("y"))

        assertEquals(2, told)
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

    private class FakeProcesses(
        override val ownPid: Long = 4242,
        override val ownStart: Instant = Instant.parse("2026-10-07T13:00:00Z"),
        private val alive: Map<Long, Instant> = emptyMap(),
    ) : Processes {
        override fun startOf(pid: Long): Instant? = alive[pid]
    }

    private fun writeMarker(text: String): Path {
        val logs = Files.createDirectories(dir.resolve("logs"))
        return Files.writeString(logs.resolve("last-run.txt"), text)
    }

    private val earlier = Instant.parse("2026-10-07T12:15:40.123Z")

    private fun start(
        hooks: MutableList<Thread> = mutableListOf(),
        processes: Processes = FakeProcesses(),
    ): DiagnosticsHandle = Diagnostics.start(dir.resolve("logs"), postToUi = { it.run() }, registerHook = { hooks += it }, processes = processes)

    @Test
    fun startPreparesTheFolderLogsTheStartupRecordAndHooksTheExceptionHandler() {
        val sentinelCalls = mutableListOf<Throwable>()
        Thread.setDefaultUncaughtExceptionHandler { _, e -> sentinelCalls += e }

        LogCapture(Diagnostics::class.java).use { capture ->
            val handle = start()
            try {
                assertTrue(Files.isDirectory(dir.resolve("logs")))
                val startup = capture.at(Level.INFO).single().formattedMessage
                assertTrue("시작 정보" in startup && "로그 폴더" in startup && dir.resolve("logs").toString() in startup, startup)

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
    fun theExitHookKnowsWhetherTheUserClosedTheWindowAndHowManyJobsAreRegistered() {
        val hooks = mutableListOf<Thread>()
        LogCapture(ExitLogger::class.java).use { capture ->
            val handle = start(hooks)
            try {
                assertEquals(1, hooks.size)
                assertEquals(-1, handle.runningJobs(), "unknown until the server says")
                hooks.single().run()
                handle.runningJobs = { 5 } // set once the server is up
                handle.markUserExit()
                hooks.single().run()

                val (external, user) = capture.events
                assertEquals(Level.WARN, external.level)
                assertTrue("창을 닫지 않은 종료" in external.formattedMessage && "등록된 작업 수를 알 수 없음" in external.formattedMessage, external.formattedMessage)
                assertEquals(Level.INFO, user.level)
                assertTrue("사용자가 창을 닫음" in user.formattedMessage && "등록된 작업 5개" in user.formattedMessage, user.formattedMessage)
            } finally {
                handle.stop()
            }
        }
    }

    @Test
    fun theExitRecordSaysWhenAnUncaughtExceptionCameBeforeAndStartForgetsEarlierOnes() {
        val hooks = mutableListOf<Thread>()
        LogCapture(ExitLogger::class.java).use { capture ->
            val first = start(hooks)
            try {
                Thread.getDefaultUncaughtExceptionHandler().uncaughtException(Thread(Runnable { }, "t"), RuntimeException("x"))
                hooks.last().run()
                assertTrue("직전에 처리되지 않은 예외가 있었음" in capture.events.last().formattedMessage, capture.events.last().formattedMessage)
            } finally {
                first.stop()
            }

            val second = start(hooks)
            try {
                hooks.last().run()
                assertFalse("직전에" in capture.events.last().formattedMessage, capture.events.last().formattedMessage)
            } finally {
                second.stop()
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
            processes = FakeProcesses(),
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

    // ---- the marker of the last run ---------------------------------------------------------

    @Test
    fun startWritesTheRunningMarkerAndTheExitHookRewritesIt() {
        val hooks = mutableListOf<Thread>()
        val marker = dir.resolve("logs").resolve("last-run.txt")
        val handle = start(hooks)
        try {
            assertEquals(RunMarker(RunState.RUNNING, 4242, Instant.parse("2026-10-07T13:00:00Z")), parseRunMarker(Files.readString(marker)))

            hooks.single().run() // an exit nobody in the window asked for
            val other = Files.readString(marker)
            assertTrue("state=exited" in other && "reason=other" in other && "pid=4242" in other && "time=" in other, other)

            handle.markUserExit()
            hooks.single().run()
            val user = Files.readString(marker)
            assertTrue("state=exited" in user && "reason=user" in user, user)
            assertEquals(RunState.EXITED, parseRunMarker(user)?.state)
        } finally {
            handle.stop()
        }
    }

    @Test
    fun aPreviousRunThatEndedCleanlyIsNotMentioned() {
        writeMarker(renderExited(77, earlier, userRequested = true, time = earlier.plusSeconds(60)))

        LogCapture(Diagnostics::class.java).use { capture ->
            start().stop()

            assertEquals(1, capture.events.size, "only the startup record: ${capture.events.map { it.formattedMessage }}")
        }
    }

    @Test
    fun aPreviousRunWithoutAnExitRecordIsAWarning() {
        writeMarker(renderRunning(77, earlier))

        LogCapture(Diagnostics::class.java).use { capture ->
            start().stop()

            val warning = capture.at(Level.WARN).single().formattedMessage
            assertTrue("이전 실행(PID 77, 시작 " in warning && "정상 종료 기록 없이 끝났음" in warning, warning)
            assertEquals(1, capture.at(Level.INFO).size, "the startup record")
        }
    }

    @Test
    fun anotherRunThatIsStillAliveIsAnInfoLine() {
        writeMarker(renderRunning(77, earlier))

        LogCapture(Diagnostics::class.java).use { capture ->
            start(processes = FakeProcesses(alive = mapOf(77L to earlier))).stop()

            assertEquals(emptyList(), capture.at(Level.WARN))
            val infos = capture.at(Level.INFO).map { it.formattedMessage }
            assertTrue("다른 실행이 아직 실행 중임 (PID 77)" in infos, infos.toString())
        }
    }

    @Test
    fun aMissingOrCorruptMarkerSaysNothing() {
        LogCapture(Diagnostics::class.java).use { capture ->
            start().stop() // no file yet
            writeMarker("this is not a marker\n\u0000\u0001")
            start().stop()

            assertEquals(2, capture.events.size, "two startup records and nothing else")
            assertEquals(emptyList(), capture.at(Level.WARN))
        }
    }
}
