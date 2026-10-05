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
    /** TALB: the video's own album, else the playlist title; null (no frame) for a single video without an album. */
    val album: String?,
    /** TPE2. */
    val albumArtist: String,
    /** TRCK: the playlist position, written without leading zeros. */
    val trackNumber: Int,
    /**
     * COMM: the video URL. Written as a real `COMM` frame by [Id3Frames] after ffmpeg is done (ffmpeg itself can only
     * write a comment as a `TXXX` frame); a null or blank comment writes no frame.
     */
    val comment: String?,
    /**
     * USLT: the lyrics found in the video description, lines separated by `\n`. Written as a real `USLT` frame by
     * [Id3Frames] after ffmpeg is done (ffmpeg would make a `TXXX` frame of it); a null or blank text writes no frame.
     */
    val lyrics: String? = null,
)
