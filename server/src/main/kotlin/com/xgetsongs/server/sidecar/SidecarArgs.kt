package com.xgetsongs.server.sidecar

import java.nio.file.InvalidPathException
import java.nio.file.Path

/**
 * The command line of the sidecar: `--app-data <absolute folder>` and, optionally, `--log-dir <absolute folder>`, in either
 * order. There is no default for the app data folder on purpose: the server clears `<app-data>\work` when it starts, so a
 * guessed folder could be one that another running copy is using. The log folder is where the shell wants the log files
 * (next to its own executable); without it the sidecar logs into `logs` of the app data folder.
 */
data class SidecarArgs(val appData: Path, val logDir: Path? = null) {
    companion object {
        const val USAGE = "사용법: --app-data <절대 경로> [--log-dir <절대 경로>]"

        /** Null when the arguments are not `--app-data` and, at most once each, `--log-dir`, with absolute folders. */
        fun parse(args: Array<String>): SidecarArgs? {
            var appData: Path? = null
            var logDir: Path? = null
            var index = 0
            while (index < args.size) {
                val path = absolute(args.getOrNull(index + 1)) ?: return null
                when (args[index]) {
                    "--app-data" -> {
                        if (appData != null) return null
                        appData = path
                    }
                    "--log-dir" -> {
                        if (logDir != null) return null
                        logDir = path
                    }
                    else -> return null
                }
                index += 2
            }
            return appData?.let { SidecarArgs(it, logDir) }
        }

        private fun absolute(text: String?): Path? {
            if (text == null || text.isBlank()) return null
            val path = try {
                Path.of(text)
            } catch (e: InvalidPathException) {
                return null
            }
            return path.takeIf { it.isAbsolute }
        }
    }
}
