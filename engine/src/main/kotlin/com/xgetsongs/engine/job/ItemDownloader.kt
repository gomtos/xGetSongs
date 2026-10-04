package com.xgetsongs.engine.job

import com.xgetsongs.engine.process.ProcessRunner
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

/** An item whose final file name is settled. */
data class PreparedItem(val item: ResolvedItem, val fileName: String)

sealed interface DownloadResult {
    /** The finished mp3, still inside the job's work directory. */
    data class Downloaded(val file: Path) : DownloadResult

    data class Failed(val failure: Failure) : DownloadResult
}

/** Downloads one video's audio with yt-dlp. Knows nothing about concurrency, retries or sinks. */
class ItemDownloader(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
    private val metadata: VideoMetadataSource,
) {
    /**
     * Settles the final file name. Items whose artist came from the channel name get a second chance:
     * the full metadata (yt-dlp `artist`/`track`) is fetched and the title is parsed again.
     */
    suspend fun prepare(item: ResolvedItem): PreparedItem {
        var artist = item.artist
        var track = item.track
        if (item.lowConfidence) {
            metadata.fetch(item.videoId)?.let { meta ->
                val parsed = TitleParser.parse(meta.title ?: item.title, meta.channel ?: item.channel, meta.artist, meta.track)
                artist = parsed.artist
                track = parsed.title
            }
        }
        return PreparedItem(item, FilenameFormatter.format(item.rank, artist, track))
    }

    /** Runs yt-dlp once. [emit] receives throttled [JobEvent.Progress] events. */
    suspend fun download(prepared: PreparedItem, workDir: Path, emit: (JobEvent) -> Unit): DownloadResult {
        val paths = tools.current()
        if (paths.ytDlp == null) {
            return DownloadResult.Failed(Failure(FailureKind.FATAL, "yt-dlp를 찾을 수 없습니다."))
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
