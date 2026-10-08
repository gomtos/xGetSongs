package com.xgetsongs.app.diagnostics

import ch.qos.logback.classic.Level
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.time.Duration
import kotlin.time.Duration.Companion.hours
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds

// None of these tests exits anything: the hook's body is called directly, and the hook is handed to a fake registrar.
class ExitLoggerTest {
    private val capture = LogCapture(ExitLogger::class.java)

    @AfterTest
    fun cleanUp() = capture.close()

    private fun logger(
        runningJobs: () -> Int = { 0 },
        uptime: () -> Duration = { 90.seconds },
        uncaughtSeen: () -> Boolean = { false },
        onExit: (Boolean) -> Unit = {},
    ) = ExitLogger(runningJobs, uptime, heapMb = { 100L to 2_000L }, uncaughtSeen = uncaughtSeen, onExit = onExit)

    // ---- describeExit ----------------------------------------------------------------------

    @Test
    fun aUserExitIsInfoAndSaysTheUserClosedTheWindow() {
        val exit = describeExit(userRequested = true, runningJobs = 0, uptime = 3.seconds)

        assertFalse(exit.warn)
        assertTrue("JVM 종료 시작" in exit.text, exit.text)
        assertTrue("사용자가 창을 닫음" in exit.text, exit.text)
        assertTrue("등록된 작업 0개" in exit.text, exit.text)
        assertTrue("가동 3초" in exit.text, exit.text)
        assertFalse("창을 닫지 않은 종료" in exit.text, exit.text)
    }

    @Test
    fun aUserExitWithRunningJobsSaysHowMany() {
        val exit = describeExit(userRequested = true, runningJobs = 2, uptime = 61.seconds)

        assertFalse(exit.warn)
        assertTrue("등록된 작업 2개" in exit.text, exit.text)
        assertTrue("가동 1분 1초" in exit.text, exit.text)
    }

    @Test
    fun anExternalExitIsAWarningThatNamesTheUsualCauses() {
        val exit = describeExit(userRequested = false, runningJobs = 1, uptime = 5.minutes)

        assertTrue(exit.warn)
        assertTrue("JVM 종료 시작" in exit.text, exit.text)
        assertTrue("창을 닫지 않은 종료 (콘솔 종료, SIGTERM, 로그오프·시스템 종료, 또는 처리되지 않은 예외 뒤의 종료)" in exit.text, exit.text)
        assertTrue("등록된 작업 1개" in exit.text, exit.text)
        assertFalse("사용자가 창을 닫음" in exit.text, exit.text)
    }

    @Test
    fun anExitAfterAnUncaughtExceptionSaysSo() {
        val exit = describeExit(userRequested = false, runningJobs = 0, uptime = 1.seconds, uncaughtBefore = true)

        assertTrue(exit.warn)
        assertTrue(exit.text.endsWith(" | 직전에 처리되지 않은 예외가 있었음"), exit.text)
        assertFalse("직전에" in describeExit(userRequested = false, runningJobs = 0, uptime = 1.seconds).text)
    }

    @Test
    fun aUserExitKeepsItsTextWhateverHappenedBefore() {
        val clean = describeExit(userRequested = true, runningJobs = 1, uptime = 1.seconds)
        val afterAnException = describeExit(userRequested = true, runningJobs = 1, uptime = 1.seconds, uncaughtBefore = true)

        assertEquals(clean, afterAnException)
    }

    @Test
    fun anUnknownNumberOfJobsIsSaidSo() {
        for (userRequested in listOf(true, false)) {
            val exit = describeExit(userRequested, runningJobs = -1, uptime = 10.seconds)

            assertTrue("등록된 작업 수를 알 수 없음" in exit.text, exit.text)
            assertFalse("-1" in exit.text, exit.text)
        }
    }

    @Test
    fun theUptimeIsWrittenInHoursMinutesAndSeconds() {
        assertEquals("0초", formatUptime(Duration.ZERO))
        assertEquals("0초", formatUptime(999.milliseconds))
        assertEquals("59초", formatUptime(59.seconds))
        assertEquals("1분 0초", formatUptime(60.seconds))
        assertEquals("12분 3초", formatUptime(12.minutes + 3.seconds))
        assertEquals("1시간 0분 0초", formatUptime(1.hours))
        assertEquals("1시간 2분 3초", formatUptime(1.hours + 2.minutes + 3.seconds))
        assertEquals("27시간 0분 1초", formatUptime(27.hours + 1.seconds))
        assertEquals("0초", formatUptime((-5).seconds), "never a negative time")
    }

