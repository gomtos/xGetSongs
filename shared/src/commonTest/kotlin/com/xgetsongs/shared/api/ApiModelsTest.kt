package com.xgetsongs.shared.api

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ApiModelsTest {
    private val json = ApiJson.instance

    private fun roundTrip(event: JobEvent): JobEvent =
        json.decodeFromString(JobEvent.serializer(), json.encodeToString(JobEvent.serializer(), event))

    @Test
    fun everyJobEventSurvivesARoundTrip() {
        val events = listOf(
            JobEvent.ItemStarted(1, "abc", "001 A - B.mp3"),
            JobEvent.Progress(1, Stage.DOWNLOADING, 42.5),
            JobEvent.Progress(1, Stage.FINISHING),
            JobEvent.ItemDone(1, "001 A - B.mp3"),
            JobEvent.ItemSkipped(2, "이미 존재"),
            JobEvent.ItemFailed(3, "boom"),
            JobEvent.JobDone(JobStatus.COMPLETED, JobSummary(1, 1, 1)),
        )
        events.forEach { assertEquals(it, roundTrip(it)) }
    }

    @Test
    fun eventTypeDiscriminatorMatchesSseName() {
        val events = listOf(
            JobEvent.ItemStarted(1, "abc", "x.mp3"),
            JobEvent.Progress(1, Stage.DOWNLOADING, 1.0),
            JobEvent.ItemDone(1, "x.mp3"),
            JobEvent.ItemSkipped(1, "r"),
            JobEvent.ItemFailed(1, "m"),
            JobEvent.JobDone(JobStatus.CANCELLED, JobSummary(0, 0, 0)),
        )
        events.forEach { event ->
            val encoded = json.encodeToString(JobEvent.serializer(), event)
            assertTrue(encoded.contains("\"type\":\"${event.sseName}\""), encoded)
        }
    }

    @Test
    fun anItemDoneSurvivesARoundTripWithEachLyricsOutcomeAndWithNone() {
        for (outcome in LyricsOutcome.entries + null) {
            val event = JobEvent.ItemDone(4, "004 A - B.mp3", outcome)

            assertEquals(event, roundTrip(event), "$outcome")
            assertEquals(outcome, (roundTrip(event) as JobEvent.ItemDone).lyrics, "$outcome")
        }
    }

    @Test
    fun theLyricsOutcomeIsSentByItsName() {
        val expectedNames = listOf("DESCRIPTION", "ONLINE", "NOT_FOUND", "SEARCH_OFF")
        assertEquals(expectedNames, LyricsOutcome.entries.map { it.name })
        for (name in expectedNames) {
            val encoded = json.encodeToString(JobEvent.serializer(), JobEvent.ItemDone(1, "x.mp3", LyricsOutcome.valueOf(name)))

            assertTrue(encoded.contains("\"lyrics\":\"$name\""), encoded)
        }
    }

    @Test
    fun anItemDoneWithoutTheLyricsFieldDecodesToNotKnown() {
        val decoded = json.decodeFromString(JobEvent.serializer(), """{"type":"item-done","rank":2,"fileName":"002 A - B.mp3"}""")

        assertEquals(JobEvent.ItemDone(2, "002 A - B.mp3", lyrics = null), decoded)
        assertNull((decoded as JobEvent.ItemDone).lyrics)
        assertNull(JobEvent.ItemDone(2, "002 A - B.mp3").lyrics, "the field defaults to not known")
    }

    @Test
    fun anItemDoneWithAnExplicitNullLyricsFieldDecodesToNotKnown() {
        val decoded = json.decodeFromString(JobEvent.serializer(), """{"type":"item-done","rank":2,"fileName":"x.mp3","lyrics":null}""")

        assertEquals(JobEvent.ItemDone(2, "x.mp3", null), decoded)
    }

    @Test
    fun theSseNameOfAnItemDoneStaysItemDoneWhateverTheOutcome() {
        for (outcome in LyricsOutcome.entries + null) {
            val event = JobEvent.ItemDone(1, "x.mp3", outcome)
            val encoded = json.encodeToString(JobEvent.serializer(), event)

            assertEquals("item-done", event.sseName, "$outcome")
            assertTrue(encoded.contains("\"type\":\"item-done\""), encoded)
        }
    }

    @Test
    fun jobRequestDefaultsApplyWhenFieldsAreOmitted() {
        val request = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1"}""")
        assertEquals(JobRequest("r1", JobOptions(), null), request)
        assertEquals(1, request.options.singleRank)
    }

    @Test
    fun aConcurrencyFromAnOlderClientIsIgnoredBecauseTheServerDecidesIt() {
        val request = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1","options":{"concurrency":3}}""")

        assertEquals(JobRequest("r1", JobOptions(), null), request)
        assertFalse(json.encodeToString(JobRequest.serializer(), request).contains("concurrency"))
    }

    @Test
    fun theRankStaysInTheFileNameUnlessTheOptionIsTurnedOff() {
        assertTrue(JobOptions().includeRank)
        val omitted = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1","options":{"outputDir":"D:/Music"}}""")
        assertTrue(omitted.options.includeRank, "a client that does not know the option keeps the old behaviour")
        val off = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1","options":{"includeRank":false}}""")
        assertFalse(off.options.includeRank)
    }

    @Test
    fun theIncludeRankOptionSurvivesARoundTrip() {
        for (value in listOf(true, false)) {
            val request = JobRequest("r1", JobOptions(outputDir = "D:/Music", overwrite = true, includeRank = value), listOf(3))
            val encoded = json.encodeToString(JobRequest.serializer(), request)

            assertTrue(encoded.contains("\"includeRank\":$value"), encoded)
            assertEquals(request, json.decodeFromString(JobRequest.serializer(), encoded))
        }
    }

    @Test
    fun theLyricsSearchIsOnUnlessTheOptionIsTurnedOff() {
        assertTrue(JobOptions().searchLyricsOnline)
        val omitted = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1","options":{"outputDir":"D:/Music"}}""")
        assertTrue(omitted.options.searchLyricsOnline, "a client that does not know the option gets the default, which is on")
        val off = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1","options":{"searchLyricsOnline":false}}""")
        assertFalse(off.options.searchLyricsOnline)
        assertTrue(json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1"}""").options.searchLyricsOnline)
    }

    @Test
    fun theSearchLyricsOnlineOptionSurvivesARoundTrip() {
        for (value in listOf(true, false)) {
            val request = JobRequest("r1", JobOptions(outputDir = "D:/Music", includeRank = false, searchLyricsOnline = value), listOf(3))
            val encoded = json.encodeToString(JobRequest.serializer(), request)

            assertTrue(encoded.contains("\"searchLyricsOnline\":$value"), encoded)
            assertEquals(request, json.decodeFromString(JobRequest.serializer(), encoded))
        }
    }

    @Test
    fun theAlbumNameIsNotSetUnlessTheClientSendsIt() {
        assertNull(JobOptions().albumName)
        val omitted = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1","options":{"outputDir":"D:/Music"}}""")
        assertNull(omitted.options.albumName, "a client that does not know the option gets the old behaviour")
        val set = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1","options":{"albumName":"내 앨범"}}""")
        assertEquals("내 앨범", set.options.albumName)
    }

    @Test
    fun anOldClientsFolderNameIsIgnored() {
        val old = json.decodeFromString(JobRequest.serializer(), """{"resolveId":"r1","options":{"albumName":"내 앨범","folderName":"내 폴더"}}""")
        assertEquals("내 앨범", old.options.albumName)
    }

    @Test
    fun theAlbumNameSurvivesARoundTrip() {
        val request = JobRequest("r1", JobOptions(outputDir = "D:/Music", albumName = "내 앨범"), listOf(3))
        val encoded = json.encodeToString(JobRequest.serializer(), request)

        assertEquals(request, json.decodeFromString(JobRequest.serializer(), encoded))
    }

    @Test
    fun resolveResponseRoundTrips() {
        val response = ResolveResponse(
            resolveId = "r1",
            kind = InputKind.PLAYLIST,
            playlistTitle = "Melon Daily Top 100",
            items = listOf(
                ResolvedItem(1, "id1", "A - B", "ch", "A", "B", false, true, null, "001 A - B.mp3"),
                ResolvedItem(2, "id2", "[Private video]", available = false, unavailableReason = "비공개 영상"),
            ),
            truncated = true,
            alsoVideoId = "id1",
        )
        val decoded = json.decodeFromString(
            ResolveResponse.serializer(),
            json.encodeToString(ResolveResponse.serializer(), response),
        )
        assertEquals(response, decoded)
    }

    @Test
    fun unknownKeysAreIgnored() {
        val decoded = json.decodeFromString(ErrorResponse.serializer(), """{"message":"m","extra":1}""")
        assertEquals(ErrorResponse("m"), decoded)
    }
}
