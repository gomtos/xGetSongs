package com.xgetsongs.server.sidecar

import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** Starts the real sidecar in a process of its own and reads what it leaves in its log folder. */
class SidecarDiagnosticsProcessTest {
    private val base: Path = Files.createTempDirectory("xgs-diag-process").toAbsolutePath()
    private val appData = base.resolve("data")

    @AfterTest
    fun cleanUp() {
        base.toFile().deleteRecursively()
    }

    private class Running(val process: Process, val port: Int, val token: String)

    private fun start(vararg extraArgs: String): Running {
        val java = Path.of(System.getProperty("java.home"), "bin", "java").toString()
        val process = ProcessBuilder(
            listOf(
                java, "-Dlogback.configurationFile=logback-sidecar.xml",
                "-cp", System.getProperty("java.class.path"),
                "com.xgetsongs.server.sidecar.SidecarMainKt", "--app-data", appData.toString(),
            ) + extraArgs,
        ).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        val line = process.inputStream.bufferedReader().readLine()
        assertTrue(line != null && line.startsWith("XGS-READY "), "the handshake line: $line")
        val parts = line.split(' ')
        return Running(process, parts[1].toInt(), parts[2])
    }

    /** Ends the process the way the shell does: [exitLine] writes the line before the pipe is closed. */
    private fun end(running: Running, exitLine: Boolean) {
        runCatching {
            if (exitLine) {
                running.process.outputStream.write("exit\n".toByteArray())
                running.process.outputStream.flush()
            }
            running.process.outputStream.close()
        }
        assertTrue(running.process.waitFor(30, TimeUnit.SECONDS), "the process ends")
        assertEquals(0, running.process.exitValue())
    }

    private fun logText(logDir: Path): String {
        val file = logDir.resolve("xgetsongs.log")
        assertTrue(Files.isRegularFile(file), "the log file is in $logDir")
        return Files.readString(file)
    }

    private fun awaitLog(logDir: Path, text: String) {
        val file = logDir.resolve("xgetsongs.log")
        val deadline = System.nanoTime() + 30_000_000_000L
        while (System.nanoTime() < deadline && !(Files.isRegularFile(file) && text in Files.readString(file))) {
            Thread.sleep(50)
        }
    }

    @Test
    fun anExitLineIsLoggedAsTheUsersEndAndTheMarkerSaysSo() {
        val logDir = base.resolve("log")
        val running = start("--log-dir", logDir.toString())
        awaitLog(logDir, "내장 서버 시작")
        assertTrue("state=running" in Files.readString(logDir.resolve("last-run.txt")))

        end(running, exitLine = true)

        val text = logText(logDir)
        for (expected in listOf("시작 정보", "내장 서버 시작", "사용자가 창 닫기를 요청함", "내장 서버 정지", "JVM 종료 시작: 사용자가 창을 닫음")) {
            assertTrue(expected in text, "$expected in the log:\n$text")
        }
        val marker = Files.readString(logDir.resolve("last-run.txt"))
        assertTrue("state=exited" in marker && "reason=user" in marker, marker)
    }

    @Test
    fun aClosedPipeWithoutAnExitLineIsLoggedAsAnEndNobodyAskedFor() {
        val logDir = base.resolve("log")
        val running = start("--log-dir", logDir.toString())
        awaitLog(logDir, "내장 서버 시작")

        end(running, exitLine = false)

        val text = logText(logDir)
        assertTrue("창을 닫지 않은 종료" in text, text)
        assertFalse("사용자가 창 닫기를 요청함" in text, text)
        val marker = Files.readString(logDir.resolve("last-run.txt"))
        assertTrue("state=exited" in marker && "reason=other" in marker, marker)
    }

    @Test
    fun theLogHoldsTheStartRecordsButNeverTheToken() {
        val logDir = base.resolve("log")
        val running = start("--log-dir", logDir.toString())
        awaitLog(logDir, "내장 서버 시작")

        end(running, exitLine = true)

        val text = logText(logDir)
        assertTrue("127.0.0.1:${running.port}" in text, text)
        assertFalse(running.token in text, "the token is a secret: it is on stdout only")
    }

    @Test
    fun aLogFolderThatCannotBeMadeFallsBackToTheAppDataFolder() {
        val blocker = Files.writeString(base.resolve("blocker"), "a file, so nothing can be made below it")
        val running = start("--log-dir", blocker.resolve("log").toString())
        val fallback = appData.resolve("logs")
        awaitLog(fallback, "내장 서버 시작")

        end(running, exitLine = true)

        assertTrue("시작 정보" in logText(fallback))
    }
}
