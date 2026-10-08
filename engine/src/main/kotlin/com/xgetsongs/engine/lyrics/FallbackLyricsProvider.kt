package com.xgetsongs.engine.lyrics

/** Asks its [providers] one after the other and answers with the first lyrics found; null when none has any. */
class FallbackLyricsProvider(private vararg val providers: LyricsProvider) : LyricsProvider {
    override suspend fun find(query: LyricsQuery): String? {
        for (provider in providers) {
            provider.find(query)?.let { return it }
        }
        return null
    }
}
