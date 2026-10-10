package com.xgetsongs.server.sidecar

import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class SidecarArgsTest {
    private val absolute = Files.createTempDirectory("xgs-args").toAbsolutePath()

    @Test
    fun anAbsoluteAppDataFolderIsAccepted() {
        assertEquals(SidecarArgs(absolute), SidecarArgs.parse(arrayOf("--app-data", absolute.toString())))
    }

    @Test
    fun missingOrUnknownOrExtraArgumentsAreRefused() {
        assertNull(SidecarArgs.parse(arrayOf()))
        assertNull(SidecarArgs.parse(arrayOf("--app-data")))
        assertNull(SidecarArgs.parse(arrayOf("--data", absolute.toString())))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--verbose")))
    }

    @Test
    fun aBlankFolderIsRefused() {
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "")))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "   ")))
    }

    @Test
    fun aRelativeFolderIsRefusedBecauseTheServerClearsItsWorkFolderAtStart() {
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "data")))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "..\\data")))
    }

    @Test
    fun aPathThatCannotBeParsedIsRefused() {
        assertNull(SidecarArgs.parse(arrayOf("--app-data", "C:\\bad\u0000path")))
    }

    @Test
    fun aLogFolderIsOptionalAndMayComeFirst() {
        val log = absolute.resolve("log")

        assertEquals(SidecarArgs(absolute, log), SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--log-dir", log.toString())))
        assertEquals(SidecarArgs(absolute, log), SidecarArgs.parse(arrayOf("--log-dir", log.toString(), "--app-data", absolute.toString())))
        assertNull(SidecarArgs(absolute).logDir)
    }

    @Test
    fun aLogFolderWithoutAnAppDataFolderIsRefused() {
        assertNull(SidecarArgs.parse(arrayOf("--log-dir", absolute.toString())))
    }

    @Test
    fun aRelativeOrRepeatedOrMissingLogFolderIsRefused() {
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--log-dir", "log")))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--log-dir")))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--app-data", absolute.toString())))
        assertNull(SidecarArgs.parse(arrayOf("--app-data", absolute.toString(), "--log-dir", absolute.toString(), "--log-dir", absolute.toString())))
    }
}
