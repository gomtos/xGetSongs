package com.xgetsongs.server.sidecar

import com.xgetsongs.shared.api.ApiHeaders
import org.junit.jupiter.api.Tag
import java.io.File
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * Runs the sidecar from the image the shell will start it from: the bundled runtime and the trimmed lib. Run through
 * `:server:sidecarImageTest`, which builds the image first and says where it is.
 */
@Tag("image")
class SidecarImageTest {
    private val image: Path = Path.of(checkNotNull(System.getProperty("xgs.sidecarImageDir")) { "run this through :server:sidecarImageTest" })

    @Test
    fun theImageHoldsNoNativesOfOtherPlatformsAndTheServerItself() {
        val names = Files.list(image.resolve("lib")).use { files -> files.map { it.fileName.toString() }.toList() }

        assertTrue(names.none { "-linux-" in it || "-osx-" in it }, "jars of other platforms: $names")
        assertTrue(names.any { it.startsWith("server") && it.endsWith(".jar") }, "the server jar: $names")
        assertTrue(Files.isRegularFile(image.resolve("runtime").resolve("bin").resolve("java.exe")), "the bundled runtime")
    }

    @Test
    fun theImageHasNoJavaFx() {
        val names = Files.list(image.resolve("lib")).use { files -> files.map { it.fileName.toString() }.toList() }

        // The hidden browser of the Google lyrics lookup is the shell's WebView2 now; JavaFX was 44 MB of the image.
        assertTrue(names.none { "javafx" in it.lowercase() }, "JavaFX jars: $names")
    }

    @Test
    fun theImageCarriesWhatJavaNeedsOnAPcWithoutTheVisualCRuntime() {
        val bin = image.resolve("runtime").resolve("bin")

        // java.exe and jvm.dll import these three. The runtime ships them as `*.dll.ortbak`, which no one loads.
        for (name in listOf("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")) {
            assertTrue(Files.isRegularFile(bin.resolve(name)), "$name next to java.exe")
        }
        val backups = Files.list(bin).use { files -> files.map { it.fileName.toString() }.filter { it.endsWith(".ortbak") }.toList() }
        assertEquals(emptyList(), backups, "files that are renamed back and no longer there under the old name")
    }

    @Test
    fun theImageLeavesOutWhatRunningDoesNotNeed() {
        val runtime = image.resolve("runtime")

        for (name in listOf("lib/src.zip", "lib/ct.sym", "lib/jvm.lib", "jmods", "include")) {
            assertTrue(Files.notExists(runtime.resolve(name)), "$name is not needed to run")
        }
    }

    @Test
    fun theBundledRuntimeRunsTheSidecarAndStopsOnTheExitLine() {
        val base = Files.createTempDirectory("xgs-image-test").toAbsolutePath()
        val logDir = base.resolve("log")
        val java = image.resolve("runtime").resolve("bin").resolve("java.exe").toString()
        val process = ProcessBuilder(
            java, "-Dlogback.configurationFile=logback-sidecar.xml",
            "-cp", image.resolve("lib").toString() + File.separator + "*",
            "com.xgetsongs.server.sidecar.SidecarMainKt",
            "--app-data", base.resolve("data").toString(), "--log-dir", logDir.toString(),
        ).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        try {
            val line = process.inputStream.bufferedReader().readLine()
            assertTrue(line != null && line.startsWith("XGS-READY "), "the handshake line: $line")
            val (port, token) = line.split(' ').let { it[1] to it[2] }

            Thread.sleep(3000)
            assertTrue(process.isAlive, "the process is still running while its stdin is open")
            val request = HttpRequest.newBuilder(URI.create("http://127.0.0.1:$port/tools")).header(ApiHeaders.TOKEN, token).build()
            assertEquals(200, HttpClient.newHttpClient().send(request, HttpResponse.BodyHandlers.ofString()).statusCode(), "the server answers")

            process.outputStream.write("exit\n".toByteArray())
            process.outputStream.flush()
            assertTrue(process.waitFor(30, TimeUnit.SECONDS), "the process ends on the exit line")
            assertEquals(0, process.exitValue())
            assertTrue("JVM 종료 시작: 사용자가 창을 닫음" in Files.readString(logDir.resolve("xgetsongs.log")))
        } finally {
            process.destroyForcibly()
            base.toFile().deleteRecursively()
        }
    }
}
