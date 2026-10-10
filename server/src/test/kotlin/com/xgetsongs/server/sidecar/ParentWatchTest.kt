package com.xgetsongs.server.sidecar

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ParentWatchTest {
    private val gone = CountDownLatch(1)
    private val calls = AtomicInteger()

    private fun watch(input: InputStream) = ParentWatch(input) {
        calls.incrementAndGet()
        gone.countDown()
    }.start()

    @Test
    fun anInputThatEndsAtOnceMeansTheParentIsGone() {
        watch(ByteArrayInputStream(ByteArray(0)))

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(1, calls.get())
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
        assertEquals(1, calls.get())
    }

    @Test
    fun aFailingInputAlsoMeansTheParentIsGone() {
        watch(object : InputStream() {
            override fun read(): Int = throw IOException("pipe broken")
        })

        assertTrue(gone.await(30, TimeUnit.SECONDS))
        assertEquals(1, calls.get())
    }

    @Test
    fun theWatchingThreadNeverKeepsTheJvmAlive() {
        val writer = PipedOutputStream()
        val thread = watch(PipedInputStream(writer))

        assertTrue(thread.isDaemon)
        writer.close()
        assertTrue(gone.await(30, TimeUnit.SECONDS))
    }
}
