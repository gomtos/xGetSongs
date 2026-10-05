package com.xgetsongs.app

import java.awt.Desktop
import java.nio.file.Files
import java.nio.file.InvalidPathException
import java.nio.file.Path
import kotlin.concurrent.thread

/**
 * The folder to show for [path]: the first existing directory on the way up from it (the path itself when it is a
 * directory, the parent of a regular file, then the parents, up to the root), or null for a blank path, one the platform
 * rejects, or one whose whole chain is missing (a drive that does not exist). A relative path is read from the working
 * folder, which is where the server would put the files too. Never throws.
 */
internal fun existingFolderFor(path: String): Path? {
    if (path.isBlank()) return null
    var candidate: Path? = try {
        Path.of(path).toAbsolutePath()
    } catch (e: InvalidPathException) {
        return null
    }
    while (candidate != null) {
        if (Files.isDirectory(candidate)) return candidate
        candidate = candidate.parent
    }
    return null
}

/**
 * Shows [existingFolderFor] of [path] in the system file manager; does nothing when there is no such folder. All of it
 * runs on a daemon thread of its own, so the UI never waits for the file manager (or for a network share that does not
 * answer). Opening the folder is only a convenience, so every failure is swallowed.
 */
internal fun openInExplorer(path: String) {
    thread(isDaemon = true, name = "open-in-explorer") {
        try {
            val folder = existingFolderFor(path) ?: return@thread
            val opened = try {
                Desktop.isDesktopSupported() && Desktop.getDesktop().isSupported(Desktop.Action.OPEN) &&
                    run { Desktop.getDesktop().open(folder.toFile()); true }
            } catch (e: Exception) {
                false
            }
            if (!opened) ProcessBuilder("explorer.exe", folder.toString()).start()
        } catch (e: Exception) {
            // Nothing opens; there is nothing useful to tell the user about it.
        }
    }
}
