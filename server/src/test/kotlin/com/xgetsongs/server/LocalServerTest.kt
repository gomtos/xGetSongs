package com.xgetsongs.server

import ch.qos.logback.classic.Level
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
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.net.ConnectException
import java.net.Socket
import java.nio.file.Files
import java.nio.file.Path
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

    private fun client(token: String? = server.token) = HttpClient(CIO) {
        install(ContentNegotiation) { json(ApiJson.instance) }
        install(SSE)
        defaultRequest {
            url("http://127.0.0.1:${server.port}")
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

    @Test
    fun startingLogsThePortAndTheToolPathsButNotTheToken() {
        val tools = ToolPathProvider {
            ToolPaths(ytDlp = Path.of("C:/t/yt-dlp.exe"), ffmpeg = null, jsRuntime = Path.of("C:/t/node.exe"))
        }

        LogCapture(LocalServer::class.java.name).use { capture ->
            val started = LocalServer.start(fakes.services, tools = tools)
            try {
                val record = capture.at(Level.INFO).single()
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
