package com.xgetsongs.engine.job

import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tags.Id3Tagger
import com.xgetsongs.engine.tags.TrackTags
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.ytdlp.ErrorClassifier
import com.xgetsongs.engine.ytdlp.Failure
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.engine.ytdlp.ProgressParser
import com.xgetsongs.engine.ytdlp.ProgressUpdate
import com.xgetsongs.engine.ytdlp.VideoMetadataSource
import com.xgetsongs.engine.ytdlp.YtDlpCommands
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.filename.FilenameFormatter
import com.xgetsongs.shared.input.ParsedInput
import com.xgetsongs.shared.title.TitleParser
import java.nio.file.Files
import java.nio.file.Path

/**
 * An item whose final file name is settled. [artist] and [track] are the parsed originals the file name was made from
 * (the ID3 tags use them as they are); [album] is the playlist title, or null for a single video.
 */
data class PreparedItem(
    val item: ResolvedItem,
    val fileName: String,
    val artist: String,
    val track: String,
    val album: String?,
)

sealed interface DownloadResult {
    /** The finished mp3, still inside the job's work directory. */
    data class Downloaded(val file: Path) : DownloadResult

    data class Failed(val failure: Failure) : DownloadResult
}

/**
 * Downloads one video's audio with yt-dlp and writes its ID3 tags. Knows nothing about concurrency, retries or sinks.
 */
class ItemDownloader(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
    private val metadata: VideoMetadataSource,
) {
    private val tagger = Id3Tagger(runner, tools)

    /**
     * Settles the final file name. Items whose artist came from the channel name get a second chance:
     * the full metadata (yt-dlp `artist`/`track`) is fetched and the title is parsed again. [album] is the playlist
     * title for the ID3 tags, or null for a single video.
     */
    suspend fun prepare(item: ResolvedItem, album: String? = null): PreparedItem {
        var artist = item.artist
        var track = item.track
        if (item.lowConfidence) {
            metadata.fetch(item.videoId)?.let { meta ->
                val parsed = TitleParser.parse(meta.title ?: item.title, meta.channel ?: item.channel, meta.artist, meta.track)
                artist = parsed.artist
                track = parsed.title
            }
        }
        return PreparedItem(item, FilenameFormatter.format(item.rank, artist, track), artist, track, album)
    }

    /**
     * Runs yt-dlp once, then writes the ID3 tags and the cover (the thumbnail yt-dlp left next to the mp3) into the
     * mp3. [emit] receives throttled [JobEvent.Progress] events.
     */
    suspend fun download(prepared: PreparedItem, workDir: Path, emit: (JobEvent) -> Unit): DownloadResult {
        val paths = tools.current()
        if (paths.ytDlp == null) {
            return DownloadResult.Failed(Failure(FailureKind.FATAL, "yt-dlp를 찾을 수 없습니다."))
        }
        // The tags are written with ffmpeg after the download, so stop before spending a download that cannot be finished.
        if (paths.ffmpeg == null) {
            return DownloadResult.Failed(Failure(FailureKind.FATAL, "ffmpeg를 찾을 수 없습니다."))
        }
        val videoId = prepared.item.videoId
        val command = YtDlpCommands.download(paths, ParsedInput.Video(videoId).canonicalUrl, workDir, videoId)
        val rank = prepared.item.rank
        val stderr = mutableListOf<String>()
        val throttle = ProgressThrottle()

        val exitCode = runner.run(
            command,
            onStdout = { line ->
                ProgressParser.parse(line)?.let { update ->
                    throttle.accept(update)?.let { emit(JobEvent.Progress(rank, it.first, it.second)) }
                }
            },
            onStderr = { line -> synchronized(stderr) { stderr += line } },
        )

        if (exitCode != 0) {
            return DownloadResult.Failed(ErrorClassifier.classify(synchronized(stderr) { stderr.toList() }))
        }
        val file = workDir.resolve("$videoId.mp3")
        if (!Files.isRegularFile(file)) {
            return DownloadResult.Failed(Failure(FailureKind.OTHER, "변환된 mp3 파일을 찾을 수 없습니다."))
        }
        val cover = workDir.resolve("$videoId.jpg").takeIf { Files.isRegularFile(it) }
        val tags = TrackTags(
            title = prepared.track,
            artist = prepared.artist,
            album = prepared.album,
            albumArtist = prepared.artist,
            trackNumber = rank,
            comment = ParsedInput.Video(videoId).canonicalUrl,
        )
        tagger.tag(file, cover, tags)?.let { return DownloadResult.Failed(it) }
        return DownloadResult.Downloaded(file)
    }

    /** Lets a progress update through only when the stage changes or the whole percent advances. */
    private class ProgressThrottle {
        private var stage: Stage? = null
        private var lastPercent = -1

        fun accept(update: ProgressUpdate): Pair<Stage, Double?>? = when (update) {
            is ProgressUpdate.Converting -> {
                if (stage == Stage.CONVERTING) {
                    null
                } else {
                    stage = Stage.CONVERTING
                    Stage.CONVERTING to null
                }
            }
            is ProgressUpdate.Downloading -> {
                val whole = update.percent?.toInt() ?: -1
                if (stage == Stage.DOWNLOADING && whole == lastPercent) {
                    null
                } else {
                    stage = Stage.DOWNLOADING
                    lastPercent = whole
                    Stage.DOWNLOADING to update.percent
                }
            }
        }
    }
}
