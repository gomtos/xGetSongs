package com.xgetsongs.app.settings

/**
 * Where the remembered options live. Kept behind an interface so `commonMain` stays free of platform file APIs: the
 * desktop app brings a file, a web client would bring the browser's storage.
 */
interface SettingsStore {
    /** The saved options, or the defaults when there are none or they cannot be read. Never throws. */
    fun load(): UserSettings

    /** Stores [settings]. A store that cannot write (read-only profile, full disk) does nothing. Never throws. */
    fun save(settings: UserSettings)
}

/** Remembers nothing: loads the defaults and drops what it is asked to save. */
object NoSettingsStore : SettingsStore {
    override fun load(): UserSettings = UserSettings()

    override fun save(settings: UserSettings) = Unit
}
