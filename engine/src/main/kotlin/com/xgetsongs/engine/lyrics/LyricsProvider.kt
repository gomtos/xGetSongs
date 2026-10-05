package com.xgetsongs.engine.lyrics

/**
 * What a lyrics lookup is told about a song: the parsed [artist] and [title] (the ones the ID3 tags get), the [album]
 * the tag will carry (null when there is none) and the [durationSeconds] of the video (null when unknown). The service
 * uses the album and the length to tell versions of a song apart.
 */
data class LyricsQuery(
    val artist: String,
    val title: String,
    val album: String?,
    val durationSeconds: Int?,
)

/** Finds the lyrics of a song somewhere else than in the video description. */
interface LyricsProvider {
    /**
     * The lyrics of the song described by [query], cleaned up (see `LyricsExtractor.tidy`), or null when none were found
     * or the lookup did not work. Never throws, except for a `CancellationException` when the calling coroutine is
     * cancelled: a lookup that fails is a song without lyrics, not a failed download.
     */
    suspend fun find(query: LyricsQuery): String?
}

/** Looks nothing up. The engine's default: it never sends anything to anybody. */
object NoLyricsProvider : LyricsProvider {
    override suspend fun find(query: LyricsQuery): String? = null
}
