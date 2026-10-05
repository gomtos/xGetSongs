package com.xgetsongs.engine.tags

import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.ytdlp.Failure
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.engine.ytdlp.FfmpegCommands
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import kotlin.io.path.nameWithoutExtension

/**
 * Writes ID3 tags and the cover into an mp3 with one ffmpeg stream-copy pass. The tag text travels in an ffmetadata
 * file next to the mp3, never on the command line. The comment and the lyrics are the exceptions to ffmpeg writing the
 * tags: they become a real `COMM` frame and a real `USLT` frame added by [Id3Frames] afterwards.
 */
class Id3Tagger(
    private val runner: ProcessRunner,
    private val tools: ToolPathProvider,
) {
    /**
     * Rewrites [file] in place with [tags] and, when given, the picture [cover]. Returns null on success, else the
     * failure; [file] is left untouched then.
     */
    suspend fun tag(file: Path, cover: Path?, tags: TrackTags): Failure? {
        val ffmpeg = tools.current().ffmpeg ?: return Failure(FailureKind.FATAL, "ffmpeg를 찾을 수 없습니다.")
        val name = file.nameWithoutExtension
        val metadataFile = file.resolveSibling("$name.ffmeta")
        val output = file.resolveSibling("$name.tagged.mp3")
        val stderr = mutableListOf<String>()
        try {
            withContext(Dispatchers.IO) { Files.writeString(metadataFile, Ffmetadata.render(tags), Charsets.UTF_8) }
            val exitCode = runner.run(
                FfmpegCommands.tag(ffmpeg, file, cover, metadataFile, output),
                onStderr = { line -> synchronized(stderr) { stderr += line } },
            )
            val written = withContext(Dispatchers.IO) { Files.isRegularFile(output) }
            if (exitCode != 0 || !written) {
                return failure(synchronized(stderr) { stderr.lastOrNull { it.isNotBlank() } }?.trim()?.take(MAX_DETAIL))
            }
            withContext(Dispatchers.IO) {
                // ffmpeg cannot write COMM or USLT frames, so they are added to ffmpeg's output before that replaces the original.
                Id3Frames.add(output, tags.comment, tags.lyrics)
                Files.move(output, file, StandardCopyOption.REPLACE_EXISTING)
            }
            return null
        } catch (e: IOException) {
            return failure(e.message)
        } finally {
            withContext(NonCancellable + Dispatchers.IO) {
                deleteQuietly(metadataFile)
                deleteQuietly(output)
            }
        }
    }

    private fun failure(detail: String?) =
        Failure(FailureKind.OTHER, "ID3 태그를 쓰지 못했습니다: ${detail ?: "알 수 없는 오류"}")

    /** A leftover temp file is harmless (the job's work folder is removed), so it must not hide the real result. */
    private fun deleteQuietly(path: Path) {
        try {
            Files.deleteIfExists(path)
        } catch (e: IOException) {
            // Ignored on purpose, see above.
        }
    }

    private companion object {
        const val MAX_DETAIL = 200
    }
}
