package com.xgetsongs.engine.job

import kotlin.test.Test
import kotlin.test.assertEquals

class DownloadConcurrencyTest {
    @Test
    fun itIsSeventyPercentOfTheCoresRoundedDown() {
        assertEquals(2, DownloadConcurrency.forCores(3))
        assertEquals(2, DownloadConcurrency.forCores(4))
        assertEquals(5, DownloadConcurrency.forCores(8))
        assertEquals(7, DownloadConcurrency.forCores(10))
        assertEquals(8, DownloadConcurrency.forCores(12))
        assertEquals(11, DownloadConcurrency.forCores(16))
        assertEquals(22, DownloadConcurrency.forCores(32))
    }

    @Test
    fun itIsNeverLessThanOne() {
        assertEquals(1, DownloadConcurrency.forCores(1))
        assertEquals(1, DownloadConcurrency.forCores(2))
        assertEquals(1, DownloadConcurrency.forCores(0))
    }

    @Test
    fun theAutomaticNumberIsForTheCoresOfThisMachine() {
        assertEquals(DownloadConcurrency.forCores(Runtime.getRuntime().availableProcessors()), DownloadConcurrency.automatic())
    }
}
