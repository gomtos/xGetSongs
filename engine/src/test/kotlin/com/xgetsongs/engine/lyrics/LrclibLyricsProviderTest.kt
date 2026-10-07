package com.xgetsongs.engine.lyrics

import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import java.io.IOException
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Proxy
import java.net.ProxySelector
import java.net.SocketAddress
import java.net.URI
import java.net.URLDecoder
import java.net.http.HttpClient
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TestTimeSource
import kotlin.time.TimeSource

/**
 * [LrclibLyricsProvider] against a fake LRCLIB on the loopback interface (the JDK's own HttpServer). Every record and
 * every lyric is made up; nothing here touches the internet.
 */
class LrclibLyricsProviderTest {
    /** One request the fake server received. */
    private data class Seen(val method: String, val path: String, val rawQuery: String?, val userAgent: String?, val accept: String?)

    private val executor: ExecutorService = Executors.newCachedThreadPool()
    private val server: HttpServer = HttpServer.create(InetSocketAddress(InetAddress.getLoopbackAddress(), 0), 0).also {
        it.executor = executor
    }
    private val seen = CopyOnWriteArrayList<Seen>()
    private var stopped = false

    @Volatile
    private var onGet: (HttpExchange) -> Unit = { it.reply(404, NOT_FOUND) }

    @Volatile
    private var onSearch: (HttpExchange) -> Unit = { it.reply(200, "[]") }

    init {
        server.createContext("/") { exchange ->
            val uri = exchange.requestURI
            seen += Seen(
                exchange.requestMethod, uri.rawPath, uri.rawQuery,
                exchange.requestHeaders.getFirst("User-Agent"), exchange.requestHeaders.getFirst("Accept"),
            )
            when (uri.rawPath) {
                "/api/get" -> onGet(exchange)
                "/api/search" -> onSearch(exchange)
                else -> exchange.reply(404, NOT_FOUND)
            }
            exchange.close()
        }
        server.start()
    }

    @AfterTest
    fun stopTheServer() {
        stopServer()
        executor.shutdownNow()
    }

    private fun stopServer() {
        if (!stopped) {
            stopped = true
            server.stop(0)
        }
    }

    private val base get() = "http://127.0.0.1:${server.address.port}/api"

    private fun provider(
        maxRequests: Int = 8,
        maxConcurrent: Int = 2,
        requestTimeout: Duration = 5.seconds,
        baseUrl: String = base,
        timeSource: TimeSource = clock,
    ) = LrclibLyricsProvider(
        baseUrl = baseUrl,
        userAgent = "xGetSongs-test/1.0 (https://example.invalid)",
        requestTimeout = requestTimeout,
        maxRequests = maxRequests,
        maxConcurrent = maxConcurrent,
        timeSource = timeSource,
    )

    /** The clock of the providers of a test: it only moves when the test moves it, so no test sleeps through a cool-down. */
    private val clock = TestTimeSource()

    /** Runs [block] on the calling thread, in real time; the return type is Unit, which JUnit needs to run the test. */
    private fun blocking(block: suspend CoroutineScope.() -> Unit) = runBlocking(block = block)

    private fun query(
        artist: String = "IU",
        title: String = "Love poem",
        album: String? = null,
        durationSeconds: Int? = 258,
    ) = LyricsQuery(artist, title, album, durationSeconds)

    // ---- the fake service ----

    private fun HttpExchange.reply(status: Int, body: String, contentType: String = "application/json; charset=utf-8") {
        val bytes = body.toByteArray(Charsets.UTF_8)
        responseHeaders.add("Content-Type", contentType)
        sendResponseHeaders(status, if (bytes.isEmpty()) -1L else bytes.size.toLong())
        if (bytes.isNotEmpty()) responseBody.use { it.write(bytes) }
    }

    /** Three made-up lines that start with [tag], so a test can tell which record the lyrics came from. */
    private fun lyricsOf(tag: String) = "$tag one\n$tag two\n$tag three"

    /** A record the way LRCLIB writes it: the fields the app reads, and some it does not. */
    private fun record(
        track: String = "Love poem",
        artist: String = "IU",
        album: String? = "Love poem",
        duration: Number? = 258,
        instrumental: Boolean = false,
        lyrics: String? = lyricsOf("tag"),
    ): String = buildJsonObject {
        put("id", 12345)
        put("name", track)
        put("trackName", track)
        put("artistName", artist)
        put("albumName", album)
        put("duration", if (duration == null) JsonNull else JsonPrimitive(duration))
        put("instrumental", instrumental)
        put("plainLyrics", lyrics)
        put("syncedLyrics", "[00:01.00] synthetic")
    }.toString()

    private fun array(vararg records: String) = records.joinToString(prefix = "[", postfix = "]", separator = ",")

