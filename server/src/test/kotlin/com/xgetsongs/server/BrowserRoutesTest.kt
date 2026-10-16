package com.xgetsongs.server

import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.BrowserAnswer
import com.xgetsongs.shared.api.BrowserCommand
import com.xgetsongs.shared.api.ErrorResponse
import io.ktor.client.HttpClient
import io.ktor.client.call.body
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.client.statement.bodyAsText
import io.ktor.http.ContentType
import io.ktor.http.HttpStatusCode
import io.ktor.http.contentType
import io.ktor.serialization.kotlinx.json.json
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds

/**
 * The two routes the desktop shell's hidden browser uses, on the real server (the poll waits in real time). Each test that needs the
 * agent present starts a poll first and waits until the server has counted it: a call made before that fails with `no-agent`.
 */
class BrowserRoutesTest {
    private val fakes = TestServices()
    private val browser = fakes.services.browser
    private lateinit var server: LocalServer

    @BeforeTest
    fun start() {
        server = LocalServer.start(fakes.services)
    }

    @AfterTest
    fun stop() {
        browser.close()
        server.stop()
    }

    private fun client(token: String? = server.token) = HttpClient(CIO) {
        install(ContentNegotiation) { json(ApiJson.instance) }
        defaultRequest {
            url("http://127.0.0.1:${server.port}")
            if (token != null) header(ApiHeaders.TOKEN, token)
        }
    }

    private suspend fun awaitAgent() = withTimeout(10_000) {
        while (!browser.hasAgent) delay(10)
    }

    private suspend fun HttpClient.postAnswer(answer: BrowserAnswer) = post("/browser/answer") {
        contentType(ContentType.Application.Json)
        setBody(answer)
    }

    @Test
    fun aPollWithNothingToDoAnswersNoContentAfterTheWait() = runBlocking {
        client().use { client ->
            val began = System.nanoTime()

            val response = client.get("/browser/next?wait=300")

            assertEquals(HttpStatusCode.NoContent, response.status)
            assertTrue(System.nanoTime() - began >= 250_000_000, "it waited")
        }
    }

    @Test
    fun aPollGetsTheQueuedCommandAsJsonWithItsType() = runBlocking {
        client().use { client ->
            withTimeout(30_000) {
                val poll = async { client.get("/browser/next?wait=5000") }
                awaitAgent()
                val call = async { browser.evaluate("1") }

                val response = poll.await()

                assertEquals(HttpStatusCode.OK, response.status)
                val body = Json.parseToJsonElement(response.bodyAsText()).jsonObject
                assertEquals("evaluate", body.getValue("type").jsonPrimitive.content)
                assertEquals("1", body.getValue("script").jsonPrimitive.content)
                call.cancelAndJoin()
            }
        }
    }

    @Test
    fun anAnswerPostedToTheRouteReachesTheCaller() = runBlocking {
        client().use { client ->
            withTimeout(30_000) {
                val poll = async { client.get("/browser/next?wait=5000") }
                awaitAgent()
                val call = async { browser.evaluate("6*7") }
                val command = ApiJson.instance.decodeFromString(BrowserCommand.serializer(), poll.await().bodyAsText())

                val response = client.postAnswer(BrowserAnswer(command.id, ok = true, value = "42"))

                assertEquals(HttpStatusCode.NoContent, response.status)
                assertEquals("42", call.await())
            }
        }
    }

    @Test
    fun anAnswerNobodyWaitsForIsNotFound() = runBlocking {
        client().use { client ->
            val response = client.postAnswer(BrowserAnswer("nobody-asked", ok = true))

            assertEquals(HttpStatusCode.NotFound, response.status)
            assertEquals("기다리는 명령이 없습니다.", response.body<ErrorResponse>().message)
        }
    }

    @Test
    fun aGarbageOrNegativeWaitDoesNotWait() = runBlocking {
        client().use { client ->
            for (raw in listOf("abc", "-5", "")) {
                val began = System.nanoTime()

                val response = client.get("/browser/next?wait=$raw")

                assertEquals(HttpStatusCode.NoContent, response.status, raw)
                assertTrue(System.nanoTime() - began < 1_000_000_000, "wait=$raw did not wait")
            }
        }
    }

    @Test
    fun theBrowserRoutesNeedTheToken() = runBlocking {
        client(token = null).use { client ->
            assertEquals(HttpStatusCode.Unauthorized, client.get("/browser/next?wait=0").status)
            assertEquals(HttpStatusCode.Unauthorized, client.postAnswer(BrowserAnswer("x", ok = true)).status)
        }
    }

    @Test
    fun anAnswerOfThreeMegabytesPasses() = runBlocking {
        client().use { client ->
            withTimeout(60_000) {
                val poll = async { client.get("/browser/next?wait=5000") }
                awaitAgent()
                val call = async { browser.evaluate("document.documentElement.outerHTML") }
                val command = ApiJson.instance.decodeFromString(BrowserCommand.serializer(), poll.await().bodyAsText())
                val html = "<p>한글 page</p>".repeat(200_000)

                val response = client.postAnswer(BrowserAnswer(command.id, ok = true, value = html))

                assertEquals(HttpStatusCode.NoContent, response.status)
                assertEquals(html, call.await())
            }
        }
    }

    @Test
    fun theWaitIsParsedAndCutAtThirtySeconds() {
        assertEquals(0.milliseconds, browserWait(null))
        assertEquals(0.milliseconds, browserWait("abc"))
        assertEquals(0.milliseconds, browserWait("-5"))
        assertEquals(100.milliseconds, browserWait("100"))
        assertEquals(30.seconds, browserWait("999999"))
    }
}
