package com.xgetsongs.engine.output

import kotlinx.coroutines.test.runTest
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LocalFolderSinkTest {
    private val root: Path = Files.createTempDirectory("xgs-sink")
    private val target = root.resolve("music/out")

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private fun source(content: String): Path {
        val file = Files.createTempFile(root, "src", ".mp3")
        Files.writeString(file, content)
        return file
    }

    @Test
    fun createsTheTargetDirectoryAndMovesTheFile() = runTest {
        val sink = LocalFolderSink(target)
        val source = source("data")

        sink.put("001 A - B.mp3", source, overwrite = false)

        assertEquals("data", Files.readString(target.resolve("001 A - B.mp3")))
        assertFalse(Files.exists(source))
    }

    @Test
    fun existsReflectsTheFolderContent() = runTest {
        val sink = LocalFolderSink(target)
        assertFalse(sink.exists("001 A - B.mp3"))

        sink.put("001 A - B.mp3", source("x"), overwrite = false)

        assertTrue(sink.exists("001 A - B.mp3"))
    }

    @Test
    fun existingFilesAreKeptUnlessOverwriteIsOn() = runTest {
        val sink = LocalFolderSink(target)
        sink.put("a.mp3", source("old"), overwrite = false)

        assertFailsWith<FileAlreadyExistsException> { sink.put("a.mp3", source("new"), overwrite = false) }
        assertEquals("old", Files.readString(target.resolve("a.mp3")))

        sink.put("a.mp3", source("new"), overwrite = true)
        assertEquals("new", Files.readString(target.resolve("a.mp3")))
    }

    @Test
    fun fileNamesWithPathSegmentsAreRejected() = runTest {
        val sink = LocalFolderSink(target)

        assertFailsWith<IllegalArgumentException> { sink.put("../evil.mp3", source("x"), overwrite = false) }
        assertFailsWith<IllegalArgumentException> { sink.put("sub/evil.mp3", source("x"), overwrite = false) }
        assertFailsWith<IllegalArgumentException> { sink.exists("..") }
    }
}
