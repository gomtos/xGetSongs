package com.xgetsongs.engine.lyrics.google

import com.xgetsongs.engine.lyrics.LyricsProvider
import com.xgetsongs.engine.lyrics.LyricsQuery
import kotlinx.coroutines.CancellationException

/**
 * Finds lyrics in the lyrics card of a Google search ("artist title lyrics"), read by a web view that is never shown
 * ([FxWebViewBrowser]). The last resort after the description and LRCLIB, and a fragile one: Google changes its pages,
 * and its terms of service all but forbid automated queries. This is a personal tool, so the lookup is polite (see
 * [GoogleLyricsFetcher]) and gives up for good at the first sign of a block. Lyrics are copyrighted works; a use beyond
 * personal use needs an official lyrics API (Musixmatch, say) instead of this.
 *
 * Never throws, except for a `CancellationException`; whatever goes wrong is "no lyrics". Close it when the application
 * ends: the web view's runtime would keep the JVM alive.
 */
class GoogleLyricsProvider internal constructor(
    private val fetcher: GoogleLyricsFetcher,
    private val onClose: () -> Unit = {},
) : LyricsProvider, AutoCloseable {
    /** The real thing; [log] gets one line per lookup (counts and result names, never a song or lyrics). */
    constructor(log: GoogleLyricsLog = GoogleLyricsLog.None) : this(FxWebViewBrowser(), log)

    private constructor(browser: FxWebViewBrowser, log: GoogleLyricsLog) : this(GoogleLyricsFetcher(browser, log = log), browser::close)

    override suspend fun find(query: LyricsQuery): String? {
        if (query.artist.isBlank() || query.title.isBlank()) return null
        return try {
            (fetcher.fetch(query.artist, query.title) as? GoogleLyricsResult.Found)?.lyrics
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            null
        }
    }

    override fun close() = onClose()
}
