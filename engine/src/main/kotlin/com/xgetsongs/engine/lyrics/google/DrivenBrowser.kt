package com.xgetsongs.engine.lyrics.google

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeSource

/**
 * A [RenderingBrowser] on a [PageDriver], a browser that is never shown: no window of its own on screen, the engine's own
 * User-Agent, and nothing kept between pages (the driver's end decides that).
 *
 * The waiting happens here, in a coroutine that looks at the page every [pollInterval] with a short script (see [PROBE_SCRIPT])
 * and lets a [PageWatch] say when to stop. The browser does not report HTTP statuses, so the page itself (its address and
 * text) tells a block from a result. The page is sent back to the blank page after every render, also when the caller was
 * cancelled. A driver that cannot be used answers [RenderResult.Unavailable] with its reason.
 */
internal class DrivenBrowser(
    private val driver: PageDriver,
    private val pollInterval: Duration = 500.milliseconds,
    private val settleTime: Duration = 4.seconds,
    private val timeSource: TimeSource = TimeSource.Monotonic,
) : RenderingBrowser {
    private val json = Json { ignoreUnknownKeys = true }

    override suspend fun render(url: String, timeout: Duration): RenderResult {
        try {
            return withTimeoutOrNull(timeout) { load(url) } ?: RenderResult.TimedOut
        } catch (e: CancellationException) {
            throw e
        } catch (e: BrowserUnavailableException) {
            return RenderResult.Unavailable(e.reason)
        } catch (e: Exception) {
            return RenderResult.Unavailable(e.javaClass.simpleName)
        } finally {
            // Stop whatever is still loading and let go of the page, also when the caller was cancelled.
            withContext(NonCancellable) { runCatching { driver.reset() } }
        }
    }

    private suspend fun load(url: String): RenderResult.Loaded {
        driver.load(url)
        val watch = PageWatch(settleTime, timeSource)
        while (true) {
            delay(pollInterval)
            val probe = probe() ?: continue
            // Right after load() the browser may still show the page that was there before.
            if (probe.url == BLANK || !watch.isDone(probe)) continue
            return snapshot(probe)
        }
    }

    /** Null when the script gave nothing or not what it should (the page is not ready yet); a browser that cannot run it throws. */
    private suspend fun probe(): PageProbe? {
        val answer = driver.evaluate(PROBE_SCRIPT) ?: return null
        return try {
            json.decodeFromString<PageProbe>(answer)
        } catch (e: IllegalArgumentException) {
            null
        }
    }

    private suspend fun snapshot(probe: PageProbe): RenderResult.Loaded {
        // No HTML is not an empty page: that would be read as "no lyrics card" and remembered for the song.
        val html = driver.evaluate("document.documentElement.outerHTML") ?: throw BrowserUnavailableException("no-html")
        return RenderResult.Loaded(url = probe.url, html = html, bodyText = probe.bodyText)
    }

    private companion object {
        const val BLANK = "about:blank"

        /** One look at the page: where it is, how far it is, whether a lyrics card shows, and the start of its text. */
        val PROBE_SCRIPT = """
            (function () {
              var text = document.body ? document.body.innerText : '';
              return JSON.stringify({
                url: location.href,
                readyState: document.readyState,
                hasCard: !!document.querySelector(${JsonPrimitive(GoogleLyricsSelectors.CARD_PRESENCE)}),
                bodyText: text.length > 4000 ? text.substring(0, 4000) : text
              });
            })()
        """.trimIndent()
    }
}
