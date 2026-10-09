package com.xgetsongs.engine.job

/** How many items of a job are downloaded at the same time: 70% of the processors of the machine, at least one. */
object DownloadConcurrency {
    /** The number for a machine with [cores] processors, rounded down. */
    fun forCores(cores: Int): Int = (cores * 7 / 10).coerceAtLeast(1)

    /** The number for this machine (the processors the JVM sees, hyper-threads included). */
    fun automatic(): Int = forCores(Runtime.getRuntime().availableProcessors())
}
