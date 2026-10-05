package com.xgetsongs.engine.integration

import com.xgetsongs.engine.lyrics.LrclibLyricsProvider
import com.xgetsongs.engine.lyrics.LyricsCandidate
import com.xgetsongs.engine.lyrics.LyricsMatcher
import com.xgetsongs.engine.lyrics.LyricsQuery
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.doubleOrNull
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.time.Duration
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * Talks to the real LRCLIB (https://lrclib.net), so it is excluded from `test` and only runs through `integrationTest`;
 * it is skipped (not failed) when the service does not answer. Lyrics are copyrighted works: these tests assert and
 * print STRUCTURE only (status codes, key names, counts of lines), never a word of what the service returns.
 */
@Tag("integration")
class RealLrclibIntegrationTest {
    private val provider = LrclibLyricsProvider()
    private val http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(10)).build()

    private fun get(url: String): HttpResponse<String> = http.send(
        HttpRequest.newBuilder(URI.create(url))
            .header("User-Agent", LrclibLyricsProvider.DEFAULT_USER_AGENT)
            .header("Accept", "application/json")
            .timeout(Duration.ofSeconds(20))
            .GET()
            .build(),
        HttpResponse.BodyHandlers.ofString(),
    )

    /** True when the service answers with anything but a server error; it is asked up to three times, a public service has its moments. */
    private fun serviceAnswers(): Boolean {
        repeat(3) { attempt ->
            val answered = try {
                get("${LrclibLyricsProvider.DEFAULT_BASE_URL}/search?track_name=zz-no-such-song-xgetsongs-0000").statusCode() < 500
            } catch (e: Exception) {
                false
            }
            if (answered) return true
            if (attempt < 2) Thread.sleep(2_000)
        }
        return false
    }

    @BeforeTest
    fun requireTheService() {
        assumeTrue(serviceAnswers(), "lrclib.net must answer")
    }

    /** What can be said about [lyrics] without showing it: the number of lines and of non-blank lines. */
    private fun describe(lyrics: String?): String =
        if (lyrics == null) "none" else "${lyrics.split("\n").size} lines, ${lyrics.split("\n").count { it.isNotBlank() }} non-blank, ${lyrics.length} characters"

    // JUnit does not discover test methods with a non-void return type, and the last expression of the runBlocking
    // blocks below is not Unit, so the return type must be declared Unit explicitly.
    @Test
    fun findsTheLyricsOfAKnownSong(): Unit = runBlocking {
        withTimeout(120_000) {
            val lyrics = provider.find(LyricsQuery(artist = "IU", title = "Love poem", album = null, durationSeconds = 258))

            println("lyrics of the known song: ${describe(lyrics)}")
            assertNotNull(lyrics, "the lyrics of a well-known song must be found")
            assertTrue(lyrics.split("\n").count { it.isNotBlank() } >= 10, "at least 10 non-blank lines expected")
        }
    }

    @Test
    fun aTitleWithAParentheticalCreditStillFindsTheSong(): Unit = runBlocking {
        withTimeout(120_000) {
            val lyrics = provider.find(LyricsQuery(artist = "IU", title = "Love poem (feat. nobody)", album = null, durationSeconds = 258))

            println("lyrics for the title with a credit: ${describe(lyrics)}")
            assertNotNull(lyrics, "the credit in brackets must not keep the song from being found")
            assertTrue(lyrics.split("\n").count { it.isNotBlank() } >= 10, "at least 10 non-blank lines expected")
        }
    }

    @Test
    fun aMadeUpSongIsNotFound(): Unit = runBlocking {
        withTimeout(120_000) {
            val lyrics = provider.find(LyricsQuery(artist = "zz-nobody", title = "zz-no-such-song-xgetsongs-0000", album = null, durationSeconds = null))

            println("lyrics of the made-up song: ${describe(lyrics)}")
            assertNull(lyrics)
        }
    }

    /** The record of an LRCLIB answer as the matcher sees it (a field of the wrong type counts as missing). */
    private fun candidateOf(record: JsonObject): LyricsCandidate {
        fun text(key: String) = (record[key] as? JsonPrimitive)?.takeIf { it.isString }?.content
        fun other(key: String) = (record[key] as? JsonPrimitive)?.takeIf { !it.isString }
        return LyricsCandidate(
            trackName = text("trackName"),
            artistName = text("artistName"),
            albumName = text("albumName"),
            duration = other("duration")?.doubleOrNull,
            instrumental = other("instrumental")?.booleanOrNull,
            plainLyrics = text("plainLyrics"),
        )
    }

    /** Describes a record by its keys and the kinds of its values, and by how many lines its plain lyrics have; no text. */
    private fun shapeOf(record: JsonObject): String {
        val kinds = record.entries.joinToString(", ") { (key, value) ->
            val kind = when {
                value is JsonPrimitive && value.isString -> "string"
                value is JsonPrimitive && value.booleanOrNull != null -> "boolean"
                value is JsonPrimitive && value.doubleOrNull != null -> "number"
                value is JsonPrimitive -> "null"
                else -> value::class.simpleName.orEmpty()
            }
            "$key:$kind"
        }
        return "{$kinds}; plainLyrics ${describe(candidateOf(record).plainLyrics)}"
    }

    @Test
    fun theAnswersOfTheServiceHaveTheShapeTheProviderReads() {
        val base = LrclibLyricsProvider.DEFAULT_BASE_URL
        val query = LyricsQuery("IU", "Love poem", null, 258)

        // /search with the track and the artist: an array of records.
        val search = get("$base/search?track_name=Love%20poem&artist_name=IU")
        println("search: HTTP ${search.statusCode()}")
        assertEquals(200, search.statusCode())
        val records = Json.parseToJsonElement(search.body()) as JsonArray
        val objects = records.filterIsInstance<JsonObject>()
        val candidates = objects.map(::candidateOf)
        println(
            "search: ${records.size} records, ${candidates.count { !it.plainLyrics.isNullOrBlank() }} with plain lyrics, " +
                "${candidates.count { it.instrumental == true }} instrumental, ${candidates.count { LyricsMatcher.isMatch(query, it) }} accepted as the song",
        )
        objects.firstOrNull()?.let { println("search: first record is ${shapeOf(it)}") }
        assertTrue(records.isNotEmpty(), "the search must find records for a well-known song")
        assertEquals(records.size, objects.size, "every element of the array is an object")
        for (record in objects) {
            for (key in listOf("trackName", "artistName", "duration", "instrumental", "plainLyrics")) {
                assertTrue(key in record, "every record has $key")
            }
        }

        // /get with the signature we can give (no album): what does the service say?
        val withoutAlbum = get("$base/get?artist_name=IU&track_name=Love%20poem&duration=258")
        println("get without an album: HTTP ${withoutAlbum.statusCode()}")
        (runCatching { Json.parseToJsonElement(withoutAlbum.body()) }.getOrNull() as? JsonObject)?.let {
            println("get without an album: ${shapeOf(it)}")
        }

        // /get with an album too (the album of the song, if the service knows it under this name).
        val withAlbum = get("$base/get?artist_name=IU&track_name=Love%20poem&album_name=Love%20poem&duration=258")
        println("get with an album: HTTP ${withAlbum.statusCode()}")
        (runCatching { Json.parseToJsonElement(withAlbum.body()) }.getOrNull() as? JsonObject)?.let {
            println("get with an album: ${shapeOf(it)}")
        }

        // A search that finds nothing: an empty array or an error status, either is "no result".
        val nothing = get("$base/search?track_name=zz-no-such-song-xgetsongs-0000&artist_name=zz-nobody")
        println("search for nothing: HTTP ${nothing.statusCode()}, body ${nothing.body().length} characters")
        assertTrue(nothing.statusCode() in 200..499)
    }
}
