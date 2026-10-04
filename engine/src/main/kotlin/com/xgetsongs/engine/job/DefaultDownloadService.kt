package com.xgetsongs.engine.job

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.DownloadService
import com.xgetsongs.engine.JobHandle
import com.xgetsongs.engine.ytdlp.FailureKind
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.ResolvedItem
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicInteger
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds

/** Aborts the whole job (disk full, no permission...). */
private class FatalJobException(message: String) : Exception(message)

class DefaultDownloadService(
    private val downloader: ItemDownloader,
    private val tempRoot: Path,
    private val scope: CoroutineScope,
    private val retryDelays: List<Duration> = listOf(2.seconds, 4.seconds),
) : DownloadService {

    override fun start(request: DownloadRequest): JobHandle {
        val events = Channel<JobEvent>(Channel.UNLIMITED)
        val job = scope.launch { runJob(request, events) }
        return JobHandle(events, job)
    }

    private class Counters {
        val succeeded = AtomicInteger()
        val skipped = AtomicInteger()
        val failed = AtomicInteger()
        fun summary() = JobSummary(succeeded.get(), skipped.get(), failed.get())
    }

    private suspend fun runJob(request: DownloadRequest, events: Channel<JobEvent>) {
        val workRoot = withContext(Dispatchers.IO) {
            Files.createDirectories(tempRoot)
            Files.createTempDirectory(tempRoot, "job-")
        }
        val counters = Counters()
        var status = JobStatus.COMPLETED
        try {
            coroutineScope {
                val gate = Semaphore(request.concurrency.coerceIn(MIN_CONCURRENCY, MAX_CONCURRENCY))
                for (item in request.items) {
                    launch { gate.withPermit { processItem(item, request, workRoot, events, counters) } }
                }
            }
        } catch (e: FatalJobException) {
            status = JobStatus.FAILED
        } catch (e: CancellationException) {
            status = JobStatus.CANCELLED
            throw e
        } finally {
            withContext(NonCancellable) {
                workRoot.toFile().deleteRecursively()
                events.trySend(JobEvent.JobDone(status, counters.summary()))
                events.close()
            }
        }
    }

    private suspend fun processItem(
        item: ResolvedItem,
        request: DownloadRequest,
        workRoot: Path,
        events: Channel<JobEvent>,
        counters: Counters,
    ) {
        try {
            val prepared = downloader.prepare(item)
            events.trySend(JobEvent.ItemStarted(item.rank, item.videoId, prepared.fileName))
            if (!request.overwrite && request.sink.exists(prepared.fileName)) {
                events.trySend(JobEvent.ItemSkipped(item.rank, "이미 존재"))
                counters.skipped.incrementAndGet()
                return
            }
            val itemDir = withContext(Dispatchers.IO) { Files.createDirectories(workRoot.resolve(item.videoId)) }
            var retries = 0
            while (true) {
                when (val result = downloader.download(prepared, itemDir) { events.trySend(it) }) {
                    is DownloadResult.Downloaded -> {
                        request.sink.put(prepared.fileName, result.file, request.overwrite)
                        events.trySend(JobEvent.ItemDone(item.rank, prepared.fileName))
                        counters.succeeded.incrementAndGet()
                        return
                    }
                    is DownloadResult.Failed -> {
                        val failure = result.failure
                        when (failure.kind) {
                            FailureKind.UNAVAILABLE -> {
                                events.trySend(JobEvent.ItemSkipped(item.rank, failure.message))
                                counters.skipped.incrementAndGet()
                                return
                            }
                            FailureKind.TRANSIENT -> {
                                if (retries < retryDelays.size) {
                                    delay(retryDelays[retries])
                                    retries++
                                    continue
                                }
                                fail(item, failure.message, events, counters)
                                return
                            }
                            FailureKind.FATAL -> {
                                fail(item, failure.message, events, counters)
                                throw FatalJobException(failure.message)
                            }
                            FailureKind.OTHER -> {
                                fail(item, failure.message, events, counters)
                                return
                            }
                        }
                    }
                }
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: FatalJobException) {
            throw e
        } catch (e: IOException) {
            fail(item, "파일 처리 중 오류: ${e.message}", events, counters)
        } catch (e: Exception) {
            fail(item, e.message ?: e::class.simpleName.orEmpty(), events, counters)
        }
    }

    private fun fail(item: ResolvedItem, message: String, events: Channel<JobEvent>, counters: Counters) {
        events.trySend(JobEvent.ItemFailed(item.rank, message))
        counters.failed.incrementAndGet()
    }

    private companion object {
        const val MIN_CONCURRENCY = 1
        const val MAX_CONCURRENCY = 4
    }
}
