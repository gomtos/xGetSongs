package com.xgetsongs.app

import com.xgetsongs.app.settings.SettingsStore
import com.xgetsongs.app.settings.UserSettings
import kotlinx.serialization.json.Json
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

/**
 * Keeps the options in a small UTF-8 JSON [file]. Neither function throws, whatever goes wrong: a file that is missing,
 * unreadable, not valid UTF-8 or not what was expected means the defaults, and a profile that cannot be written to means
 * the options are not kept. (This is blocking code, so nothing in it can raise a `CancellationException`; catching
 * every `Exception` cannot swallow one. A throw from [load] would crash the start-up, one from [save] would keep the
 * window from closing.)
 */
class JsonSettingsStore(private val file: Path) : SettingsStore {
    private val json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
        prettyPrint = true
    }

    override fun load(): UserSettings = try {
        // Strict: bytes that are not UTF-8 (a file saved in the ANSI code page) must give the defaults, not a folder name
        // full of U+FFFD. A byte order mark, which Notepad and PowerShell like to add, is not part of the JSON.
        val text = Files.readAllBytes(file).decodeToString(throwOnInvalidSequence = true).removePrefix("\uFEFF")
        val stored = json.decodeFromString(UserSettings.serializer(), text)
        stored.copy(concurrency = stored.concurrency.coerceIn(UserSettings.MIN_CONCURRENCY, UserSettings.MAX_CONCURRENCY))
    } catch (e: Exception) {
        UserSettings()
    }

    /** Writes next to the target and moves it over, so a crash never leaves half a file behind. */
    override fun save(settings: UserSettings) {
        var temp: Path? = null
        try {
            val name = file.fileName ?: return // a file system root has no name to build the temp file's from
            temp = file.resolveSibling("$name.tmp")
            file.toAbsolutePath().parent?.let { Files.createDirectories(it) }
            Files.write(temp, json.encodeToString(UserSettings.serializer(), settings).toByteArray(Charsets.UTF_8))
            Files.move(temp, file, StandardCopyOption.REPLACE_EXISTING)
        } catch (e: Exception) {
            // Nothing sensible to do: the app works the same without remembering its options.
        } finally {
            try {
                temp?.let { Files.deleteIfExists(it) }
            } catch (e: Exception) {
                // A leftover temp file is overwritten by the next save.
            }
        }
    }
}
