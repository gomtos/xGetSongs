package com.xgetsongs.server

import com.xgetsongs.engine.DownloadService
import com.xgetsongs.engine.Resolver
import com.xgetsongs.engine.ToolManager

/**
 * LOCAL: the desktop app's embedded server. Clients may choose the output folder.
 * HOSTED: a deployed web server. Clients must never choose server-side paths.
 */
enum class ServerMode { LOCAL, HOSTED }

data class ServerConfig(
    val token: String,
    val mode: ServerMode = ServerMode.LOCAL,
)

/** The engine pieces the routes talk to; tests replace them with fakes. */
class Services(
    val resolver: Resolver,
    val downloads: DownloadService,
    val tools: ToolManager,
)
