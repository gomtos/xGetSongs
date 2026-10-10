package com.xgetsongs.engine.ytdlp

import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class FfmpegCommandsTest {
    private val ffmpeg = Path.of("C:/tools/ffmpeg.exe")
    private val input = Path.of("C:/work/1/vid.m4a")
    private val cover = Path.of("C:/work/1/vid.jpg")
    private val metadata = Path.of("C:/work/1/vid.ffmeta")
    private val output = Path.of("C:/work/1/vid.tagged.m4a")

    @Test
    fun withoutACoverCopiesTheAudioAndTagsItFromTheMetadataFile() {
        val command = FfmpegCommands.tag(ffmpeg, input, null, metadata, output)

        assertEquals(
            listOf(
                ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-i", input.toString(), "-f", "ffmetadata", "-i", metadata.toString(),
                "-map", "0:a", "-map_chapters", "-1", "-map_metadata", "1",
                "-c:a", "copy",
                output.toString(),
            ),
            command,
        )
    }

    @Test
    fun withACoverAddsItAsAnAttachedSquareMjpegPicture() {
        val command = FfmpegCommands.tag(ffmpeg, input, cover, metadata, output)

        assertEquals(
            listOf(
                ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-i", input.toString(), "-i", cover.toString(), "-f", "ffmetadata", "-i", metadata.toString(),
                "-map", "0:a", "-map", "1:v", "-map_chapters", "-1", "-map_metadata", "2",
                "-c:a", "copy",
                "-c:v", "mjpeg", "-q:v", "2", "-vf", "crop=min(iw\\,ih):min(iw\\,ih)", "-disposition:v", "attached_pic",
                "-metadata:s:v", "title=Album cover", "-metadata:s:v", "comment=Cover (front)",
                output.toString(),
            ),
            command,
        )
    }

    @Test
    fun theCropFilterIsOneArgumentWithAnEscapedComma() {
        val command = FfmpegCommands.tag(ffmpeg, input, cover, metadata, output)

        assertEquals("""crop=min(iw\,ih):min(iw\,ih)""", command[command.indexOf("-vf") + 1])
    }

    @Test
    fun theOutputMustBeAnM4aSoFfmpegPicksTheMp4Muxer() {
        for (wrong in listOf("vid.tmp", "vid.mp3")) {
            assertFailsWith<IllegalArgumentException>(wrong) {
                FfmpegCommands.tag(ffmpeg, input, null, metadata, Path.of("C:/work/1/$wrong"))
            }
        }
    }
}
