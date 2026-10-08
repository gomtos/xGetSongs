package com.xgetsongs.app.state

import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.LyricsOutcome
import com.xgetsongs.shared.api.ResolveResponse
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class LabelsTest {
    @Test
    fun statusLabels() {
        assertEquals("준비됨", statusLabel(ItemStatus.Ready))
        assertEquals("대기 중", statusLabel(ItemStatus.Waiting))
        assertEquals("다운로드 중…", statusLabel(ItemStatus.Downloading(null)))
        assertEquals("다운로드 42%", statusLabel(ItemStatus.Downloading(42.9)))
        assertEquals("mp3 변환 중…", statusLabel(ItemStatus.Converting))
        assertEquals("완료", statusLabel(ItemStatus.Done()))
        assertEquals("건너뜀: 비공개 영상", statusLabel(ItemStatus.Skipped("비공개 영상")))
        assertEquals("실패: boom", statusLabel(ItemStatus.Failed("boom")))
    }

    @Test
    fun aFinishedRowSaysWhereItsLyricsCameFromOrWhyItHasNone() {
        assertEquals("완료", statusLabel(ItemStatus.Done(null)))
        assertEquals("완료 · 가사 ✓ 설명란", statusLabel(ItemStatus.Done(LyricsOutcome.DESCRIPTION)))
        assertEquals("완료 · 가사 ✓ 인터넷", statusLabel(ItemStatus.Done(LyricsOutcome.ONLINE)))
        assertEquals("완료 · 가사 없음", statusLabel(ItemStatus.Done(LyricsOutcome.NOT_FOUND)))
        assertEquals("완료 · 가사 없음 (검색 끔)", statusLabel(ItemStatus.Done(LyricsOutcome.SEARCH_OFF)))
    }

    @Test
    fun aFinishedRowWithoutAKnownOutcomeIsTheSameAsTheDefault() {
        assertEquals(ItemStatus.Done(null), ItemStatus.Done())
        assertEquals(statusLabel(ItemStatus.Done()), statusLabel(ItemStatus.Done(null)))
    }

    @Test
    fun everyLyricsOutcomeHasItsOwnLabelStartingWithDone() {
        val labels = LyricsOutcome.entries.map { statusLabel(ItemStatus.Done(it)) }

        assertEquals(labels.size, labels.toSet().size, "no two outcomes may look alike: $labels")
        assertTrue(labels.all { it.startsWith("완료 · 가사 ") }, labels.toString())
    }

    @Test
    fun theLongestFinishedLabelStillFitsOnOneLineOfTheStatusCell() {
        // The status cell is 220.dp wide and its text is 12.sp (bodySmall). No character is wider than one em, so a label
        // of at most 17 characters needs at most 17 * 12 = 204.dp: it never wraps, whatever the font.
        val longest = (LyricsOutcome.entries.map { ItemStatus.Done(it) } + ItemStatus.Done(null)).maxOf { statusLabel(it).length }

        assertTrue(longest * 12 <= 220, "the longest finished label has $longest characters")
    }

    @Test
    fun summaryTextByJobStatus() {
        val counts = JobSummary(2, 1, 3)
        assertEquals("완료 — 성공 2 · 건너뜀 1 · 실패 3", summaryText(UiState(jobStatus = JobStatus.COMPLETED, summary = counts)))
        assertEquals("취소됨 — 성공 2 · 건너뜀 1 · 실패 3", summaryText(UiState(jobStatus = JobStatus.CANCELLED, summary = counts)))
        assertEquals("중단됨 — 성공 2 · 건너뜀 1 · 실패 3", summaryText(UiState(jobStatus = JobStatus.FAILED, summary = counts)))
    }

    @Test
    fun noSummaryUntilAJobEnds() {
        assertNull(summaryText(UiState()))
        assertNull(summaryText(UiState(summary = JobSummary(1, 0, 0), jobStatus = null)))
    }

    private fun resolvedState(
        kind: InputKind,
        playlistTitle: String? = null,
        outputDir: String = "D:\\Music",
    ) = UiState(
        outputDir = outputDir,
        resolved = ResolveResponse(resolveId = "r1", kind = kind, playlistTitle = playlistTitle, items = emptyList()),
    )

    @Test
    fun destinationIsNullBeforeAnythingIsResolved() {
        assertNull(destinationLabel(UiState(outputDir = "D:\\Music")))
    }

    @Test
    fun destinationIsNullWhileTheOutputFolderIsBlank() {
        assertNull(destinationLabel(resolvedState(InputKind.PLAYLIST, "Sample", outputDir = "")))
        assertNull(destinationLabel(resolvedState(InputKind.VIDEO, outputDir = "   ")))
    }

    @Test
    fun aVideoIsSavedInTheOutputFolderItself() {
        assertEquals("저장 위치: D:\\Music", destinationLabel(resolvedState(InputKind.VIDEO)))
        // a title on a video is ignored: only the kind decides whether there is a sub-folder
        assertEquals("저장 위치: D:\\Music", destinationLabel(resolvedState(InputKind.VIDEO, "Sample")))
    }

    @Test
    fun aPlaylistIsSavedInAFolderNamedAfterIt() {
        assertEquals("저장 위치: D:\\Music\\Sample", destinationLabel(resolvedState(InputKind.PLAYLIST, "Sample")))
    }

    @Test
    fun trailingSlashesOfTheOutputFolderAreDroppedBeforeTheFolderName() {
        assertEquals("저장 위치: D:\\Music\\Sample", destinationLabel(resolvedState(InputKind.PLAYLIST, "Sample", "D:\\Music\\")))
        assertEquals("저장 위치: D:/Music\\Sample", destinationLabel(resolvedState(InputKind.PLAYLIST, "Sample", "D:/Music/")))
        assertEquals("저장 위치: D:\\Sample", destinationLabel(resolvedState(InputKind.PLAYLIST, "Sample", "D:\\")))
    }

    @Test
    fun theFolderNameIsSanitizedLikeTheServerDoesIt() {
        assertEquals(
            "저장 위치: D:\\Music\\Best\uFF1A Of\uFF1F", // full-width colon and question mark
            destinationLabel(resolvedState(InputKind.PLAYLIST, "Best: Of?")),
        )
        assertEquals("저장 위치: D:\\Music\\재생목록", destinationLabel(resolvedState(InputKind.PLAYLIST, null)))
    }

    // ---- destinationPath: the path part of the destination label ----

    @Test
    fun destinationPathIsNullBeforeAnythingIsResolved() {
        assertNull(destinationPath(UiState(outputDir = "D:\\Music")))
    }

    @Test
    fun destinationPathIsNullWhileTheOutputFolderIsBlank() {
        assertNull(destinationPath(resolvedState(InputKind.PLAYLIST, "Sample", outputDir = "")))
        assertNull(destinationPath(resolvedState(InputKind.VIDEO, outputDir = "   ")))
    }

    @Test
    fun theDestinationPathOfAVideoIsTheOutputFolderItself() {
        assertEquals("D:\\Music", destinationPath(resolvedState(InputKind.VIDEO)))
        assertEquals("D:\\Music", destinationPath(resolvedState(InputKind.VIDEO, "Sample")))
    }

    @Test
    fun theDestinationPathOfAPlaylistIsAFolderNamedAfterIt() {
        assertEquals("D:\\Music\\Sample", destinationPath(resolvedState(InputKind.PLAYLIST, "Sample")))
    }

    @Test
    fun theDestinationPathDropsTrailingSeparatorsOfTheOutputFolder() {
        assertEquals("D:\\Music\\Sample", destinationPath(resolvedState(InputKind.PLAYLIST, "Sample", "D:\\Music\\")))
        assertEquals("D:/Music\\Sample", destinationPath(resolvedState(InputKind.PLAYLIST, "Sample", "D:/Music/")))
        assertEquals("D:\\Sample", destinationPath(resolvedState(InputKind.PLAYLIST, "Sample", "D:\\")))
    }

    @Test
    fun theDestinationPathUsesTheSanitizedFolderName() {
        assertEquals(
            "D:\\Music\\Best\uFF1A Of\uFF1F", // full-width colon and question mark
            destinationPath(resolvedState(InputKind.PLAYLIST, "Best: Of?")),
        )
        assertEquals("D:\\Music\\재생목록", destinationPath(resolvedState(InputKind.PLAYLIST, null)))
    }

    @Test
    fun theDestinationLabelIsThePathWithItsCaptionAndNullWhenThereIsNoPath() {
        val states = listOf(
            UiState(outputDir = "D:\\Music"),
            resolvedState(InputKind.PLAYLIST, "Sample", outputDir = ""),
            resolvedState(InputKind.VIDEO),
            resolvedState(InputKind.PLAYLIST, "Sample", "D:\\Music\\"),
            resolvedState(InputKind.PLAYLIST, null),
        )
        for (state in states) {
            assertEquals(destinationPath(state)?.let { "저장 위치: $it" }, destinationLabel(state))
        }
        assertEquals("저장 위치: D:\\Music\\재생목록", destinationLabel(resolvedState(InputKind.PLAYLIST, null)))
    }

    @Test
    fun rankIsZeroPadded() {
        assertEquals("007", rankLabel(7))
        assertEquals("999", rankLabel(999))
    }

    @Test
    fun rankInputKeepsOnlyUpToThreeDigits() {
        assertEquals("123", rankInputText("12a3"))
        assertEquals("500", rankInputText("5000"))
        assertEquals("", rankInputText(""))
        assertEquals("", rankInputText("abc"))
    }

    @Test
    fun rankFromInputIsNullForEmptyText() {
        assertNull(rankFromInput(""))
    }

    @Test
    fun rankFromInputClampsToTheValidRange() {
        assertEquals(1, rankFromInput("0"))
        assertEquals(7, rankFromInput("7"))
        assertEquals(999, rankFromInput("999"))
        assertEquals(1, rankFromInput("000"))
    }
}
