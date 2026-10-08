package com.xgetsongs.app.diagnostics

import java.nio.file.Files
import java.nio.file.Path
import java.time.Instant
import java.time.ZoneOffset
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RunMarkerTest {
    private val dir: Path = Files.createTempDirectory("xgs-marker")
    private val started = Instant.parse("2026-10-07T12:15:40.123Z")
    private val running = RunMarker(RunState.RUNNING, 1234, started)

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    // ---- parsing and writing the text ------------------------------------------------------

    @Test
    fun aRunningMarkerIsParsed() {
        assertEquals(running, parseRunMarker("state=running\npid=1234\nstarted=2026-10-07T12:15:40.123Z\n"))
    }

    @Test
    fun anExitedMarkerIsParsedAndTheReasonAndTimeAreOnlyForReaders() {
        val text = "state=exited\npid=1234\nstarted=2026-10-07T12:15:40.123Z\nreason=user\ntime=2026-10-07T12:20:00Z\n"

        assertEquals(RunMarker(RunState.EXITED, 1234, started), parseRunMarker(text))
    }

    @Test
    fun anExitedMarkerNeedsNothingButTheState() {
        assertEquals(RunMarker(RunState.EXITED, null, null), parseRunMarker("state=exited"))
    }

    @Test
    fun windowsLineEndsSpacesUnknownKeysBlankLinesAndABomAreAllRight() {
        val text = "﻿\r\n state = running \r\n\r\npid= 1234\r\nstarted=2026-10-07T12:15:40.123Z\r\nfuture-key=whatever\r\n# not a pair\r\n"

        assertEquals(running, parseRunMarker(text))
    }

    @Test
    fun theLastValueOfAKeyWins() {
        assertEquals(running, parseRunMarker("state=exited\nstate=running\npid=1234\nstarted=2026-10-07T12:15:40.123Z"))
    }

    @Test
    fun textThatIsNotAMarkerIsNull() {
        for (text in listOf(
            "",
            "   \n\n",
            "hello world",
            "state=weird\npid=1\nstarted=2026-10-07T12:15:40.123Z",
            "state=running", // nothing to tell the process by
            "state=running\npid=1234",
            "state=running\nstarted=2026-10-07T12:15:40.123Z",
            "state=running\npid=abc\nstarted=2026-10-07T12:15:40.123Z",
            "state=running\npid=-5\nstarted=2026-10-07T12:15:40.123Z",
            "state=running\npid=1234\nstarted=yesterday",
            "pid=1234\nstarted=2026-10-07T12:15:40.123Z",
            "\u0000\u0001\u0002 binary ÿ",
        )) {
            assertNull(parseRunMarker(text), text)
        }
    }

    @Test
    fun theRunningMarkerIsWrittenAsKeyValueLinesAndReadBack() {
        val text = renderRunning(1234, started)

        assertEquals("state=running\npid=1234\nstarted=2026-10-07T12:15:40.123Z\n", text)
        assertEquals(running, parseRunMarker(text))
    }

    @Test
    fun theExitedMarkerNamesTheReasonAndTheTime() {
        val time = Instant.parse("2026-10-07T12:20:00Z")

        assertEquals(
            "state=exited\npid=1234\nstarted=2026-10-07T12:15:40.123Z\nreason=user\ntime=2026-10-07T12:20:00Z\n",
            renderExited(1234, started, userRequested = true, time = time),
        )
        assertTrue("reason=other\n" in renderExited(1234, started, userRequested = false, time = time))
        assertEquals(RunMarker(RunState.EXITED, 1234, started), parseRunMarker(renderExited(1234, started, false, time)))
    }

    // ---- what the marker says about the previous run ---------------------------------------

    private val noProcess: (Long) -> Instant? = { null }

    @Test
    fun noMarkerMeansNothingToSay() {
        assertEquals(PreviousRun.None, judgePreviousRun(null, noProcess))
    }

    @Test
    fun aCleanExitMeansNothingToSay() {
        assertEquals(PreviousRun.None, judgePreviousRun(RunMarker(RunState.EXITED, 1234, started), noProcess))
        assertEquals(PreviousRun.None, judgePreviousRun(RunMarker(RunState.EXITED, null, null), noProcess))
    }

    @Test
    fun aRunningMarkerWhoseProcessIsGoneIsAnUncleanEnd() {
        val asked = mutableListOf<Long>()

        val judged = judgePreviousRun(running) { pid -> asked += pid; null }

        assertEquals(PreviousRun.Unclean(1234, started), judged)
        assertEquals(listOf(1234L), asked)
    }

    @Test
    fun aRunningMarkerWhoseProcessLivesWithTheSameStartTimeIsAnotherRunStillGoing() {
        assertEquals(PreviousRun.StillRunning(1234), judgePreviousRun(running) { started })
    }

    @Test
    fun aReusedProcessNumberWithAnotherStartTimeIsAnUncleanEnd() {
        val judged = judgePreviousRun(running) { started.plusSeconds(3_600) }

        assertEquals(PreviousRun.Unclean(1234, started), judged)
    }

    @Test
    fun theRealProcessTableRecognisesThisProcessByNumberAndStartTime() {
        val own = RunMarker(RunState.RUNNING, SystemProcesses.ownPid, SystemProcesses.ownStart)

        assertEquals(SystemProcesses.ownStart, SystemProcesses.startOf(SystemProcesses.ownPid))
        assertEquals(PreviousRun.StillRunning(SystemProcesses.ownPid), judgePreviousRun(own, SystemProcesses::startOf))
        assertNull(SystemProcesses.startOf(Long.MAX_VALUE), "no process has that number")
        assertEquals(
            PreviousRun.Unclean(SystemProcesses.ownPid, SystemProcesses.ownStart.minusSeconds(60)),
            judgePreviousRun(own.copy(started = SystemProcesses.ownStart.minusSeconds(60)), SystemProcesses::startOf),
            "the same number with another start time is another process",
        )
    }

    @Test
    fun theWordsForTheTwoCases() {
        val still = describePreviousRun(PreviousRun.StillRunning(1234), ZoneOffset.UTC)
        assertEquals(PreviousRunNote(warn = false, text = "다른 실행이 아직 실행 중임 (PID 1234)"), still)

        val unclean = describePreviousRun(PreviousRun.Unclean(1234, started), ZoneOffset.UTC)
        assertEquals(
            PreviousRunNote(
                warn = true,
                text = "이전 실행(PID 1234, 시작 2026-10-07 12:15:40)이 정상 종료 기록 없이 끝났음: 강제 종료, 크래시, 전원 차단 등의 가능성",
            ),
            unclean,
        )
        assertNull(describePreviousRun(PreviousRun.None, ZoneOffset.UTC))
    }

    @Test
    fun theStartTimeIsShownInTheZoneOfTheMachine() {
        val seoul = describePreviousRun(PreviousRun.Unclean(1, started), java.time.ZoneId.of("Asia/Seoul"))

        assertTrue("시작 2026-10-07 21:15:40)" in seoul!!.text, seoul.text)
    }

    // ---- the file --------------------------------------------------------------------------

    private fun marker(name: String = "last-run.txt") = RunMarkerFile(dir.resolve(name), pid = 1234, started = started)

    @Test
    fun aMissingFileReadsAsNull() {
        assertNull(marker().read())
    }

    @Test
    fun aFileThatIsNotAMarkerReadsAsNull() {
        Files.writeString(dir.resolve("last-run.txt"), "garbage")
        assertNull(marker().read())

        Files.write(dir.resolve("last-run.txt"), byteArrayOf(0x73, 0x74, 0xFF.toByte(), 0xFE.toByte())) // not UTF-8
        assertNull(marker().read())
    }

    @Test
    fun somethingThatCannotBeReadAsAFileReadsAsNull() {
        Files.createDirectory(dir.resolve("last-run.txt"))

        assertNull(marker().read())
    }

    @Test
    fun markingRunningWritesTheMarkerAtomicallyWithoutLeavingATempFile() {
        val file = marker()

        file.markRunning()

        assertEquals(running, file.read())
        assertEquals(listOf("last-run.txt"), Files.list(dir).use { s -> s.map { it.fileName.toString() }.toList() })
    }

    @Test
    fun markingExitedReplacesTheRunningMarker() {
        val file = marker()
        file.markRunning()

        file.markExited(userRequested = true, time = Instant.parse("2026-10-07T12:20:00Z"))

        val text = Files.readString(dir.resolve("last-run.txt"))
        assertTrue("state=exited" in text && "reason=user" in text && "time=2026-10-07T12:20:00Z" in text, text)
        assertEquals(RunState.EXITED, file.read()?.state)

        file.markExited(userRequested = false)
        assertTrue("reason=other" in Files.readString(dir.resolve("last-run.txt")))
    }

    @Test
    fun theFileHoldsOnlyAscii() {
        marker().markRunning()

        val bytes = Files.readAllBytes(dir.resolve("last-run.txt"))
        assertTrue(bytes.all { it in 0..127 })
    }

    @Test
    fun theFolderIsCreatedWhenItIsMissing() {
        val file = RunMarkerFile(dir.resolve("a").resolve("b").resolve("last-run.txt"), 1234, started)

        file.markRunning()

        assertEquals(running, file.read())
    }

    @Test
    fun writingNeverThrowsWhateverGoesWrong() {
        val blocker = Files.writeString(dir.resolve("a-file"), "not a folder")
        val file = RunMarkerFile(blocker.resolve("last-run.txt"), 1234, started)

        file.markRunning() // must not throw
        file.markExited(userRequested = true) // nor this

        assertNull(file.read())
        assertFalse(Files.exists(blocker.resolve("last-run.txt")))
    }
}
