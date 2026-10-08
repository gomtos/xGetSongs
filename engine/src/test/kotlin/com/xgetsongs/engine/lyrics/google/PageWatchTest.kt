package com.xgetsongs.engine.lyrics.google

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TestTimeSource

class PageWatchTest {
    private val clock = TestTimeSource()
    private val watch = PageWatch(settleTime = 4.seconds, timeSource = clock)

    private fun probe(
        url: String = "https://www.google.com/search?q=x",
        state: String = "complete",
        card: Boolean = false,
        body: String = "",
    ) = PageProbe(url = url, readyState = state, hasCard = card, bodyText = body)

    @Test
    fun aVisibleCardEndsTheWaitAtOnce() {
        assertTrue(watch.isDone(probe(state = "loading", card = true)))
    }

    @Test
    fun aBlockPageEndsTheWaitAtOnce() {
        assertTrue(watch.isDone(probe(url = "https://www.google.com/sorry/index?continue=x", state = "loading")))
        assertTrue(watch.isDone(probe(state = "complete", body = "unusual traffic")))
    }

    @Test
    fun aPageThatIsStillLoadingIsNeverDone() {
        assertFalse(watch.isDone(probe(state = "loading")))
        clock += 1.minutes
        assertFalse(watch.isDone(probe(state = "loading")))
    }

    @Test
    fun aCompletePageWithoutACardIsDoneOnlyOnceItHasStayedQuiet() {
        assertFalse(watch.isDone(probe()))
        clock += 3.seconds
        assertFalse(watch.isDone(probe()))
        clock += 1.seconds
        assertTrue(watch.isDone(probe()))
    }

    @Test
    fun loadingAgainStartsTheQuietTimeOver() {
        assertFalse(watch.isDone(probe()))
        clock += 3.seconds
        assertFalse(watch.isDone(probe(state = "loading")))
        clock += 3.seconds
        assertFalse(watch.isDone(probe()))
        clock += 3.seconds
        assertFalse(watch.isDone(probe()))
        clock += 1.seconds
        assertTrue(watch.isDone(probe()))
    }

    @Test
    fun anotherAddressStartsTheQuietTimeOver() {
        assertFalse(watch.isDone(probe(url = "https://www.google.com/search?q=a")))
        clock += 3.seconds
        assertFalse(watch.isDone(probe(url = "https://www.google.com/search?q=b")))
        clock += 3.seconds
        assertFalse(watch.isDone(probe(url = "https://www.google.com/search?q=b")))
        clock += 1.seconds
        assertTrue(watch.isDone(probe(url = "https://www.google.com/search?q=b")))
    }
}
