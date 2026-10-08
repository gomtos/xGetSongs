package com.xgetsongs.engine.job

import com.xgetsongs.engine.lyrics.LyricsProvider
import com.xgetsongs.engine.lyrics.LyricsQuery
import com.xgetsongs.engine.lyrics.NoLyricsProvider
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tags.Id3Tagger
import com.xgetsongs.engine.tags.TrackTags
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.ytdlp.ErrorClassifier
import com.xgetsongs.engine.ytdlp.Failure
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.engine.ytdlp.ProgressParser
import com.xgetsongs.engine.ytdlp.ProgressUpdate
import com.xgetsongs.engine.ytdlp.VideoInfoFile
import com.xgetsongs.engine.ytdlp.VideoMetadataSource
import com.xgetsongs.engine.ytdlp.YtDlpCommands
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.LyricsOutcome
import com.xgetsongs.shared.api.ResolvedItem
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.filename.FilenameFormatter
import com.xgetsongs.shared.input.ParsedInput
import com.xgetsongs.shared.lyrics.LyricsExtractor
import com.xgetsongs.shared.title.TitleParser
import kotlinx.coroutines.CancellationException
import java.nio.file.Files
import java.nio.file.Path

/**
 * An item whose final file name is settled. [artist] and [track] are the parsed originals the file name was made from
 * (the ID3 tags use them as they are); [album] is only the fallback for the album tag, used when the video has no
 * album of its own: the playlist title, or null for a single video. [searchLyricsOnline] says whether the lyrics may be
 * looked up on the internet when the video description has none.
 */
data class PreparedItem(
    val item: ResolvedItem,
    val fileName: String,
    val artist: String,
    val track: String,
    val album: String?,
    val searchLyricsOnline: Boolean = false,
)

sealed interface DownloadResult {
    /**
     * The finished mp3, still inside the job's work directory. [lyrics] says what the file got as lyrics, or why it got
     * none: it describes what the tag step wrote, so it exists only for a file whose tags were written.
     */
    data class Downloaded(val file: Path, val lyrics: LyricsOutcome) : DownloadResult

    data class Failed(val failure: Failure) : DownloadResult
}

/**
 * Downloads one video's audio with yt-dlp and writes its ID3 tags. Knows nothing about concurrency, retries or sinks.
 * [lyrics] is asked for the lyrics of a song whose description has none (and only when the item allows it); the default
 * looks nothing up.
 */
class ItemDownloader(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
    private val metadata: VideoMetadataSource,
    private val lyrics: LyricsProvider = NoLyricsProvider,
) {
    private val tagger = Id3Tagger(runner, tools)

    /**
     * Settles the final file name. Items whose artist came from the channel name get a second chance:
     * the full metadata (yt-dlp `artist`/`track`) is fetched and the title is parsed again. [album] is the fallback
     * for the album tag (the playlist title, or null for a single video), used when the video has no album of its own.
     * [includeRank] puts the rank in front of the file name; the tags keep the rank as the track number either way.
     * [searchLyricsOnline] is carried to [download], which may then ask the lyrics provider for a song whose description
     * has no lyrics.
     */
    suspend fun prepare(
        item: ResolvedItem,
        album: String? = null,
        includeRank: Boolean = true,
        searchLyricsOnline: Boolean = false,
    ): PreparedItem {
        var artist = item.artist
        var track = item.track
        if (item.lowConfidence) {
            metadata.fetch(item.videoId)?.let { meta ->
                val parsed = TitleParser.parse(meta.title ?: item.title, meta.channel ?: item.channel, meta.artist, meta.track)
                artist = parsed.artist
                track = parsed.title
            }
        }
        return PreparedItem(item, FilenameFormatter.format(item.rank, artist, track, includeRank), artist, track, album, searchLyricsOnline)
    }

    /**
     * Runs yt-dlp once, then writes the ID3 tags and the cover (the thumbnail yt-dlp left next to the mp3) into the
     * mp3. The album tag is the video's own album from the info file yt-dlp left next to the mp3, else
     * [PreparedItem.album]; the lyrics tag is the lyrics section of the video description in the same file, if it has
     * one, else (when [PreparedItem.searchLyricsOnline] is set) what the lyrics provider finds for the artist, title, tag
     * album and length of the video, else nothing: with no lyrics from either source no lyrics frame is written. A
     * missing or broken info file means no own album, no description lyrics and no length, and never fails the item; a
     * lookup that fails is no lyrics. The [DownloadResult.Downloaded.lyrics] of the result tells which of these happened;
     * it is reported only once the tags are written, so a tag failure is a failure with no outcome. [emit] receives
     * throttled [JobEvent.Progress] events.
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
        val info = VideoInfoFile.read(workDir.resolve("$videoId.info.json"))
        val album = info.album ?: prepared.album
        val fromDescription = LyricsExtractor.extract(info.description)
        val found = if (fromDescription == null) lookUpLyrics(prepared, album, info.duration) else null
        val tags = TrackTags(
            title = prepared.track,
            artist = prepared.artist,
            album = album,
            albumArtist = prepared.artist,
            trackNumber = rank,
            comment = ParsedInput.Video(videoId).canonicalUrl,
            lyrics = fromDescription ?: found,
        )
        tagger.tag(file, cover, tags)?.let { return DownloadResult.Failed(it) }
        return DownloadResult.Downloaded(file, lyricsOutcome(fromDescription, found, prepared.searchLyricsOnline))
    }

    /**
     * What [Id3Tagger] wrote as lyrics: the description's, else the lookup's, else nothing, which is "off" when the
     * lookup was not allowed and "not found" when it ran (or failed) and gave nothing. Both texts are non-blank when
     * present, so a non-null one is a real lyrics frame.
     */
    private fun lyricsOutcome(fromDescription: String?, found: String?, lookupAllowed: Boolean): LyricsOutcome = when {
        fromDescription != null -> LyricsOutcome.DESCRIPTION
        found != null -> LyricsOutcome.ONLINE
        !lookupAllowed -> LyricsOutcome.SEARCH_OFF
        else -> LyricsOutcome.NOT_FOUND
    }

    /**
     * What the lyrics provider finds for [prepared]: null when the item does not allow a lookup, when nothing is found
     * and when the lookup fails (a song without lyrics is no reason to fail the item). Only a cancellation gets through.
     */
    private suspend fun lookUpLyrics(prepared: PreparedItem, album: String?, durationSeconds: Int?): String? {
        if (!prepared.searchLyricsOnline) return null
        return try {
            lyrics.find(LyricsQuery(artist = prepared.artist, title = prepared.track, album = album, durationSeconds = durationSeconds))
                ?.takeIf { it.isNotBlank() }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            null
        }
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
