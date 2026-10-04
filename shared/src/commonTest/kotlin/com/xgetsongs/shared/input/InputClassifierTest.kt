package com.xgetsongs.shared.input

import kotlin.test.Test
import kotlin.test.assertEquals

class InputClassifierTest {
    private val playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"
    private val videoId = "dQw4w9WgXcQ"

    private fun ok(input: ParsedInput) = ClassifyResult.Ok(input)
    private fun rejected(reason: RejectReason) = ClassifyResult.Rejected(reason)

    @Test
    fun barePlaylistId() {
        assertEquals(ok(ParsedInput.Playlist(playlistId)), InputClassifier.classify(playlistId))
    }

    @Test
    fun bareVideoId() {
        assertEquals(ok(ParsedInput.Video(videoId)), InputClassifier.classify(videoId))
    }

    @Test
    fun elevenCharIdStartingWithPlIsAVideo() {
        assertEquals(ok(ParsedInput.Video("PLxxxxxxxxx")), InputClassifier.classify("PLxxxxxxxxx"))
    }

    @Test
    fun playlistUrl() {
        assertEquals(
            ok(ParsedInput.Playlist(playlistId)),
            InputClassifier.classify("https://www.youtube.com/playlist?list=$playlistId"),
        )
    }

    @Test
    fun watchUrl() {
        assertEquals(
            ok(ParsedInput.Video(videoId)),
            InputClassifier.classify("https://www.youtube.com/watch?v=$videoId"),
        )
    }

    @Test
    fun shortUrlsAndShorts() {
        assertEquals(ok(ParsedInput.Video(videoId)), InputClassifier.classify("https://youtu.be/$videoId?si=abc"))
        assertEquals(ok(ParsedInput.Video(videoId)), InputClassifier.classify("https://www.youtube.com/shorts/$videoId"))
    }

    @Test
    fun urlWithoutScheme() {
        assertEquals(ok(ParsedInput.Video(videoId)), InputClassifier.classify("music.youtube.com/watch?v=$videoId"))
    }

    @Test
    fun watchUrlWithListIsPlaylistAndRemembersVideo() {
        assertEquals(
            ok(ParsedInput.Playlist(playlistId, alsoVideoId = videoId)),
            InputClassifier.classify("https://www.youtube.com/watch?v=$videoId&list=$playlistId"),
        )
    }

    @Test
    fun mixListWithVideoFallsBackToVideo() {
        assertEquals(
            ok(ParsedInput.Video(videoId)),
            InputClassifier.classify("https://www.youtube.com/watch?v=$videoId&list=RD$videoId"),
        )
    }

    @Test
    fun mixListAloneIsRejected() {
        assertEquals(
            rejected(RejectReason.MIX_PLAYLIST),
            InputClassifier.classify("https://www.youtube.com/playlist?list=RDCLAK5uy_abcdefgh"),
        )
    }

    @Test
    fun blankIsRejected() {
        assertEquals(rejected(RejectReason.EMPTY), InputClassifier.classify("   "))
    }

    @Test
    fun foreignHostsAreRejected() {
        assertEquals(rejected(RejectReason.UNSUPPORTED_HOST), InputClassifier.classify("https://evil.com/watch?v=$videoId"))
        assertEquals(
            rejected(RejectReason.UNSUPPORTED_HOST),
            InputClassifier.classify("https://www.youtube.com.evil.com/watch?v=$videoId"),
        )
        assertEquals(
            rejected(RejectReason.UNSUPPORTED_HOST),
            InputClassifier.classify("https://youtube.com@evil.com/watch?v=$videoId"),
        )
    }

    @Test
    fun garbageIsRejected() {
        assertEquals(rejected(RejectReason.UNRECOGNIZED), InputClassifier.classify("PLshort"))
        assertEquals(rejected(RejectReason.UNRECOGNIZED), InputClassifier.classify("hello world"))
        assertEquals(rejected(RejectReason.UNSUPPORTED_HOST), InputClassifier.classify("PLabc; rm -rf /"))
    }

    @Test
    fun canonicalUrlIsRebuiltFromIdOnly() {
        val result = InputClassifier.classify(
            "https://www.youtube.com/watch?v=$videoId&list=PLx%20--exec&t=10s",
        ) as ClassifyResult.Ok
        assertEquals("https://www.youtube.com/watch?v=$videoId", result.input.canonicalUrl)
    }

    @Test
    fun playlistCanonicalUrl() {
        val result = InputClassifier.classify(playlistId) as ClassifyResult.Ok
        assertEquals("https://www.youtube.com/playlist?list=$playlistId", result.input.canonicalUrl)
    }
}
