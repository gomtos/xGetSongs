package com.xgetsongs.app

import com.xgetsongs.app.settings.UserSettings
import java.lang.reflect.Proxy
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
        assertEquals(
            UserSettings(outputDir = null, overwrite = false, includeRank = true, concurrency = 2, searchLyricsOnline = true),
            UserSettings(),
        )
        assertTrue(UserSettings().searchLyricsOnline, "the lyrics search is on until the user turns it off")
    }

    @Test
    fun savedSettingsComeBackUnchangedIncludingAKoreanPath() {
        val settings = UserSettings(outputDir = "D:\\음악\\내 재생목록", overwrite = true, includeRank = false, concurrency = 3, searchLyricsOnline = false)

        store.save(settings)

        assertEquals(settings, store.load())
        assertEquals(settings, JsonSettingsStore(file).load(), "a second store reads the same file")
        assertTrue(Files.readString(file, Charsets.UTF_8).contains("내 재생목록"), "the file is UTF-8 text")
    }

    @Test
    fun defaultValuesAreWrittenOutToo() {
        store.save(UserSettings())

        val text = Files.readString(file, Charsets.UTF_8)
        for (key in listOf("outputDir", "overwrite", "includeRank", "concurrency", "searchLyricsOnline")) {
            assertTrue("\"$key\"" in text, "$key in $text")
        }
        assertTrue("\"searchLyricsOnline\": true" in text, text)
    }

    @Test
    fun anOldFileWithoutTheLyricsKeyLoadsWithTheSearchOn() {
        write("""{"outputDir":"D:/x","overwrite":true,"includeRank":false,"concurrency":3}""")

        assertEquals(
            UserSettings(outputDir = "D:/x", overwrite = true, includeRank = false, concurrency = 3, searchLyricsOnline = true),
            store.load(),
        )
        assertTrue(store.load().searchLyricsOnline)
    }

    @Test
    fun aStoredSearchLyricsOnlineValueIsRead() {
        write("""{"searchLyricsOnline":false}""")
        assertEquals(false, store.load().searchLyricsOnline)

        write("""{"overwrite":true,"searchLyricsOnline":true,"concurrency":4}""")
        assertEquals(UserSettings(overwrite = true, concurrency = 4, searchLyricsOnline = true), store.load())
    }

    @Test
    fun aTurnedOffLyricsSearchIsWrittenAndComesBack() {
        store.save(UserSettings(searchLyricsOnline = false))

        assertTrue("\"searchLyricsOnline\": false" in Files.readString(file, Charsets.UTF_8))
        assertEquals(false, JsonSettingsStore(file).load().searchLyricsOnline)
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
            "a lyrics option of the wrong type" to """{"overwrite":true,"searchLyricsOnline":"no"}""",
            "a number where the folder should be" to """{"outputDir":5}""",
            "a concurrency too big for an Int" to """{"concurrency":99999999999}""",
        )
        for ((name, content) in cases) {
            write(content)

            assertEquals(UserSettings(), store.load(), name)
        }
    }

    @Test
    fun validJsonWithBytesThatAreNotUtf8GivesTheDefaultsInsteadOfAGarbledFolder() {
        // The syntax is fine and only the encoding is wrong: what an ANSI/CP949 editor writes for a Korean folder name
        // (0xC0 0xBD 0xBE 0xC7) is not UTF-8, and must not turn into U+FFFD characters in the output folder.
        val body = """{"outputDir":"D:\\""".toByteArray(Charsets.UTF_8) +
            byteArrayOf(0xC0.toByte(), 0xBD.toByte(), 0xBE.toByte(), 0xC7.toByte()) +
            """","overwrite":true,"concurrency":4}""".toByteArray(Charsets.UTF_8)
        Files.write(file, body)

        assertEquals(UserSettings(), store.load())
    }

    @Test
    fun aUtf16FileGivesTheDefaults() {
        // What Windows PowerShell's Out-File writes by default: a byte order mark, then two bytes per character.
        Files.write(file, """{"overwrite":true}""".toByteArray(Charsets.UTF_16LE).let { byteArrayOf(0xFF.toByte(), 0xFE.toByte()) + it })

        assertEquals(UserSettings(), store.load())
    }

    @Test
    fun aUtf8ByteOrderMarkInFrontOfValidJsonIsIgnored() {
        val json = """{"outputDir":"D:\\음악","overwrite":true,"includeRank":false,"concurrency":3}"""
        Files.write(file, byteArrayOf(0xEF.toByte(), 0xBB.toByte(), 0xBF.toByte()) + json.toByteArray(Charsets.UTF_8))

        assertEquals(UserSettings(outputDir = "D:\\음악", overwrite = true, includeRank = false, concurrency = 3), store.load())
    }

    @Test
    fun aFolderInPlaceOfTheFileGivesTheDefaults() {
        Files.createDirectory(file)

        assertEquals(UserSettings(), store.load())
    }

    @Test
    fun unknownKeysAreIgnoredAndTheKnownOnesStillRead() {
        write("""{"outputDir":"D:/x","future":{"a":[1,2]},"overwrite":true,"theme":"dark","includeRank":false,"concurrency":3,"searchLyricsOnline":false}""")

        assertEquals(UserSettings(outputDir = "D:/x", overwrite = true, includeRank = false, concurrency = 3, searchLyricsOnline = false), store.load())
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
        write("""{"outputDir":"D:/old","overwrite":true,"includeRank":false,"concurrency":4,"searchLyricsOnline":false,"future":1}""")
        val settings = UserSettings(outputDir = "D:/new", overwrite = false, includeRank = true, concurrency = 1, searchLyricsOnline = true)

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
    fun anUnexpectedRuntimeFailureNeverEscapesFromLoadOrSave() {
        // A path whose every operation fails with a plain runtime exception, not an IOException.
        val broken = Proxy.newProxyInstance(Path::class.java.classLoader, arrayOf(Path::class.java)) { _, method, _ ->
            if (method.name == "toString") "broken path" else throw IllegalStateException("${method.name} failed")
        } as Path
        val brokenStore = JsonSettingsStore(broken)

        brokenStore.save(UserSettings(outputDir = "D:/Songs", overwrite = true))

        assertEquals(UserSettings(), brokenStore.load())
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
