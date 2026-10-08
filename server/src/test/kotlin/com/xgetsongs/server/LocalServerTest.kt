package com.xgetsongs.server

import ch.qos.logback.classic.Level
import ch.qos.logback.classic.spi.ILoggingEvent
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.tools.ToolPaths
import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolveRequest
import com.xgetsongs.shared.api.ResolveResponse
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.plugins.sse.SSE
import io.ktor.client.plugins.sse.sse
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.http.contentType
import io.ktor.serialization.kotlinx.json.json
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.job
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.net.ConnectException
import java.net.Socket
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** Runs the real Netty server on a random loopback port, which the in-memory test host cannot do. */
class LocalServerTest {
    private val fakes = TestServices()
    private lateinit var server: LocalServer
    private val outDir = Files.createTempDirectory("xgs-local").resolve("out")

    @BeforeTest
    fun start() {
        server = LocalServer.start(fakes.services)
    }

    @AfterTest
    fun stop() {
        server.stop()
    }

    private fun client(token: String? = server.token, port: Int = server.port) = HttpClient(CIO) {
        install(ContentNegotiation) { json(ApiJson.instance) }
        install(SSE)
        defaultRequest {
            url("http://127.0.0.1:$port")
            if (token != null) header(ApiHeaders.TOKEN, token)
        }
    }

    private suspend fun HttpClient.startJob(): String {
        val resolved = post("/resolve") {
            contentType(ContentType.Application.Json)
            setBody(ResolveRequest("PLabcdefghijkl"))
        }.body<ResolveResponse>()
        return post("/jobs") {
            contentType(ContentType.Application.Json)
            setBody(JobRequest(resolved.resolveId, JobOptions(outputDir = outDir.toString())))
        }.body<JobCreated>().jobId
    }

    @Test
    fun theServerListensOnLoopbackAndNeedsTheToken() = runBlocking {
        client().use { assertEquals(HttpStatusCode.OK, it.get("/tools").status) }
        client(token = null).use { assertEquals(HttpStatusCode.Unauthorized, it.get("/tools").status) }
        client(token = "wrong").use { assertEquals(HttpStatusCode.Unauthorized, it.get("/tools").status) }
    }

    @Test
    fun browserOriginsAreRefused() = runBlocking {
        client().use {
            val response = it.get("/tools") { header(HttpHeaders.Origin, "http://evil.example") }
            assertEquals(HttpStatusCode.Forbidden, response.status)
        }
    }

    @Test
    fun foreignHostHeadersAreRefused() {
        Socket("127.0.0.1", server.port).use { socket ->
            val request = "GET /tools HTTP/1.1\r\nHost: evil.example\r\n${ApiHeaders.TOKEN}: ${server.token}\r\nConnection: close\r\n\r\n"
            socket.getOutputStream().write(request.toByteArray())
            val statusLine = socket.getInputStream().bufferedReader().readLine()
            assertTrue(statusLine.contains("403"), statusLine)
        }
    }

