package com.xgetsongs.engine.job

import kotlin.test.Test
import kotlin.test.assertEquals

class DownloadConcurrencyTest {
    @Test
    fun itIsOneItemPerCore() {
        assertEquals(3, DownloadConcurrency.forCores(3))
        assertEquals(4, DownloadConcurrency.forCores(4))
        assertEquals(8, DownloadConcurrency.forCores(8))
        assertEquals(12, DownloadConcurrency.forCores(12))
        assertEquals(32, DownloadConcurrency.forCores(32))
    }

    @Test
    fun itIsNeverLessThanOne() {
        assertEquals(1, DownloadConcurrency.forCores(1))
        assertEquals(1, DownloadConcurrency.forCores(0))
        assertEquals(1, DownloadConcurrency.forCores(-1))
    }

    @Test
    fun theAutomaticNumberIsForTheCoresOfThisMachine() {
        assertEquals(DownloadConcurrency.forCores(Runtime.getRuntime().availableProcessors()), DownloadConcurrency.automatic())
    }
}