    private fun HttpExchange.rawParams(): Map<String, String> =
        requestURI.rawQuery.orEmpty().split('&').filter { it.isNotEmpty() }.associate { it.substringBefore('=') to it.substringAfter('=', "") }

    private fun Seen.params(): Map<String, String> =
        rawQuery.orEmpty().split('&').filter { it.isNotEmpty() }
            .associate { it.substringBefore('=') to URLDecoder.decode(it.substringAfter('=', ""), Charsets.UTF_8) }

    private val paths get() = seen.map { it.path }

    private companion object {
        const val NOT_FOUND = """{"code":404,"name":"TrackNotFound","message":"Failed to find specified track"}"""
    }

    // ---- /get ----

    @Test
    fun aGetHitIsReturnedWithoutCallingSearch() = blocking {
        onGet = { it.reply(200, record(lyrics = lyricsOf("get"))) }

        val lyrics = provider().find(query())

        assertEquals(lyricsOf("get"), lyrics)
        assertEquals(listOf("/api/get"), paths)
    }

    @Test
    fun theLyricsAreTidied() = blocking {
        onGet = { it.reply(200, record(lyrics = "  첫 줄  \r\n둘째 줄\r\n\r\n\r\n\r\n셋째 줄\t\r\n\r\n")) }

        assertEquals("  첫 줄\n둘째 줄\n\n\n셋째 줄", provider().find(query()))
    }

    @Test
    fun aGetHitWhoseTitleOnlyMatchesWithoutItsBracketsAndCreditIsAccepted() = blocking {
        onGet = { it.reply(200, record(lyrics = lyricsOf("get"))) }

        assertEquals(lyricsOf("get"), provider().find(query(title = "Love poem (feat. nobody)")))
        assertEquals(lyricsOf("get"), provider().find(query(title = "Love poem feat. nobody")))
    }

    @Test
    fun aGetHitThatIsNotTheSongFallsThroughToSearch() = blocking {
        onGet = { it.reply(200, record(artist = "Somebody Else", lyrics = lyricsOf("wrong"))) }
        onSearch = { it.reply(200, array(record(lyrics = lyricsOf("search")))) }

        assertEquals(lyricsOf("search"), provider().find(query()))
        assertEquals(listOf("/api/get", "/api/search"), paths)
    }

    @Test
    fun aGetHitWithTheWrongLengthOrWithoutLyricsOrInstrumentalFallsThroughToSearch() = blocking {
        val rejected = listOf(
            record(duration = 300, lyrics = lyricsOf("long")),
            record(lyrics = null),
            record(lyrics = "  "),
            record(instrumental = true, lyrics = lyricsOf("instrumental")),
        )
        for (answer in rejected) {
            seen.clear()
            onGet = { it.reply(200, answer) }
            onSearch = { it.reply(200, "[]") }

            assertNull(provider().find(query()), answer)
            assertEquals(listOf("/api/get", "/api/search"), paths, answer)
        }
    }

    @Test
    fun aGetMissThenASearchPicksTheCandidateWithTheClosestLength() = blocking {
        onSearch = {
            it.reply(
                200,
                array(
                    record(track = "Another song", duration = 258, lyrics = lyricsOf("other")),
                    record(duration = 264, lyrics = lyricsOf("far")),
                    record(duration = 259.4, lyrics = lyricsOf("best")),
                    record(duration = 258.9, instrumental = true, lyrics = lyricsOf("instrumental")),
                    record(duration = 261, lyrics = lyricsOf("near")),
                ),
            )
        }

        val lyrics = provider().find(query())

        assertEquals(lyricsOf("best"), lyrics)
        assertEquals(listOf("/api/get", "/api/search"), paths, "the first accepted result ends the call")
    }

    @Test
    fun theDurationOfARecordMayBeAnIntegerOrADecimalAndUnknownFieldsAreIgnored() = blocking {
        onSearch = {
            it.reply(200, array(record(duration = 258.37, lyrics = lyricsOf("decimal"))))
        }
        assertEquals(lyricsOf("decimal"), provider().find(query()))

        onSearch = {
            it.reply(200, array(record(duration = 260, lyrics = lyricsOf("integer"))))
        }
        assertEquals(lyricsOf("integer"), provider().find(query()))
    }

    @Test
    fun recordsOfTheWrongShapeInASearchAnswerAreSkipped() = blocking {
        val good = record(lyrics = lyricsOf("good"))
        onSearch = {
            it.reply(
                200,
                """[1,"text",null,[],{"trackName":5,"artistName":["IU"],"plainLyrics":${JsonPrimitive(lyricsOf("typed"))}},
                    {"trackName":"Love poem","artistName":"IU","duration":"long","instrumental":"no","plainLyrics":"x"},$good]""",
            )
        }

        assertEquals(lyricsOf("good"), provider().find(query()))
    }

    // ---- what is sent ----

