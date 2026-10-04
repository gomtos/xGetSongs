package com.xgetsongs.engine.ytdlp

import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.Resolver
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.tools.ToolPaths
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.filename.FilenameFormatter
import com.xgetsongs.shared.input.ClassifyResult
import com.xgetsongs.shared.input.InputClassifier
import com.xgetsongs.shared.input.ParsedInput
import com.xgetsongs.shared.title.Confidence
import com.xgetsongs.shared.title.TitleParser
import kotlinx.serialization.SerializationException
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/** The metadata of one video that the title parser can use. */
data class VideoMeta(val title: String?, val channel: String?, val artist: String?, val track: String?)

fun interface VideoMetadataSource {
    /** Returns null when the metadata cannot be fetched; callers fall back to what they already know. */
    suspend fun fetch(videoId: String): VideoMeta?
}

@Serializable
internal data class RawEntry(
    val id: String? = null,
    val title: String? = null,
    val channel: String? = null,
    val uploader: String? = null,
    val availability: String? = null,
    val artist: String? = null,
    val track: String? = null,
)

@Serializable
internal data class RawPlaylist(
    val title: String? = null,
    val entries: List<RawEntry?> = emptyList(),
)

class YtDlpResolver(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
    private val json: Json = ApiJson.instance,
) : Resolver, VideoMetadataSource {

    override suspend fun resolve(input: String): ResolveResponse {
        val parsed = when (val result = InputClassifier.classify(input)) {
            is ClassifyResult.Ok -> result.input
            is ClassifyResult.Rejected -> throw ResolveException(result.reason.message)
        }
        val paths = requireYtDlp()
        return when (parsed) {
            is ParsedInput.Playlist -> resolvePlaylist(parsed, paths)
            is ParsedInput.Video -> resolveVideo(parsed, paths)
        }
    }

    override suspend fun fetch(videoId: String): VideoMeta? {
        val paths = tools.current().takeIf { it.ytDlp != null } ?: return null
        val url = ParsedInput.Video(videoId).canonicalUrl
        return try {
            val entry = json.decodeFromString(RawEntry.serializer(), runYtDlp(YtDlpCommands.resolveVideo(paths, url)))
            VideoMeta(entry.title, entry.channel ?: entry.uploader, entry.artist, entry.track)
        } catch (e: ResolveException) {
            null
        } catch (e: SerializationException) {
            null
        }
    }

    private suspend fun resolvePlaylist(input: ParsedInput.Playlist, paths: ToolPaths): ResolveResponse {
        val stdout = runYtDlp(YtDlpCommands.resolvePlaylist(paths, input.canonicalUrl))
        val playlist = parse(RawPlaylist.serializer(), stdout)
        val entries = playlist.entries
        val items = entries.take(FilenameFormatter.MAX_RANK).mapIndexed { index, entry -> toItem(index + 1, entry) }
        return ResolveResponse(
            kind = InputKind.PLAYLIST,
            playlistTitle = playlist.title,
            items = items,
            truncated = entries.size > FilenameFormatter.MAX_RANK,
            alsoVideoId = input.alsoVideoId,
        )
    }

    private suspend fun resolveVideo(input: ParsedInput.Video, paths: ToolPaths): ResolveResponse {
        val stdout = runYtDlp(YtDlpCommands.resolveVideo(paths, input.canonicalUrl))
        val entry = parse(RawEntry.serializer(), stdout)
        return ResolveResponse(
            kind = InputKind.VIDEO,
            playlistTitle = null,
            items = listOf(toItem(rank = 1, entry = entry)),
        )
    }

    private fun toItem(rank: Int, entry: RawEntry?): ResolvedItem {
        val id = entry?.id
        val title = entry?.title.orEmpty()
        val reason = unavailableReason(entry)
        if (id == null || reason != null) {
            return ResolvedItem(
                rank = rank,
                videoId = id.orEmpty(),
                title = title,
                available = false,
                unavailableReason = reason ?: "알 수 없는 항목",
            )
        }
        val channel = entry.channel ?: entry.uploader
        val parsed = TitleParser.parse(title, channel, entry.artist, entry.track)
        return ResolvedItem(
            rank = rank,
            videoId = id,
            title = title,
            channel = channel,
            artist = parsed.artist,
            track = parsed.title,
            lowConfidence = parsed.confidence == Confidence.LOW,
            expectedFileName = FilenameFormatter.format(rank, parsed.artist, parsed.title),
        )
    }

    private fun unavailableReason(entry: RawEntry?): String? {
        if (entry == null) return "알 수 없는 항목"
        return when {
            entry.title == "[Private video]" -> "비공개 영상"
            entry.title == "[Deleted video]" -> "삭제된 영상"
            entry.availability in UNAVAILABLE_AVAILABILITY -> "사용할 수 없는 영상 (${entry.availability})"
            else -> null
        }
    }

    private fun requireYtDlp(): ToolPaths =
        tools.current().takeIf { it.ytDlp != null }
            ?: throw ResolveException("yt-dlp를 찾을 수 없습니다. 도구 설치 후 다시 시도하세요.")

    private suspend fun runYtDlp(command: List<String>): String {
        val stdout = StringBuilder()
        val stderr = mutableListOf<String>()
        val exitCode = runner.run(
            command,
            onStdout = { synchronized(stdout) { stdout.append(it).append('\n') } },
            onStderr = { synchronized(stderr) { stderr += it } },
        )
        if (exitCode != 0) {
            val failure = synchronized(stderr) { ErrorClassifier.classify(stderr.toList()) }
            throw ResolveException(failure.message)
        }
        return stdout.toString()
    }

    private fun <T> parse(serializer: kotlinx.serialization.KSerializer<T>, text: String): T =
        try {
            json.decodeFromString(serializer, text)
        } catch (e: SerializationException) {
            throw ResolveException("yt-dlp 응답을 해석할 수 없습니다.")
        }

    private companion object {
        val UNAVAILABLE_AVAILABILITY = setOf("private", "needs_auth", "premium_only", "subscriber_only")
    }
}
