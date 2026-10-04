package com.xgetsongs.app.state

import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class LabelsTest {
    @Test
    fun statusLabels() {
        assertEquals("준비됨", statusLabel(ItemStatus.Ready))
        assertEquals("대기 중", statusLabel(ItemStatus.Waiting))
        assertEquals("다운로드 중…", statusLabel(ItemStatus.Downloading(null)))
        assertEquals("다운로드 42%", statusLabel(ItemStatus.Downloading(42.9)))
        assertEquals("mp3 변환 중…", statusLabel(ItemStatus.Converting))
        assertEquals("완료", statusLabel(ItemStatus.Done))
        assertEquals("건너뜀: 비공개 영상", statusLabel(ItemStatus.Skipped("비공개 영상")))
        assertEquals("실패: boom", statusLabel(ItemStatus.Failed("boom")))
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

    @Test
    fun rankIsZeroPadded() {
        assertEquals("007", rankLabel(7))
        assertEquals("999", rankLabel(999))
    }
}