    @Test
    fun theGetRequestCarriesTheUnmodifiedArtistTitleAlbumAndDurationPercentEncodedAsUtf8() = blocking {
        provider().find(query(artist = "소연 (SOYEON)", title = "사랑 & 시 (Live)", album = "앨범 1", durationSeconds = 258))

        val get = seen.first()
        assertEquals("GET", get.method)
        assertEquals("/api/get", get.path)
        assertEquals(
            "artist_name=%EC%86%8C%EC%97%B0%20%28SOYEON%29&track_name=%EC%82%AC%EB%9E%91%20%26%20%EC%8B%9C%20%28Live%29" +
                "&album_name=%EC%95%A8%EB%B2%94%201&duration=258",
            get.rawQuery,
        )
    }

    @Test
    fun anAbsentAlbumAndAnAbsentDurationAreLeftOutOfTheGetRequest() = blocking {
        val provider = provider()
        provider.find(query(album = null, durationSeconds = null))
        provider.find(query(album = "My List", durationSeconds = null))
        provider.find(query(album = null, durationSeconds = 258))

        val gets = seen.filter { it.path == "/api/get" }
        assertEquals(
            listOf(
                "artist_name=IU&track_name=Love%20poem",
                "artist_name=IU&track_name=Love%20poem&album_name=My%20List",
                "artist_name=IU&track_name=Love%20poem&duration=258",
            ),
            gets.map { it.rawQuery },
        )
    }

    @Test
    fun everyRequestIsAGetWithTheUserAgentAndTheAcceptHeader() = blocking {
        provider().find(query())

        assertEquals(listOf("/api/get", "/api/search"), paths)
        for (request in seen) {
            assertEquals("GET", request.method)
            assertEquals("xGetSongs-test/1.0 (https://example.invalid)", request.userAgent)
            assertEquals("application/json", request.accept)
        }
    }

    @Test
    fun theDefaultUserAgentNamesTheApplicationAndItsHomePage() = blocking {
        // The service asks for a descriptive User-Agent; the default must not be the JDK's.
        LrclibLyricsProvider(baseUrl = base).find(query())

        assertEquals("xGetSongs/1.0 (https://github.com/gomtos/xGetSongs)", seen.first().userAgent)
        assertEquals(LrclibLyricsProvider.DEFAULT_USER_AGENT, seen.first().userAgent)
        assertEquals("https://lrclib.net/api", LrclibLyricsProvider.DEFAULT_BASE_URL)
    }

    @Test
    fun theSearchRequestAsksByTrackNameAndArtistName() = blocking {
        provider().find(query(artist = "소연 (SOYEON)", title = "Love poem", album = "My List"))

        val search = seen.first { it.path == "/api/search" }
        assertEquals("track_name=Love%20poem&artist_name=%EC%86%8C%EC%97%B0%20%28SOYEON%29", search.rawQuery)
    }

    @Test
    fun artistAndTitleVariantsAreTriedInOrderAfterGet() = blocking {
        val lyrics = provider().find(query(artist = "소연 (SOYEON)", title = "Title (feat. X)"))

        assertNull(lyrics)
        assertEquals(
            listOf("/api/get") + List(6) { "/api/search" },
            paths,
            "one get and one search for each of the 3 artist and 2 title variants",
        )
        assertEquals(
            listOf(
                "소연 (SOYEON)" to "Title (feat. X)",
                "소연 (SOYEON)" to "Title",
                "소연" to "Title (feat. X)",
                "소연" to "Title",
                "SOYEON" to "Title (feat. X)",
                "SOYEON" to "Title",
            ),
            seen.filter { it.path == "/api/search" }.map { it.params().getValue("artist_name") to it.params().getValue("track_name") },
        )
        assertEquals("소연 (SOYEON)", seen.first().params().getValue("artist_name"), "get uses the unmodified artist")
        assertEquals("Title (feat. X)", seen.first().params().getValue("track_name"), "get uses the unmodified title")
    }

    @Test
    fun theSearchStopsAtTheFirstVariantThatFindsTheSong() = blocking {
        onSearch = { exchange ->
            val title = exchange.rawParams().getValue("track_name")
            if (title == "Title") exchange.reply(200, array(record(track = "Title", artist = "소연", lyrics = lyricsOf("variant")))) else exchange.reply(200, "[]")
        }

        val lyrics = provider().find(query(artist = "소연 (SOYEON)", title = "Title (feat. X)"))

        assertEquals(lyricsOf("variant"), lyrics)
        assertEquals(listOf("/api/get", "/api/search", "/api/search"), paths)
    }

    // ---- the cap ----

    @Test
    fun noMoreThanMaxRequestsAreMadeInOneCall() = blocking {
        // 1 get + 3 artist variants x 4 title variants = 13 possible requests.
        provider().find(query(artist = "소연 (SOYEON)", title = "\"Title\" (Live) feat. X"))

        assertEquals(8, seen.size)
        assertEquals("/api/get", seen.first().path)
    }

