package com.xgetsongs.server.sidecar

import java.io.IOException
import java.io.InputStream
import kotlin.concurrent.thread

/**
 * Calls [onGone] once when [input] ends or fails. The shell that owns the other end of the sidecar's stdin closes it to ask
 * the sidecar to stop, and the operating system closes it when the shell dies, so this one signal covers both.
 */
class ParentWatch(private val input: InputStream, private val onGone: () -> Unit) {
    /** Starts the watching thread. It is a daemon: it never keeps the JVM alive. */
    fun start(): Thread = thread(name = "parent-watch", isDaemon = true) {
        val buffer = ByteArray(256)
        try {
            while (input.read(buffer) != -1) {
                // What the shell writes is not interpreted: only the end of the stream matters.
            }
        } catch (e: IOException) {
            // A stream that fails is as good as one that ended.
        }
        onGone()
    }
}
