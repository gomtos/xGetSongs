package com.xgetsongs.server.sidecar

import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals

class SidecarLoggingTest {
    private val temp = Files.createTempDirectory("xgs-logging").toAbsolutePath()
    private val appData = temp.resolve("data")

    @Test
    fun theFolderTheShellAskedForComesFirstThenTheAppDataFolderThenTheTempFolder() {
        val log = temp.resolve("shell-log")

        assertEquals(
            listOf(log, appData.resolve("logs"), temp.resolve("xgetsongs-logs")),
            sidecarLogCandidates(SidecarArgs(appData, log), temp),
        )
    }

    @Test
    fun withoutAFolderFromTheShellTheAppDataFolderIsTriedFirst() {
        assertEquals(
            listOf(appData.resolve("logs"), temp.resolve("xgetsongs-logs")),
            sidecarLogCandidates(SidecarArgs(appData), temp),
        )
    }
}
