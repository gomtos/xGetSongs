package com.xgetsongs.app

import java.io.File
import java.nio.file.FileSystems
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

// None of these tests starts a file manager: openInExplorer is only called with paths that have no folder to open.
class ExplorerOpenerTest {
    private val dir: Path = Files.createTempDirectory("xgs-explorer")

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    @Test
    fun anExistingDirectoryReturnsItself() {
        val folder = Files.createDirectories(dir.resolve("Music"))

        assertEquals(folder, existingFolderFor(folder.toString()))
        assertEquals(dir, existingFolderFor(dir.toString()))
    }

    @Test
    fun aMissingChildOfAnExistingDirectoryReturnsTheParent() {
        val folder = Files.createDirectories(dir.resolve("Music"))

        assertEquals(folder, existingFolderFor(folder.resolve("My Playlist").toString()))
    }

    @Test
    fun severalMissingLevelsReturnTheNearestExistingAncestor() {
        val folder = Files.createDirectories(dir.resolve("Music").resolve("Rock"))

        assertEquals(folder, existingFolderFor(folder.resolve("a").resolve("b").resolve("c").toString()))
        assertEquals(dir, existingFolderFor(dir.resolve("x").resolve("y").toString()))
    }

    @Test
    fun aRegularFileReturnsItsParentDirectory() {
        val folder = Files.createDirectories(dir.resolve("Music"))
        val file = Files.writeString(folder.resolve("001 A - T.mp3"), "data")

        assertEquals(folder, existingFolderFor(file.toString()))
    }

    @Test
    fun aPathBelowARegularFileReturnsTheDirectoryAroundTheFile() {
        val file = Files.writeString(dir.resolve("a-file"), "data")

        assertEquals(dir, existingFolderFor(file.resolve("child").toString()))
    }

    @Test
    fun aBlankPathReturnsNull() {
        assertNull(existingFolderFor(""))
        assertNull(existingFolderFor("   "))
        assertNull(existingFolderFor("\t"))
    }

    @Test
    fun aPathThePlatformRejectsReturnsNullAndDoesNotThrow() {
        assertNull(existingFolderFor("${dir}${File.separator}bad\u0000name"))
        assertNull(existingFolderFor("\u0000"))
    }

    @Test
    fun aPathWithNoExistingAncestorReturnsNull() {
        // Only Windows has roots that can be missing (a drive letter nobody uses); every other root always exists.
        if (File.separatorChar != '\\') return
        val used = FileSystems.getDefault().rootDirectories.map { it.toString().take(1).uppercase() }.toSet()
        val missing = ('A'..'Z').last { it.toString() !in used }

        assertNull(existingFolderFor("$missing:\\Music\\My Playlist"))
        assertNull(existingFolderFor("$missing:\\"))
    }

    @Test
    fun aRelativePathIsLookedUpFromTheWorkingFolder() {
        val missing = "xgs-missing-folder-" + System.nanoTime()

        val found = existingFolderFor(missing)

        assertEquals(Path.of("").toAbsolutePath(), found, "the nearest existing ancestor of a relative path is the working folder")
        assertTrue(found != null && Files.isDirectory(found))
    }

    @Test
    fun openingWithoutAFolderToOpenDoesNothingAndDoesNotThrow() {
        openInExplorer("")
        openInExplorer("   ")
        openInExplorer("\u0000")
    }
}
