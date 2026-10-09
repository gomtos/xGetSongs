package com.xgetsongs.server

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.ToolException
import com.xgetsongs.engine.job.DownloadConcurrency
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.output.OutputSink
import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.ErrorResponse
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.api.JobCreated
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobOptions
import com.xgetsongs.shared.api.JobRequest
import com.xgetsongs.shared.api.ResolveRequest
import com.xgetsongs.shared.api.sseName
import com.xgetsongs.shared.filename.FilenameFormatter
import io.ktor.http.HttpStatusCode
import io.ktor.serialization.kotlinx.json.json
import io.ktor.server.application.Application
import io.ktor.server.application.install
import io.ktor.server.plugins.BadRequestException
import io.ktor.server.plugins.contentnegotiation.ContentNegotiation
import io.ktor.server.plugins.statuspages.StatusPages
import io.ktor.server.request.receive
import io.ktor.server.response.respond
import io.ktor.server.routing.delete
import io.ktor.server.routing.get
import io.ktor.server.routing.post
import io.ktor.server.routing.routing
import io.ktor.server.sse.SSE
import io.ktor.server.sse.sse
import io.ktor.sse.ServerSentEvent
import org.slf4j.Logger
import org.slf4j.LoggerFactory
import java.io.IOException
import java.nio.file.InvalidPathException
import java.nio.file.Path

/** A client mistake that maps straight to an HTTP status with a user-facing message. */
class ApiException(val status: HttpStatusCode, message: String) : Exception(message)

private val jobLog = LoggerFactory.getLogger(JobLog.LOGGER_NAME)

private fun Logger.write(tag: String, line: JobLogLine) = when (line.level) {
    JobLogLevel.DEBUG -> debug("[{}] {}", tag, line.text)
    JobLogLevel.INFO -> info("[{}] {}", tag, line.text)
    JobLogLevel.WARN -> warn("[{}] {}", tag, line.text)
}

