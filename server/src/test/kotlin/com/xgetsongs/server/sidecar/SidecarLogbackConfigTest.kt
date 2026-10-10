package com.xgetsongs.server.sidecar

import ch.qos.logback.classic.LoggerContext
import ch.qos.logback.classic.joran.JoranConfigurator
import ch.qos.logback.classic.util.LogbackMDCAdapter
import ch.qos.logback.core.ConsoleAppender
import ch.qos.logback.core.rolling.RollingFileAppender
import ch.qos.logback.core.rolling.TimeBasedRollingPolicy
import java.nio.charset.StandardCharsets.UTF_8
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** Loads the logback-sidecar.xml that the sidecar starts with into a context of its own, so the tests never touch the real logging. */
class SidecarLogbackConfigTest {
    private val dir: Path = Files.createTempDirectory("xgs-sidecar-logback")
    private val contexts = mutableListOf<LoggerContext>()

    @AfterTest
    fun cleanUp() {
        contexts.forEach { it.stop() }
        dir.toFile().deleteRecursively()
    }

    private fun configure(): LoggerContext {
        val context = LoggerContext()
        context.mdcAdapter = LogbackMDCAdapter() // a context that logback did not create itself has to be given one
        contexts += context
        context.putProperty("xgs.logDir", dir.toString())
        val configurator = JoranConfigurator()
        configurator.context = context
        configurator.doConfigure(checkNotNull(javaClass.getResource("/logback-sidecar.xml")) { "logback-sidecar.xml is not on the class path" })
        return context
    }

    private fun root(context: LoggerContext) = context.getLogger(org.slf4j.Logger.ROOT_LOGGER_NAME)

    @Test
    fun theLogFileIsWrittenInUtf8WithTheGivenFolder() {
        val context = configure()

        context.getLogger("com.xgetsongs.test").info("한글 로그 한 줄")

        val text = Files.readString(dir.resolve("xgetsongs.log"), UTF_8)
        assertTrue("한글 로그 한 줄" in text, text)
    }

    @Test
    fun theLogFileKeepsEveryLineAtOnceAndRollsOverByDayAndSize() {
        val file = root(configure()).getAppender("FILE") as RollingFileAppender<*>

        assertEquals(dir.resolve("xgetsongs.log"), Path.of(file.file))
        assertTrue(file.isImmediateFlush, "a crash or a kill must not lose the last lines")
        assertEquals(7, (file.rollingPolicy as TimeBasedRollingPolicy<*>).maxHistory)
    }

    @Test
    fun theConsoleIsStderrBecauseStdoutCarriesTheHandshake() {
        val console = root(configure()).getAppender("CONSOLE") as ConsoleAppender<*>

        assertEquals("System.err", console.target)
    }

    @Test
    fun theAppsOwnDebugLinesGoToTheFileAndNettyNoiseDoesNot() {
        val context = configure()

        context.getLogger("com.xgetsongs.engine.x").debug("debug of the app")
        context.getLogger("io.netty.channel").info("noise of netty")

        val text = Files.readString(dir.resolve("xgetsongs.log"), UTF_8)
        assertTrue("debug of the app" in text, text)
        assertFalse("noise of netty" in text, text)
    }
}
