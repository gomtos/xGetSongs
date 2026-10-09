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
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.DelicateCoroutinesApi
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
import java.nio.file.AccessDeniedException
import java.nio.file.FileAlreadyExistsException
import java.nio.file.FileSystemException
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicInteger
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds

/** Aborts the whole job (disk full, no permission...). */
private class FatalJobException(message: String) : Exception(message)

private val DISK_FULL_MARKERS = listOf("no space left", "not enough space", "disk full", "공간이 부족")

/**
 * The JDK reports a full disk only through text: the reason of a [FileSystemException] (its message also holds the
 * file names, which must not be matched), else the message of any other [IOException].
 */
private fun isDiskFull(e: IOException): Boolean {
    val text = (if (e is FileSystemException) e.reason else e.message).orEmpty().lowercase()
    return DISK_FULL_MARKERS.any { it in text }
}

class DefaultDownloadService(
    private val downloader: ItemDownloader,
    private val tempRoot: Path,
    private val scope: CoroutineScope,
    private val retryDelays: List<Duration> = listOf(2.seconds, 4.seconds),
) : DownloadService {

    // ATOMIC is intentional: a job cancelled before its first dispatch must still run its finally, sending JobDone and closing the channel.
    @OptIn(DelicateCoroutinesApi::class)
    override fun start(request: DownloadRequest): JobHandle {
        val events = Channel<JobEvent>(Channel.UNLIMITED)
        val job = scope.launch(start = CoroutineStart.ATOMIC) { runJob(request, events) }
        return JobHandle(events, job)
    }

    private class Counters {
        val succeeded = AtomicInteger()
        val skipped = AtomicInteger()
        val failed = AtomicInteger()
        fun summary() = JobSummary(succeeded.get(), skipped.get(), failed.get())
    }

    private suspend fun runJob(request: DownloadRequest, events: Channel<JobEvent>) {
        val counters = Counters()
        var status = JobStatus.FAILED
        var workRoot: Path? = null
        try {
            // Assigned inside the block so that the finally clause sees the folder even if cancellation follows.
            val root = withContext(Dispatchers.IO) {
                Files.createDirectories(tempRoot)
                Files.createTempDirectory(tempRoot, "job-").also { workRoot = it }
            }
            coroutineScope {
                val gate = Semaphore(request.concurrency.coerceIn(MIN_CONCURRENCY, MAX_CONCURRENCY))
                for (item in request.items) {
                    launch { gate.withPermit { processItem(item, request, root, events, counters) } }
                }
            }
            status = JobStatus.COMPLETED
        } catch (e: FatalJobException) {
            status = JobStatus.FAILED
        } catch (e: CancellationException) {
            status = JobStatus.CANCELLED
            throw e
        } catch (e: IOException) {
            // The work folder could not be set up: no item was started, so there is no per-item event.
            status = JobStatus.FAILED
        } finally {
            withContext(NonCancellable) {
                withContext(Dispatchers.IO) { workRoot?.toFile()?.deleteRecursively() }
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
            val prepared = downloader.prepare(item, request.album, request.includeRank, request.searchLyricsOnline, request.albumOverride)
            events.trySend(JobEvent.ItemStarted(item.rank, item.videoId, prepared.fileName))
            if (!request.overwrite && request.sink.exists(prepared.fileName)) {
                skip(item, ALREADY_EXISTS, events, counters)
                return
            }
            val itemDir = withContext(Dispatchers.IO) { Files.createDirectories(workRoot.resolve(item.rank.toString())) }
            var retries = 0
            while (true) {
                when (val result = downloader.download(prepared, itemDir) { events.trySend(it) }) {
                    is DownloadResult.Downloaded -> {
                        try {
                            request.sink.put(prepared.fileName, result.file, request.overwrite)
                        } catch (e: FileAlreadyExistsException) {
                            // Free when the item started, taken now: another item of this job with the same name (the rank
                            // is not part of it) or another program finished first; the first one to finish wins. Same
                            // outcome as finding it taken up front.
                            if (request.overwrite || !request.sink.exists(prepared.fileName)) throw e
                            skip(item, ALREADY_EXISTS, events, counters)
                            return
                        }
                        events.trySend(JobEvent.ItemDone(item.rank, prepared.fileName, result.lyrics))
                        counters.succeeded.incrementAndGet()
                        return
                    }
                    is DownloadResult.Failed -> {
                        val failure = result.failure
                        when (failure.kind) {
                            FailureKind.UNAVAILABLE -> {
                                skip(item, failure.message, events, counters)
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
        } catch (e: AccessDeniedException) {
            fail(item, "출력 폴더에 쓸 권한이 없습니다.", events, counters)
            throw FatalJobException("출력 폴더에 쓸 권한이 없습니다.")
        } catch (e: IOException) {
            if (isDiskFull(e)) {
                fail(item, "디스크 공간이 부족합니다.", events, counters)
                throw FatalJobException("디스크 공간이 부족합니다.")
            }
            fail(item, "파일 처리 중 오류: ${e.message}", events, counters)
        } catch (e: Exception) {
            fail(item, e.message ?: e::class.simpleName.orEmpty(), events, counters)
        }
    }

    private fun skip(item: ResolvedItem, reason: String, events: Channel<JobEvent>, counters: Counters) {
        events.trySend(JobEvent.ItemSkipped(item.rank, reason))
        counters.skipped.incrementAndGet()
    }

    private fun fail(item: ResolvedItem, message: String, events: Channel<JobEvent>, counters: Counters) {
        events.trySend(JobEvent.ItemFailed(item.rank, message))
        counters.failed.incrementAndGet()
    }

    private companion object {
        const val MIN_CONCURRENCY = 1
        const val MAX_CONCURRENCY = 4
        const val ALREADY_EXISTS = "이미 존재"
    }
}
