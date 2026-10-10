package com.xgetsongs.engine.testutil

import com.xgetsongs.engine.tags.Ffmetadata
import com.xgetsongs.engine.tags.TrackTags
import kotlin.test.Test
import kotlin.test.assertEquals

class FfmetadataReaderTest {
    @Test
    fun readsBackWhatFfmetadataRenderWrote() {
        val tags = TrackTags(
            title = "a=b;c#d\\e",
            artist = "방탄소년단",
            album = "100% [x] {y} #1; k=v",
            albumArtist = "Various Artists",
            trackNumber = 7,
            comment = "line one\nline two\r\nline three",
            lyrics = "첫 줄\n\nLa la=la 🎵\n마지막",
        )

        assertEquals(
            mapOf(
                "title" to tags.title,
                "artist" to tags.artist,
                "album_artist" to tags.albumArtist,
                "album" to tags.album,
                "track" to "7",
                "comment" to tags.comment,
                "lyrics" to tags.lyrics,
            ),
            FfmetadataReader.read(Ffmetadata.render(tags)),
        )
    }
}