    @Test
    fun aSmallerCapStopsEarlier() = blocking {
        provider(maxRequests = 3).find(query(artist = "소연 (SOYEON)", title = "Title (feat. X)"))

        assertEquals(listOf("/api/get", "/api/search", "/api/search"), paths)
    }

    @Test
    fun aCapOfOneMakesOnlyTheGetRequestAndZeroMakesNone() = blocking {
        assertNull(provider(maxRequests = 1).find(query()))
        assertEquals(listOf("/api/get"), paths)

        seen.clear()
        assertNull(provider(maxRequests = 0).find(query()))
        assertEquals(emptyList(), paths)
    }

    @Test
    fun theCapIsPerCall() = blocking {
        val provider = provider(maxRequests = 2)

        provider.find(query())
        provider.find(query())

        assertEquals(4, seen.size)
    }

    // ---- failures ----

    @Test
    fun aTooManyRequestsAnswerToGetStopsEverything() = blocking {
        onGet = { it.reply(429, """{"message":"slow down"}""") }

        assertNull(provider().find(query(artist = "소연 (SOYEON)")))
        assertEquals(listOf("/api/get"), paths)
    }

    @Test
    fun aTooManyRequestsAnswerToASearchStopsEverything() = blocking {
        onSearch = { it.reply(429, "") }

        assertNull(provider().find(query(artist = "소연 (SOYEON)")))
        assertEquals(listOf("/api/get", "/api/search"), paths)
    }

    @Test
    fun aServerErrorOnOneRequestStillLetsTheNextBeTried() = blocking {
        onGet = { it.reply(500, "oops") }
        onSearch = { it.reply(200, array(record(lyrics = lyricsOf("after500")))) }

        assertEquals(lyricsOf("after500"), provider().find(query()))
        assertEquals(listOf("/api/get", "/api/search"), paths)
    }

    @Test
    fun everyAnswerThatIsNot200CountsAsNoResultAndTheNextRequestIsStillMade() = blocking {
        for (status in listOf(400, 403, 404, 500)) {
            seen.clear()
            onGet = { it.reply(status, "x") }
            onSearch = { it.reply(status, "x") }

            assertNull(provider().find(query(artist = "소연 (SOYEON)")), "status $status")
            assertEquals(1 + 3, seen.size, "status $status: get and the three artist variants of one title")
        }
    }

    @Test
    fun aServerErrorOnASearchLetsTheNextSearchBeTried() = blocking {
        onSearch = { exchange ->
            if (exchange.rawParams().getValue("artist_name").startsWith("%EC%86%8C%EC%97%B0%20")) {
                exchange.reply(500, "oops")
            } else {
                exchange.reply(200, array(record(artist = "소연", lyrics = lyricsOf("second"))))
            }
        }

        assertEquals(lyricsOf("second"), provider().find(query(artist = "소연 (SOYEON)")))
        assertEquals(listOf("/api/get", "/api/search", "/api/search"), paths)
    }

    @Test
    fun badJsonGivesNullAndTheNextRequestIsStillTried() = blocking {
        onGet = { it.reply(200, "this is not json") }
        onSearch = { it.reply(200, "{\"truncated\":") }

        assertNull(provider().find(query()))
        assertEquals(listOf("/api/get", "/api/search"), paths)
    }

    @Test
    fun anAnswerOfTheWrongShapeGivesNull() = blocking {
        onGet = { it.reply(200, "[]") }
        onSearch = { it.reply(200, """{"records":[]}""") }

        assertNull(provider().find(query()))

        onGet = { it.reply(200, "") }
        onSearch = { it.reply(200, "null") }
        assertNull(provider().find(query()))
    }

    @Test
    fun anEmptySearchResultGivesNull() = blocking {
        onSearch = { it.reply(200, "[]") }

        assertNull(provider().find(query()))
    }

    @Test
    fun anInstrumentalRecordGivesNull() = blocking {
        onGet = { it.reply(200, record(instrumental = true, lyrics = lyricsOf("instrumental"))) }
        onSearch = { it.reply(200, array(record(instrumental = true, lyrics = lyricsOf("instrumental")))) }

        assertNull(provider().find(query()))
    }

    @Test
    fun aMismatchingRecordGivesNull() = blocking {
        onGet = { it.reply(200, record(track = "Another song", lyrics = lyricsOf("title"))) }
        onSearch = {
            it.reply(
                200,
                array(
                    record(artist = "Somebody Else", lyrics = lyricsOf("artist")),
                    record(duration = 320, lyrics = lyricsOf("length")),
                    record(track = "Love poem 2", lyrics = lyricsOf("title2")),
                ),
            )
        }

        assertNull(provider().find(query()))
    }

