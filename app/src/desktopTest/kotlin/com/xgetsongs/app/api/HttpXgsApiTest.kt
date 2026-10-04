package com.xgetsongs.app.api

import com.xgetsongs.engine.ResolveException
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ToolInfo
import io.ktor.server.testing.testApplication
import kotlinx.coroutines.flow.toList
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
}
