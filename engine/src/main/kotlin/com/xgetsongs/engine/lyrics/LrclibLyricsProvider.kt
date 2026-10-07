package com.xgetsongs.engine.lyrics

import com.xgetsongs.shared.lyrics.LyricsExtractor
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.doubleOrNull
import java.io.IOException
import java.io.InputStream
import java.io.InterruptedIOException
import java.net.ConnectException
import java.net.NoRouteToHostException
import java.net.SocketTimeoutException
import java.net.URI
import java.net.URLEncoder
import java.net.UnknownHostException
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.net.http.HttpTimeoutException
import java.nio.charset.StandardCharsets
import kotlin.time.Duration
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeMark
import kotlin.time.TimeSource
import kotlin.time.toJavaDuration

/**
 * Looks lyrics up in LRCLIB (https://lrclib.net), an open lyrics database with a public JSON API meant for programs: no
 * key, and a request for a descriptive `User-Agent` ([userAgent]). Only plain, unsynchronised lyrics are used.
 *
 * A call makes at most [maxRequests] requests and stops at the first accepted record ([LyricsMatcher.isMatch]) that has
 * usable lyrics (at least [LyricsExtractor.tidy] says so: a record with a line or two of "lyrics" is no answer and does
 * not hide another record that has them):
 *  1. `GET {baseUrl}/get` with the artist and title as given, and the album and length when there are some: the service's
 *     own exact match;
 *  2. `GET {baseUrl}/search` for every artist variant and, within it, every title variant ([LyricsMatcher]), best one first;
 *     of the records of an answer the best one is taken ([LyricsMatcher.pick]: the best title match, then the same album,
 *     then the closest length).
 *
 * Whatever is not HTTP 200 with a body that can be read counts as "no result" for that request and the next one is still
 * made, and so does a body of more than [MAX_BODY_BYTES] bytes (it is never read to the end). The exceptions are 429 (too
 * many requests), 502, 503 and 504 (the service or what is in front of it is down or overloaded) and a failure to connect
 * or a timeout: those end the call, and the provider then answers null at once, without a request, for [coolDown], so a
 * service that is down or throttling is not asked again and again, by this call or the calls of the other songs of a
 * playlist. The answer is [LyricsExtractor.tidy] of the lyrics of the accepted record, null when there is none.
 *
 * The requests run on the IO dispatcher and are interrupted when the calling coroutine is cancelled. At most
 * [maxConcurrent] requests of this provider are in flight at once, however many songs are being looked up. Every
 * exception but a cancellation is swallowed: [find] returns null instead.
 *
 * @param client the HTTP client; null builds one that follows redirects and gives up connecting after [connectTimeout]
 * (an HTTP client has one connect timeout of its own, so [connectTimeout] means nothing for a given [client]).
 * @param requestTimeout how long one request may take, from sending it until its answer is in.
 * @param coolDown how long this instance makes no request after a call ended in trouble (see above); it belongs to the
 * instance, not to the service.
 * @param timeSource the clock of the cool-down; a test gives one that it moves itself.
 */
