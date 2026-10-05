package com.xgetsongs.engine.output

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

class LocalFolderSink(val directory: Path) : OutputSink {
    init {
        Files.createDirectories(directory)
    }

    override suspend fun exists(fileName: String): Boolean = withContext(Dispatchers.IO) {
        Files.exists(resolve(fileName))
    }

    override suspend fun put(fileName: String, source: Path, overwrite: Boolean) {
        withContext(Dispatchers.IO) {
            val target = resolve(fileName)
            if (overwrite) {
                Files.move(source, target, StandardCopyOption.REPLACE_EXISTING)
            } else {
                Files.move(source, target) // throws FileAlreadyExistsException when the file exists
            }
        }
    }

    private fun resolve(fileName: String): Path {
        require(
            fileName.isNotBlank() &&
                fileName != "." && fileName != ".." &&
                fileName == Path.of(fileName).fileName.toString(),
        ) {
            "file name must not contain path segments: $fileName"
        }
        return directory.resolve(fileName)
    }
}
