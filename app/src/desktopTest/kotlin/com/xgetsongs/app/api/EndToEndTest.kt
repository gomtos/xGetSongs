package com.xgetsongs.app.api

import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.state.ItemStatus
import com.xgetsongs.app.state.Phase
import com.xgetsongs.app.state.statusLabel
import com.xgetsongs.shared.api.JobSummary
import com.xgetsongs.shared.api.LyricsOutcome
import io.ktor.server.testing.testApplication
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.first
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class EndToEndTest {
    @Test
    fun theScreenLogicWorksAgainstTheRealServerRoutes() = testApplication {
        val engine = FakeEngine()
        val api = apiFor(engine)
        coroutineScope {
            val holder = AppStateHolder(api, this, defaultOutputDir = engine.outputDir)
            holder.onInput("PLabcdefghijkl")
            holder.resolve()
            holder.state.first { it.phase == Phase.PREVIEW }

            holder.startDownload()
            val state = holder.state.first { it.phase == Phase.FINISHED }

            assertEquals(ItemStatus.Done(LyricsOutcome.ONLINE), state.rows.single().status, "the outcome travels engine event -> SSE -> client -> row")
            assertEquals("완료 · 가사 ✓ 인터넷", statusLabel(state.rows.single().status))
            assertEquals(JobSummary(1, 0, 0), state.summary)
            assertNull(state.error)
        }
    }
}
