package com.xgetsongs.engine.ytdlp

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ErrorClassifierTest {
    private fun classify(vararg lines: String) = ErrorClassifier.classify(lines.toList())

    @Test
    fun privateVideoIsUnavailable() {
        val failure = classify("ERROR: [youtube] abc: Private video. Sign in if you've been granted access to this video")
        assertEquals(FailureKind.UNAVAILABLE, failure.kind)
        assertEquals("비공개 영상", failure.message)
    }

    @Test
    fun geoBlockedAndAgeRestrictedAreUnavailable() {
        assertEquals("지역 제한 영상", classify("ERROR: The uploader has not made this video available in your country").message)
        assertEquals(FailureKind.UNAVAILABLE, classify("ERROR: [youtube] abc: Sign in to confirm your age").kind)
        assertEquals(FailureKind.UNAVAILABLE, classify("ERROR: [youtube] abc: Video unavailable").kind)
    }

    @Test
    fun rateLimitAndNetworkErrorsAreTransient() {
        assertEquals(FailureKind.TRANSIENT, classify("ERROR: unable to download video data: HTTP Error 429: Too Many Requests").kind)
        assertEquals(FailureKind.TRANSIENT, classify("ERROR: [Errno 11001] getaddrinfo failed").kind)
        assertEquals(FailureKind.TRANSIENT, classify("WARNING: The read operation timed out").kind)
    }

    @Test
    fun diskAndPermissionErrorsAreFatal() {
        assertEquals(FailureKind.FATAL, classify("OSError: [Errno 28] No space left on device").kind)
        assertEquals(FailureKind.FATAL, classify("PermissionError: [Errno 13] Permission denied: 'x.mp3'").kind)
    }

    @Test
    fun fatalWinsOverTransient() {
        assertEquals(
            FailureKind.FATAL,
            classify("ERROR: unable to download video data", "OSError: [Errno 28] No space left on device").kind,
        )
    }

    @Test
    fun unknownErrorsKeepTheErrorLine() {
        val failure = classify("[youtube] abc: Downloading webpage", "ERROR: [youtube] abc: Something odd happened")
        assertEquals(FailureKind.OTHER, failure.kind)
        assertEquals("[youtube] abc: Something odd happened", failure.message)
    }

    @Test
    fun botCheckIsNotTreatedAsUnavailable() {
        val failure = classify("ERROR: [youtube] abc: Sign in to confirm you're not a bot")
        assertEquals(FailureKind.OTHER, failure.kind)
    }

    @Test
    fun emptyStderrStillGivesAMessage() {
        assertTrue(classify().message.isNotBlank())
    }
}
