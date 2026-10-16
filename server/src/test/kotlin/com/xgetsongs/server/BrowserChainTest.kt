package com.xgetsongs.server

import com.xgetsongs.engine.lyrics.LyricsQuery
import com.xgetsongs.engine.lyrics.google.GoogleLyricsProvider
import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.BrowserAnswer
import com.xgetsongs.shared.api.BrowserCommand
import io.ktor.client.HttpClient
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
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

/**
 * A lyrics lookup from the Google provider down to the HTTP routes and back, with a scripted agent in place of the shell's WebView2:
 * the server asks, the agent answers what a page with a lyrics card would. (The card is made-up markup of the shape the real page
 * has; there are no real lyrics in this repository.)
 */
class BrowserChainTest {
    private val fakes = TestServices()
    private val browser = fakes.services.browser
    private lateinit var server: LocalServer
    private val query = LyricsQuery(artist = "IU", title = "Love poem", album = null, durationSeconds = 258)

    @BeforeTest
    fun start() {
        server = LocalServer.start(fakes.services)
    }

    @AfterTest
    fun stop() {
        browser.close()
        server.stop()
    }

    private fun client() = HttpClient(CIO) {
        install(ContentNegotiation) { json(ApiJson.instance) }
        defaultRequest {
            url("http://127.0.0.1:${server.port}")
            header(ApiHeaders.TOKEN, server.token)
        }
    }

    private val probe =
        """{"url":"https://www.google.com/search?q=x&hl=en","readyState":"complete","hasCard":true,"bodyText":""}"""

    private val cardPage = "<html><body><div data-lyricid=\"id-1\"><div jsname=\"WbKHeb\"><div jsname=\"U8S5sf\">" +
        "<span jsname=\"YS01Ge\">더미 하나</span><br><span jsname=\"YS01Ge\">더미 둘</span><br><span jsname=\"YS01Ge\">더미 셋</span>" +
        "</div></div></div></body></html>"

    @Test
    fun aLyricsLookupGoesThroughTheRoutesAndComesBackFound() = runBlocking {
        client().use { client ->
            withTimeout(60_000) {
                val seen = CopyOnWriteArrayList<String>()
                val agent = launch {
                    while (isActive) {
                        val poll = client.get("/browser/next?wait=2000")
                        if (poll.status != HttpStatusCode.OK) continue
                        val command = ApiJson.instance.decodeFromString(BrowserCommand.serializer(), poll.bodyAsText())
                        val answer = when (command) {
                            is BrowserCommand.Load -> BrowserAnswer(command.id, ok = true).also { seen += "load" }
                            is BrowserCommand.Evaluate ->
                                BrowserAnswer(command.id, ok = true, value = if ("outerHTML" in command.script) cardPage else probe)
                                    .also { seen += "evaluate" }
                            is BrowserCommand.Reset -> BrowserAnswer(command.id, ok = true).also { seen += "reset" }
                        }
                        client.post("/browser/answer") {
                            contentType(ContentType.Application.Json)
                            setBody(answer)
                        }
                    }
                }
                while (!browser.hasAgent) delay(10)

                val lyrics = GoogleLyricsProvider(browser).find(query)

                assertEquals("더미 하나\n더미 둘\n더미 셋", lyrics)
                assertEquals(listOf("load", "evaluate", "evaluate", "reset"), seen.toList())
                agent.cancelAndJoin()
            }
        }
    }

    @Test
    fun aLookupWithoutAnAgentEndsAtOnceWithNothing() = runBlocking {
        withTimeout(1_000) {
            assertNull(GoogleLyricsProvider(browser).find(query))
        }
    }
}
