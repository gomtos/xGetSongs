package com.xgetsongs.engine.lyrics.google

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.concurrent.ConcurrentHashMap
import kotlin.time.Duration
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeMark
import kotlin.time.TimeSource

/**
 * Looks a song up in Google's lyrics card through a [RenderingBrowser], politely. The rules:
 *  - one trip per song, or two when the first finds no card and the artist has a parenthesised name
 *    ([GoogleSearchQuery.requests]); trips run one at a time, at least [minInterval] after the previous one ended;
 *  - Google answering with trouble ([GoogleLyricsResult.Captcha], [GoogleLyricsResult.Consent],
 *    [GoogleLyricsResult.RateLimited], [GoogleLyricsResult.Timeout]) puts Google off limits for [firstBackoff], then twice
 *    that for the next trouble in a row, and so on up to [maxBackoff]; an ordinary answer ends the streak. While it is off
 *    limits the answer is [GoogleLyricsResult.CoolingDown] and no trip is made. Nothing tries to get around a block;
 *  - what Google said about a song ([GoogleLyricsResult.Found], [GoogleLyricsResult.NoCard],
 *    [GoogleLyricsResult.ExtractionFailed]) is remembered for as long as the fetcher lives; trouble and an unusable
 *    browser are not, they say nothing about the song.
 *
 * Whatever goes wrong is a [GoogleLyricsResult]; only a cancellation is thrown. [pause] is how the interval is waited
 * out (a test replaces it).
 */
internal class GoogleLyricsFetcher(
    private val browser: RenderingBrowser,
    private val timeSource: TimeSource = TimeSource.Monotonic,
    private val minInterval: Duration = 10.seconds,
    private val firstBackoff: Duration = 5.minutes,
    private val maxBackoff: Duration = 60.minutes,
    private val pageTimeout: Duration = 20.seconds,
    private val log: GoogleLyricsLog = GoogleLyricsLog.None,
    private val pause: suspend (Duration) -> Unit = { delay(it) },
) {
    private val lock = Mutex()
    private val remembered = ConcurrentHashMap<String, GoogleLyricsResult>()

    // The three below are only touched while holding [lock].
    private var lastTripEnd: TimeMark? = null
    private var coolDownEnd: TimeMark? = null
    private var troubleStreak = 0

    suspend fun fetch(artist: String, title: String): GoogleLyricsResult {
        val requests = GoogleSearchQuery.requests(artist, title)
        if (requests.isEmpty()) return GoogleLyricsResult.NoCard
        val key = GoogleSearchQuery.cacheKey(requests.first())
        remembered[key]?.let { return it }
        return lock.withLock {
            // Another call may have asked about this song while this one waited for the lock.
            remembered[key]?.let { return@withLock it }
            var result: GoogleLyricsResult = GoogleLyricsResult.NoCard
            for (request in requests) {
                if (isCoolingDown()) {
                    result = GoogleLyricsResult.CoolingDown
                    break
                }
                waitForInterval()
                result = tripTo(request)
                if (result != GoogleLyricsResult.NoCard) break
            }
            if (result is GoogleLyricsResult.Found || result == GoogleLyricsResult.NoCard || result == GoogleLyricsResult.ExtractionFailed) {
                remembered[key] = result
            }
            result
        }
    }

    private fun isCoolingDown(): Boolean = coolDownEnd?.let { !it.hasPassedNow() } ?: false

    private suspend fun waitForInterval() {
        val last = lastTripEnd ?: return
        val wait = minInterval - last.elapsedNow()
        if (wait.isPositive()) pause(wait)
    }

    /** One trip: the browser, then the classification of what it brought back. */
    private suspend fun tripTo(request: GoogleSearchRequest): GoogleLyricsResult {
        val result = try {
            when (val page = browser.render(request.url, pageTimeout)) {
                RenderResult.TimedOut -> GoogleLyricsResult.Timeout
                is RenderResult.Unavailable -> GoogleLyricsResult.BrowserUnavailable(page.reason)
                is RenderResult.Loaded -> PageClassifier.blockOf(page.url, null, page.bodyText) ?: LyricsCardParser.parse(page.html)
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            GoogleLyricsResult.BrowserUnavailable(e.javaClass.simpleName)
        }
        lastTripEnd = timeSource.markNow()
        record(result)
        return result
    }

    /** Logs [result] (counts and names only) and keeps the cool-down books. */
    private fun record(result: GoogleLyricsResult) {
        when (result) {
            is GoogleLyricsResult.Found -> {
                troubleStreak = 0
                log.info("구글 가사 카드: 찾음 (줄 ${result.lineCount}, 문단 ${result.paragraphCount})")
            }
            GoogleLyricsResult.NoCard -> {
                troubleStreak = 0
                log.info("구글 가사 카드: 카드 없음")
            }
            GoogleLyricsResult.ExtractionFailed -> {
                troubleStreak = 0
                log.warn("구글 가사 카드: 카드는 있으나 줄을 읽지 못함 (구글 페이지 형식이 바뀌었을 수 있음)")
            }
            GoogleLyricsResult.Captcha -> startCoolDown("CAPTCHA로 막힘")
            GoogleLyricsResult.Consent -> startCoolDown("동의창이 뜸")
            GoogleLyricsResult.RateLimited -> startCoolDown("요청이 너무 많다는 응답(429)")
            GoogleLyricsResult.Timeout -> startCoolDown("시간 초과")
            is GoogleLyricsResult.BrowserUnavailable -> log.warn("구글 가사 카드: 웹뷰를 쓸 수 없음 (${result.reason})")
            GoogleLyricsResult.CoolingDown -> Unit
        }
    }

    private fun startCoolDown(what: String) {
        val length = minOf(maxBackoff, firstBackoff * (1 shl minOf(troubleStreak, MAX_DOUBLINGS)))
        troubleStreak++
        coolDownEnd = timeSource.markNow() + length
        log.warn("구글 가사 카드: $what, ${length.inWholeMinutes}분 동안 구글 검색을 쉼")
    }

    private companion object {
        /** More doublings than this are beyond any [maxBackoff]; it only keeps the shift from overflowing. */
        const val MAX_DOUBLINGS = 16
    }
}
