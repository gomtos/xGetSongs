package com.xgetsongs.engine.tags

/**
 * The ID3 values of one track, as they are written into the mp3. They are the original text: unlike the file name
 * nothing is sanitized or shortened.
 */
data class TrackTags(
    /** TIT2. */
    val title: String,
    /** TPE1. */
    val artist: String,
    /** TALB: the playlist title, or null for a single video. */
    val album: String?,
    /** TPE2. */
    val albumArtist: String,
    /** TRCK: the playlist position, written without leading zeros. */
    val trackNumber: Int,
    /** COMM: the video URL. */
    val comment: String?,
)
