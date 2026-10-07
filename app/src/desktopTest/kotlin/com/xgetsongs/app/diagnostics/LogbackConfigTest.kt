package com.xgetsongs.app.diagnostics

import ch.qos.logback.classic.Level
import ch.qos.logback.classic.LoggerContext
import ch.qos.logback.classic.encoder.PatternLayoutEncoder
import ch.qos.logback.classic.joran.JoranConfigurator
import ch.qos.logback.classic.util.LogbackMDCAdapter
import ch.qos.logback.core.rolling.RollingFileAppender
import ch.qos.logback.core.rolling.SizeAndTimeBasedRollingPolicy
import ch.qos.logback.core.rolling.TimeBasedRollingPolicy
import ch.qos.logback.core.status.Status
import ch.qos.logback.core.util.FileSize
import java.io.ByteArrayOutputStream
import java.io.PrintStream
import java.nio.charset.StandardCharsets.UTF_8
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** Loads the logback.xml that ships with the app into a context of its own, so the tests never touch the real logging. */
class LogbackConfigTest {
    private val dir: Path = Files.createTempDirectory("xgs-logback")
    private val contexts = mutableListOf<LoggerContext>()
    private val realOut = System.out

    @AfterTest
    fun cleanUp() {
        System.setOut(realOut)
        contexts.forEach { it.stop() }
        dir.toFile().deleteRecursively()
    }

    private fun configure(vararg properties: Pair<String, String>): LoggerContext {
        val context = LoggerContext()
        context.mdcAdapter = LogbackMDCAdapter() // a context that logback did not create itself has to be given one
        contexts += context
        properties.forEach { (key, value) -> context.putProperty(key, value) }
        val configurator = JoranConfigurator()
        configurator.context = context
        configurator.doConfigure(checkNotNull(javaClass.getResource("/logback.xml")) { "logback.xml is not on the class path" })
        return context
    }

    private fun fileAppender(context: LoggerContext): RollingFileAppender<*> =
        context.getLogger(org.slf4j.Logger.ROOT_LOGGER_NAME).getAppender("FILE") as RollingFileAppender<*>

    /** Runs [block] with System.out going into a buffer (the console appender writes to whatever System.out is at the time). */
    private fun captureConsole(block: () -> Unit): String {
        val buffer = ByteArrayOutputStream()
        System.setOut(PrintStream(buffer, true, UTF_8))
        try {
            block()
        } finally {
            System.setOut(realOut)
        }
        return buffer.toString(UTF_8)
    }

    private fun field(owner: Class<*>, name: String, instance: Any): Any? =
        owner.getDeclaredField(name).apply { isAccessible = true }.get(instance)

    @Test
    fun theShippedConfigurationLoadsWithoutWarningsOrErrors() {
        val context = configure("xgs.logDir" to dir.toString())

        val problems = context.statusManager.copyOfStatusList.filter { it.effectiveLevel >= Status.WARN }
        assertEquals(emptyList(), problems.map { it.toString() })
    }

    @Test
    fun theFileHoldsInfoAndTheAppsDebugLinesButNotNettyOrKtorDebugLines() {
        val context = configure("xgs.logDir" to dir.toString())

        captureConsole {
            context.getLogger("com.xgetsongs.server.Jobs").info("info line of the app")
            context.getLogger("com.xgetsongs.server.Jobs").debug("debug line of the app")
            context.getLogger("io.netty.channel.DefaultChannelPipeline").debug("debug line of netty")
            context.getLogger("io.netty.channel.DefaultChannelPipeline").warn("warn line of netty")
            context.getLogger("io.ktor.server.Application").debug("debug line of ktor")
            context.getLogger("io.ktor.server.Application").info("info line of ktor")
            context.getLogger("some.other.library").debug("debug line of a library")
            context.getLogger("some.other.library").info("info line of a library")
        }

        // Read before the context is stopped: every line has to be on disk the moment it is logged.
        val log = Files.readString(dir.resolve("xgetsongs.log"), UTF_8)
        assertTrue("info line of the app" in log, log)
        assertTrue("debug line of the app" in log, log)
        assertTrue("warn line of netty" in log, log)
        assertTrue("info line of ktor" in log, log)
        assertTrue("info line of a library" in log, log)
        assertFalse("debug line of netty" in log, log)
        assertFalse("debug line of ktor" in log, log)
        assertFalse("debug line of a library" in log, log)
    }

