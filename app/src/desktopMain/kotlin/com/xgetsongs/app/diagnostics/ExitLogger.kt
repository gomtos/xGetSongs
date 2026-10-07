package com.xgetsongs.app.diagnostics

import org.slf4j.Logger
import org.slf4j.LoggerFactory
import kotlin.time.Duration

/** The words of the exit record: [warn] says whether it is a warning (an exit nobody in the window asked for). */
internal data class ExitDescription(val warn: Boolean, val text: String)

/**
 * Says what is known when the JVM starts to go down. The window's close button is the one exit the app knows about
 * ([userRequested]); anything else (the console closed, SIGTERM, a log-off or a shutdown of Windows) arrives as an exit
 * nobody in the window asked for. [runningJobs] is the number of running downloads, or negative when nobody can tell.
 */
internal fun describeExit(userRequested: Boolean, runningJobs: Int, uptime: Duration): ExitDescription {
    val jobs = if (runningJobs < 0) "진행 중인 작업 수를 알 수 없음" else "진행 중인 작업 ${runningJobs}개"
    val details = "가동 ${formatUptime(uptime)}, $jobs"
    return if (userRequested) {
        ExitDescription(warn = false, text = "JVM 종료 시작: 사용자가 창을 닫음 ($details)")
    } else {
        ExitDescription(warn = true, text = "JVM 종료 시작: 외부 종료 요청: 콘솔 종료, SIGTERM, 로그오프·시스템 종료 등 ($details)")
    }
}

/** `3초`, `12분 3초` or `1시간 2분 3초`; never negative. */
internal fun formatUptime(uptime: Duration): String =
    uptime.coerceAtLeast(Duration.ZERO).toComponents { hours, minutes, seconds, _ ->
        when {
            hours > 0 -> "${hours}시간 ${minutes}분 ${seconds}초"
            minutes > 0 -> "${minutes}분 ${seconds}초"
            else -> "${seconds}초"
        }
    }

/**
 * Writes one record when the JVM exits, whatever the reason: this is the line that tells a closed window from a process
 * that was terminated from outside, which otherwise leaves no trace. The window's close handler calls [markUserExit]
 * before it lets the JVM go down. [runningJobs], [uptime] and [heapMb] (used and maximum heap in megabytes) are asked at
 * the moment of the exit; [runningJobs] can be replaced later, because the server that knows the number starts after the
 * diagnostics do.
 */
internal class ExitLogger(
    @field:Volatile var runningJobs: () -> Int,
    private val uptime: () -> Duration,
    private val heapMb: () -> Pair<Long, Long> = ::heapUseMb,
) {
    private val log: Logger = LoggerFactory.getLogger(ExitLogger::class.java)

    @Volatile
    private var userRequested = false

    fun markUserExit() {
        userRequested = true
    }

    /**
     * The body of the shutdown hook. It runs while the JVM is going down, so it does nothing but ask for three numbers and
     * make one log call, and it never throws.
     */
    fun logExit() {
        try {
            val jobs = try { runningJobs() } catch (e: Exception) { -1 }
            val up = try { uptime() } catch (e: Exception) { Duration.ZERO }
            val heap = try {
                val (used, max) = heapMb()
                " | 힙 사용 ${used}MB (최대 ${max}MB)"
            } catch (e: Exception) {
                ""
            }
            val exit = describeExit(userRequested, jobs, up)
            if (exit.warn) log.warn("{}{}", exit.text, heap) else log.info("{}{}", exit.text, heap)
        } catch (e: Throwable) {
            // Nobody is left to report to: an exception out of a shutdown hook is only printed.
        }
    }

    /** Hands a thread that runs [logExit] to [addHook] (in the app: `Runtime.addShutdownHook`) and returns it. */
    fun register(addHook: (Thread) -> Unit = Runtime.getRuntime()::addShutdownHook): Thread {
        val hook = Thread(::logExit, "xgs-exit-logger")
        addHook(hook)
        return hook
    }
}

private fun heapUseMb(): Pair<Long, Long> {
    val runtime = Runtime.getRuntime()
    val mb = 1024L * 1024L
    return (runtime.totalMemory() - runtime.freeMemory()) / mb to runtime.maxMemory() / mb
}
