package com.xgetsongs.app.state

import com.xgetsongs.app.settings.SettingsStore
import com.xgetsongs.app.settings.UserSettings

/** Hands out [stored] on [load] and records every [save], in order. */
class FakeSettingsStore(var stored: UserSettings = UserSettings()) : SettingsStore {
    val saved = mutableListOf<UserSettings>()
    var loadCalls = 0

    override fun load(): UserSettings {
        loadCalls++
        return stored
    }

    override fun save(settings: UserSettings) {
        saved += settings
    }
}
