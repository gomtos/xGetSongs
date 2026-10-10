package com.xgetsongs.server.sidecar

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ParentWatchTest {
    private val gone = CountDownLatch(1)
    private val reasons = CopyOnWriteArrayList<Boolean>()

    private fun watch(input: InputStream) = ParentWatch(input) { userRequested ->
        reasons += userRequested
        gone.countDown()
    }.start()

    @Test
    fun anInputThatEndsAtOnceMeansTheParentIsGone() {
        watch(ByteArrayInputStream(ByteArray(0)))

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(false), reasons.toList())
    }

    @Test
    fun bytesFromTheParentAreIgnoredUntilTheEnd() {
        val writer = PipedOutputStream()
        watch(PipedInputStream(writer))

        writer.write("ping\n".toByteArray())
        writer.flush()
        assertFalse(gone.await(300, TimeUnit.MILLISECONDS), "the pipe is still open")

        writer.close() // the parent goes away
        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(false), reasons.toList())
    }

    @Test
    fun aFailingInputAlsoMeansTheParentIsGone() {
        watch(object : InputStream() {
            override fun read(): Int = throw IOException("pipe broken")
        })

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(false), reasons.toList())
    }

    @Test
    fun theWatchingThreadNeverKeepsTheJvmAlive() {
        val writer = PipedOutputStream()
        val thread = watch(PipedInputStream(writer))

        assertTrue(thread.isDaemon)
        writer.close()
        assertTrue(gone.await(30, TimeUnit.SECONDS))
    }

    @Test
    fun anExitLineMeansTheUserAskedForTheEndAndDoesNotWaitForTheStreamToEnd() {
        val writer = PipedOutputStream()
        watch(PipedInputStream(writer))

        writer.write("exit\n".toByteArray())
        writer.flush() // the pipe stays open

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(true), reasons.toList())
    }

    @Test
    fun theExitLineMayComeAfterOtherLines() {
        watch(ByteArrayInputStream("hello\nworld\nexit\n".toByteArray()))

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(true), reasons.toList())
    }

    @Test
    fun aPartialExitLineAtTheEndIsNotAnExit() {
        watch(ByteArrayInputStream("exi".toByteArray())) // the shell died in the middle of writing

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(listOf(false), reasons.toList())
    }

    @Test
    fun theEndAfterAnExitLineIsNotReportedAgain() {
        watch(ByteArrayInputStream("exit\n".toByteArray()))

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        Thread.sleep(300)
        assertEquals(listOf(true), reasons.toList())
    }
}
