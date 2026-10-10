package com.xgetsongs.server.sidecar

import java.io.IOException
import java.io.InputStream
import kotlin.concurrent.thread

/**
 * Tells once that the shell that owns the other end of the sidecar's stdin is gone, and whether it asked for that.
 *
 * The shell writes a line `exit` when the user closes the window; that line alone ends the watch (`onGone(true)`). When the
 * stream just ends or fails without it, the shell died or was killed and the operating system closed the pipe
 * (`onGone(false)`). The exit record of the log tells the two apart by this.
 */
class ParentWatch(private val input: InputStream, private val onGone: (userRequested: Boolean) -> Unit) {
    /** Starts the watching thread. It is a daemon: it never keeps the JVM alive. */
    fun start(): Thread = thread(name = "parent-watch", isDaemon = true) {
        var userRequested = false
        try {
            val reader = input.bufferedReader(Charsets.UTF_8)
            while (true) {
                val line = reader.readLine() ?: break
                if (line.trim() == EXIT_LINE) {
                    userRequested = true
                    break
                }
                // Any other line is not interpreted: only the exit line and the end of the stream matter.
            }
        } catch (e: IOException) {
            // A stream that fails is as good as one that ended.
        }
        onGone(userRequested)
    }

    companion object {
        /** The line the shell writes before it closes the pipe when the user asked for the end. */
        const val EXIT_LINE = "exit"
    }
}
