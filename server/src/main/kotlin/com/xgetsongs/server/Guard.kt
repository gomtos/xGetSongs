package com.xgetsongs.server

import com.xgetsongs.shared.api.ApiHeaders
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.ErrorResponse
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.server.application.Application
import io.ktor.server.application.ApplicationCallPipeline
import io.ktor.server.application.PipelineCall
import io.ktor.server.application.call
import io.ktor.server.response.respondText
import io.ktor.util.pipeline.PipelineContext
import java.security.MessageDigest

private val LOCAL_HOSTS = setOf("127.0.0.1", "localhost", "[::1]", "::1")

/**
 * Protects a localhost server from other programs and from web pages the user happens to visit:
 * - the `Host` header must be a loopback name (blocks DNS rebinding),
 * - requests carrying an `Origin` header are refused (browsers always send it on cross-site calls;
 *   the desktop client never does),
 * - the secret token generated at start-up must be present.
 */
fun Application.installLocalGuard(config: ServerConfig) {
    val expected = config.token.toByteArray(Charsets.UTF_8)
    intercept(ApplicationCallPipeline.Plugins) {
        val host = call.request.local.serverHost.lowercase()
        when {
            host !in LOCAL_HOSTS -> reject(HttpStatusCode.Forbidden, "허용되지 않은 Host 입니다.")
            call.request.headers[HttpHeaders.Origin] != null -> reject(HttpStatusCode.Forbidden, "브라우저 요청은 허용되지 않습니다.")
            !tokenMatches(call.request.headers[ApiHeaders.TOKEN], expected) -> reject(HttpStatusCode.Unauthorized, "인증 토큰이 올바르지 않습니다.")
        }
    }
}

private suspend fun PipelineContext<Unit, PipelineCall>.reject(
    status: HttpStatusCode,
    message: String,
) {
    // Serialised by hand so the guard works without any other plugin installed.
    val body = ApiJson.instance.encodeToString(ErrorResponse.serializer(), ErrorResponse(message))
    call.respondText(body, ContentType.Application.Json, status)
    finish()
}

private fun tokenMatches(provided: String?, expected: ByteArray): Boolean =
    provided != null && MessageDigest.isEqual(provided.toByteArray(Charsets.UTF_8), expected)