    @Test
    fun aGetRecordWhoseLyricsAreTooShortIsNotAnAnswerAndTheSearchIsStillTried() = blocking {
        onGet = { it.reply(200, record(lyrics = "Line one\nLine two")) }
        onSearch = { it.reply(200, array(record(lyrics = lyricsOf("full")))) }

        assertEquals(lyricsOf("full"), provider().find(query()))
        assertEquals(listOf("/api/get", "/api/search"), paths)
    }

    @Test
    fun aCloserSearchRecordWithTooShortLyricsDoesNotHideAnotherAcceptedRecordWithFullLyrics() = blocking {
        onSearch = {
            it.reply(
                200,
                array(
                    record(duration = 258, lyrics = "Line one\nLine two"),
                    record(duration = 258.2, lyrics = "  \n "),
                    record(duration = 258.4, lyrics = null),
                    record(duration = 263, lyrics = lyricsOf("full")),
                ),
            )
        }

        assertEquals(lyricsOf("full"), provider().find(query()))
        assertEquals(listOf("/api/get", "/api/search"), paths)
    }

    @Test
    fun whenNoRecordHasLyricsOfEnoughLinesTheResultIsNull() = blocking {
        onGet = { it.reply(200, record(lyrics = "Line one\nLine two")) }
        onSearch = { it.reply(200, array(record(lyrics = "Only one"), record(lyrics = ""))) }

        assertNull(provider().find(query()))
    }

    @Test
    fun aReadTimeoutGivesNullAndEndsTheCall() = blocking {
        onGet = {
            Thread.sleep(5_000)
            it.reply(200, record())
        }
        val begin = System.nanoTime()

        val lyrics = provider(requestTimeout = 1.seconds).find(query())

        assertNull(lyrics)
        assertTrue(System.nanoTime() - begin < 4_000_000_000L, "the call must end at the time limit, not when the server wakes up")
        assertEquals(listOf("/api/get"), paths, "a service that does not answer is not asked again")
    }

    @Test
    fun aSlowSearchAfterAFastGetEndsTheCallToo() = blocking {
        onSearch = {
            Thread.sleep(5_000)
            it.reply(200, "[]")
        }
        val begin = System.nanoTime()

        assertNull(provider(requestTimeout = 1.seconds).find(query(artist = "소연 (SOYEON)")))

        assertTrue(System.nanoTime() - begin < 4_000_000_000L, "the call must end at the time limit, not when the server wakes up")
        assertEquals(listOf("/api/get", "/api/search"), paths)
    }

    @Test
    fun aRefusedConnectionGivesNullWithoutThrowing() = blocking {
        val deadBase = base
        stopServer()
        val begin = System.nanoTime()

        assertNull(provider(baseUrl = deadBase).find(query(artist = "소연 (SOYEON)")))
        assertNull(provider(baseUrl = deadBase).find(query()))
        assertTrue(System.nanoTime() - begin < 10_000_000_000L)
    }

    @Test
    fun aQueryWithoutAnArtistOrATitleMakesNoRequest() = blocking {
        assertNull(provider().find(query(artist = "  ")))
        assertNull(provider().find(query(title = "")))
        assertEquals(emptyList(), paths)
    }

    @Test
    fun aBlankAlbumAndANonPositiveDurationAreLeftOutOfTheGetRequest() = blocking {
        provider(maxRequests = 1).find(query(album = "  ", durationSeconds = 0))

        assertEquals("artist_name=IU&track_name=Love%20poem", seen.single().rawQuery)
    }

    @Test
    fun aBrokenBaseUrlGivesNullWithoutThrowing() = blocking {
        assertNull(provider(baseUrl = "http://[not a url").find(query()))
        assertNull(provider(baseUrl = "").find(query()))
        assertNull(provider(baseUrl = "ftp://127.0.0.1/api").find(query()))
    }

    @Test
    fun aGivenHttpClientIsUsed() = blocking {
        onGet = { it.reply(200, record(lyrics = lyricsOf("given"))) }
        // The given client leaves two traces that a client the provider builds itself cannot: its proxy selector is asked
        // where to connect, and its executor runs the tasks of the exchange.
        val proxySelections = AtomicInteger()
        val executedTasks = AtomicInteger()
        val client = HttpClient.newBuilder()
            .version(HttpClient.Version.HTTP_1_1)
            .proxy(
                object : ProxySelector() {
                    override fun select(uri: URI?): List<Proxy> {
                        proxySelections.incrementAndGet()
                        return listOf(Proxy.NO_PROXY)
                    }

                    override fun connectFailed(uri: URI?, address: SocketAddress?, failure: IOException?) = Unit
                },
            )
            .executor { task ->
                executedTasks.incrementAndGet()
                task.run()
            }
            .build()

        val lyrics = LrclibLyricsProvider(client = client, baseUrl = base).find(query())

        assertEquals(lyricsOf("given"), lyrics)
        assertTrue(proxySelections.get() > 0, "the request must have gone through the given client")
        assertTrue(executedTasks.get() > 0, "the request must have been run by the executor of the given client")
    }

