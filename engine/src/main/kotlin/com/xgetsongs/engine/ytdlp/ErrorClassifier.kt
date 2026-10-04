package com.xgetsongs.engine.ytdlp

enum class FailureKind {
    /** The video cannot be downloaded at all (private, removed, geo-blocked...). Skip it. */
    UNAVAILABLE,

    /** Worth retrying (network hiccup, rate limit). */
    TRANSIENT,

    /** Retrying or continuing is pointless (disk full, no permission). Abort the job. */
    FATAL,

    OTHER,
}

data class Failure(val kind: FailureKind, val message: String)

/** Maps yt-dlp's stderr to a [Failure]. */
object ErrorClassifier {
    private val FATAL = listOf(
        "no space left on device" to "디스크 공간이 부족합니다.",
        "not enough space on the disk" to "디스크 공간이 부족합니다.",
        "disk quota exceeded" to "디스크 공간이 부족합니다.",
        "permission denied" to "출력 폴더에 쓸 권한이 없습니다.",
    )

    /** A rate-limited session also says "Video unavailable", so this must be checked before [UNAVAILABLE]. */
    private val RATE_LIMITED = listOf("try again later", "rate-limited", "rate limited")

    private val UNAVAILABLE = listOf(
        "private video" to "비공개 영상",
        "sign in to confirm your age" to "연령 제한 영상",
        "age-restricted" to "연령 제한 영상",
        "available in your country" to "지역 제한 영상",
        "blocked it in your country" to "지역 제한 영상",
        "who has blocked it" to "지역 제한 영상",
        "members-only" to "멤버 전용 영상",
        "join this channel" to "멤버 전용 영상",
        "copyright" to "저작권으로 차단된 영상",
        "has been removed" to "삭제된 영상",
        "account associated with this video has been terminated" to "삭제된 영상",
        "video unavailable" to "사용할 수 없는 영상",
        "this video is not available" to "사용할 수 없는 영상",
    )

    private val TRANSIENT = listOf(
        "http error 429", "http error 403", "http error 500", "http error 502", "http error 503",
        "http error 504", "timed out", "connection reset", "connection aborted", "remote end closed",
        "incompleteread", "temporary failure in name resolution", "getaddrinfo failed",
        "network is unreachable", "unable to download",
    )

    fun classify(stderrLines: List<String>): Failure {
        val text = stderrLines.joinToString("\n").lowercase()
        FATAL.firstOrNull { text.contains(it.first) }?.let { return Failure(FailureKind.FATAL, it.second) }
        if (RATE_LIMITED.any { text.contains(it) }) return Failure(FailureKind.TRANSIENT, summarize(stderrLines))
        UNAVAILABLE.firstOrNull { text.contains(it.first) }?.let { return Failure(FailureKind.UNAVAILABLE, it.second) }
        val message = summarize(stderrLines)
        if (TRANSIENT.any { text.contains(it) }) return Failure(FailureKind.TRANSIENT, message)
        return Failure(FailureKind.OTHER, message)
    }

    private fun summarize(stderrLines: List<String>): String {
        val error = stderrLines.lastOrNull { it.trimStart().startsWith("ERROR:") }
            ?.trim()?.removePrefix("ERROR:")?.trim()
        val summary = error
            ?: stderrLines.lastOrNull { it.isNotBlank() }?.trim()
            ?: "yt-dlp가 비정상 종료했습니다."
        return summary.take(300)
    }
}
