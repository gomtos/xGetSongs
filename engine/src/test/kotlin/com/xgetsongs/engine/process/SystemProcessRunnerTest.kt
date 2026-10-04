package com.xgetsongs.engine.process

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SystemProcessRunnerTest {
    private val dir: Path = Files.createTempDirectory("xgs-runner")
    private val java: String = Path.of(System.getProperty("java.home"), "bin", "java").toString()
    private val runner = SystemProcessRunner()

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    private fun source(name: String, body: String): String {
        val file = dir.resolve("$name.java")
        Files.writeString(file, "public class $name { public static void main(String[] args) throws Exception { $body } }")
        return file.toString()
    }

    @Test
    fun capturesOutputLinesAndExitCode() = runBlocking {
        val out = mutableListOf<String>()
        val err = mutableListOf<String>()
        val file = source("Hello", """System.out.println("out-line"); System.err.println("err-line"); System.exit(3);""")

        val exitCode = runner.run(listOf(java, file), { out += it }, { err += it })

        assertEquals(3, exitCode)
        assertEquals(listOf("out-line"), out)
        assertEquals(listOf("err-line"), err)
    }

    @Test
    fun decodesOutputAsUtf8() = runBlocking {
        val out = mutableListOf<String>()
        val file = source("Korean", """System.out.println("한글 제목");""")

        runner.run(listOf(java, "-Dstdout.encoding=UTF-8", file), { out += it })

        assertEquals(listOf("한글 제목"), out)
    }

    @Test
    fun cancellingTheCallerKillsTheProcess() = runBlocking {
        val started = CompletableDeferred<Unit>()
        val file = source("Sleeper", """System.out.println("started"); Thread.sleep(60000);""")
        val job = launch {
            runner.run(listOf(java, file), { if (it == "started") started.complete(Unit) })
        }
        withTimeout(30_000) { started.await() }

        val begin = System.nanoTime()
        withTimeout(15_000) { job.cancelAndJoin() }
        val elapsedSeconds = (System.nanoTime() - begin) / 1_000_000_000.0

        assertTrue(elapsedSeconds < 10, "cancel took $elapsedSeconds s; the process was probably not killed")
    }
}