/** [jobs] is the registry the routes use; the caller passes its own to be able to ask how many jobs are running. */
fun Application.module(services: Services, config: ServerConfig, jobs: JobRegistry = JobRegistry()) {
    val json = ApiJson.instance
    val resolveCache = ResolveCache()

    install(ContentNegotiation) { json(json) }
    install(SSE)
    install(StatusPages) {
        exception<ApiException> { call, e -> call.respond(e.status, ErrorResponse(e.message.orEmpty())) }
        exception<ResolveException> { call, e ->
            call.respond(HttpStatusCode.UnprocessableEntity, ErrorResponse(e.message.orEmpty()))
        }
        exception<ToolException> { call, e ->
            call.respond(HttpStatusCode.UnprocessableEntity, ErrorResponse(e.message.orEmpty()))
        }
        exception<BadRequestException> { call, _ ->
            call.respond(HttpStatusCode.BadRequest, ErrorResponse("요청 형식이 올바르지 않습니다."))
        }
    }
    installLocalGuard(config)

    routing {
        get("/tools") { call.respond(services.tools.status()) }
        post("/tools/yt-dlp/install") { call.respond(services.tools.installYtDlp()) }
        post("/tools/yt-dlp/update") { call.respond(services.tools.updateYtDlp()) }

        post("/resolve") {
            val request = call.receive<ResolveRequest>()
            call.respond(resolveCache.put(services.resolver.resolve(request.input)))
        }

        post("/jobs") {
            val request = call.receive<JobRequest>()
            val resolved = resolveCache.get(request.resolveId)
                ?: throw ApiException(HttpStatusCode.NotFound, "조회 결과가 만료되었습니다. 다시 조회하세요.")
            val options = request.options
            val isVideo = resolved.kind == InputKind.VIDEO
            if (isVideo && options.singleRank !in FilenameFormatter.MIN_RANK..FilenameFormatter.MAX_RANK) {
                throw ApiException(HttpStatusCode.BadRequest, "순위 번호는 1~999 사이여야 합니다.")
            }
            // A playlist goes into a folder named after it and its title is the album of its files (it wins over a
            // video's own album in the engine); a single video goes straight into the output folder. The album is the
            // original title, not the sanitized folder name. An album name the user typed wins over both of these, for a
            // single video too: it is the album of every file and the name of their folder.
            val isPlaylist = resolved.kind == InputKind.PLAYLIST
            val folder = FilenameFormatter.destinationFolder(isPlaylist, resolved.playlistTitle, options.albumName)
            val album = if (isPlaylist) resolved.playlistTitle else null
            val albumOverride = options.albumName?.trim()?.takeIf { it.isNotEmpty() }
            val sink = sinkFor(config, options, folder)
            val items = resolved.items
                .filter { it.available && (request.ranks == null || it.rank in request.ranks!!) }
                .map { if (isVideo) it.copy(rank = options.singleRank) else it }
            if (items.isEmpty()) throw ApiException(HttpStatusCode.BadRequest, "다운로드할 항목이 없습니다.")

            // The server decides how many items run at once: it is the machine that does the work.
            val concurrency = DownloadConcurrency.automatic()
            val handle = services.downloads.start(
                DownloadRequest(
                    items, sink, options.overwrite, concurrency, album, options.includeRank, options.searchLyricsOnline, albumOverride,
                ),
            )
            val jobId = jobs.register(handle)
            jobLog.info("[{}] {}", JobLog.shortId(jobId), JobLog.started(jobId, resolved.kind, items.size, options, concurrency))
            call.respond(HttpStatusCode.Created, JobCreated(jobId))
        }

        sse("/jobs/{id}/events") {
            val id = call.parameters["id"].orEmpty()
            val handle = jobs.claim(id)
            if (handle == null) {
                val message = if (jobs.exists(id)) "이미 다른 곳에서 이 작업의 이벤트를 받고 있습니다." else "작업을 찾을 수 없습니다."
                send(ServerSentEvent(data = json.encodeToString(ErrorResponse.serializer(), ErrorResponse(message)), event = "error"))
                return@sse
            }
            val tag = JobLog.shortId(id)
            // The file name each rank was started with: the reasons of its later events are stripped of it.
            val fileNames = HashMap<Int, String>()
            var ended = false
            try {
                for (event in handle.events) {
                    if (event is JobEvent.ItemStarted) fileNames[event.rank] = event.fileName
                    JobLog.describe(event, fileNames)?.let { jobLog.write(tag, it) }
                    send(ServerSentEvent(data = json.encodeToString(JobEvent.serializer(), event), event = event.sseName))
                }
                ended = true
            } finally {
                // Not a catch: the cancellation of a closed connection has to go on its way. A stream that ends because the
                // server is stopping is not a dropped connection.
                if (!ended && !jobs.isClosing) jobLog.warn(JobLog.eventsDisconnected(id))
            }
            // Only reached when the job ended; a dropped connection leaves the job cancellable.
            jobs.remove(id)
        }

        delete("/jobs/{id}") {
            val id = call.parameters["id"].orEmpty()
            if (!jobs.cancel(id)) throw ApiException(HttpStatusCode.NotFound, "작업을 찾을 수 없습니다.")
            jobLog.info("[{}] {}", JobLog.shortId(id), JobLog.CANCEL_REQUESTED)
            call.respond(HttpStatusCode.NoContent)
        }
    }
}

/** [folder] is a ready-made folder name inside the output folder, or null to save in the output folder itself. */
private fun sinkFor(config: ServerConfig, options: JobOptions, folder: String?): OutputSink = when (config.mode) {
    ServerMode.LOCAL -> {
        val dir = options.outputDir?.takeIf { it.isNotBlank() }
            ?: throw ApiException(HttpStatusCode.BadRequest, "출력 폴더를 지정하세요.")
        val path = try {
            Path.of(dir)
        } catch (e: InvalidPathException) {
            throw ApiException(HttpStatusCode.BadRequest, "출력 폴더 경로가 올바르지 않습니다.")
        }
        if (!path.isAbsolute) throw ApiException(HttpStatusCode.BadRequest, "출력 폴더는 절대 경로여야 합니다.")
        try {
            LocalFolderSink(if (folder == null) path else path.resolve(folder))
        } catch (e: IOException) {
            throw ApiException(HttpStatusCode.BadRequest, "출력 폴더를 만들 수 없습니다: ${e.message}")
        }
    }
    ServerMode.HOSTED -> {
        if (options.outputDir != null) {
            throw ApiException(HttpStatusCode.BadRequest, "이 서버에서는 출력 폴더를 지정할 수 없습니다.")
        }
        throw ApiException(HttpStatusCode.NotImplemented, "웹 배포용 출력은 아직 지원하지 않습니다.")
    }
}
