package com.xgetsongs.engine.lyrics.google

import javafx.application.Platform
import javafx.scene.web.WebEngine
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeSource

/**
 * A [RenderingBrowser] on a JavaFX [WebEngine] that is never shown: no window, no process of its own, the engine's own
 * User-Agent, and no cookie store (so nothing is kept between pages). The engine is made on first use and reused.
 *
 * Everything that touches the engine runs on the JavaFX thread; the waiting happens in a coroutine that looks at the
 * page every [pollInterval] with a short script (see [PROBE_SCRIPT]) and lets a [PageWatch] say when to stop. The
 * engine does not report HTTP statuses, so the page itself (its address and text) tells a block from a result.
 *
 * The JavaFX thread keeps the JVM alive until the runtime is shut down, so whoever owns this browser must [close] it
 * when the application ends. If JavaFX is missing or does not start, [render] answers [RenderResult.Unavailable].
 */
internal class FxWebViewBrowser(
    private val pollInterval: Duration = 500.milliseconds,
    private val settleTime: Duration = 4.seconds,
) : RenderingBrowser, AutoCloseable {
    private val json = Json { ignoreUnknownKeys = true }
    private val startLock = Any()

    @Volatile
    private var started = false

    /** Only touched on the JavaFX thread. */
    private var engine: WebEngine? = null

    override suspend fun render(url: String, timeout: Duration): RenderResult {
        try {
            startToolkit()
        } catch (e: Exception) {
            return RenderResult.Unavailable(e.javaClass.simpleName)
        } catch (e: LinkageError) {
            // JavaFX classes or natives missing from the class path.
            return RenderResult.Unavailable(e.javaClass.simpleName)
        }
        try {
            return withTimeoutOrNull(timeout) { load(url) } ?: RenderResult.TimedOut
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            return RenderResult.Unavailable(e.javaClass.simpleName)
        } finally {
            // Stop whatever is still loading and let go of the page, also when the caller was cancelled.
            withContext(NonCancellable) { runCatching { onFxThread { webEngine().load(BLANK) } } }
        }
    }

    private suspend fun load(url: String): RenderResult.Loaded {
        onFxThread { webEngine().load(url) }
        val watch = PageWatch(settleTime, TimeSource.Monotonic)
        while (true) {
            delay(pollInterval)
            val probe = onFxThread { probe() } ?: continue
            // Right after load() the engine may still show the page that was there before.
            if (probe.url == BLANK || !watch.isDone(probe)) continue
            return onFxThread { snapshot(probe) }
        }
    }

    /** Null when the script cannot run (no document yet) or does not answer what it should. */
    private fun probe(): PageProbe? = try {
        (webEngine().executeScript(PROBE_SCRIPT) as? String)?.let { json.decodeFromString<PageProbe>(it) }
    } catch (e: Exception) {
        null
    }

    private fun snapshot(probe: PageProbe): RenderResult.Loaded {
        val html = webEngine().executeScript("document.documentElement.outerHTML") as? String ?: ""
        return RenderResult.Loaded(url = probe.url, html = html, bodyText = probe.bodyText)
    }

    private fun webEngine(): WebEngine = engine ?: WebEngine().also { engine = it }

    /** Starts the JavaFX runtime once, without a window and without it ending on its own. */
    private fun startToolkit() {
        if (started) return
        synchronized(startLock) {
            if (started) return
            Platform.setImplicitExit(false)
            try {
                Platform.startup { }
            } catch (e: IllegalStateException) {
                // The runtime is running already (something else started it).
            }
            started = true
        }
    }

    /** Shuts the JavaFX runtime down (it cannot be started again in this JVM). Does nothing when it never started. */
    override fun close() {
        synchronized(startLock) {
            if (!started) return
            started = false
            Platform.exit()
        }
    }

    /** Runs [block] on the JavaFX thread and waits for its value. */
    private suspend fun <T> onFxThread(block: () -> T): T = suspendCancellableCoroutine { continuation ->
        Platform.runLater {
            try {
                continuation.resume(block())
            } catch (e: Throwable) {
                continuation.resumeWithException(e)
            }
        }
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
