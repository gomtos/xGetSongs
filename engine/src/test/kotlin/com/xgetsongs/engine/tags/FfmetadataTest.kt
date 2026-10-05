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
                "comment=note",
            ),
            lines(text),
        )
        assertTrue(text.endsWith("\n"))
        assertFalse(text.contains("\r"))
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

            assertEquals(listOf(";FFMETADATA1", "title", "artist", "album_artist", "track", "comment"), keys)
        }
    }

    @Test
    fun omitsTheCommentWhenItIsNullOrBlank() {
        for (comment in listOf(null, "", "\t ")) {
            val keys = lines(Ffmetadata.render(tags(comment = comment))).map { it.substringBefore('=') }

            assertEquals(listOf(";FFMETADATA1", "title", "artist", "album_artist", "album", "track"), keys)
        }
    }

    @Test
    fun writesTheTrackNumberAsAPlainInteger() {
        assertTrue("track=1" in lines(Ffmetadata.render(tags(trackNumber = 1))))
        assertTrue("track=999" in lines(Ffmetadata.render(tags(trackNumber = 999))))
    }

    @Test
    fun escapesEqualsSemicolonHashAndBackslash() {
        val text = Ffmetadata.render(tags(title = "a=b;c#d\\e", comment = "https://www.youtube.com/watch?v=abc"))

        assertTrue("""title=a\=b\;c\#d\\e""" in lines(text))
        assertTrue("""comment=https://www.youtube.com/watch?v\=abc""" in lines(text))
    }

    @Test
    fun escapesALineBreakByKeepingItAfterABackslash() {
        val text = Ffmetadata.render(tags(title = "first\nsecond", comment = null, album = null))

        assertEquals(
            ";FFMETADATA1\ntitle=first\\\nsecond\nartist=BTS\nalbum_artist=BTS\ntrack=7\n",
            text,
        )
    }

    @Test
    fun escapesACarriageReturnToo() {
        val text = Ffmetadata.render(tags(title = "first\r\nsecond", comment = null, album = null))

        assertTrue(text.startsWith(";FFMETADATA1\ntitle=first\\\r\\\nsecond\n"), text)
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
                "comment=note",
            ),
            lines(text),
        )
    }
}
