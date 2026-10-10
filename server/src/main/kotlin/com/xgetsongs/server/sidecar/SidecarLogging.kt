package com.xgetsongs.server.sidecar

import java.nio.file.Path

/**
 * The folders the sidecar tries for its log files, in order: the one the shell asked for, `logs` in the app data folder,
 * and the temp folder (an install folder can be read-only). The first one that can be made is used.
 */
internal fun sidecarLogCandidates(args: SidecarArgs, tempDir: Path): List<Path> =
    listOfNotNull(args.logDir, args.appData.resolve("logs"), tempDir.resolve("xgetsongs-logs"))
