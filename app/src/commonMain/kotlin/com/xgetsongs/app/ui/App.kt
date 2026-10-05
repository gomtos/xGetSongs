package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.AppStateHolder

/**
 * The whole screen. [pickFolder] opens a platform folder chooser with the current folder preselected
 * and returns the chosen path, or null when the user cancels. [openFolder] shows a folder in the platform's
 * file manager (the nearest existing parent when the path does not exist yet); it must not block.
 */
@Composable
fun App(
    holder: AppStateHolder,
    pickFolder: suspend (initial: String) -> String?,
    openFolder: (path: String) -> Unit,
) {
    val state by holder.state.collectAsState()
    LaunchedEffect(Unit) { holder.refreshTools() }

    MaterialTheme {
        Surface(modifier = Modifier.fillMaxSize()) {
            Column(
                modifier = Modifier.fillMaxSize().padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                ToolsPanel(state, onInstall = holder::installYtDlp, onUpdate = holder::updateYtDlp)
                InputPanel(state, onInput = holder::onInput, onResolve = holder::resolve)
                state.error?.let { ErrorBanner(it, onDismiss = holder::dismissError) }

                if (state.resolved != null) {
                    ResolveInfo(state, onSwitchToVideoOnly = holder::switchToVideoOnly)
                    OptionsPanel(state, holder, pickFolder, openFolder)
                    PreviewList(state.rows, modifier = Modifier.weight(1f))
                    ActionBar(state, holder)
                } else {
                    Spacer(Modifier.weight(1f))
                }
            }
        }
    }
}
