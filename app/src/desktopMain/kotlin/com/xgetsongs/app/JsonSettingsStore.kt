package com.xgetsongs.app

import com.xgetsongs.app.settings.SettingsStore
import com.xgetsongs.app.settings.UserSettings
import kotlinx.serialization.json.Json
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

/**
 * Keeps the options in a small UTF-8 JSON [file]. Neither function throws: a file that is missing, unreadable or not
 * what was expected means the defaults, and a profile that cannot be written to means the options are not kept.
 */
class JsonSettingsStore(private val file: Path) : SettingsStore {
    private val json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
        prettyPrint = true
    }

    override fun load(): UserSettings = try {
        val stored = json.decodeFromString(UserSettings.serializer(), Files.readAllBytes(file).decodeToString())
        stored.copy(concurrency = stored.concurrency.coerceIn(UserSettings.MIN_CONCURRENCY, UserSettings.MAX_CONCURRENCY))
    } catch (e: IOException) {
        UserSettings()
    } catch (e: IllegalArgumentException) {
        // Not JSON, or JSON of another shape: kotlinx.serialization reports both as a SerializationException.
        UserSettings()
    }

    /** Writes next to the target and moves it over, so a crash never leaves half a file behind. */
    override fun save(settings: UserSettings) {
        val temp = file.resolveSibling(file.fileName.toString() + ".tmp")
        try {
            file.toAbsolutePath().parent?.let { Files.createDirectories(it) }
            Files.write(temp, json.encodeToString(UserSettings.serializer(), settings).toByteArray(Charsets.UTF_8))
            Files.move(temp, file, StandardCopyOption.REPLACE_EXISTING)
        } catch (e: IOException) {
            // Nothing sensible to do: the app works the same without remembering its options.
        } finally {
            try {
                Files.deleteIfExists(temp)
            } catch (e: IOException) {
                // A leftover temp file is overwritten by the next save.
            }
        }
    }
}
