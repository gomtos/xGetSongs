package com.xgetsongs.shared.input

/** A user input that has been validated and reduced to YouTube IDs. */
sealed interface ParsedInput {
    /** Canonical URL rebuilt from the ID only. The raw user text is never passed on. */
    val canonicalUrl: String

    data class Playlist(val id: String, val alsoVideoId: String? = null) : ParsedInput {
        override val canonicalUrl: String get() = "https://www.youtube.com/playlist?list=$id"
    }

    data class Video(val id: String) : ParsedInput {
        override val canonicalUrl: String get() = "https://www.youtube.com/watch?v=$id"
    }
}

enum class RejectReason(val message: String) {
    EMPTY("재생목록 ID 또는 영상 주소를 입력하세요."),
    UNSUPPORTED_HOST("YouTube 주소만 사용할 수 있습니다."),
    MIX_PLAYLIST("자동 생성 믹스 목록은 지원하지 않습니다."),
    UNRECOGNIZED("재생목록 ID 또는 영상 주소로 인식할 수 없습니다."),
}

sealed interface ClassifyResult {
    data class Ok(val input: ParsedInput) : ClassifyResult
    data class Rejected(val reason: RejectReason) : ClassifyResult
}

object InputClassifier {
    private val PLAYLIST_PREFIXES = listOf("PL", "UU", "LL", "FL", "OL")
    private const val MIX_PREFIX = "RD"
    private const val MIN_PLAYLIST_ID_LENGTH = 12
    private const val VIDEO_ID_LENGTH = 11
    private val ALLOWED_HOSTS = setOf(
        "youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com", "youtu.be",
    )
    private val VIDEO_PATH_PREFIXES = listOf("shorts", "embed", "live")

    fun classify(raw: String): ClassifyResult {
        val text = raw.trim()
        if (text.isEmpty()) return ClassifyResult.Rejected(RejectReason.EMPTY)
        return when {
            isIdChars(text) -> classifyBareId(text)
            !text.contains('.') && !text.contains('/') -> ClassifyResult.Rejected(RejectReason.UNRECOGNIZED)
            else -> classifyUrl(text)
        }
    }

    private fun isIdChars(s: String): Boolean =
        s.isNotEmpty() && s.all { (it.isLetterOrDigit() && it.code < 128) || it == '_' || it == '-' }

    private fun isPlaylistId(s: String): Boolean =
        s.length >= MIN_PLAYLIST_ID_LENGTH && isIdChars(s) && PLAYLIST_PREFIXES.any { s.startsWith(it) }

    private fun isMixId(s: String): Boolean =
        s.length >= MIN_PLAYLIST_ID_LENGTH && isIdChars(s) && s.startsWith(MIX_PREFIX)

    private fun isVideoId(s: String): Boolean = s.length == VIDEO_ID_LENGTH && isIdChars(s)

    private fun classifyBareId(text: String): ClassifyResult = when {
        isPlaylistId(text) -> ok(ParsedInput.Playlist(text))
        isMixId(text) -> ClassifyResult.Rejected(RejectReason.MIX_PLAYLIST)
        isVideoId(text) -> ok(ParsedInput.Video(text))
        else -> ClassifyResult.Rejected(RejectReason.UNRECOGNIZED)
    }

    private fun classifyUrl(text: String): ClassifyResult {
        val afterScheme = text.substringAfter("://", missingDelimiterValue = text)
        val hostEnd = afterScheme.indexOfFirst { it == '/' || it == '?' || it == '#' }
        val host = (if (hostEnd < 0) afterScheme else afterScheme.substring(0, hostEnd)).lowercase()
        if (host !in ALLOWED_HOSTS) return ClassifyResult.Rejected(RejectReason.UNSUPPORTED_HOST)

        val rest = if (hostEnd < 0) "" else afterScheme.substring(hostEnd)
        val beforeFragment = rest.substringBefore('#')
        val path = beforeFragment.substringBefore('?')
        val query = if (beforeFragment.contains('?')) beforeFragment.substringAfter('?') else ""
        val params = query.split('&').filter { it.contains('=') }
            .associate { it.substringBefore('=') to it.substringAfter('=') }

        val segments = path.split('/').filter { it.isNotEmpty() }
        val videoCandidate = when {
            host == "youtu.be" -> segments.firstOrNull()
            segments.firstOrNull() in VIDEO_PATH_PREFIXES -> segments.getOrNull(1)
            else -> params["v"]
        }
        val videoId = videoCandidate?.takeIf { isVideoId(it) }
        val listId = params["list"]

        return when {
            listId != null && isPlaylistId(listId) -> ok(ParsedInput.Playlist(listId, alsoVideoId = videoId))
            listId != null && isMixId(listId) ->
                if (videoId != null) ok(ParsedInput.Video(videoId))
                else ClassifyResult.Rejected(RejectReason.MIX_PLAYLIST)
            videoId != null -> ok(ParsedInput.Video(videoId))
            else -> ClassifyResult.Rejected(RejectReason.UNRECOGNIZED)
        }
    }

    private fun ok(input: ParsedInput): ClassifyResult = ClassifyResult.Ok(input)
}