class LrclibLyricsProvider(
    client: HttpClient? = null,
    baseUrl: String = DEFAULT_BASE_URL,
    private val userAgent: String = DEFAULT_USER_AGENT,
    connectTimeout: Duration = 10.seconds,
    private val requestTimeout: Duration = 15.seconds,
    private val maxRequests: Int = 8,
    maxConcurrent: Int = 2,
    private val coolDown: Duration = 5.minutes,
    private val timeSource: TimeSource = TimeSource.Monotonic,
) : LyricsProvider {
    private val client: HttpClient = client
        ?: HttpClient.newBuilder().followRedirects(HttpClient.Redirect.NORMAL).connectTimeout(connectTimeout.toJavaDuration()).build()
    private val baseUrl = baseUrl.trimEnd('/')
    private val slots = Semaphore(maxConcurrent.coerceAtLeast(1))

    /** When the last trouble happened; null until there was some. Written by whichever call meets the trouble. */
    @Volatile
    private var troubleMark: TimeMark? = null

    /** What one request came to. */
    private sealed interface Outcome {
        /** HTTP 200: the body, decoded as UTF-8. */
        class Body(val text: String) : Outcome

        /** No result from this request; the next one is still made. */
        data object Miss : Outcome

        /** The call ends here: the service is throttling, down or unreachable, or the cap of requests is reached. */
        data object Stop : Outcome
    }

    override suspend fun find(query: LyricsQuery): String? {
        try {
            return lookUp(query)
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            return null
        }
    }

    private suspend fun lookUp(query: LyricsQuery): String? {
        if (query.artist.isBlank() || query.title.isBlank() || isCoolingDown()) return null
        var used = 0
        suspend fun fetch(url: String): Outcome {
            if (used >= maxRequests) return Outcome.Stop
            used++
            return send(url)
        }

        when (val answer = fetch(getUrl(query))) {
            Outcome.Stop -> return null
            Outcome.Miss -> Unit
            is Outcome.Body -> {
                val record = candidateOf(parse(answer.text))?.let(::withTidyLyrics)
                if (record != null && LyricsMatcher.isMatch(query, record)) return record.plainLyrics
            }
        }
        for (artist in LyricsMatcher.artistVariants(query.artist)) {
            for (title in LyricsMatcher.titleVariants(query.title)) {
                when (val answer = fetch(searchUrl(artist, title))) {
                    Outcome.Stop -> return null
                    Outcome.Miss -> Unit
                    is Outcome.Body -> {
                        val record = LyricsMatcher.pick(query, candidatesOf(parse(answer.text)))
                        if (record != null) return record.plainLyrics
                    }
                }
            }
        }
        return null
    }

    /** True while the last trouble is less than [coolDown] ago. */
    private fun isCoolingDown(): Boolean = troubleMark?.let { it.elapsedNow() < coolDown } ?: false

    /** The service did not answer as it should: this provider leaves it alone for [coolDown]. */
    private fun startCoolDown() {
        troubleMark = timeSource.markNow()
    }

    /** One request, one slot of [slots] for as long as it lasts. */
    private suspend fun send(url: String): Outcome {
        try {
            return slots.withPermit {
                // Another call may have run into trouble while this one waited for its slot.
                if (isCoolingDown()) return@withPermit Outcome.Stop
                // The client's own timeout covers the wait for the answer; this one also covers a body that never ends.
                val outcome = withTimeoutOrNull(requestTimeout) { runInterruptible(Dispatchers.IO) { exchange(url) } } ?: Outcome.Stop
                if (outcome === Outcome.Stop) startCoolDown()
                outcome
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: IOException) {
            if (!isConnectionProblem(e)) return Outcome.Miss
            startCoolDown()
            return Outcome.Stop
        } catch (e: Exception) {
            return Outcome.Miss
        }
    }

    /** The blocking part: sends a GET for [url] and says what came of it. */
    private fun exchange(url: String): Outcome {
        val request = HttpRequest.newBuilder(URI.create(url))
            .header("User-Agent", userAgent)
            .header("Accept", "application/json")
            .timeout(requestTimeout.toJavaDuration())
            .GET()
            .build()
        val response = client.send(request, HttpResponse.BodyHandlers.ofInputStream())
        // Closing the stream, read or not, drops what the service has not sent yet; a body is never held in memory in full.
        return response.body().use { stream ->
            when (response.statusCode()) {
                HTTP_OK -> {
                    val bytes = readAtMost(stream, MAX_BODY_BYTES + 1)
                    if (bytes.size > MAX_BODY_BYTES) Outcome.Miss else Outcome.Body(String(bytes, StandardCharsets.UTF_8))
                }
                HTTP_TOO_MANY_REQUESTS, HTTP_BAD_GATEWAY, HTTP_SERVICE_UNAVAILABLE, HTTP_GATEWAY_TIMEOUT -> Outcome.Stop
                else -> Outcome.Miss
            }
        }
    }

    /**
     * Up to [limit] bytes of [stream]. The stream of the HTTP client reports an interrupt of the waiting thread (the time
     * limit, or the cancellation of the call) as an [IOException]; it is passed on as the [InterruptedException] that it
     * was, which is what the coroutine machinery turns into a cancellation.
     */
    private fun readAtMost(stream: InputStream, limit: Int): ByteArray {
        try {
            return stream.readNBytes(limit)
        } catch (e: IOException) {
            val causes = generateSequence<Throwable>(e) { it.cause }
            if (Thread.currentThread().isInterrupted || causes.any { it is InterruptedException || it is InterruptedIOException }) {
                throw InterruptedException()
            }
            throw e
        }
    }

    /** True when [e] (or what caused it) says the service could not be reached in time. */
    private fun isConnectionProblem(e: Throwable): Boolean = generateSequence(e) { it.cause }.any {
        it is ConnectException || it is UnknownHostException || it is NoRouteToHostException ||
            it is HttpTimeoutException || it is SocketTimeoutException
    }

    private fun getUrl(query: LyricsQuery): String = buildString {
        append(baseUrl).append("/get?artist_name=").append(encode(query.artist)).append("&track_name=").append(encode(query.title))
        query.album?.takeIf { it.isNotBlank() }?.let { append("&album_name=").append(encode(it)) }
        query.durationSeconds?.takeIf { it > 0 }?.let { append("&duration=").append(it) }
    }

    private fun searchUrl(artist: String, title: String): String =
        "$baseUrl/search?track_name=${encode(title)}&artist_name=${encode(artist)}"

    /** [value] as UTF-8 percent-encoding; a space is `%20`, never `+`. */
    private fun encode(value: String): String = URLEncoder.encode(value, StandardCharsets.UTF_8).replace("+", "%20")

    /** The JSON in [text]; null when it is not JSON. */
    private fun parse(text: String): JsonElement? = try {
        Json.parseToJsonElement(text)
    } catch (e: IllegalArgumentException) {
        // A SerializationException (invalid JSON) is one of these.
        null
    }

    /** The record in [element] when it is a JSON object; a field of an unexpected type is read as missing. */
    private fun candidateOf(element: JsonElement?): LyricsCandidate? {
        val record = element as? JsonObject ?: return null
        return LyricsCandidate(
            trackName = text(record, "trackName"),
            artistName = text(record, "artistName"),
            albumName = text(record, "albumName"),
            duration = number(record, "duration"),
            instrumental = flag(record, "instrumental"),
            plainLyrics = text(record, "plainLyrics"),
        )
    }

    /**
     * The records in [element] when it is a JSON array; its elements that are not objects are skipped, and so are the ones
     * without usable lyrics.
     */
    private fun candidatesOf(element: JsonElement?): List<LyricsCandidate> =
        (element as? JsonArray).orEmpty().mapNotNull { candidateOf(it)?.let(::withTidyLyrics) }

    /** [record] with its lyrics as [LyricsExtractor.tidy] makes them; null when they are not usable as lyrics. */
    private fun withTidyLyrics(record: LyricsCandidate): LyricsCandidate? =
        LyricsExtractor.tidy(record.plainLyrics)?.let { record.copy(plainLyrics = it) }

    private fun text(record: JsonObject, key: String): String? =
        (record[key] as? JsonPrimitive)?.takeIf { it.isString }?.content

    /** An integer or a decimal. */
    private fun number(record: JsonObject, key: String): Double? =
        (record[key] as? JsonPrimitive)?.takeIf { !it.isString }?.doubleOrNull

    private fun flag(record: JsonObject, key: String): Boolean? =
        (record[key] as? JsonPrimitive)?.takeIf { !it.isString }?.booleanOrNull

    companion object {
        const val DEFAULT_BASE_URL = "https://lrclib.net/api"
        const val DEFAULT_USER_AGENT = "xGetSongs/1.0 (https://github.com/gomtos/xGetSongs)"

        /** An answer longer than this (bytes) is not read: the lyrics of a song are a few kilobytes. */
        private const val MAX_BODY_BYTES = 2 * 1024 * 1024
        private const val HTTP_OK = 200
        private const val HTTP_TOO_MANY_REQUESTS = 429
        private const val HTTP_BAD_GATEWAY = 502
        private const val HTTP_SERVICE_UNAVAILABLE = 503
        private const val HTTP_GATEWAY_TIMEOUT = 504
    }
}
