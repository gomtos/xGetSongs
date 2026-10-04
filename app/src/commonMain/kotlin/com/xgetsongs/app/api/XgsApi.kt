package com.xgetsongs.app.api

import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.ErrorResponse
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ResolveRequest
import com.xgetsongs.shared.api.ResolveResponse
import com.xgetsongs.shared.api.ToolsStatus
import io.ktor.client.HttpClient
import io.ktor.client.HttpClientConfig
import io.ktor.client.call.body
import io.ktor.client.plugins.contentnegotiation.ContentNegotiation
import io.ktor.client.plugins.defaultRequest
import io.ktor.client.plugins.sse.SSE
import io.ktor.client.plugins.sse.SSEClientException
import io.ktor.client.plugins.sse.sse
import io.ktor.client.request.delete
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.client.request.url
import io.ktor.client.statement.HttpResponse
import io.ktor.http.ContentType
import io.ktor.http.contentType
import io.ktor.http.isSuccess
import io.ktor.serialization.kotlinx.json.json
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.serialization.SerializationException

/** A failure reported by the server (or a lost connection) with a message fit for the user. */
class ApiError(message: String) : Exception(message)

/** Everything the UI needs from the server. Faked in tests. */
interface XgsApi {
    suspend fun tools(): ToolsStatus
    suspend fun installYtDlp(): ActionResult
    suspend fun updateYtDlp(): ActionResult
    suspend fun resolve(input: String): ResolveResponse
    suspend fun startJob(request: JobRequest): JobCreated

    /**
     * Emits the job's events and completes after the final `job-done` event. Throws [ApiError] on a server-side
     * error event, when the stream cannot be opened (for example a non-200 answer) or cannot be decoded, and when
     * it ends without `job-done`.
     */
    fun events(jobId: String): Flow<JobEvent>

    suspend fun cancel(jobId: String)
}

/** Installs JSON, SSE and the auth token. [baseUrl] is empty when the client already points at the server. */
fun HttpClientConfig<*>.configureXgs(token: String, baseUrl: String = "") {
    install(ContentNegotiation) { json(ApiJson.instance) }
    install(SSE)
    defaultRequest {
        if (baseUrl.isNotEmpty()) url(baseUrl)
        header(ApiHeaders.TOKEN, token)
    }
}

class HttpXgsApi(private val client: HttpClient) : XgsApi {
    private val json = ApiJson.instance

    override suspend fun tools(): ToolsStatus = client.get("/tools").checked().body()

    override suspend fun installYtDlp(): ActionResult = client.post("/tools/yt-dlp/install").checked().body()

    override suspend fun updateYtDlp(): ActionResult = client.post("/tools/yt-dlp/update").checked().body()

    override suspend fun resolve(input: String): ResolveResponse =
        client.post("/resolve") {
            contentType(ContentType.Application.Json)
            setBody(ResolveRequest(input))
        }.checked().body()

    override suspend fun startJob(request: JobRequest): JobCreated =
        client.post("/jobs") {
            contentType(ContentType.Application.Json)
            setBody(request)
        }.checked().body()

    override fun events(jobId: String): Flow<JobEvent> = channelFlow {
        // The SSE client wraps anything thrown inside its block, so remember the error and throw it afterwards.
        var serverError: String? = null
        var sawJobDone = false
        try {
            client.sse("/jobs/$jobId/events") {
                incoming.collect { event ->
                    val data = event.data.orEmpty()
                    if (event.event == "error") {
                        serverError = json.decodeFromString(ErrorResponse.serializer(), data).message
                    } else {
                        val jobEvent = json.decodeFromString(JobEvent.serializer(), data)
                        if (jobEvent is JobEvent.JobDone) sawJobDone = true
                        send(jobEvent)
                    }
                }
            }
        } catch (e: SSEClientException) {
            throw ApiError(
                if (e.cause is SerializationException) {
                    "서버 이벤트를 해석할 수 없습니다."
                } else {
                    "이벤트 스트림을 열 수 없습니다: ${e.message}"
                },
            )
        }
        serverError?.let { throw ApiError(it) }
        // The engine always sends job-done before it closes the stream, so a stream without it was cut off.
        if (!sawJobDone) throw ApiError("서버와의 연결이 끊어졌습니다.")
    }

    override suspend fun cancel(jobId: String) {
        client.delete("/jobs/$jobId").checked()
    }

    private suspend fun HttpResponse.checked(): HttpResponse {
        if (status.isSuccess()) return this
        val message = try {
            body<ErrorResponse>().message
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            "서버 오류 (${status.value})"
        }
        throw ApiError(message)
    }
}