    @Test
    fun eventsAreForwardedLiveWhileTheJobRuns() = runBlocking {
        fakes.downloads.queued = listOf(JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"))
        fakes.downloads.closeAfterQueued = false
        client().use { client ->
            withTimeout(30_000) {
                val jobId = client.startJob()
                val firstSeen = CompletableDeferred<Unit>()
                val received = mutableListOf<String?>()
                coroutineScope {
                    val reader = launch {
                        client.sse("/jobs/$jobId/events") {
                            incoming.collect {
                                received += it.event
                                if (it.event == "item-started") firstSeen.complete(Unit)
                            }
                        }
                    }
                    firstSeen.await() // arrived while the job is still running
                    fakes.downloads.channel!!.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)))
                    fakes.downloads.channel!!.close()
                    reader.join()
                }
                assertEquals(listOf<String?>("item-started", "job-done"), received)
            }
        }
    }

    @Test
    fun onlyTheFirstReaderOwnsTheEventStream() = runBlocking {
        fakes.downloads.queued = listOf(JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"))
        fakes.downloads.closeAfterQueued = false
        client().use { client ->
            withTimeout(30_000) {
                val jobId = client.startJob()
                val firstSeen = CompletableDeferred<Unit>()
                coroutineScope {
                    val first = launch {
                        client.sse("/jobs/$jobId/events") {
                            incoming.collect { if (it.event == "item-started") firstSeen.complete(Unit) }
                        }
                    }
                    firstSeen.await()

                    val second = mutableListOf<String?>()
                    client.sse("/jobs/$jobId/events") { incoming.collect { second += it.event } }

                    assertEquals(listOf<String?>("error"), second)
                    fakes.downloads.channel!!.close()
                    first.join()
                }
            }
        }
    }

    @Test
    fun theNumberOfRunningJobsIsTheNumberOfJobsWhoseEventStreamHasNotEnded() = runBlocking {
        fakes.downloads.queued = listOf(JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"))
        fakes.downloads.closeAfterQueued = false
        assertEquals(0, server.runningJobs())
        client().use { client ->
            withTimeout(30_000) {
                val jobId = client.startJob()
                assertEquals(1, server.runningJobs(), "registered by POST /jobs")
                val firstSeen = CompletableDeferred<Unit>()
                coroutineScope {
                    val reader = launch {
                        client.sse("/jobs/$jobId/events") {
                            incoming.collect { if (it.event == "item-started") firstSeen.complete(Unit) }
                        }
                    }
                    firstSeen.await()
                    assertEquals(1, server.runningJobs(), "still running while its events are read")
                    fakes.downloads.channel!!.trySend(JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)))
                    fakes.downloads.channel!!.close()
                    reader.join()
                }
                assertEquals(0, server.runningJobs(), "gone once the job ended")
            }
        }
    }

    /** The start line comes from a coroutine of its own, so it may be a moment late: wait for it (generously). */
    private fun LogCapture.awaitInfo(count: Int = 1): List<ILoggingEvent> {
        val deadline = System.nanoTime() + 30_000_000_000L
        while (at(Level.INFO).size < count && System.nanoTime() < deadline) Thread.sleep(10)
        return at(Level.INFO)
    }

    @Test
    fun startingLogsThePortAndTheToolPathsButNotTheToken() {
        val tools = ToolPathProvider {
            ToolPaths(ytDlp = Path.of("C:/t/yt-dlp.exe"), ffmpeg = null, jsRuntime = Path.of("C:/t/node.exe"))
        }

        LogCapture(LocalServer::class.java.name).use { capture ->
            val started = LocalServer.start(fakes.services, tools = tools)
            try {
                val record = capture.awaitInfo().single()
                assertTrue("127.0.0.1:${started.port}" in record.formattedMessage, record.formattedMessage)
                assertTrue("yt-dlp=${Path.of("C:/t/yt-dlp.exe")}" in record.formattedMessage, record.formattedMessage)
                assertTrue("ffmpeg=없음" in record.formattedMessage, record.formattedMessage)
                assertTrue("JS 런타임=${Path.of("C:/t/node.exe")}" in record.formattedMessage, record.formattedMessage)
                assertFalse(started.token in capture.events.joinToString("\n") { it.formattedMessage })
            } finally {
                started.stop()
            }
        }
    }

    @Test
    fun theToolLookupForTheStartLineDoesNotHoldUpStartingTheServer() {
        val release = CountDownLatch(1)
        val finished = AtomicBoolean(false)
        val lookupThread = AtomicReference<Thread>()
        val tools = ToolPathProvider {
            lookupThread.set(Thread.currentThread())
            release.await(30, TimeUnit.SECONDS)
            finished.set(true)
            ToolPaths(ytDlp = Path.of("C:/t/yt-dlp.exe"), ffmpeg = null, jsRuntime = null)
        }

        LogCapture(LocalServer::class.java.name).use { capture ->
            val started = LocalServer.start(fakes.services, tools = tools) // returns while the lookup is still stuck
            try {
                assertFalse(finished.get(), "the lookup has not finished: start did not wait for it")
                assertEquals(emptyList(), capture.at(Level.INFO), "no start line yet")
                runBlocking { client(started.token, started.port).use { assertEquals(HttpStatusCode.OK, it.get("/tools").status) } }

                release.countDown()
                val record = capture.awaitInfo().single()
                assertTrue("127.0.0.1:${started.port}" in record.formattedMessage && "yt-dlp=" in record.formattedMessage, record.formattedMessage)
                assertTrue(lookupThread.get() !== Thread.currentThread(), "it ran on another thread")
            } finally {
                release.countDown()
                started.stop()
            }
        }
    }

    @Test
    fun aToolLookupThatFailsStillLogsThePortAndNeverThrows() {
        val tools = ToolPathProvider { throw IllegalStateException("PATH is broken") }

        LogCapture(LocalServer::class.java.name).use { capture ->
            val started = LocalServer.start(fakes.services, tools = tools)
            try {
                val record = capture.awaitInfo().single()
                assertTrue("127.0.0.1:${started.port}" in record.formattedMessage, record.formattedMessage)
                assertTrue("도구 경로를 확인하지 못함 (IllegalStateException)" in record.formattedMessage, record.formattedMessage)
                assertEquals(emptyList(), capture.at(Level.ERROR))
            } finally {
                started.stop()
            }
        }
    }

    @Test
    fun aDroppedEventConnectionIsLoggedAsAWarning() = runBlocking {
        fakes.downloads.queued = listOf(JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"))
        fakes.downloads.closeAfterQueued = false
        LogCapture(JobLog.LOGGER_NAME).use { capture ->
            client().use { client ->
                withTimeout(60_000) {
                    val jobId = client.startJob()
                    val firstSeen = CompletableDeferred<Unit>()
                    coroutineScope {
                        val reader = launch {
                            client.sse("/jobs/$jobId/events") { incoming.collect { firstSeen.complete(Unit) } }
                        }
                        firstSeen.await()
                        reader.cancelAndJoin() // the client goes away

                        // The server learns about it when it next writes: keep the job producing events until it does.
                        while (capture.at(Level.WARN).isEmpty()) {
                            fakes.downloads.channel!!.trySend(JobEvent.ItemDone(1, "001 A - One.mp3"))
                            delay(50)
                        }
                    }
                    val warnings = capture.at(Level.WARN).map { it.formattedMessage }
                    assertEquals(listOf("이벤트 연결이 끊어짐 (작업 ${jobId.take(8)})"), warnings)
                    assertFalse(fakes.downloads.jobs.single().isCancelled, "the job itself goes on")
                    assertEquals(1, server.runningJobs(), "and is still registered")
                }
            }
        }
    }

    @Test
    fun stoppingTheServerWhileEventsAreReadIsNotLoggedAsADroppedConnection() = runBlocking {
        fakes.downloads.queued = listOf(JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"))
        fakes.downloads.closeAfterQueued = false
        LogCapture(JobLog.LOGGER_NAME).use { capture ->
            client().use { client ->
                withTimeout(60_000) {
                    val jobId = client.startJob()
                    val firstSeen = CompletableDeferred<Unit>()
                    coroutineScope {
                        val reader = launch {
                            try {
                                client.sse("/jobs/$jobId/events") { incoming.collect { firstSeen.complete(Unit) } }
                            } catch (e: Exception) {
                                // the connection is closed under the reader: that is the point of the test
                            }
                        }
                        firstSeen.await()

                        server.stop() // the app is closing: the handler of the open stream is cancelled
                        reader.join()
                        delay(500) // the handler's own end comes a moment after the connection's
                    }
                    assertEquals(emptyList(), capture.at(Level.WARN).map { it.formattedMessage })
                }
            }
        }
        server = LocalServer.start(fakes.services) // so @AfterTest has something to stop
    }

    @Test
    fun theStartLineIsNeverLoggedAfterTheStopLine() {
        val release = CountDownLatch(1)
        val lookedUp = CountDownLatch(1)
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val tools = ToolPathProvider {
            lookedUp.countDown()
            release.await(30, TimeUnit.SECONDS)
            ToolPaths(ytDlp = Path.of("C:/t/yt-dlp.exe"), ffmpeg = null, jsRuntime = null)
        }

        LogCapture(LocalServer::class.java.name).use { capture ->
            val started = LocalServer.start(fakes.services, scope, tools)
            assertTrue(lookedUp.await(30, TimeUnit.SECONDS), "the lookup is under way")

            started.stop() // while the lookup is still running
            release.countDown()
            runBlocking { scope.coroutineContext.job.children.toList().forEach { it.join() } } // the lookup coroutine has ended

            assertEquals(listOf("내장 서버 정지"), capture.at(Level.INFO).map { it.formattedMessage })
        }
    }

    @Test
    fun aJobThatEndsNormallyLogsNoWarningAboutTheConnection() = runBlocking {
        fakes.downloads.queued = listOf(
            JobEvent.ItemStarted(1, "vid00000001", "001 A - One.mp3"),
            JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 0, 0)),
        )
        LogCapture(JobLog.LOGGER_NAME).use { capture ->
            client().use { client ->
                withTimeout(30_000) {
                    val jobId = client.startJob()
                    client.sse("/jobs/$jobId/events") { incoming.collect { } }
                }
            }

            assertEquals(emptyList(), capture.at(Level.WARN))
        }
    }

    @Test
    fun startingWithoutToolPathsLogsJustThePortAndStoppingIsLogged() {
        LogCapture(LocalServer::class.java.name).use { capture ->
            val started = LocalServer.start(fakes.services)
            started.stop()

            val messages = capture.at(Level.INFO).map { it.formattedMessage }
            assertEquals(2, messages.size, messages.toString())
            assertTrue("127.0.0.1:${started.port}" in messages[0] && "yt-dlp" !in messages[0], messages[0])
            assertEquals("내장 서버 정지", messages[1])
        }
    }

    @Test
    fun stoppingTheServerClosesThePort() {
        val port = server.port
        server.stop()

        assertFailsWith<ConnectException> { Socket("127.0.0.1", port).close() }

        server = LocalServer.start(fakes.services) // so @AfterTest has something to stop
    }
}
