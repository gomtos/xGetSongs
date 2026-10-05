package com.xgetsongs.engine.ytdlp

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class ProgressParserTest {
    @Test
    fun percentFromDownloadedAndTotalBytes() {
        val update = ProgressParser.parse("XGSP|downloading|500|1000|NA")
        assertEquals(ProgressUpdate.Downloading(50.0), update)
    }

    @Test
    fun fallsBackToTheEstimatedTotal() {
        val update = ProgressParser.parse("XGSP|downloading|250|NA|1000") as ProgressUpdate.Downloading
        assertEquals(25.0, update.percent)
    }

    @Test
    fun percentIsUnknownWithoutAnyTotal() {
        assertEquals(ProgressUpdate.Downloading(null), ProgressParser.parse("XGSP|downloading|500|NA|NA"))
    }

    @Test
    fun percentIsCappedAt100() {
        assertEquals(ProgressUpdate.Downloading(100.0), ProgressParser.parse("XGSP|downloading|2000|1000|NA"))
    }

    @Test
    fun finishedMeansHundredPercent() {
        assertEquals(ProgressUpdate.Downloading(100.0), ProgressParser.parse("XGSP|finished|1000|1000|NA"))
    }

    @Test
    fun postprocessorStartMeansConverting() {
        assertEquals(ProgressUpdate.Converting, ProgressParser.parse("XGSPP|started|ExtractAudio"))
    }

    // yt-dlp runs the thumbnail converter (--convert-thumbnails) before the download starts and every post-processor
    // prints the template, so this one must not switch the UI to "converting" before the download.
    @Test
    fun thumbnailConversionIsNotTheAudioConversion() {
        assertNull(ProgressParser.parse("XGSPP|started|ThumbnailsConvertor"))
        assertNull(ProgressParser.parse("XGSPP|finished|ThumbnailsConvertor"))
    }

    @Test
    fun audioExtractionStillMeansConvertingNextToTheThumbnailConvertor() {
        assertEquals(ProgressUpdate.Converting, ProgressParser.parse("XGSPP|started|ExtractAudio"))
        assertNull(ProgressParser.parse("XGSPP|started|ThumbnailsConvertor"))
        assertEquals(ProgressUpdate.Converting, ProgressParser.parse("XGSPP|started|ExtractAudio"))
    }

    @Test
    fun otherLinesAreIgnored() {
        assertNull(ProgressParser.parse("[youtube] Extracting URL"))
        assertNull(ProgressParser.parse("XGSPP|finished|ExtractAudio"))
        assertNull(ProgressParser.parse(""))
    }
}
