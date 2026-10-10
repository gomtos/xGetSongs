package com.xgetsongs.app.state

import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.LyricsOutcome
import com.xgetsongs.shared.filename.FilenameFormatter
import kotlin.time.Duration

/** The label of the button that updates yt-dlp; [FORMAT_FAILURE_HELP] points to it by this name. */
const val UPDATE_YT_DLP_LABEL = "yt-dlp 업데이트"

/**
 * The help behind the `?` button of the tools panel. "m4a 오디오 형식이 없습니다." (the engine's message) is also what an
 * outdated yt-dlp or a broken JavaScript runtime makes every song fail with, so the user is told to check those first.
 */
const val FORMAT_FAILURE_HELP =
    "재생목록의 모든 곡이 “m4a 오디오 형식이 없습니다.”로 실패하면 영상 문제가 아니라 yt-dlp가 낡았거나 " +
        "JavaScript 런타임(Node.js, Deno)에 문제가 있는 것일 수 있습니다. 앱 위쪽의 $UPDATE_YT_DLP_LABEL 버튼으로 " +
        "최신 버전으로 바꾸고 Node.js 22+ 또는 Deno 2.3+가 설치돼 있는지 확인한 뒤 다시 받으세요."

/** The text shown in an item's status cell. */
fun statusLabel(status: ItemStatus): String = when (status) {
    ItemStatus.Ready -> "준비됨"
    ItemStatus.Waiting -> "대기 중"
    is ItemStatus.Downloading -> status.percent?.let { "다운로드 ${it.toInt()}%" } ?: "다운로드 중…"
    ItemStatus.Finishing -> "마무리 중…"
    is ItemStatus.Done -> doneLabel(status.lyrics)
    is ItemStatus.Skipped -> "건너뜀: ${status.reason}"
    is ItemStatus.Failed -> "실패: ${status.message}"
}

/**
 * A finished row says where its lyrics came from or why it has none (all of these fit one line of the 220.dp status cell:
 * the longest is 17 characters, at most 17 * 12.sp wide). Plain `완료` when the server did not report an outcome.
 */
private fun doneLabel(lyrics: LyricsOutcome?): String = when (lyrics) {
    null -> "완료"
    LyricsOutcome.DESCRIPTION -> "완료 · 가사 ✓ 설명란"
    LyricsOutcome.ONLINE -> "완료 · 가사 ✓ 인터넷"
    LyricsOutcome.NOT_FOUND -> "완료 · 가사 없음"
    LyricsOutcome.SEARCH_OFF -> "완료 · 가사 없음 (검색 끔)"
}

/** The one-line result shown after a job ends, with how long it took when that is known; null while there is nothing to report. */
fun summaryText(state: UiState): String? {
    val summary = state.summary ?: return null
    val took = state.elapsed?.let { " · 소요 ${elapsedLabel(it)}" }.orEmpty()
    val counts = "성공 ${summary.succeeded} · 건너뜀 ${summary.skipped} · 실패 ${summary.failed}$took"
    return when (state.jobStatus) {
        JobStatus.COMPLETED -> "완료 — $counts"
        JobStatus.CANCELLED -> "취소됨 — $counts"
        JobStatus.FAILED -> "중단됨 — $counts"
        null -> null
    }
}

/** [elapsed] in whole seconds as `42초`, `3분 05초` or `1시간 02분 05초`; never negative. */
fun elapsedLabel(elapsed: Duration): String {
    val total = elapsed.inWholeSeconds.coerceAtLeast(0)
    val hours = total / 3600
    val minutes = total % 3600 / 60
    val seconds = total % 60
    fun two(value: Long) = value.toString().padStart(2, '0')
    return when {
        hours > 0 -> "${hours}시간 ${two(minutes)}분 ${two(seconds)}초"
        minutes > 0 -> "${minutes}분 ${two(seconds)}초"
        else -> "${seconds}초"
    }
}

/**
 * Where the files will be saved: a folder named after the album name the user typed inside the output folder, else a
 * folder named after the playlist for a playlist, else the output folder itself for a video (the same names the server
 * uses). Null until something is resolved and an output folder is typed.
 */
fun destinationPath(state: UiState): String? {
    val resolved = state.resolved ?: return null
    if (state.outputDir.isBlank()) return null
    val folder = FilenameFormatter.destinationFolder(resolved.kind == InputKind.PLAYLIST, resolved.playlistTitle, state.albumName)
        ?: return state.outputDir
    return state.outputDir.trimEnd('\\', '/') + "\\" + folder
}

/**
 * What the album name field shows: the text the user typed, else the title of the playlist, which the files get as
 * their album when the field is left alone (a single video keeps the album of its own then), else nothing. The folder
 * is named after the same text (see [destinationPath]).
 */
fun albumNameText(state: UiState): String = state.albumName ?: state.resolved
    ?.takeIf { it.kind == InputKind.PLAYLIST }
    ?.playlistTitle
    .orEmpty()

/** The line shown above the options: [destinationPath] with its caption; null when there is no destination yet. */
fun destinationLabel(state: UiState): String? = destinationPath(state)?.let { "저장 위치: $it" }

/** The zero-padded rank shown in the list, e.g. `007`. */
fun rankLabel(rank: Int): String = rank.toString().padStart(3, '0')

/** What the rank text field may hold while the user types: digits only, at most three of them. */
fun rankInputText(raw: String): String = raw.filter(Char::isDigit).take(3)

/** The rank typed so far, kept within the valid range; null while the field is empty. */
fun rankFromInput(text: String): Int? =
    text.toIntOrNull()?.coerceIn(FilenameFormatter.MIN_RANK, FilenameFormatter.MAX_RANK)
