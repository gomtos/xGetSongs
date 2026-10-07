package com.xgetsongs.server

import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions

internal enum class JobLogLevel { DEBUG, INFO, WARN }

internal class JobLogLine(val level: JobLogLevel, val text: String) {
    override fun toString() = "$level $text"
}

/**
 * The words the job log is written in, kept apart from the logging itself so they can be tested. A line holds ranks, video
 * ids, statuses, counts, options and the reason text of a skipped or failed item, and nothing that names a song: no title,
 * no file name, no lyrics. (The request's headers, and with them the token, never get here at all.)
 */
internal object JobLog {
    const val LOGGER_NAME = "com.xgetsongs.server.Jobs"

    /** Longest reason text kept in a line; a yt-dlp error can run to several screens. */
    const val MAX_REASON_LENGTH = 500

    const val CANCEL_REQUESTED = "취소 요청을 받음"

    /** The part of a job's ID that tags its lines: enough to tell the jobs of one run apart. */
    fun shortId(jobId: String): String = jobId.take(8)

    fun started(jobId: String, kind: InputKind, itemCount: Int, options: JobOptions): String {
        val kindText = if (kind == InputKind.PLAYLIST) "재생목록" else "영상"
        return "작업 시작: id=$jobId, 종류=$kindText, 항목=${itemCount}개, overwrite=${options.overwrite}, " +
            "includeRank=${options.includeRank}, concurrency=${options.concurrency}, " +
            "searchLyricsOnline=${options.searchLyricsOnline}, outputDir=${options.outputDir ?: "없음"}"
    }

    /** The line for [event], or null for the events that are not logged (progress comes many times a second). */
    fun describe(event: JobEvent): JobLogLine? = when (event) {
        is JobEvent.ItemStarted -> JobLogLine(JobLogLevel.DEBUG, "항목 시작: 순위 ${event.rank}, 영상 ${event.videoId}")
        is JobEvent.Progress -> null
        is JobEvent.ItemDone -> JobLogLine(JobLogLevel.INFO, "항목 완료: 순위 ${event.rank}")
        is JobEvent.ItemSkipped -> JobLogLine(JobLogLevel.INFO, "항목 건너뜀: 순위 ${event.rank}, 사유: ${oneLine(event.reason)}")
        is JobEvent.ItemFailed -> JobLogLine(JobLogLevel.WARN, "항목 실패: 순위 ${event.rank}, 사유: ${oneLine(event.message)}")
        is JobEvent.JobDone -> JobLogLine(
            JobLogLevel.INFO,
            "작업 종료: 상태=${event.status}, 성공 ${event.summary.succeeded}, 건너뜀 ${event.summary.skipped}, 실패 ${event.summary.failed}",
        )
    }

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