    // ---- the hook --------------------------------------------------------------------------

    @Test
    fun theHookLogsAnExitTheUserAskedForAtInfoWithJobsAndHeap() {
        val exitLogger = logger(runningJobs = { 3 }, uptime = { 125.seconds })

        exitLogger.markUserExit()
        exitLogger.logExit()

        val record = capture.events.single()
        assertEquals(Level.INFO, record.level)
        assertTrue("JVM 종료 시작" in record.formattedMessage, record.formattedMessage)
        assertTrue("사용자가 창을 닫음" in record.formattedMessage, record.formattedMessage)
        assertTrue("등록된 작업 3개" in record.formattedMessage, record.formattedMessage)
        assertTrue("가동 2분 5초" in record.formattedMessage, record.formattedMessage)
        assertTrue("힙 사용 100MB (최대 2000MB)" in record.formattedMessage, record.formattedMessage)
    }

    @Test
    fun theHookLogsAnExitNobodyAskedForAtWarn() {
        val exitLogger = logger(runningJobs = { 1 })

        exitLogger.logExit()

        val record = capture.events.single()
        assertEquals(Level.WARN, record.level)
        assertTrue("창을 닫지 않은 종료" in record.formattedMessage, record.formattedMessage)
        assertTrue("등록된 작업 1개" in record.formattedMessage, record.formattedMessage)
    }

    @Test
    fun theHookMentionsAnUncaughtExceptionBeforeTheHeapFigure() {
        val exitLogger = logger(uncaughtSeen = { true })

        exitLogger.logExit()

        val message = capture.events.single().formattedMessage
        assertTrue("창을 닫지 않은 종료" in message && " | 직전에 처리되지 않은 예외가 있었음 | 힙 사용" in message, message)
    }

    @Test
    fun theExitCallbackGetsTheUserFlagAfterTheRecordIsWritten() {
        val seen = mutableListOf<Pair<Boolean, Int>>()
        val exitLogger = logger(onExit = { user -> seen += user to capture.events.size })

        exitLogger.logExit()
        exitLogger.markUserExit()
        exitLogger.logExit()

        assertEquals(listOf(false to 1, true to 2), seen, "false, then true, each after its own log record")
    }

    @Test
    fun aFailingExitCallbackAndAFailingFlagNeverBreakTheHook() {
        val exitLogger = logger(uncaughtSeen = { throw IllegalStateException("flag") }, onExit = { throw IllegalStateException("callback") })

        exitLogger.logExit() // must not throw

        assertEquals(1, capture.events.size)
    }

    @Test
    fun theHookStillLogsWhenTheJobCountCannotBeAsked() {
        val exitLogger = logger(runningJobs = { throw IllegalStateException("server is gone") })

        exitLogger.logExit() // must not throw

        val record = capture.events.single()
        assertEquals(Level.WARN, record.level)
        assertTrue("등록된 작업 수를 알 수 없음" in record.formattedMessage, record.formattedMessage)
    }

    @Test
    fun theHookNeverThrowsWhateverGoesWrong() {
        val exitLogger = ExitLogger(
            runningJobs = { throw IllegalStateException("jobs") },
            uptime = { throw IllegalStateException("clock") },
            heapMb = { throw IllegalStateException("heap") },
        )

        exitLogger.markUserExit()
        exitLogger.logExit() // must not throw

        assertEquals(1, capture.events.size, "it still says that the JVM is going down")
        assertTrue("JVM 종료 시작" in capture.events.single().formattedMessage)
    }

    @Test
    fun theUserFlagIsNotUndoneByLoggingTwice() {
        val exitLogger = logger()
        exitLogger.markUserExit()

        exitLogger.logExit()
        exitLogger.logExit()

        assertEquals(listOf(Level.INFO, Level.INFO), capture.events.map { it.level })
    }

    @Test
    fun registeringAddsExactlyOneHookThatRunsTheLoggingBody() {
        val added = mutableListOf<Thread>()
        val exitLogger = logger(runningJobs = { 4 })

        val hook = exitLogger.register { added += it }

        assertEquals(listOf(hook), added)
        assertEquals("xgs-exit-logger", hook.name)
        assertEquals(emptyList(), capture.events, "registering logs nothing")
        hook.run() // what the JVM does on its exit thread
        assertEquals(1, capture.events.size)
        assertTrue("등록된 작업 4개" in capture.events.single().formattedMessage)
    }
}
