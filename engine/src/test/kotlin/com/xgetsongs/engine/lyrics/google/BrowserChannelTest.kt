package com.xgetsongs.engine.lyrics.google

import com.xgetsongs.shared.api.BrowserAnswer
import com.xgetsongs.shared.api.BrowserCommand
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.currentTime
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TestTimeSource

class BrowserChannelTest {
    @Test
    fun aCallWithNoAgentFailsAtOnceWithNoAgent() = runTest {
        val channel = BrowserChannel(TestTimeSource())

        val error = assertFailsWith<BrowserUnavailableException> { channel.load("https://example.test/") }

        assertEquals("no-agent", error.reason)
        assertNull(channel.nextCommand(1.milliseconds), "nothing was queued")
    }

    @Test
    fun aCommandReachesThePollingAgentAndItsAnswerReturnsToTheCaller() = runTest {
        val channel = BrowserChannel(TestTimeSource())
        val poll = async { channel.nextCommand(25.seconds) }
        runCurrent()

        val call = async { channel.evaluate("1+1") }
        val command = poll.await()

        assertNotNull(command)
        assertEquals(BrowserCommand.Evaluate(command.id, "1+1"), command)
        assertTrue(channel.answer(BrowserAnswer(command.id, ok = true, value = "2")))
        assertEquals("2", call.await())
    }

    @Test
    fun anEvaluateAnsweredWithAnErrorGivesNull() = runTest {
        val channel = BrowserChannel(TestTimeSource())
        val poll = async { channel.nextCommand(25.seconds) }
        runCurrent()

        val call = async { channel.evaluate("document.body.innerText") }
        val command = checkNotNull(poll.await())
        channel.answer(BrowserAnswer(command.id, ok = false, error = "no document"))

        assertNull(call.await())
    }

    @Test
    fun aLoadAnsweredWithAnErrorFailsWithTheErrorText() = runTest {
        val channel = BrowserChannel(TestTimeSource())
        val poll = async { channel.nextCommand(25.seconds) }
        runCurrent()

        val call = async { runCatching { channel.load("https://example.test/") } }
        val command = checkNotNull(poll.await())
        channel.answer(BrowserAnswer(command.id, ok = false, error = "window gone"))

        val error = call.await().exceptionOrNull()
        assertTrue(error is BrowserUnavailableException, "$error")
        assertEquals("window gone", error.reason)
    }

    @Test
    fun aCommandNobodyAnswersFailsWithNoAnswerAfterTheLimit() = runTest {
        val channel = BrowserChannel(TestTimeSource(), answerLimit = 15.seconds)
        val poll = async { channel.nextCommand(25.seconds) }
        runCurrent()

        val call = async { runCatching { channel.evaluate("x") } }
        checkNotNull(poll.await()) // the agent took the command and never answers
        val taken = currentTime
        val error = call.await().exceptionOrNull()

        assertTrue(error is BrowserUnavailableException, "$error")
        assertEquals("no-answer", error.reason)
        assertEquals(15_000L, currentTime - taken)
    }

    @Test
    fun aCommandThatTimedOutIsNotHandedOutLater() = runTest {
        val channel = BrowserChannel(TestTimeSource(), answerLimit = 15.seconds)
        assertNull(channel.nextCommand(1.seconds), "the agent was here, with nothing to do")

        val error = assertFailsWith<BrowserUnavailableException> { channel.evaluate("x") }

        assertEquals("no-answer", error.reason)
        assertNull(channel.nextCommand(1.seconds), "the caller gave up: nobody waits for what it asked")
    }

    @Test
    fun aCommandOfACancelledCallerIsNotHandedOutLater() = runTest {
        val channel = BrowserChannel(TestTimeSource())
        assertNull(channel.nextCommand(1.seconds))
        val call = launch { channel.evaluate("x") }
        runCurrent()

        call.cancelAndJoin()

        assertNull(channel.nextCommand(1.seconds))
    }

    @Test
    fun anAnswerNobodyWaitsForIsRefused() = runTest {
        val channel = BrowserChannel(TestTimeSource())
        assertFalse(channel.answer(BrowserAnswer("never-asked", ok = true)), "an id the channel did not make")
        val poll = async { channel.nextCommand(25.seconds) }
        runCurrent()
        val call = async { channel.evaluate("x") }
        val command = checkNotNull(poll.await())

        assertTrue(channel.answer(BrowserAnswer(command.id, ok = true, value = "v")))
        assertEquals("v", call.await())
        assertFalse(channel.answer(BrowserAnswer(command.id, ok = true, value = "again")), "answered already")
    }

    @Test
    fun theAgentCountsAsPresentForTheFreshnessTimeOnly() = runTest {
        val time = TestTimeSource()
        val channel = BrowserChannel(time, agentFreshness = 40.seconds)
        assertFalse(channel.hasAgent, "nobody has been in touch yet")
        assertNull(channel.nextCommand(1.milliseconds))
        assertTrue(channel.hasAgent)

        time += 39.seconds
        val queued = async { runCatching { channel.evaluate("x") } }
        runCurrent()
        assertFalse(queued.isCompleted, "still fresh at 39 s: the command waits for the agent")
        queued.cancelAndJoin()

        time += 2.seconds
        assertFalse(channel.hasAgent)
        val error = assertFailsWith<BrowserUnavailableException> { channel.evaluate("y") }
        assertEquals("no-agent", error.reason)
    }

    @Test
    fun closingFailsPendingCallsAndEndsPolls() = runTest {
        val channel = BrowserChannel(TestTimeSource())
        assertNull(channel.nextCommand(1.milliseconds))
        val pending = async { runCatching { channel.evaluate("x") } }
        runCurrent()

        channel.close()

        val error = pending.await().exceptionOrNull()
        assertTrue(error is BrowserUnavailableException, "$error")
        assertEquals("closed", error.reason)
        val before = currentTime
        assertNull(channel.nextCommand(25.seconds))
        assertEquals(before, currentTime, "a poll of a closed channel ends at once")
        assertEquals("closed", assertFailsWith<BrowserUnavailableException> { channel.load("https://example.test/") }.reason)
    }

    @Test
    fun everyCommandGetsItsOwnId() = runTest {
        val channel = BrowserChannel(TestTimeSource())
        val ids = mutableListOf<String>()
        val agent = launch {
            repeat(2) {
                val command = checkNotNull(channel.nextCommand(25.seconds))
                ids += command.id
                channel.answer(BrowserAnswer(command.id, ok = true, value = "v"))
            }
        }
        runCurrent()

        channel.evaluate("a")
        channel.evaluate("b")
        agent.join()

        assertEquals(2, ids.toSet().size, "$ids")
    }
}
