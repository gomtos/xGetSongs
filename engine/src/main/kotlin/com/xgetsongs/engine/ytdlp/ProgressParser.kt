package com.xgetsongs.engine.ytdlp

sealed interface ProgressUpdate {
    /** [percent] is null when yt-dlp does not know the total size yet. */
    data class Downloading(val percent: Double?) : ProgressUpdate

    data object Converting : ProgressUpdate
}

/** Parses the lines produced by the `--progress-template`s in [YtDlpCommands.download]. */
object ProgressParser {
    fun parse(line: String): ProgressUpdate? {
        val text = line.trim()
        return when {
            text.startsWith("${YtDlpCommands.PROGRESS_PREFIX}|") -> parseDownload(text)
            text.startsWith("${YtDlpCommands.POSTPROCESS_PREFIX}|") -> parsePostprocess(text)
            else -> null
        }
    }

    private fun parseDownload(text: String): ProgressUpdate? {
        val parts = text.split('|')
        val status = parts.getOrNull(1) ?: return null
        return when (status) {
            "finished" -> ProgressUpdate.Downloading(100.0)
            "downloading" -> {
                val downloaded = parts.getOrNull(2)?.toDoubleOrNull()
                val total = parts.getOrNull(3)?.toDoubleOrNull() ?: parts.getOrNull(4)?.toDoubleOrNull()
                val percent = if (downloaded != null && total != null && total > 0) {
                    // Multiply first: 29 / 100 * 100 is 28.999999999999996 in floating point.
                    (downloaded * 100.0 / total).coerceIn(0.0, 100.0)
                } else {
                    null
                }
                ProgressUpdate.Downloading(percent)
            }
            else -> null
        }
    }

    private fun parsePostprocess(text: String): ProgressUpdate? {
        val status = text.split('|').getOrNull(1)
        return if (status == "started") ProgressUpdate.Converting else null
    }
}
