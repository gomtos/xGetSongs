package com.xgetsongs.server

import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.log.LogRedaction

internal enum class JobLogLevel { DEBUG, INFO, WARN }

internal class JobLogLine(val level: JobLogLevel, val text: String) {
    override fun toString() = "$level $text"
}

/**
 * The words the job log is written in, kept apart from the logging itself so they can be tested. A line holds ranks, video
 * ids, statuses, counts, options, the lyrics outcome of a finished item (the name of the enum, such as `ONLINE`) and the
 * reason text of a skipped or failed item, and nothing that names a song: no title, no file name, no path, no lyrics.
 * (The request's headers, and with them the token, never get here at all.)
 */
internal object JobLog {
    const val LOGGER_NAME = "com.xgetsongs.server.Jobs"

    /** Longest reason text kept in a line; a yt-dlp error can run to several screens. */
    const val MAX_REASON_LENGTH = 500

    const val CANCEL_REQUESTED = "취소 요청을 받음"

    /** The part of a job's ID that tags its lines: enough to tell the jobs of one run apart. */
    fun shortId(jobId: String): String = jobId.take(8)

    /** The names the user typed are free text like the titles, so the line only says that there were some. */
    fun started(jobId: String, kind: InputKind, itemCount: Int, options: JobOptions): String {
        val kindText = if (kind == InputKind.PLAYLIST) "재생목록" else "영상"
        val typed = buildString {
            if (!options.albumName.isNullOrBlank()) append(", albumName=지정")
            if (!options.folderName.isNullOrBlank()) append(", folderName=지정")
        }
        return "작업 시작: id=$jobId, 종류=$kindText, 항목=${itemCount}개, overwrite=${options.overwrite}, " +
            "includeRank=${options.includeRank}, concurrency=${options.concurrency}, " +
            "searchLyricsOnline=${options.searchLyricsOnline}, outputDir=${options.outputDir ?: "없음"}$typed"
    }

    /** The line for the event stream of a job that ended without the job being done: the reader went away or the server is going down. */
    fun eventsDisconnected(jobId: String): String = "이벤트 연결이 끊어짐 (작업 ${shortId(jobId)})"

    /**
     * [text] without Windows paths and without the [knownNames] (the file name an item was started with, say): see
     * [LogRedaction]. The messages of the engine come from the file system and yt-dlp and are full of both.
     */
    fun redact(text: String, knownNames: Collection<String>): String = LogRedaction.redact(text, knownNames)

    /**
     * The line for [event], or null for the events that are not logged (progress comes many times a second). [fileNames]
     * holds the file name each rank was started with: the reason of a skipped or failed item is stripped of it.
     */
    fun describe(event: JobEvent, fileNames: Map<Int, String> = emptyMap()): JobLogLine? = when (event) {
        is JobEvent.ItemStarted -> JobLogLine(JobLogLevel.DEBUG, "항목 시작: 순위 ${event.rank}, 영상 ${event.videoId}")
        is JobEvent.Progress -> null
        is JobEvent.ItemDone -> JobLogLine(JobLogLevel.INFO, finished(event))
        is JobEvent.ItemSkipped -> JobLogLine(JobLogLevel.INFO, "항목 건너뜀: 순위 ${event.rank}, 사유: ${reason(event.reason, event.rank, fileNames)}")
        is JobEvent.ItemFailed -> JobLogLine(JobLogLevel.WARN, "항목 실패: 순위 ${event.rank}, 사유: ${reason(event.message, event.rank, fileNames)}")
        is JobEvent.JobDone -> JobLogLine(
            JobLogLevel.INFO,
            "작업 종료: 상태=${event.status}, 성공 ${event.summary.succeeded}, 건너뜀 ${event.summary.skipped}, 실패 ${event.summary.failed}",
        )
    }

    /** The rank, and the name of the lyrics outcome when the event has one (`ONLINE`, not the lyrics and not the file name). */
    private fun finished(event: JobEvent.ItemDone): String =
        "항목 완료: 순위 ${event.rank}" + (event.lyrics?.let { ", 가사 ${it.name}" } ?: "")

    // Redacted before it is cut, so no part of a path survives the cut; and before the line breaks go, which end a path.
    private fun reason(text: String, rank: Int, fileNames: Map<Int, String>): String =
        oneLine(redact(text, listOfNotNull(fileNames[rank])))

    /** One line of at most [MAX_REASON_LENGTH] characters (and a `…` when it was cut): line breaks would split a log record. */
    private fun oneLine(text: String): String {
        val flat = text.map { if (it.isISOControl() || it.isWhitespace()) ' ' else it }.joinToString("")
            .replace(Regex(" {2,}"), " ")
            .trim()
        if (flat.length <= MAX_REASON_LENGTH) return flat
        // Never end on the first half of a surrogate pair.
        val end = if (flat[MAX_REASON_LENGTH - 1].isHighSurrogate()) MAX_REASON_LENGTH - 1 else MAX_REASON_LENGTH
        return flat.take(end) + "…"
    }
}
