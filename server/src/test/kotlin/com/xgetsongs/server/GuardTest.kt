package com.xgetsongs.server

import com.xgetsongs.shared.api.ApiHeaders
import io.ktor.client.HttpClient
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.statement.bodyAsText
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.server.response.respondText
import io.ktor.server.routing.get
import io.ktor.server.routing.routing
import io.ktor.server.testing.ApplicationTestBuilder
import io.ktor.server.testing.testApplication
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.Test
import kotlin.test.assertEquals

/** Tests the guard on its own, in front of a single route that counts how often it is reached. */
class GuardTest {
    private val token = "guard-test-token"
    private val reached = AtomicInteger()

    private fun ApplicationTestBuilder.guardedApp(): HttpClient {
        application {
            installLocalGuard(ServerConfig(token))
            routing {
                get("/ping") {
                    reached.incrementAndGet()
                    call.respondText("pong")
                }
            }
        }
        return createClient { }
    }

    @Test
    fun requestsWithoutTheTokenAreRefusedAndNeverReachTheRoute() = testApplication {
        val response = guardedApp().get("/ping")

        assertEquals(HttpStatusCode.Unauthorized, response.status)
        assertEquals(0, reached.get())
    }

    @Test
    fun aWrongTokenIsRefused() = testApplication {
        val response = guardedApp().get("/ping") { header(ApiHeaders.TOKEN, "nope") }

        assertEquals(HttpStatusCode.Unauthorized, response.status)
        assertEquals(0, reached.get())
    }

    @Test
    fun browserRequestsAreRefusedEvenWithAValidToken() = testApplication {
        val response = guardedApp().get("/ping") {
            header(ApiHeaders.TOKEN, token)
            header(HttpHeaders.Origin, "http://evil.example")
        }

        assertEquals(HttpStatusCode.Forbidden, response.status)
        assertEquals(0, reached.get())
    }

    @Test
    fun nonLoopbackHostsAreRefused() = testApplication {
        val response = guardedApp().get("/ping") {
            header(ApiHeaders.TOKEN, token)
            header(HttpHeaders.Host, "evil.example:8080")
        }

        assertEquals(HttpStatusCode.Forbidden, response.status)
        assertEquals(0, reached.get())
    }

    @Test
    fun loopbackHostsAreAccepted() = testApplication {
        val client = guardedApp()

        assertEquals(HttpStatusCode.OK, client.get("/ping") { header(ApiHeaders.TOKEN, token); header(HttpHeaders.Host, "127.0.0.1:51234") }.status)
        assertEquals(HttpStatusCode.OK, client.get("/ping") { header(ApiHeaders.TOKEN, token); header(HttpHeaders.Host, "localhost:51234") }.status)
    }

    @Test
    fun validRequestsReachTheRoute() = testApplication {
        val response = guardedApp().get("/ping") { header(ApiHeaders.TOKEN, token) }

        assertEquals(HttpStatusCode.OK, response.status)
        assertEquals("pong", response.bodyAsText())
        assertEquals(1, reached.get())
    }
}
