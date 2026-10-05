package com.xgetsongs.app.state

import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.filename.FilenameFormatter

/** The text shown in an item's status cell. */
fun statusLabel(status: ItemStatus): String = when (status) {
    ItemStatus.Ready -> "준비됨"
    ItemStatus.Waiting -> "대기 중"
    is ItemStatus.Downloading -> status.percent?.let { "다운로드 ${it.toInt()}%" } ?: "다운로드 중…"
    ItemStatus.Converting -> "mp3 변환 중…"
    ItemStatus.Done -> "완료"
    is ItemStatus.Skipped -> "건너뜀: ${status.reason}"
    is ItemStatus.Failed -> "실패: ${status.message}"
}

/** The one-line result shown after a job ends; null while there is nothing to report. */
fun summaryText(state: UiState): String? {
    val summary = state.summary ?: return null
    val counts = "성공 ${summary.succeeded} · 건너뜀 ${summary.skipped} · 실패 ${summary.failed}"
    return when (state.jobStatus) {
        JobStatus.COMPLETED -> "완료 — $counts"
        JobStatus.CANCELLED -> "취소됨 — $counts"
        JobStatus.FAILED -> "중단됨 — $counts"
        null -> null
    }
}

/**
 * Where the files will be saved: the output folder for a video, a folder named after the playlist inside it for a
 * playlist (the same name the server uses). Null until something is resolved and an output folder is typed.
 */
fun destinationPath(state: UiState): String? {
    val resolved = state.resolved ?: return null
    if (state.outputDir.isBlank()) return null
    return when (resolved.kind) {
        InputKind.VIDEO -> state.outputDir
        InputKind.PLAYLIST ->
            state.outputDir.trimEnd('\\', '/') + "\\" + FilenameFormatter.folderName(resolved.playlistTitle)
    }
}

/** The line shown above the options: [destinationPath] with its caption; null when there is no destination yet. */
fun destinationLabel(state: UiState): String? = destinationPath(state)?.let { "저장 위치: $it" }

/** The zero-padded rank shown in the list, e.g. `007`. */
fun rankLabel(rank: Int): String = rank.toString().padStart(3, '0')

/** What the rank text field may hold while the user types: digits only, at most three of them. */
fun rankInputText(raw: String): String = raw.filter(Char::isDigit).take(3)

/** The rank typed so far, kept within the valid range; null while the field is empty. */
fun rankFromInput(text: String): Int? =
    text.toIntOrNull()?.coerceIn(FilenameFormatter.MIN_RANK, FilenameFormatter.MAX_RANK)
