package com.xgetsongs.engine.testutil

import com.xgetsongs.engine.lyrics.LyricsProvider
import com.xgetsongs.engine.lyrics.LyricsQuery
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

/** Pretends yt-dlp also wrote the video's info file: creates `<dir>/<id>.info.json` holding [json]. */
fun writeFakeInfo(command: List<String>, json: String) {
    Files.writeString(outputDirOf(command).resolve("${videoIdOf(command)}.info.json"), json)
}

/** The audio bytes of what the fake ffmpeg writes. Nothing after ffmpeg may change them. */
const val FAKE_AUDIO = "tagged-mp3-data"

/**
 * What the fake ffmpeg writes: a minimal ID3v2.4 file (a header, one `TIT2` frame and 4 bytes of padding) followed by
 * [FAKE_AUDIO]. It has to be a real tag because the engine adds the comment frame to ffmpeg's output.
 */
fun fakeTaggedMp3(): ByteArray {
    // Frame: id, 4-byte syncsafe size (1 encoding byte + 5 text bytes = 6, which is the same bytes as a plain integer), 2 flag bytes, body.
    val frame = "TIT2".toByteArray(Charsets.ISO_8859_1) + byteArrayOf(0, 0, 0, 6, 0, 0) + byteArrayOf(0) + "Title".toByteArray(Charsets.ISO_8859_1)
    val padding = ByteArray(4)
    val tagSize = frame.size + padding.size // 20: fits in the last syncsafe byte
    val header = "ID3".toByteArray(Charsets.ISO_8859_1) + byteArrayOf(4, 0, 0, 0, 0, 0, tagSize.toByte())
    return header + frame + padding + FAKE_AUDIO.toByteArray(Charsets.ISO_8859_1)
}

/** True when the file still ends with the audio of [fakeTaggedMp3]. */
fun endsWithFakeAudio(file: Path): Boolean = String(Files.readAllBytes(file), Charsets.ISO_8859_1).endsWith(FAKE_AUDIO)

/** True for the ID3 tagging pass (the command starts with the ffmpeg path); false for yt-dlp commands. */
fun isFfmpegCommand(command: List<String>): Boolean = command.first() == TEST_TOOLS.ffmpeg.toString()

/** Pretends ffmpeg finished tagging: creates the output file, which is the last argument. */
fun writeFakeTagged(command: List<String>) {
    Files.write(Path.of(command.last()), fakeTaggedMp3())
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

/** A [LyricsProvider] whose answer is a lambda; records every query it is asked. */
class FakeLyricsProvider(private val answer: suspend (LyricsQuery) -> String? = { null }) : LyricsProvider {
    val queries = CopyOnWriteArrayList<LyricsQuery>()

    override suspend fun find(query: LyricsQuery): String? {
        queries += query
        return answer(query)
    }
}
