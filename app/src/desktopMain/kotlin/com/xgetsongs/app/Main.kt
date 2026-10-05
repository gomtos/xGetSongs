package com.xgetsongs.app

import androidx.compose.runtime.remember
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.xgetsongs.app.api.HttpXgsApi
import com.xgetsongs.app.api.configureXgs
import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.ui.App
import com.xgetsongs.server.LocalServer
import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import java.nio.file.Path

fun main() {
    val server = LocalServer.start(appDataDirectory())
    val http = HttpClient(CIO) {
        configureXgs(token = server.token, baseUrl = "http://127.0.0.1:${server.port}")
        // No request timeout: resolving a big playlist and the SSE stream can legitimately be silent for a while.
        engine { requestTimeout = 0 }
    }
    val uiScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    application {
        val holder = remember {
            AppStateHolder(
                api = HttpXgsApi(http),
                scope = uiScope,
                defaultOutputDir = defaultOutputDirectory().toString(),
                settings = JsonSettingsStore(appDataDirectory().resolve("settings.json")),
            )
        }
        Window(
            onCloseRequest = {
                // Before the scope goes away: a save that is still waiting for its quiet period would be lost.
                holder.flushSettings()
                uiScope.cancel()
                http.close()
                server.stop()
                exitApplication()
            },
            title = "xGetSongs",
            state = rememberWindowState(width = 1000.dp, height = 760.dp),
        ) {
            App(holder, pickFolder = ::pickFolder)
        }
    }
}

/** Where the app keeps its own files: the yt-dlp it installed and temporary download folders. */
internal fun appDataDirectory(): Path {
    val appData = System.getenv("APPDATA")
    return if (appData != null) Path.of(appData, "xGetSongs") else Path.of(System.getProperty("user.home"), ".xgetsongs")
}

internal fun defaultOutputDirectory(): Path = Path.of(System.getProperty("user.home"), "Music", "xGetSongs")