    // ---- the cool-down after trouble ----

    @Test
    fun afterATooManyRequestsAnswerTheNextCallsMakeNoRequestAtAll() = blocking {
        val provider = provider()
        onGet = { it.reply(429, "{}") }
        assertNull(provider.find(query()))
        assertEquals(1, seen.size)
        onGet = { it.reply(200, record(lyrics = lyricsOf("recovered"))) }

        repeat(3) { assertNull(provider.find(query(title = "Another song $it")), "call $it") }

        assertEquals(1, seen.size, "the service gets its rest: no request while the provider cools down")
    }

    @Test
    fun theCoolDownEndsAfterFiveMinutesAndTheNextCallAsksAgain() = blocking {
        val provider = provider()
        onGet = { it.reply(429, "{}") }
        assertNull(provider.find(query()))
        onGet = { it.reply(200, record(lyrics = lyricsOf("recovered"))) }

        clock += 4.minutes + 59.seconds
        assertNull(provider.find(query()))
        assertEquals(1, seen.size, "4 minutes 59 seconds are not enough")

        clock += 1.seconds
        assertEquals(lyricsOf("recovered"), provider.find(query()))
        assertEquals(2, seen.size)
    }

    @Test
    fun aSecondTroubleAfterTheCoolDownStartsANewOne() = blocking {
        val provider = provider()
        onGet = { it.reply(429, "{}") }
        assertNull(provider.find(query()))
        clock += 5.minutes

        assertNull(provider.find(query()))
        assertEquals(2, seen.size, "after the cool-down it asks again, and is throttled again")
        clock += 4.minutes
        assertNull(provider.find(query()))
        assertEquals(2, seen.size, "the new cool-down counts from the second trouble")
        clock += 1.minutes
        assertNull(provider.find(query()))
        assertEquals(3, seen.size)
    }

    @Test
    fun theCoolDownIsAConstructorParameter() = blocking {
        val provider = LrclibLyricsProvider(baseUrl = base, coolDown = 30.seconds, timeSource = clock)
        onGet = { it.reply(429, "{}") }
        assertNull(provider.find(query()))

        clock += 29.seconds
        assertNull(provider.find(query()))
        assertEquals(1, seen.size)
        clock += 1.seconds
        assertNull(provider.find(query()))
        assertEquals(2, seen.size)
    }

    @Test
    fun badGatewayServiceUnavailableAndGatewayTimeoutAreTroubleLikeTooManyRequests() = blocking {
        for (status in listOf(502, 503, 504)) {
            seen.clear()
            val provider = provider()
            onGet = { it.reply(status, "x") }
            onSearch = { it.reply(status, "x") }

            assertNull(provider.find(query(artist = "소연 (SOYEON)")), "status $status")
            assertEquals(listOf("/api/get"), paths, "status $status stops the call after one request")
            assertNull(provider.find(query(artist = "소연 (SOYEON)")), "status $status")
            assertEquals(listOf("/api/get"), paths, "status $status: no request while it cools down")
        }
    }

    @Test
    fun aBadGatewayOnASearchStopsTheCallAndStartsTheCoolDownToo() = blocking {
        val provider = provider()
        onSearch = { it.reply(502, "x") }

        assertNull(provider.find(query(artist = "소연 (SOYEON)")))
        assertEquals(listOf("/api/get", "/api/search"), paths)
        assertNull(provider.find(query()))
        assertEquals(2, seen.size)
    }

    @Test
    fun notFoundAndServerErrorsAndOtherClientErrorsDoNotStartACoolDown() = blocking {
        for (status in listOf(400, 403, 404, 500)) {
            seen.clear()
            val provider = provider()
            onGet = { it.reply(status, "x") }
            onSearch = { it.reply(status, "x") }

            assertNull(provider.find(query()), "status $status")
            assertNull(provider.find(query()), "status $status")

            assertEquals(4, seen.size, "status $status: both calls made their get and their search")
        }
    }

    @Test
    fun aTimeoutStartsTheCoolDown() = blocking {
        val provider = provider(requestTimeout = 1.seconds)
        onGet = {
            Thread.sleep(5_000)
            it.reply(200, record())
        }
        assertNull(provider.find(query()))
        assertEquals(1, seen.size)
        onGet = { it.reply(200, record(lyrics = lyricsOf("recovered"))) }

        assertNull(provider.find(query()))
        assertEquals(1, seen.size, "no request while it cools down")
        clock += 5.minutes
        assertEquals(lyricsOf("recovered"), provider.find(query()))
    }