    @Test
    fun theConsoleGetsNoDebugLine() {
        val context = configure("xgs.logDir" to dir.toString())

        val console = captureConsole {
            context.getLogger("com.xgetsongs.server.Jobs").debug("debug line of the app")
            context.getLogger("com.xgetsongs.server.Jobs").info("info line of the app")
            context.getLogger("com.xgetsongs.server.Jobs").warn("warn line of the app")
            context.getLogger("io.netty.buffer.PooledByteBufAllocator").debug("debug line of netty")
        }

        assertFalse("debug line" in console, console)
        assertTrue("info line of the app" in console, console)
        assertTrue("warn line of the app" in console, console)
    }

    @Test
    fun theFileLinesCarryTheFullDateAndThePlainConsoleLinesDoNot() {
        val context = configure("xgs.logDir" to dir.toString())

        val console = captureConsole { context.getLogger("com.xgetsongs.Demo").info("pattern check") }

        val fileLine = Files.readAllLines(dir.resolve("xgetsongs.log"), UTF_8).single { "pattern check" in it }
        assertTrue(Regex("""^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} INFO  \[.+] com\.xgetsongs\.Demo - pattern check$""").matches(fileLine), fileLine)
        val consoleLine = console.lines().single { "pattern check" in it }
        assertTrue(Regex("""^\d{2}:\d{2}:\d{2}\.\d{3} INFO  \[.+] com\.xgetsongs\.Demo - pattern check$""").matches(consoleLine), consoleLine)
    }

    @Test
    fun koreanTextIsWrittenAsUtf8() {
        val context = configure("xgs.logDir" to dir.toString())

        captureConsole { context.getLogger("com.xgetsongs.Demo").info("UI 응답 회복 (총 8초)") }

        assertTrue("UI 응답 회복 (총 8초)" in Files.readString(dir.resolve("xgetsongs.log"), UTF_8))
    }

    @Test
    fun theRollingPolicyKeepsSevenDaysFiftyMegabytesInFiveMegabyteFiles() {
        val context = configure("xgs.logDir" to dir.toString())

        val appender = fileAppender(context)
        val policy = appender.rollingPolicy as SizeAndTimeBasedRollingPolicy<*>
        assertEquals(7, policy.maxHistory)
        assertEquals(50L * 1024 * 1024, (field(TimeBasedRollingPolicy::class.java, "totalSizeCap", policy) as FileSize).size)
        assertEquals(5L * 1024 * 1024, (field(SizeAndTimeBasedRollingPolicy::class.java, "maxFileSize", policy) as FileSize).size)
        assertEquals("${dir}/xgetsongs.%d{yyyy-MM-dd}.%i.log".replace('\\', '/'), policy.fileNamePattern.replace('\\', '/'))
        assertEquals("${dir}/xgetsongs.log".replace('\\', '/'), appender.file.replace('\\', '/'))
    }

    @Test
    fun theFileAppenderFlushesEveryLineAndWritesUtf8() {
        val context = configure("xgs.logDir" to dir.toString())

        val appender = fileAppender(context)

        assertTrue(appender.isImmediateFlush)
        assertEquals(UTF_8, (appender.encoder as PatternLayoutEncoder).charset)
    }

    @Test
    fun theRootLevelIsInfoAndTheAppsOwnLoggersAreDebug() {
        val context = configure("xgs.logDir" to dir.toString())

        assertEquals(Level.INFO, context.getLogger(org.slf4j.Logger.ROOT_LOGGER_NAME).level)
        assertEquals(Level.DEBUG, context.getLogger("com.xgetsongs").level)
        assertEquals(Level.WARN, context.getLogger("io.netty").level)
        assertEquals(Level.INFO, context.getLogger("io.ktor").level)
    }

    @Test
    fun withoutALogFolderPropertyTheFilesGoToTheTempFolder() {
        val fakeTemp = dir.resolve("temp")
        val context = configure("java.io.tmpdir" to fakeTemp.toString()) // no xgs.logDir

        val appender = fileAppender(context)

        assertEquals(fakeTemp.resolve("xgetsongs-logs").resolve("xgetsongs.log").toString().replace('\\', '/'), appender.file.replace('\\', '/'))
        context.getLogger("com.xgetsongs.Demo").info("fallback line")
        assertTrue("fallback line" in Files.readString(fakeTemp.resolve("xgetsongs-logs").resolve("xgetsongs.log"), UTF_8))
    }
}
