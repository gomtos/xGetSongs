package com.xgetsongs.engine.testutil

import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.tools.ToolPaths
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicInteger

/** A [ProcessRunner] whose behaviour is a lambda; records every command and the peak concurrency. */
class FakeProcessRunner(
    private val handler: suspend (command: List<String>, onStdout: (String) -> Unit, onStderr: (String) -> Unit) -> Int,
) : ProcessRunner {
    val commands = CopyOnWriteArrayList<List<String>>()
    val maxActive = AtomicInteger()
    private val active = AtomicInteger()

    override suspend fun run(command: List<String>, onStdout: (String) -> Unit, onStderr: (String) -> Unit): Int {
        commands += command
        val now = active.incrementAndGet()
        maxActive.accumulateAndGet(now) { a, b -> maxOf(a, b) }
        try {
            return handler(command, onStdout, onStderr)
        } finally {
            active.decrementAndGet()
        }
    }
}

val TEST_TOOLS = ToolPaths(
    ytDlp = Path.of("C:/tools/yt-dlp.exe"),
    ffmpeg = Path.of("C:/tools/ffmpeg.exe"),
    jsRuntime = Path.of("C:/tools/node.exe"),
)

fun toolsOf(paths: ToolPaths = TEST_TOOLS) = ToolPathProvider { paths }

/** The directory a download command writes to, read from its `-o` argument. */
fun outputDirOf(command: List<String>): Path {
    val template = command[command.indexOf("-o") + 1]
    return Path.of(template).parent
}

/** The video ID a download command is for, read from the `-o` argument (`<dir>/<id>.%(ext)s`). */
fun videoIdOf(command: List<String>): String {
    val template = command[command.indexOf("-o") + 1]
    return Path.of(template).fileName.toString().removeSuffix(".%(ext)s")
}

/** Pretends yt-dlp finished: creates `<dir>/<id>.mp3` the way the real tool would. */
fun writeFakeMp3(command: List<String>) {
    Files.writeString(outputDirOf(command).resolve("${videoIdOf(command)}.mp3"), "mp3-data")
}