    @Test
    fun aRefusedConnectionStartsTheCoolDown() = blocking {
        val deadBase = base
        stopServer()
        // A request cannot be counted at a server that is not there, but the proxy selector of a client is asked once per request.
        val requests = AtomicInteger()
        val client = HttpClient.newBuilder()
            .version(HttpClient.Version.HTTP_1_1)
            .proxy(
                object : ProxySelector() {
                    override fun select(uri: URI?): List<Proxy> {
                        requests.incrementAndGet()
                        return listOf(Proxy.NO_PROXY)
                    }

                    override fun connectFailed(uri: URI?, address: SocketAddress?, failure: IOException?) = Unit
                },
            )
            .build()
        val provider = LrclibLyricsProvider(client = client, baseUrl = deadBase, timeSource = clock)

        assertNull(provider.find(query()))
        assertEquals(1, requests.get(), "the get request fails to connect and ends the call")
        assertNull(provider.find(query()))
        assertEquals(1, requests.get(), "no request while it cools down")
        clock += 5.minutes
        assertNull(provider.find(query()))
        assertEquals(2, requests.get(), "after the cool-down it tries again")
    }

    @Test
    fun anEmptyAnswerABadAnswerAndTheRequestCapDoNotStartACoolDown() = blocking {
        val empty = provider()
        assertNull(empty.find(query()))
        assertNull(empty.find(query()))
        assertEquals(4, seen.size, "get and an empty search, twice")

        seen.clear()
        onGet = { it.reply(200, "not json") }
        val bad = provider()
        assertNull(bad.find(query()))
        assertNull(bad.find(query()))
        assertEquals(4, seen.size)

        seen.clear()
        val capped = provider(maxRequests = 1)
        assertNull(capped.find(query()))
        assertNull(capped.find(query()))
        assertEquals(2, seen.size, "reaching the cap is not trouble of the service")
    }

    @Test
    fun theCoolDownBelongsToTheInstance() = blocking {
        val throttled = provider()
        val other = provider()
        onGet = { it.reply(429, "{}") }
        assertNull(throttled.find(query()))
        onGet = { it.reply(200, record(lyrics = lyricsOf("other"))) }

        assertEquals(lyricsOf("other"), other.find(query()))
        assertNull(throttled.find(query()))
    }

    @Test
    fun callsWaitingForASlotStopWhenAnotherCallRunsIntoTrouble() = blocking {
        onGet = {
            Thread.sleep(300)
            it.reply(429, "{}")
        }
        val provider = provider(maxConcurrent = 1)

        val results = coroutineScope { (1..3).map { async { provider.find(query(title = "Song $it")) } }.awaitAll() }

        assertEquals(List(3) { null }, results)
        assertEquals(1, seen.size, "the two calls queued behind the first one never made their request")
    }

    // ---- the size of an answer ----

    private val twoMebibytes = 2 * 1024 * 1024

    /** A record of exactly [size] bytes: its fields, and a field the provider does not read as padding. */
    private fun recordOfSize(size: Int, lyrics: String): String {
        val head = record(lyrics = lyrics).dropLast(1) + ",\"padding\":\""
        val tail = "\"}"
        return head + "x".repeat(size - head.length - tail.length) + tail
    }

    @Test
    fun anAnswerOfExactlyTwoMebibytesIsRead() = blocking {
        val answer = recordOfSize(twoMebibytes, lyricsOf("big"))
        assertEquals(twoMebibytes, answer.toByteArray(Charsets.UTF_8).size)
        onGet = { it.reply(200, answer) }

        assertEquals(lyricsOf("big"), provider().find(query()))
        assertEquals(listOf("/api/get"), paths)
    }

    @Test
    fun anAnswerOfOneByteMoreIsAMissAndTheNextRequestIsStillMade() = blocking {
        val answer = recordOfSize(twoMebibytes + 1, lyricsOf("too big"))
        assertEquals(twoMebibytes + 1, answer.toByteArray(Charsets.UTF_8).size)
        onGet = { it.reply(200, answer) }
        onSearch = { it.reply(200, array(record(lyrics = lyricsOf("small")))) }

        assertEquals(lyricsOf("small"), provider().find(query()))
        assertEquals(listOf("/api/get", "/api/search"), paths)
    }

    @Test
    fun aBodyThatNeverEndsIsCutOffAfterTwoMebibytesAndNeverBuffered() = blocking {
        val written = AtomicLong()
        val handlerDone = CountDownLatch(1)
        val total = 32L * 1024 * 1024
        onGet = { exchange ->
            try {
                exchange.responseHeaders.add("Content-Type", "application/json")
                exchange.sendResponseHeaders(200, 0) // chunked: no length is announced
                val block = ByteArray(64 * 1024) { 'x'.code.toByte() }
                exchange.responseBody.use { out ->
                    while (written.get() < total) {
                        out.write(block)
                        written.addAndGet(block.size.toLong())
                    }
                }
            } catch (e: IOException) {
                // The client went away: that is what is meant to happen.
            } finally {
                handlerDone.countDown()
            }
        }

        assertNull(provider().find(query()))

        assertTrue(handlerDone.await(15, TimeUnit.SECONDS), "the server must notice that the client went away")
        assertTrue(written.get() < total, "the whole body must not have been read: the server wrote ${written.get()} of $total bytes")
        assertEquals(listOf("/api/get", "/api/search"), paths, "a body that is too large is a miss, the next request is still made")
    }

