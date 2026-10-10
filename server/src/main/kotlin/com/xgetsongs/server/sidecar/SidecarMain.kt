package com.xgetsongs.server.sidecar

import com.xgetsongs.server.LocalServer
import java.io.InputStream
import java.io.PrintStream
import java.nio.file.Files
import java.nio.file.Path
import kotlin.system.exitProcess

// No logger is declared in this file: logback reads its configuration the first time anything asks for one, and the
// start script already names the file (-Dlogback.configurationFile=logback-sidecar.xml, see build.gradle.kts).

/**
 * Starts the server for the shell that owns this process: tells it where the server is on stdout, and stops when
 * [stdin] ends or carries an `exit` line. [onParentGone] is told whether the shell asked for the end (the `exit` line)
 * before the server is stopped. [exit] ends the process; the tests pass a recorder, so this function returns after it.
 *
 * Returns the thread that watches [stdin], or null when the sidecar did not start. The caller has to wait for that thread:
 * the server's own threads are daemons, so nothing else would keep the JVM from shutting down (and taking the server with
 * it) as soon as main returns.
 */
internal fun runSidecar(
    args: Array<String>,
    stdin: InputStream,
    stdout: PrintStream,
    stderr: PrintStream,
    startServer: (Path) -> LocalServer,
    onParentGone: (userRequested: Boolean) -> Unit = {},
    exit: (Int) -> Unit,
): Thread? {
    val parsed = SidecarArgs.parse(args)
    if (parsed == null) {
        stderr.println(SidecarArgs.USAGE)
        exit(2)
        return null
    }
    val server = try {
        Files.createDirectories(parsed.appData)
        startServer(parsed.appData)
    } catch (e: Exception) {
        stderr.println("사이드카를 시작하지 못했습니다: ${e.javaClass.simpleName}: ${e.message}")
        exit(1)
        return null
    }
    stdout.println(Handshake.line(server.port, server.token))
    stdout.flush()
    return ParentWatch(stdin) { userRequested ->
        onParentGone(userRequested)
        server.stop()
        exit(0)
    }.start()
}

fun main(args: Array<String>) {
    // The arguments are read first, without a logger: logback learns the log folder from them (see SidecarDiagnostics).
    val diagnostics = SidecarArgs.parse(args)?.let { SidecarDiagnostics.start(it) }
    // Waiting for the watching thread keeps the JVM alive until the parent is gone; that thread ends the process itself.
    runSidecar(
        args, System.`in`, System.out, System.err,
        startServer = { appData ->
            LocalServer.start(appData).also { server -> diagnostics?.runningJobs = server::runningJobs }
        },
        onParentGone = { userRequested -> if (userRequested) diagnostics?.markUserExit() },
        exit = { code ->
            diagnostics?.stop()
            exitProcess(code)
        },
    )?.join()
}
