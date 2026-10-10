package com.xgetsongs.server.sidecar

import com.xgetsongs.server.LocalServer
import com.xgetsongs.server.TestServices
import com.xgetsongs.shared.api.ApiHeaders
import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.http.HttpStatusCode
import kotlinx.coroutines.runBlocking
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.io.PrintStream
import java.net.ConnectException
import java.net.Socket
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class SidecarMainTest {
    private val fakes = TestServices()
    private val stdout = ByteArrayOutputStream()
    private val stderr = ByteArrayOutputStream()
    private val exits = LinkedBlockingQueue<Int>()

    private fun out() = stdout.toString("UTF-8")
    private fun err() = stderr.toString("UTF-8")

    private fun runWith(args: Array<String>, stdin: InputStream, startServer: (Path) -> LocalServer) =
        runSidecar(args, stdin, PrintStream(stdout, true, "UTF-8"), PrintStream(stderr, true, "UTF-8"), startServer) { exits.put(it) }

    @Test
    fun startsTheServerPrintsOnlyTheHandshakeAndStopsWhenStdinEnds() {
        val appData = Files.createTempDirectory("xgs-sidecar").resolve("data")
        val parentWriter = PipedOutputStream()

        runWith(arrayOf("--app-data", appData.toString()), PipedInputStream(parentWriter)) { LocalServer.start(fakes.services) }

        val lines = out().lines().filter { it.isNotBlank() }
        assertEquals(1, lines.size, "nothing but the handshake on stdout: $lines")
        val parts = lines.single().split(' ')
        assertEquals(3, parts.size, lines.single())
        assertEquals("XGS-READY", parts[0])
        val port = parts[1].toInt()
        val token = parts[2]
        assertTrue(Files.isDirectory(appData), "the app data folder is made")
        runBlocking {
            HttpClient(CIO) {
                defaultRequest {
                    url("http://127.0.0.1:$port")
                    header(ApiHeaders.TOKEN, token)
                }
            }.use { assertEquals(HttpStatusCode.OK, it.get("/tools").status) }
        }

        parentWriter.close() // the shell goes away

        assertEquals(0, exits.poll(30, TimeUnit.SECONDS))
        assertFailsWith<ConnectException> { Socket("127.0.0.1", port).close() }
    }

    @Test
    fun wrongArgumentsPrintTheUsageAndExitWithTwoWithoutStartingAServer() {
        runWith(arrayOf("--bogus"), ByteArrayInputStream(ByteArray(0))) { error("the server must not start") }

        assertEquals(listOf(2), exits.toList())
        assertTrue("--app-data" in err(), err())
        assertEquals("", out())
    }

    @Test
    fun aServerThatCannotStartIsReportedAndTheProcessExitsWithOne() {
        val appData = Files.createTempDirectory("xgs-sidecar").toAbsolutePath()

        runWith(arrayOf("--app-data", appData.toString()), ByteArrayInputStream(ByteArray(0))) {
            throw IllegalStateException("port in use")
        }

        assertEquals(listOf(1), exits.toList())
        assertTrue("IllegalStateException" in err(), err())
        assertEquals("", out(), "no handshake for a server that is not there")
    }

    @Test
    fun anExitLineFromTheShellStopsTheServerAndIsReportedAsTheUsers() {
        val appData = Files.createTempDirectory("xgs-sidecar").resolve("data")
        val parentWriter = PipedOutputStream()
        val asked = LinkedBlockingQueue<Boolean>()

        runSidecar(
            arrayOf("--app-data", appData.toString()),
            PipedInputStream(parentWriter),
            PrintStream(stdout, true, "UTF-8"),
            PrintStream(stderr, true, "UTF-8"),
            startServer = { LocalServer.start(fakes.services) },
            onParentGone = { asked.put(it) },
            exit = { exits.put(it) },
        )
        val port = out().lines().first { it.isNotBlank() }.split(' ')[1].toInt()

        parentWriter.write("exit\n".toByteArray())
        parentWriter.flush() // the pipe stays open: the line alone ends it

        assertEquals(true, asked.poll(30, TimeUnit.SECONDS))
        assertEquals(0, exits.poll(30, TimeUnit.SECONDS))
        assertFailsWith<ConnectException> { Socket("127.0.0.1", port).close() }
    }

    @Test
    fun aClosedPipeWithoutAnExitLineIsReportedAsNotAskedFor() {
        val appData = Files.createTempDirectory("xgs-sidecar").resolve("data")
        val parentWriter = PipedOutputStream()
        val asked = LinkedBlockingQueue<Boolean>()

        runSidecar(
            arrayOf("--app-data", appData.toString()),
            PipedInputStream(parentWriter),
            PrintStream(stdout, true, "UTF-8"),
            PrintStream(stderr, true, "UTF-8"),
            startServer = { LocalServer.start(fakes.services) },
            onParentGone = { asked.put(it) },
            exit = { exits.put(it) },
        )

        parentWriter.close()

        assertEquals(false, asked.poll(30, TimeUnit.SECONDS))
        assertEquals(0, exits.poll(30, TimeUnit.SECONDS))
    }
}
