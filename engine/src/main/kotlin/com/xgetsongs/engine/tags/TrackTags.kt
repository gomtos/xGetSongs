package com.xgetsongs.engine.tags

/**
 * The tag values of one track, as they are written into the m4a. They are the original text: unlike the file name
 * nothing is sanitized or shortened.
 */
data class TrackTags(
    /** The title (`©nam`). */
    val title: String,
    /** The artist (`©ART`). */
    val artist: String,
    /** The album (`©alb`): the name of the folder (the user's album name, else the playlist title), else the video's own album; null (no tag) if none. */
    val album: String?,
    /** The album artist (`aART`). */
    val albumArtist: String,
    /** The track number (`trkn`): the playlist position, written without leading zeros. */
    val trackNumber: Int,
    /** The comment (`©cmt`): the video URL. A null or blank comment writes no tag. */
    val comment: String?,
    /** The lyrics (`©lyr`) found in the video description or by the lookup, lines separated by `\n`. A null or blank text writes no tag. */
    val lyrics: String? = null,
)
