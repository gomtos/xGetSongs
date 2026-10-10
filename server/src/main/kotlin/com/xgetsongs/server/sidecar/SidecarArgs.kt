package com.xgetsongs.server.sidecar

import java.nio.file.InvalidPathException
import java.nio.file.Path

/**
 * The command line of the sidecar: `--app-data <absolute folder>`. There is no default on purpose: the server clears
 * `<app-data>\work` when it starts, so a guessed folder could be one that another running copy is using.
 */
data class SidecarArgs(val appData: Path) {
    companion object {
        const val USAGE = "사용법: --app-data <절대 경로>"

        /** Null when the arguments are not exactly `--app-data` and an absolute folder. */
        fun parse(args: Array<String>): SidecarArgs? {
            if (args.size != 2 || args[0] != "--app-data" || args[1].isBlank()) return null
            val path = try {
                Path.of(args[1])
            } catch (e: InvalidPathException) {
                return null
            }
            return if (path.isAbsolute) SidecarArgs(path) else null
        }
    }
}
