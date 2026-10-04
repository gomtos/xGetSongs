package com.xgetsongs.engine.output

import java.nio.file.Path

/**
 * Where finished mp3 files end up. The desktop app writes to a folder ([LocalFolderSink]); a web
 * deployment will hand files to the browser instead.
 */
interface OutputSink {
    suspend fun exists(fileName: String): Boolean

    /** Moves the finished file at [source] into the sink under [fileName]. */
    suspend fun put(fileName: String, source: Path, overwrite: Boolean)
}
