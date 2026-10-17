package com.xgetsongs.engine.lyrics.google

import com.xgetsongs.shared.api.BrowserAnswer
import com.xgetsongs.shared.api.BrowserCommand
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeMark
import kotlin.time.TimeSource

/**
 * The server's end of the hidden browser, which lives in the desktop shell (a WebView2 window). The lookup calls [load],
 * [evaluate] and [reset] as a [PageDriver]; each becomes a [BrowserCommand] in a queue. The shell, the agent, takes commands with
 * [nextCommand] (a long poll, over HTTP) and reports each with [answer].
 *
 * Nothing waits for a browser that is not there: a call fails with `no-agent` when the shell has not been in touch (a poll or an
 * answer) within [agentFreshness], so a sidecar that runs alone (tests, development) ends a lookup at once with "no lyrics".
 * A command that gets no answer within [answerLimit] fails with `no-answer`. A command whose caller has given up (timed out,
 * cancelled) is never handed out.
 */
class BrowserChannel(
    private val timeSource: TimeSource = TimeSource.Monotonic,
    private val agentFreshness: Duration = 40.seconds,
    private val answerLimit: Duration = 15.seconds,
) : PageDriver, AutoCloseable {
    // A command taken by a poll that was cancelled at that very moment goes back into the queue.
    private val queue: Channel<BrowserCommand> = Channel(Channel.UNLIMITED) { undelivered -> queue.trySend(undelivered) }
    private val waiting = ConcurrentHashMap<String, CompletableDeferred<BrowserAnswer>>()

    @Volatile
    private var lastContact: TimeMark? = null

    @Volatile
    private var closed = false

    /** The agent's poll: the next command, or null when none came within [wait] or the channel is closed. */
    suspend fun nextCommand(wait: Duration): BrowserCommand? {
        if (closed) return null
        touch()
        val command = withTimeoutOrNull(wait) { takeLive() }
        touch()
        return command
    }

    /** The agent's report on a command. False when nobody waits for it any more (given up, answered already, not ours). */
    fun answer(answer: BrowserAnswer): Boolean {
        val deferred = waiting[answer.id] ?: return false
        touch()
        return deferred.complete(answer)
    }

    override suspend fun load(url: String) {
        val answer = call { BrowserCommand.Load(it, url) }
        if (!answer.ok) throw BrowserUnavailableException(answer.error ?: "failed")
    }

    override suspend fun evaluate(script: String): String? {
        val answer = call { BrowserCommand.Evaluate(it, script) }
        // A script the browser could not run (its window is gone, the page does not answer) is not "nothing": waiting on that page
        // for the rest of the time limit would end as a timeout, which is held against Google.
        if (!answer.ok) throw BrowserUnavailableException(answer.error ?: "failed")
        return answer.value
    }

    override suspend fun reset() {
        val answer = call { BrowserCommand.Reset(it) }
        if (!answer.ok) throw BrowserUnavailableException(answer.error ?: "failed")
    }

    /** Ends the polls and fails the calls that wait for an answer (with `closed`). */
    override fun close() {
        closed = true
        queue.close()
        val error = BrowserUnavailableException("closed")
        for (deferred in waiting.values) deferred.completeExceptionally(error)
        waiting.clear()
    }

    private fun touch() {
        lastContact = timeSource.markNow()
    }

    /** Whether the agent has been in touch (a poll or an answer) within the freshness time: calls fail with `no-agent` when it has not. */
    val hasAgent: Boolean get() = lastContact?.let { it.elapsedNow() < agentFreshness } ?: false

    /** The next command somebody still waits for; null when the queue is closed. */
    private suspend fun takeLive(): BrowserCommand? {
        while (true) {
            val command = queue.receiveCatching().getOrNull() ?: return null
            if (waiting.containsKey(command.id)) return command
        }
    }

    private suspend fun call(command: (id: String) -> BrowserCommand): BrowserAnswer {
        if (closed) throw BrowserUnavailableException("closed")
        if (!hasAgent) throw BrowserUnavailableException("no-agent")
        val id = UUID.randomUUID().toString()
        val deferred = CompletableDeferred<BrowserAnswer>()
        waiting[id] = deferred
        try {
            if (closed) throw BrowserUnavailableException("closed")
            queue.trySend(command(id))
            return withTimeoutOrNull(answerLimit) { deferred.await() } ?: throw BrowserUnavailableException("no-answer")
        } finally {
            waiting.remove(id)
        }
    }
}
