package com.xgetsongs.engine.ytdlp

import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class VideoInfoFileTest {
    private val dir: Path = Files.createTempDirectory("xgs-info")
    private val file = dir.resolve("vid00000001.info.json")

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    /** Writes [json] as UTF-8 into [file] and reads the album from it. */
    private fun albumOf(json: String): String? {
        Files.writeString(file, json)
        return VideoInfoFile.readAlbum(file)
    }

    @Test
    fun readsTheAlbumOfTheVideo() {
        assertEquals("Palette", albumOf("""{"id":"vid00000001","title":"Palette (Official)","album":"Palette"}"""))
    }

    @Test
    fun trimsTheAlbum() {
        assertEquals("Love poem", albumOf("""{"album":"  Love poem \t"}"""))
    }

    @Test
    fun readsAKoreanAlbum() {
        assertEquals("사랑의 시", albumOf("""{"album":"사랑의 시"}"""))
    }

    @Test
    fun decodesEscapesInTheAlbum() {
        assertEquals("Love \"poem\" \u00e9", albumOf("""{"album":"Love \"poem\" \u00e9"}"""))
    }

    @Test
    fun onlyTheAlbumOfTheRootObjectCounts() {
        assertNull(albumOf("""{"formats":[{"album":"X"}],"meta":{"album":"Y"}}"""))
    }

    @Test
    fun aMissingAlbumKeyGivesNull() {
        assertNull(albumOf("""{"id":"vid00000001","artist":"IU","track":"Palette"}"""))
    }

    @Test
    fun aNullAlbumGivesNull() {
        assertNull(albumOf("""{"album":null}"""))
    }

    @Test
    fun aNumberAlbumGivesNull() {
        assertNull(albumOf("""{"album":2024}"""))
    }

    @Test
    fun anAlbumThatIsNotAStringGivesNull() {
        assertNull(albumOf("""{"album":["Palette"]}"""))
        assertNull(albumOf("""{"album":{"name":"Palette"}}"""))
        assertNull(albumOf("""{"album":true}"""))
    }

    @Test
    fun aBlankAlbumGivesNull() {
        assertNull(albumOf("""{"album":""}"""))
        assertNull(albumOf("""{"album":"  \t "}"""))
    }

    @Test
    fun theLiteralNAPlaceholderGivesNull() {
        assertNull(albumOf("""{"album":"NA"}"""))
        assertNull(albumOf("""{"album":" NA "}"""))
    }

    @Test
    fun textThatMerelyContainsNAIsAnAlbum() {
        assertEquals("NA Nights", albumOf("""{"album":"NA Nights"}"""))
        assertEquals("Na", albumOf("""{"album":"Na"}"""))
    }

    @Test
    fun invalidJsonGivesNull() {
        assertNull(albumOf("""{"album":"Palette""""))
        assertNull(albumOf("not json at all"))
    }

    @Test
    fun anEmptyFileGivesNull() {
        assertNull(albumOf(""))
    }

    @Test
    fun aRootThatIsNotAnObjectGivesNull() {
        assertNull(albumOf("""[{"album":"Palette"}]"""))
        assertNull(albumOf(""""Palette""""))
    }

    @Test
    fun aMissingFileGivesNull() {
        assertNull(VideoInfoFile.readAlbum(dir.resolve("nothing.info.json")))
    }

    @Test
    fun aDirectoryInsteadOfAFileGivesNull() {
        assertNull(VideoInfoFile.readAlbum(dir))
    }

    @Test
    fun bytesThatAreNotUtf8GiveNull() {
        // 0xC3 0x28 is not valid UTF-8; a lenient decoder would turn it into U+FFFD and still find an album.
        val bytes = """{"album":"Pal""".toByteArray() + byteArrayOf(0xC3.toByte(), 0x28) + """ette"}""".toByteArray()
        Files.write(file, bytes)

        assertNull(VideoInfoFile.readAlbum(file))
    }

    // ---- read: the album and the description together ----

    /** Writes [json] as UTF-8 into [file] and reads everything the engine needs from it. */
    private fun infoOf(json: String): VideoInfo {
        Files.writeString(file, json)
        return VideoInfoFile.read(file)
    }

    private val nothing = VideoInfo(album = null, description = null)

    @Test
    fun readReturnsTheAlbumAndTheDescriptionTogether() {
        val info = infoOf("""{"id":"vid00000001","album":" Palette ","description":"[Lyrics]\nLine one\nLine two"}""")

        assertEquals(VideoInfo(album = "Palette", description = "[Lyrics]\nLine one\nLine two"), info)
    }

    @Test
    fun readKeepsTheInnerLineBreaksAndDecodesEscapesOfTheDescription() {
        val info = infoOf("""{"description":"첫 줄\r\n\"둘째\" 줄\n\né 🎵"}""")

        assertEquals("첫 줄\r\n\"둘째\" 줄\n\né 🎵", info.description)
        assertNull(info.album)
    }

    @Test
    fun readDropsALeadingAndTrailingBlankRunOfTheDescriptionAndNothingElse() {
        assertEquals("  Line one\nLine two", infoOf("""{"description":"\n \n  Line one\nLine two\n\n  \t\n"}""").description)
        assertEquals("a  b", infoOf("""{"description":"a  b"}""").description)
    }

    @Test
    fun readGivesAnAlbumWithoutADescriptionAndTheOtherWayRound() {
        assertEquals(VideoInfo("Palette", null), infoOf("""{"album":"Palette"}"""))
        assertEquals(VideoInfo(null, "Some text"), infoOf("""{"description":"Some text"}"""))
    }

    @Test
    fun aMissingDescriptionGivesNull() {
        assertNull(infoOf("""{"id":"vid00000001","album":"Palette"}""").description)
    }

    @Test
    fun aNullDescriptionGivesNull() {
        assertNull(infoOf("""{"description":null}""").description)
    }

    @Test
    fun aDescriptionThatIsNotAStringGivesNull() {
        assertNull(infoOf("""{"description":2024}""").description)
        assertNull(infoOf("""{"description":true}""").description)
        assertNull(infoOf("""{"description":["Line one"]}""").description)
        assertNull(infoOf("""{"description":{"text":"Line one"}}""").description)
    }

    @Test
    fun aBlankDescriptionGivesNull() {
        assertNull(infoOf("""{"description":""}""").description)
        assertNull(infoOf("""{"description":"  \t \n \r\n "}""").description)
    }

    @Test
    fun onlyTheDescriptionOfTheRootObjectCounts() {
        val info = infoOf("""{"formats":[{"description":"X"}],"meta":{"description":"Y"},"album":"Palette"}""")

        assertEquals(VideoInfo("Palette", null), info)
    }

    @Test
    fun readGivesNothingForAnyFileItCannotUse() {
        assertEquals(nothing, infoOf("""{"album":"Palette","description":"Line one""""))
        assertEquals(nothing, infoOf("not json at all"))
        assertEquals(nothing, infoOf(""))
        assertEquals(nothing, infoOf("""[{"album":"Palette","description":"Line one"}]"""))
        assertEquals(nothing, VideoInfoFile.read(dir.resolve("nothing.info.json")))
        assertEquals(nothing, VideoInfoFile.read(dir))
        Files.write(file, """{"album":"Pal""".toByteArray() + byteArrayOf(0xC3.toByte(), 0x28) + """ette","description":"Line one"}""".toByteArray())
        assertEquals(nothing, VideoInfoFile.read(file))
    }

    @Test
    fun readAlbumGivesTheAlbumOfRead() {
        Files.writeString(file, """{"album":"  Palette ","description":"Line one"}""")

        assertEquals(VideoInfoFile.read(file).album, VideoInfoFile.readAlbum(file))
        assertEquals("Palette", VideoInfoFile.readAlbum(file))
    }

    @Test
    fun readAppliesTheAlbumRulesOfReadAlbum() {
        assertNull(infoOf("""{"album":"NA","description":"Line one"}""").album)
        assertNull(infoOf("""{"album":"  ","description":"Line one"}""").album)
        assertNull(infoOf("""{"album":2024,"description":"Line one"}""").album)
        assertEquals("Line one", infoOf("""{"album":"NA","description":"Line one"}""").description)
    }

    @Test
    fun aLargeFileWithManyOtherFieldsStillWorks() {
        // A real info file is several hundred KB because of the formats list.
        val formats = (1..1000).joinToString(",") { """{"format_id":"$it","url":"https://example.invalid/videoplayback?id=$it&sig=${"x".repeat(150)}","height":$it}""" }
        val json = """{"id":"vid00000001","formats":[$formats],"album":"Palette","description":"${"d".repeat(50_000)}"}"""
        Files.writeString(file, json)
        assertTrue(Files.size(file) > 200_000, "the test file must be large: ${Files.size(file)} bytes")

        assertEquals("Palette", VideoInfoFile.readAlbum(file))
        assertEquals(VideoInfo("Palette", "d".repeat(50_000)), VideoInfoFile.read(file))
    }
}
