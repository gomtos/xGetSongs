package com.xgetsongs.engine.testutil

import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.tools.ToolPaths
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicInteger

/** What a [FakeProcessRunner] does for one command: writes lines to the callbacks and returns the exit code. */
typealias ProcessHandler = suspend (command: List<String>, onStdout: (String) -> Unit, onStderr: (String) -> Unit) -> Int

/** A [ProcessRunner] whose behaviour is a lambda; records every command and the peak concurrency. */
class FakeProcessRunner(private val handler: ProcessHandler) : ProcessRunner {
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

/** Pretends yt-dlp also saved the converted thumbnail: creates `<dir>/<id>.jpg`. */
fun writeFakeCover(command: List<String>) {
    Files.writeString(outputDirOf(command).resolve("${videoIdOf(command)}.jpg"), "jpg-data")
}

/** What the fake ffmpeg writes as the tagged file. */
const val FAKE_TAGGED_MP3 = "tagged-mp3-data"

/** True for the ID3 tagging pass (the command starts with the ffmpeg path); false for yt-dlp commands. */
fun isFfmpegCommand(command: List<String>): Boolean = command.first() == TEST_TOOLS.ffmpeg.toString()

/** Pretends ffmpeg finished tagging: creates the output file, which is the last argument. */
fun writeFakeTagged(command: List<String>) {
    Files.writeString(Path.of(command.last()), FAKE_TAGGED_MP3)
}

/** The text of the ffmetadata file an ffmpeg tagging command reads (the input after `-f ffmetadata`). */
fun ffmetadataTextOf(command: List<String>): String =
    Files.readString(Path.of(command[command.indexOf("ffmetadata") + 2]))

/**
 * A runner for a whole download: the ffmpeg tagging pass succeeds by writing the tagged file, every other command
 * (yt-dlp) is handled by [ytDlp].
 */
fun downloadRunner(ytDlp: ProcessHandler) = FakeProcessRunner { command, onStdout, onStderr ->
    if (isFfmpegCommand(command)) {
        writeFakeTagged(command)
        0
    } else {
        ytDlp(command, onStdout, onStderr)
    }
}

val FakeProcessRunner.ytDlpCommands: List<List<String>> get() = commands.filterNot(::isFfmpegCommand)

val FakeProcessRunner.ffmpegCommands: List<List<String>> get() = commands.filter(::isFfmpegCommand)
