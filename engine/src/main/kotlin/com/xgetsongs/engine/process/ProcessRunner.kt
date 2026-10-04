package com.xgetsongs.engine.process

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withContext

/** Runs an external program. Implementations must never pass [command] through a shell. */
interface ProcessRunner {
    /**
     * Runs [command], calling [onStdout]/[onStderr] once per output line (possibly from different
     * threads), and returns the exit code. Cancelling the calling coroutine kills the whole process tree.
     */
    suspend fun run(
        command: List<String>,
        onStdout: (String) -> Unit = {},
        onStderr: (String) -> Unit = {},
    ): Int
}

class SystemProcessRunner : ProcessRunner {
    override suspend fun run(
        command: List<String>,
        onStdout: (String) -> Unit,
        onStderr: (String) -> Unit,
    ): Int = withContext(Dispatchers.IO) {
        val builder = ProcessBuilder(command)
        builder.environment()["PYTHONIOENCODING"] = "utf-8"
        builder.environment()["PYTHONUTF8"] = "1"
        val process = builder.start()
        process.outputStream.close()

        coroutineScope {
            val stdout = async { process.inputStream.bufferedReader(Charsets.UTF_8).forEachLine(onStdout) }
            val stderr = async { process.errorStream.bufferedReader(Charsets.UTF_8).forEachLine(onStderr) }
            try {
                val exitCode = runInterruptible { process.waitFor() }
                stdout.await()
                stderr.await()
                exitCode
            } finally {
                // Killing the process closes its streams, which lets the blocked readers finish.
                if (process.isAlive) {
                    process.descendants().forEach { it.destroyForcibly() }
                    process.destroyForcibly()
                }
            }
        }
    }
}
