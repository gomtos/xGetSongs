package com.xgetsongs.engine.tags

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class FfmetadataTest {
    private fun tags(
        title: String = "Dynamite",
        artist: String = "BTS",
        album: String? = "Best of BTS",
        albumArtist: String = artist,
        trackNumber: Int = 7,
        comment: String? = "note",
    ) = TrackTags(title, artist, album, albumArtist, trackNumber, comment)

    private fun lines(text: String) = text.removeSuffix("\n").split("\n")

    @Test
    fun writesTheHeaderThenEveryTagInOrder() {
        val text = Ffmetadata.render(tags())

        assertEquals(
            listOf(
                ";FFMETADATA1",
                "title=Dynamite",
                "artist=BTS",
                "album_artist=BTS",
                "album=Best of BTS",
                "track=7",
            ),
            lines(text),
        )
        assertTrue(text.endsWith("\n"))
        assertFalse(text.contains("\r"))
    }

    @Test
    fun theLyricsAreNotRendered() {
        val withLyrics = tags().copy(lyrics = "Line one\nLine two=three\n첫 번째 줄")

        assertEquals(Ffmetadata.render(tags()), Ffmetadata.render(withLyrics))
        assertFalse(Ffmetadata.render(withLyrics).contains("lyrics"))
    }

    @Test
    fun albumArtistComesFromItsOwnField() {
        val text = Ffmetadata.render(tags(artist = "Artist", albumArtist = "Various"))

        assertTrue("artist=Artist" in lines(text))
        assertTrue("album_artist=Various" in lines(text))
    }

    @Test
    fun omitsTheAlbumWhenItIsNullOrBlank() {
        for (album in listOf(null, "", "   ")) {
            val keys = lines(Ffmetadata.render(tags(album = album))).map { it.substringBefore('=') }

            assertEquals(listOf(";FFMETADATA1", "title", "artist", "album_artist", "track"), keys)
        }
    }

    @Test
    fun neverWritesTheCommentBecauseFfmpegWouldStoreItAsTxxx() {
        for (comment in listOf(null, "", "\t ", "https://www.youtube.com/watch?v=abc")) {
            val text = Ffmetadata.render(tags(comment = comment))

            assertFalse(lines(text).any { it.startsWith("comment") }, text)
            assertFalse("youtube" in text, text)
        }
    }

    @Test
    fun writesTheTrackNumberAsAPlainInteger() {
        assertTrue("track=1" in lines(Ffmetadata.render(tags(trackNumber = 1))))
        assertTrue("track=999" in lines(Ffmetadata.render(tags(trackNumber = 999))))
    }

    @Test
    fun escapesEqualsSemicolonHashAndBackslash() {
        val text = Ffmetadata.render(tags(title = "a=b;c#d\\e"))

        assertTrue("""title=a\=b\;c\#d\\e""" in lines(text))
    }

    @Test
    fun escapesALineBreakByKeepingItAfterABackslash() {
        val text = Ffmetadata.render(tags(title = "first\nsecond", album = null))

        assertEquals(
            ";FFMETADATA1\ntitle=first\\\nsecond\nartist=BTS\nalbum_artist=BTS\ntrack=7\n",
            text,
        )
    }

    @Test
    fun escapesACarriageReturnToo() {
        val text = Ffmetadata.render(tags(title = "first\r\nsecond", album = null))

        assertTrue(text.startsWith(";FFMETADATA1\ntitle=first\\\r\\\nsecond\n"), text)
    }

    @Test
    fun aValueEndingInALineBreakStaysOnItsOwnTag() {
        val text = Ffmetadata.render(tags(title = "first\n", album = null))

        assertEquals(";FFMETADATA1\ntitle=first\\\n\nartist=BTS\nalbum_artist=BTS\ntrack=7\n", text)
    }

    @Test
    fun passesKoreanTextAndQuotesThroughUnchanged() {
        val text = Ffmetadata.render(tags(title = "\"Golden\" 'x' 골든 (feat. 지민)", artist = "방탄소년단", album = "한국 노래 모음"))

        assertEquals(
            listOf(
                ";FFMETADATA1",
                "title=\"Golden\" 'x' 골든 (feat. 지민)",
                "artist=방탄소년단",
                "album_artist=방탄소년단",
                "album=한국 노래 모음",
                "track=7",
            ),
            lines(text),
        )
    }

    // ffmpeg treats a line that ends in a backslash as continuing on the next line, even when that backslash is itself
    // escaped, so the next tag would be swallowed into the value. A trailing backslash becomes a full-width one.

    @Test
    fun aTrailingBackslashBecomesAFullWidthBackslash() {
        val text = Ffmetadata.render(tags(title = "foo\\"))

        assertEquals(
            listOf(";FFMETADATA1", "title=foo＼", "artist=BTS", "album_artist=BTS", "album=Best of BTS", "track=7"),
            lines(text),
        )
    }

    @Test
    fun everyTrailingBackslashIsReplacedButInnerOnesAreJustEscaped() {
        val text = Ffmetadata.render(tags(title = "a\\b\\\\", artist = "x\\y\\", album = "plain"))

        assertTrue("title=a\\\\b＼＼" in lines(text), text)
        assertTrue("artist=x\\\\y＼" in lines(text), text)
        assertTrue("album=plain" in lines(text), text)
    }

    @Test
    fun aValueThatIsOnlyABackslashBecomesOnlyAFullWidthBackslash() {
        val text = Ffmetadata.render(tags(title = "\\", artist = "\\\\"))

        assertTrue("title=＼" in lines(text), text)
        assertTrue("artist=＼＼" in lines(text), text)
        assertTrue("album_artist=＼＼" in lines(text), text)
    }

    @Test
    fun noLineEndsInABackslashWhateverTheValues() {
        val nasty = listOf(
            "\\", "a\\", "a\\\\", "a\\\n", "\\\n\\", "a;\\", "a=\\", "end\r\n\\", "\n\\\n",
        )
        for (value in nasty) {
            val text = Ffmetadata.render(tags(title = value, artist = value, album = value))

            // Walk the text the way ffmpeg splits it: a backslash escapes the next character, so only an unescaped
            // line feed ends a line. None of those may directly follow a backslash.
            var lineEnds = 0
            var i = 0
            while (i < text.length) {
                when (text[i]) {
                    '\\' -> i++
                    '\n' -> {
                        lineEnds++
                        assertTrue(text[i - 1] != '\\', "a line ends in a backslash for ${value.length} chars: $text")
                    }
                }
                i++
            }
            assertEquals(6, lineEnds, "header and five tags must stay separate lines: $text")
        }
    }

    @Test
    fun aNulCannotEndALineOrInjectATag() {
        val text = Ffmetadata.render(tags(title = "abc\u0000album=Injected", album = "Real"))

        assertTrue("title=abcalbum\\=Injected" in lines(text), text)
        assertEquals(1, lines(text).count { it.startsWith("album=") })
        assertTrue("album=Real" in lines(text), text)
        assertFalse('\u0000' in text)
    }

    @Test
    fun otherControlCharactersAreDroppedButTabAndLineBreaksStay() {
        val text = Ffmetadata.render(tags(title = "a\u0001b\u0007c\u001Bd\u001Fe\tf\ng\rh", album = null))

        assertTrue(text.startsWith(";FFMETADATA1\ntitle=abcde\tf\\\ng\\\rh\nartist=BTS\n"), text)
    }

    @Test
    fun spacesAndOtherPrintableTextAreKept() {
        val text = Ffmetadata.render(tags(title = " leading and trailing  ", album = "~!@\$%^&*()_+{}|:\"<>?[]'`/,."))

        assertTrue("title= leading and trailing  " in lines(text), text)
        assertTrue("album=~!@\$%^&*()_+{}|:\"<>?[]'`/,." in lines(text), text)
    }
}