    @Test
    fun aBodyThatStallsAfterItsHeadersEndsTheCallAtTheTimeLimitAndStartsTheCoolDown() = blocking {
        onGet = { exchange ->
            exchange.responseHeaders.add("Content-Type", "application/json")
            exchange.sendResponseHeaders(200, 0)
            exchange.responseBody.write("[".toByteArray())
            exchange.responseBody.flush()
            Thread.sleep(5_000)
        }
        val provider = provider(requestTimeout = 1.seconds)
        val begin = System.nanoTime()

        assertNull(provider.find(query()))

        assertTrue(System.nanoTime() - begin < 4_000_000_000L, "the call must end at the time limit, not when the server wakes up")
        assertEquals(listOf("/api/get"), paths, "a service that stalls is not asked again")
        assertNull(provider.find(query()))
        assertEquals(1, seen.size, "no request while it cools down")
    }

    // ---- cancellation and concurrency ----

    @Test
    fun cancellingTheCallWhileTheServerSleepsReturnsPromptlyWithACancellationException() = blocking {
        val started = CountDownLatch(1)
        onGet = {
            started.countDown()
            Thread.sleep(10_000)
            it.reply(200, record())
        }
        val outcome = CompletableDeferred<Throwable?>()
        val call = launch(Dispatchers.Default) {
            try {
                provider(requestTimeout = 30.seconds).find(query())
                outcome.complete(null)
            } catch (e: Throwable) {
                outcome.complete(e)
                throw e
            }
        }
        assertTrue(started.await(5, TimeUnit.SECONDS), "the request must reach the server")
        val begin = System.nanoTime()

        call.cancel()
        withTimeout(3.seconds) { call.join() }

        assertTrue(System.nanoTime() - begin < 3_000_000_000L)
        assertTrue(outcome.await() is CancellationException, "got ${outcome.await()}")
    }

    @Test
    fun aCancelledCallGivesItsSlotBack() = blocking {
        val started = CountDownLatch(1)
        onGet = {
            started.countDown()
            Thread.sleep(10_000)
            it.reply(200, record())
        }
        val provider = provider(maxConcurrent = 1, requestTimeout = 30.seconds)
        val first = launch(Dispatchers.Default) { provider.find(query()) }
        assertTrue(started.await(5, TimeUnit.SECONDS))
        first.cancel()
        withTimeout(3.seconds) { first.join() }
        onGet = { it.reply(200, record(lyrics = lyricsOf("later"))) }

        val lyrics = withTimeout(5.seconds) { provider.find(query()) }

        assertEquals(lyricsOf("later"), lyrics)
    }

    @Test
    fun noMoreThanMaxConcurrentRequestsAreInFlightAtOnce() = blocking {
        val active = AtomicInteger()
        val peak = AtomicInteger()
        val slow: (HttpExchange) -> Unit = { exchange ->
            val now = active.incrementAndGet()
            peak.accumulateAndGet(now) { a, b -> maxOf(a, b) }
            try {
                Thread.sleep(120)
            } finally {
                active.decrementAndGet()
            }
            if (exchange.requestURI.rawPath.endsWith("/get")) exchange.reply(404, NOT_FOUND) else exchange.reply(200, "[]")
        }
        onGet = slow
        onSearch = slow
        val provider = provider(maxConcurrent = 2)

        val results = coroutineScope { (1..6).map { async { provider.find(query(title = "Song $it")) } }.awaitAll() }

        assertEquals(List(6) { null }, results)
        assertEquals(12, seen.size, "every call made its get and its one search")
        assertEquals(2, peak.get(), "two requests ran at the same time, never three")
    }

    @Test
    fun theSlotsAreSharedByEveryCallOfOneProviderButNotByTwoProviders() = blocking {
        val active = AtomicInteger()
        val peak = AtomicInteger()
        onGet = { exchange ->
            val now = active.incrementAndGet()
            peak.accumulateAndGet(now) { a, b -> maxOf(a, b) }
            try {
                Thread.sleep(150)
            } finally {
                active.decrementAndGet()
            }
            exchange.reply(404, NOT_FOUND)
        }
        val one = provider(maxConcurrent = 1, maxRequests = 1)
        coroutineScope { (1..4).map { async { one.find(query()) } }.awaitAll() }
        assertEquals(1, peak.get())

        peak.set(0)
        val a = provider(maxConcurrent = 1, maxRequests = 1)
        val b = provider(maxConcurrent = 1, maxRequests = 1)
        coroutineScope { listOf(async { a.find(query()) }, async { b.find(query()) }).awaitAll() }
        assertEquals(2, peak.get(), "each provider has its own limit")
    }
}
