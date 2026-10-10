package com.xgetsongs.diagnostics

import java.lang.management.ManagementFactory
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/** Heap use in whole megabytes. */
internal data class MemorySnapshot(val usedMb: Long, val totalMb: Long, val maxMb: Long)

/** What one garbage collector has done since the JVM started. */
internal data class GcSnapshot(val name: String, val count: Long, val timeMs: Long)

/**
 * The text that goes into the log and into `ui-hang-*.txt` when the UI thread stops answering: when and why, the heap and
 * the garbage collectors, then every thread with its stack. Nothing else about the machine is written (no environment
 * variables, no system properties): the startup record already holds the few that matter.
 */
internal object ThreadDump {
    /** Frames kept per thread, counted from the innermost call. */
    const val MAX_FRAMES = 60

    private const val EVENT_QUEUE_PREFIX = "AWT-EventQueue"
    private val timeFormat = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss.SSS")

    // Threads of the AWT event queue first (that is the one the user is waiting for), then the others by name.
    private val threadOrder = compareBy<Thread> { if (it.name.startsWith(EVENT_QUEUE_PREFIX)) 0 else 1 }
        .thenBy(String.CASE_INSENSITIVE_ORDER) { it.name }
        .thenBy { it.name }

    /**
     * Renders the dump from data that is already collected, so a test can hand in made-up threads. [states] holds the
     * state each thread had when its stack was taken; a thread missing from it shows its current state.
     */
    fun render(
        time: LocalDateTime,
        silentMs: Long,
        reason: String,
        stacks: Map<Thread, Array<StackTraceElement>>,
        states: Map<Thread, Thread.State>,
        memory: MemorySnapshot,
        gcs: List<GcSnapshot>,
    ): String = buildString {
        appendLine("스레드 덤프 ${timeFormat.format(time)} | 무응답 ${silentMs}ms | 사유: $reason")
        appendLine("메모리: 사용 ${memory.usedMb}MB / 할당 ${memory.totalMb}MB / 최대 ${memory.maxMb}MB")
        for (gc in gcs) appendLine("GC \"${gc.name}\": ${gc.count}회, ${gc.timeMs}ms")
        appendLine("스레드: ${stacks.size}개")
        appendLine()
        for (thread in stacks.keys.sortedWith(threadOrder)) {
            val state = states[thread] ?: thread.state
            appendLine("\"${thread.name}\" ${if (thread.isDaemon) "daemon " else ""}$state")
            val frames = stacks.getValue(thread)
            for (frame in frames.take(MAX_FRAMES)) appendLine("\tat $frame")
            if (frames.size > MAX_FRAMES) appendLine("\t... ${frames.size - MAX_FRAMES}개 프레임 생략")
            appendLine()
        }
    }

    /** Takes the stacks of all live threads now and renders them. */
    fun capture(silentMs: Long, reason: String, time: LocalDateTime = LocalDateTime.now()): String {
        val stacks = Thread.getAllStackTraces()
        val states = stacks.keys.associateWith { it.state }
        return render(time, silentMs, reason, stacks, states, memorySnapshot(), gcSnapshots())
    }

    private fun memorySnapshot(): MemorySnapshot {
        val runtime = Runtime.getRuntime()
        val mb = 1024L * 1024L
        return MemorySnapshot(
            usedMb = (runtime.totalMemory() - runtime.freeMemory()) / mb,
            totalMb = runtime.totalMemory() / mb,
            maxMb = runtime.maxMemory() / mb,
        )
    }

    // The garbage collector beans come from java.management, which a trimmed-down runtime image may not have.
    private fun gcSnapshots(): List<GcSnapshot> = try {
        ManagementFactory.getGarbageCollectorMXBeans().map { GcSnapshot(it.name, it.collectionCount, it.collectionTime) }
    } catch (e: Exception) {
        emptyList()
    } catch (e: LinkageError) {
        emptyList()
    }
}
