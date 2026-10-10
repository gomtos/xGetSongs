package com.xgetsongs.engine.job

import com.xgetsongs.engine.lyrics.LyricsProvider
import com.xgetsongs.engine.lyrics.LyricsQuery
import com.xgetsongs.engine.lyrics.NoLyricsProvider
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tags.M4aTagger
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
 * (the tags use them as they are); [album] is the album tag the files get unless [albumOverride] is set: the playlist
 * title, or null for a single video, which then gets the album of its own video if it has one. [searchLyricsOnline] says
 * whether the lyrics may be looked up on the internet when the video description has none. [albumOverride] is the album
 * name the user chose, if any: the album tag gets it instead of [album].
 */
data class PreparedItem(
    val item: ResolvedItem,
    val fileName: String,
    val artist: String,
    val track: String,
    val album: String?,
    val searchLyricsOnline: Boolean = false,
    val albumOverride: String? = null,
)

/** The album artist of every file: one value for all tracks, so a player groups a playlist into one album. */
private const val ALBUM_ARTIST = "Various Artists"

sealed interface DownloadResult {
    /**
     * The finished m4a, still inside the job's work directory. [lyrics] says what the file got as lyrics, or why it got
     * none: it describes what the tag step wrote, so it exists only for a file whose tags were written.
     */
    data class Downloaded(val file: Path, val lyrics: LyricsOutcome) : DownloadResult

    data class Failed(val failure: Failure) : DownloadResult
}

/**
 * Downloads one video's audio with yt-dlp and writes its tags. Knows nothing about concurrency, retries or sinks.
 * [lyrics] is asked for the lyrics of a song whose description has none (and only when the item allows it); the default
 * looks nothing up.
 */
class ItemDownloader(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
    private val metadata: VideoMetadataSource,
    private val lyrics: LyricsProvider = NoLyricsProvider,
) {
    private val tagger = M4aTagger(runner, tools)

    /**
     * Settles the final file name. Items whose artist came from the channel name get a second chance:
     * the full metadata (yt-dlp `artist`/`track`) is fetched and the title is parsed again. [album] is the fallback
     * for the album tag (the playlist title, or null for a single video), used when the video has no album of its own.
     * [includeRank] puts the rank in front of the file name; the tags keep the rank as the track number either way.
     * [searchLyricsOnline] is carried to [download], which may then ask the lyrics provider for a song whose description
     * has no lyrics. [albumOverride] is carried to [download] as well, where it replaces the album of the tag.
     */
    suspend fun prepare(
        item: ResolvedItem,
        album: String? = null,
        includeRank: Boolean = true,
        searchLyricsOnline: Boolean = false,
        albumOverride: String? = null,
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
        val fileName = FilenameFormatter.format(item.rank, artist, track, includeRank)
        return PreparedItem(item, fileName, artist, track, album, searchLyricsOnline, albumOverride?.takeIf { it.isNotBlank() })
    }

    /**
     * Runs yt-dlp once, then writes the tags and the cover (the thumbnail yt-dlp left next to the m4a) into the
     * m4a. The album tag is [PreparedItem.albumOverride], else [PreparedItem.album], else the video's own album from the
     * info file yt-dlp left next to the m4a; the lyrics tag is the lyrics section of the video description in the same
     * file, if it has one, else (when [PreparedItem.searchLyricsOnline] is set) what the lyrics provider finds for the
     * artist, title, the video's own album (else [PreparedItem.album]) and length of the video, else nothing: with no
     * lyrics from either source no lyrics tag is written. A
     * missing or broken info file means no own album, no description lyrics and no length, and never fails the item; a
     * lookup that fails is no lyrics. The [DownloadResult.Downloaded.lyrics] of the result tells which of these happened;
     * it is reported only once the tags are written, so a tag failure is a failure with no outcome. [emit] receives
     * the throttled [JobEvent.Progress] events of the download and then, once yt-dlp has finished successfully and
     * before the tags are written, one [JobEvent.Progress] event with [Stage.FINISHING].
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
                    if (throttle.accept(update)) emit(JobEvent.Progress(rank, Stage.DOWNLOADING, update.percent))
                }
            },
            onStderr = { line -> synchronized(stderr) { stderr += line } },
        )

        if (exitCode != 0) {
            return DownloadResult.Failed(ErrorClassifier.classify(synchronized(stderr) { stderr.toList() }))
        }
        // From here on the work is ours (tags, cover, lyrics), so we say so ourselves instead of reading yt-dlp's output.
        emit(JobEvent.Progress(rank, Stage.FINISHING, null))
        val file = workDir.resolve("$videoId.m4a")
        if (!Files.isRegularFile(file)) {
            return DownloadResult.Failed(Failure(FailureKind.OTHER, "받은 m4a 파일을 찾을 수 없습니다."))
        }
        val cover = workDir.resolve("$videoId.jpg").takeIf { Files.isRegularFile(it) }
        val info = VideoInfoFile.read(workDir.resolve("$videoId.info.json"))
        // The lookup goes by the album the video really has: a name the user made up, or a playlist title, would only
        // hurt the match.
        val ownAlbum = info.album ?: prepared.album
        val fromDescription = LyricsExtractor.extract(info.description)
        val found = if (fromDescription == null) lookUpLyrics(prepared, ownAlbum, info.duration) else null
        // The tag follows the folder: the album name the user chose, else the playlist title; the video's own album only
        // when there is neither (a single video).
        val folderAlbum = prepared.albumOverride ?: prepared.album?.takeIf { it.isNotBlank() }
        val tags = TrackTags(
            title = prepared.track,
            artist = prepared.artist,
            album = folderAlbum ?: info.album,
            albumArtist = ALBUM_ARTIST,
            trackNumber = rank,
            comment = ParsedInput.Video(videoId).canonicalUrl,
            lyrics = fromDescription ?: found,
        )
        tagger.tag(file, cover, tags)?.let { return DownloadResult.Failed(it) }
        return DownloadResult.Downloaded(file, lyricsOutcome(fromDescription, found, prepared.searchLyricsOnline))
    }

    /**
     * What [M4aTagger] wrote as lyrics: the description's, else the lookup's, else nothing, which is "off" when the
     * lookup was not allowed and "not found" when it ran (or failed) and gave nothing. Both texts are non-blank when
     * present, so a non-null one is a real lyrics tag.
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

    /** Lets a download update through only when it is the first one or the whole percent advances. */
    private class ProgressThrottle {
        private var reported = false
        private var lastPercent = -1

        fun accept(update: ProgressUpdate.Downloading): Boolean {
            val whole = update.percent?.toInt() ?: -1
            if (reported && whole == lastPercent) return false
            reported = true
            lastPercent = whole
            return true
        }
    }
}
