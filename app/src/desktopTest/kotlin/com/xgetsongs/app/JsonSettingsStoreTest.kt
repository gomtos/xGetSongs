package com.xgetsongs.app

import com.xgetsongs.app.settings.UserSettings
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class JsonSettingsStoreTest {
    private val dir: Path = Files.createTempDirectory("xgs-settings")
    private val file = dir.resolve("settings.json")
    private val store = JsonSettingsStore(file)

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    private fun write(text: String) = Files.write(file, text.toByteArray(Charsets.UTF_8))

    private fun filesIn(directory: Path): List<String> =
        Files.list(directory).use { stream -> stream.map { it.fileName.toString() }.sorted().toList() }

    // ---- load -------------------------------------------------------------------------------

    @Test
    fun aMissingFileGivesTheDefaults() {
        assertEquals(UserSettings(), store.load())
    }

    @Test
    fun theDefaultsAreWhatTheSpecSays() {
        assertEquals(UserSettings(outputDir = null, overwrite = false, includeRank = true, concurrency = 2), UserSettings())
    }

    @Test
    fun savedSettingsComeBackUnchangedIncludingAKoreanPath() {
        val settings = UserSettings(outputDir = "D:\\음악\\내 재생목록", overwrite = true, includeRank = false, concurrency = 3)

        store.save(settings)

        assertEquals(settings, store.load())
        assertEquals(settings, JsonSettingsStore(file).load(), "a second store reads the same file")
        assertTrue(Files.readString(file, Charsets.UTF_8).contains("내 재생목록"), "the file is UTF-8 text")
    }

    @Test
    fun defaultValuesAreWrittenOutToo() {
        store.save(UserSettings())

        val text = Files.readString(file, Charsets.UTF_8)
        for (key in listOf("outputDir", "overwrite", "includeRank", "concurrency")) assertTrue("\"$key\"" in text, "$key in $text")
    }

    @Test
    fun filesThatCannotBeUsedGiveTheDefaultsAndNeverThrow() {
        val cases = mapOf(
            "corrupt JSON" to "{",
            "empty file" to "",
            "blank file" to "  \n ",
            "wrong shape" to "[]",
            "a bare value" to "42",
            "the null literal" to "null",
            "text that is not JSON" to "hello",
            "a field of the wrong type" to """{"overwrite":"yes"}""",
            "a number where the folder should be" to """{"outputDir":5}""",
            "a concurrency too big for an Int" to """{"concurrency":99999999999}""",
        )
        for ((name, content) in cases) {
            write(content)

            assertEquals(UserSettings(), store.load(), name)
        }
    }

    @Test
    fun bytesThatAreNotUtf8GiveTheDefaults() {
        Files.write(file, byteArrayOf(0xFF.toByte(), 0xFE.toByte(), 0x7B, 0xC3.toByte()))

        assertEquals(UserSettings(), store.load())
    }

    @Test
    fun aFolderInPlaceOfTheFileGivesTheDefaults() {
        Files.createDirectory(file)

        assertEquals(UserSettings(), store.load())
    }

    @Test
    fun unknownKeysAreIgnoredAndTheKnownOnesStillRead() {
        write("""{"outputDir":"D:/x","future":{"a":[1,2]},"overwrite":true,"theme":"dark","includeRank":false,"concurrency":3}""")

        assertEquals(UserSettings(outputDir = "D:/x", overwrite = true, includeRank = false, concurrency = 3), store.load())
    }

    @Test
    fun missingKeysTakeTheirDefaults() {
        write("""{"overwrite":true}""")

        assertEquals(UserSettings(overwrite = true), store.load())
    }

    @Test
    fun theConcurrencyIsBroughtIntoOneToFour() {
        for ((stored, expected) in mapOf(9 to 4, 5 to 4, 4 to 4, 1 to 1, 0 to 1, -3 to 1)) {
            write("""{"concurrency":$stored}""")

            assertEquals(expected, store.load().concurrency, "stored $stored")
        }
    }

    @Test
    fun aBlankStoredFolderIsKeptAsItIsForTheCallerToReplace() {
        write("""{"outputDir":"   "}""")
        assertEquals("   ", store.load().outputDir)

        write("""{"outputDir":""}""")
        assertEquals("", store.load().outputDir)

        write("""{"outputDir":null}""")
        assertEquals(null, store.load().outputDir)
    }

    // ---- save -------------------------------------------------------------------------------

    @Test
    fun saveCreatesTheParentDirectories() {
        val nested = JsonSettingsStore(dir.resolve("a").resolve("b").resolve("settings.json"))
        val settings = UserSettings(outputDir = "D:/Songs", overwrite = true)

        nested.save(settings)

        assertEquals(settings, nested.load())
        assertEquals(listOf("settings.json"), filesIn(dir.resolve("a").resolve("b")))
    }

    @Test
    fun saveLeavesNoTemporaryFileBehind() {
        store.save(UserSettings(outputDir = "D:/Songs"))
        store.save(UserSettings(outputDir = "D:/Other"))

        assertEquals(listOf("settings.json"), filesIn(dir))
    }

    @Test
    fun saveReplacesAnExistingFile() {
        write("""{"outputDir":"D:/old","overwrite":true,"includeRank":false,"concurrency":4,"future":1}""")
        val settings = UserSettings(outputDir = "D:/new", overwrite = false, includeRank = true, concurrency = 1)

        store.save(settings)

        assertEquals(settings, store.load())
        assertEquals(listOf("settings.json"), filesIn(dir))
        assertTrue("future" !in Files.readString(file, Charsets.UTF_8), "the old content is gone")
    }

    @Test
    fun saveIntoALocationThatCannotBeWrittenDoesNotThrow() {
        val blocker = Files.write(dir.resolve("not-a-folder"), byteArrayOf(1))
        val broken = JsonSettingsStore(blocker.resolve("settings.json"))

        broken.save(UserSettings(outputDir = "D:/Songs"))

        assertEquals(UserSettings(), broken.load())
        assertEquals(listOf("not-a-folder"), filesIn(dir), "nothing else was created")
    }

    @Test
    fun saveOverADirectoryThatIsNotEmptyDoesNotThrowAndLeavesNoTemporaryFile() {
        Files.createDirectory(file)
        Files.write(file.resolve("keep.txt"), byteArrayOf(1))

        store.save(UserSettings(outputDir = "D:/Songs"))

        assertEquals(listOf("settings.json"), filesIn(dir))
        assertEquals(listOf("keep.txt"), filesIn(file))
    }
}
