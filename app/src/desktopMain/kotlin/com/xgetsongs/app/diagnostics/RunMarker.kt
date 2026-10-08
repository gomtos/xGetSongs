package com.xgetsongs.app.diagnostics

import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException

internal enum class RunState { RUNNING, EXITED }

/**
 * What the marker file of a run says that matters to the next run: [state], and for a running one the [pid] and the
 * [started] time of that process (an exited marker may lack both).
 */
internal data class RunMarker(val state: RunState, val pid: Long?, val started: Instant?)

/**
 * Reads the text of a marker file (UTF-8 `key=value` lines: unknown keys, blank lines, lines without `=`, a byte order mark
 * and Windows line ends are all fine). Null when it is not a marker: no `state`, an unknown one, or a running one without
 * a usable `pid` and `started` (a marker that cannot tell which process it was is of no use).
 */
internal fun parseRunMarker(text: String): RunMarker? {
    val values = HashMap<String, String>()
    for (line in text.removePrefix("﻿").lines()) {
        val at = line.indexOf('=')
        if (at > 0) values[line.substring(0, at).trim()] = line.substring(at + 1).trim()
    }
    val pid = values["pid"]?.toLongOrNull()?.takeIf { it > 0 }
    val started = values["started"]?.let { try { Instant.parse(it) } catch (e: DateTimeParseException) { null } }
    return when (values["state"]) {
        "running" -> if (pid != null && started != null) RunMarker(RunState.RUNNING, pid, started) else null
        "exited" -> RunMarker(RunState.EXITED, pid, started)
        else -> null
    }
}

/** The marker text of a run that has just started. */
internal fun renderRunning(pid: Long, started: Instant): String = "state=running\npid=$pid\nstarted=$started\n"

/** The marker text of a run that is ending: [userRequested] is `reason=user`, anything else `reason=other`. */
internal fun renderExited(pid: Long, started: Instant, userRequested: Boolean, time: Instant): String =
    "state=exited\npid=$pid\nstarted=$started\nreason=${if (userRequested) "user" else "other"}\ntime=$time\n"

/** What the marker the previous run left says about that run. */
internal sealed interface PreviousRun {
    /** Nothing to tell: there was no usable marker, or the run ended with an exit record. */
    data object None : PreviousRun

    /** The process of the previous run is still alive: this is a second run of the app. */
    data class StillRunning(val pid: Long) : PreviousRun

    /** The marker still says running, but the process is gone (or its number belongs to a newer process). */
    data class Unclean(val pid: Long, val started: Instant) : PreviousRun
}

/**
 * Decides what [previous] means. A running marker counts as another live run only when [startOf] (the start time of the
 * live process with that number, or null when there is none) gives exactly the recorded start time: Windows reuses
 * process numbers, so a live process with another start time is somebody else.
 */
internal fun judgePreviousRun(previous: RunMarker?, startOf: (Long) -> Instant?): PreviousRun {
    if (previous == null || previous.state != RunState.RUNNING) return PreviousRun.None
    val pid = previous.pid ?: return PreviousRun.None
    val started = previous.started ?: return PreviousRun.None
    return if (startOf(pid) == started) PreviousRun.StillRunning(pid) else PreviousRun.Unclean(pid, started)
}

/** A line for the log: [warn] says whether it is a warning. */
internal data class PreviousRunNote(val warn: Boolean, val text: String)

private val noteTime = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")

/** The words for [run] (the start time in [zone]), or null when there is nothing to say. */
internal fun describePreviousRun(run: PreviousRun, zone: ZoneId = ZoneId.systemDefault()): PreviousRunNote? = when (run) {
    PreviousRun.None -> null
    is PreviousRun.StillRunning -> PreviousRunNote(warn = false, text = "다른 실행이 아직 실행 중임 (PID ${run.pid})")
    is PreviousRun.Unclean -> PreviousRunNote(
        warn = true,
        text = "이전 실행(PID ${run.pid}, 시작 ${noteTime.format(run.started.atZone(zone))})이 정상 종료 기록 없이 끝났음: 강제 종료, 크래시, 전원 차단 등의 가능성",
    )
}

/**
 * The marker file of this run (`last-run.txt` next to the log): [markRunning] when the app starts, [markExited] from the
 * exit hook. Neither throws, whatever goes wrong: a marker that cannot be written only means the next run learns nothing.
 * The text goes to a temp file next to the marker and is moved over it, so a kill never leaves half a file behind.
 */
internal class RunMarkerFile(private val file: Path, private val pid: Long, private val started: Instant) {
    /** The marker of the previous run, or null when there is none or it cannot be read as one. */
    fun read(): RunMarker? = try {
        parseRunMarker(Files.readAllBytes(file).decodeToString(throwOnInvalidSequence = true))
    } catch (e: Exception) {
        null
    }

    fun markRunning() = write(renderRunning(pid, started))

    fun markExited(userRequested: Boolean, time: Instant = Instant.now()) = write(renderExited(pid, started, userRequested, time))

    private fun write(text: String) {
        var temp: Path? = null
        try {
            val name = file.fileName ?: return
            file.toAbsolutePath().parent?.let { Files.createDirectories(it) }
            temp = file.resolveSibling("$name.tmp")
            Files.write(temp, text.toByteArray(Charsets.UTF_8))
            Files.move(temp, file, StandardCopyOption.REPLACE_EXISTING)
        } catch (e: Exception) {
            // The next run learns nothing from this one; nothing else suffers.
        } finally {
            try {
                temp?.let { Files.deleteIfExists(it) }
            } catch (e: Exception) {
                // A leftover temp file is overwritten by the next write.
            }
        }
    }
}
