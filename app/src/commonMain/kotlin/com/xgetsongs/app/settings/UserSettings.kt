package com.xgetsongs.app.settings

import kotlinx.serialization.Serializable

/**
 * The options the app restores on the next start. The typed playlist or video address and the rank of a single video
 * are not part of it. [outputDir] is null (or blank) when the user never chose a folder: the app then uses its default.
 * [searchLyricsOnline] is on for a settings file written before the option existed. A `concurrency` in an older settings
 * file is ignored: the server decides how many items are downloaded at once.
 */
@Serializable
data class UserSettings(
    val outputDir: String? = null,
    val overwrite: Boolean = false,
    val includeRank: Boolean = true,
    val searchLyricsOnline: Boolean = true,
)
