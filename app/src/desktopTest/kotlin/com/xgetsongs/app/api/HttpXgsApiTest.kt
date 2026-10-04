package com.xgetsongs.app.api

import com.xgetsongs.engine.ResolveException
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ToolInfo
import io.ktor.client.HttpClient
import io.ktor.client.engine.mock.MockEngine
import io.ktor.client.engine.mock.respond
import io.ktor.client.statement.HttpResponsePipeline
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.http.headersOf
import io.ktor.server.testing.testApplication
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.supervisorScope
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

/** Runs [HttpXgsApi] against the real server routes with a fake engine behind them. */
class HttpXgsApiTest {
    private val engine = FakeEngine()

    @Test
    fun readsToolStatusAndRunsToolActions() = testApplication {
        val api = apiFor(engine)

        assertEquals(ToolInfo(false), api.tools().jsRuntime)
        assertEquals("installed", api.installYtDlp().message)
        assertEquals("updated", api.updateYtDlp().message)
    }

    @Test
    fun resolveReturnsItemsWithAnId() = testApplication {
        val response = apiFor(engine).resolve("PLabcdefghijkl")

        assertTrue(response.resolveId.isNotBlank())
        assertEquals(listOf(1), response.items.map { it.rank })
    }

    @Test
    fun serverErrorsBecomeApiErrorsWithTheServerMessage() = testApplication {
        val api = apiFor(engine)
        engine.resolveError = ResolveException("비공개 재생목록")

        val error = assertFailsWith<ApiError> { api.resolve("PLabcdefghijkl") }

        assertEquals("비공개 재생목록", error.message)
    }

    @Test
    fun aWrongTokenIsReportedAsAnApiError() = testApplication {
        val error = assertFailsWith<ApiError> { apiFor(engine, clientToken = "wrong").tools() }

        assertTrue(error.message!!.contains("토큰"))
    }

    @Test
    fun jobEventsArriveInOrder() = testApplication {
        val api = apiFor(engine)
        val resolveId = api.resolve("PLabcdefghijkl").resolveId
        val jobId = api.startJob(JobRequest(resolveId, JobOptions(outputDir = engine.outputDir))).jobId

        val events = api.events(jobId).toList()

        assertEquals(engine.finishedEvents, events)
    }

    @Test
    fun eventsOfAnUnknownJobFailWithAnApiError() = testApplication {
        val error = assertFailsWith<ApiError> { apiFor(engine).events("nope").toList() }

        assertTrue(error.message!!.contains("찾을 수 없습니다"))
    }

    @Test
    fun cancelReachesTheJob() = testApplication {
        val api = apiFor(engine)
        val resolveId = api.resolve("PLabcdefghijkl").resolveId
        val jobId = api.startJob(JobRequest(resolveId, JobOptions(outputDir = engine.outputDir))).jobId

        api.cancel(jobId)

        assertTrue(engine.jobs.single().isCancelled)
        assertFailsWith<ApiError> { api.cancel("nope") }
    }

    @Test
    fun eventsOfAJobThatEndsWithoutJobDoneFailWithAnApiError() = testApplication {
        engine.eventsToSend = engine.finishedEvents.dropLast(1)
        val api = apiFor(engine)
        val resolveId = api.resolve("PLabcdefghijkl").resolveId
        val jobId = api.startJob(JobRequest(resolveId, JobOptions(outputDir = engine.outputDir))).jobId

        val error = assertFailsWith<ApiError> { api.events(jobId).toList() }

        assertTrue(error.message!!.contains("\uc5f0\uacb0\uc774 \ub04a\uc5b4\uc84c\uc2b5\ub2c8\ub2e4"))
    }

    @Test
    fun eventsWithAWrongTokenFailWithAnApiErrorNotAnSseException() = testApplication {
        // The real server answers 401 to the SSE request itself, which the SSE plugin reports as SSEClientException.
        assertFailsWith<ApiError> { apiFor(engine, clientToken = "wrong").events("any").toList() }
    }

    @Test
    fun cancellationWhileReadingAnErrorBodyIsNotTurnedIntoAnApiError() = runBlocking {
        val mock = MockEngine {
            respond(
                content = "{}",
                status = HttpStatusCode.InternalServerError,
                headers = headersOf(HttpHeaders.ContentType, ContentType.Application.Json.toString()),
            )
        }
        val client = HttpClient(mock) { configureXgs("t") }
        // Park the caller inside body<ErrorResponse>() (the plain get() has already finished by then), so the
        // cancellation below reaches exactly the place where checked() reads the error body.
        val reading = CompletableDeferred<Unit>()
        client.responsePipeline.intercept(HttpResponsePipeline.Receive) {
            reading.complete(Unit)
            awaitCancellation()
        }

        supervisorScope {
            val call = async { HttpXgsApi(client).tools() }
            reading.await()
            call.cancel()

            // Not an ApiError: the caller was cancelled and has to see that.
            assertFailsWith<CancellationException> { call.await() }
        }
        Unit
    }

    @Test
    fun anErrorBodyThatIsNotJsonFallsBackToTheStatusCode() = runBlocking {
        val mock = MockEngine {
            respond(
                content = "boom",
                status = HttpStatusCode.InternalServerError,
                headers = headersOf(HttpHeaders.ContentType, ContentType.Text.Plain.toString()),
            )
        }
        val api = HttpXgsApi(HttpClient(mock) { configureXgs("t") })

        val error = assertFailsWith<ApiError> { api.tools() }

        assertEquals("\uc11c\ubc84 \uc624\ub958 (500)", error.message)
    }
}
