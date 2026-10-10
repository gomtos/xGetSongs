package com.xgetsongs.server.sidecar

import com.xgetsongs.shared.api.ApiHeaders
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * Starts the real thing in a process of its own. The unit tests of [runSidecar] run in a JVM that the test runner keeps
 * alive, so they cannot tell whether the sidecar itself stays up: its server's threads are daemons, and a JVM with
 * nothing else to wait for starts to shut down as soon as main returns (and takes the server with it, a moment later).
 */
class SidecarProcessTest {
    @Test
    fun theRealProcessKeepsServingUntilItsStdinIsClosedAndThenEndsWithZero() {
        val appData = Files.createTempDirectory("xgs-sidecar-process").toAbsolutePath()
        val java = Path.of(System.getProperty("java.home"), "bin", "java").toString()
        val process = ProcessBuilder(
            java,
            "-Dlogback.configurationFile=logback-sidecar.xml",
            "-cp", System.getProperty("java.class.path"),
            "com.xgetsongs.server.sidecar.SidecarMainKt",
            "--app-data", appData.toString(),
        ).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        try {
            val line = process.inputStream.bufferedReader().readLine()
            assertTrue(line != null && line.startsWith("XGS-READY "), "the handshake line: $line")
            val (port, token) = line.split(' ').let { it[1] to it[2] }

            // Long enough for a JVM that is shutting down to be gone, which a shorter wait would not show.
            Thread.sleep(3000)
            assertTrue(process.isAlive, "the process is still running while its stdin is open")
            val request = HttpRequest.newBuilder(URI.create("http://127.0.0.1:$port/tools")).header(ApiHeaders.TOKEN, token).build()
            val response = HttpClient.newHttpClient().send(request, HttpResponse.BodyHandlers.ofString())
            assertEquals(200, response.statusCode(), "the server still answers")

            process.outputStream.close() // the shell goes away
            assertTrue(process.waitFor(30, TimeUnit.SECONDS), "the process ends after its stdin is closed")
            assertEquals(0, process.exitValue())
        } finally {
            process.destroyForcibly()
        }
    }
}
