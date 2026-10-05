package com.xgetsongs.app.settings

import kotlinx.serialization.Serializable

/**
 * The options the app restores on the next start. The typed playlist or video address and the rank of a single video
 * are not part of it. [outputDir] is null (or blank) when the user never chose a folder: the app then uses its default.
 */
@Serializable
data class UserSettings(
    val outputDir: String? = null,
    val overwrite: Boolean = false,
    val includeRank: Boolean = true,
    val concurrency: Int = 2,
) {
    companion object {
        const val MIN_CONCURRENCY = 1
        const val MAX_CONCURRENCY = 4
    }
}
