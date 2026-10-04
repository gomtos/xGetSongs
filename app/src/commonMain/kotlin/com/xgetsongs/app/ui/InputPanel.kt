package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.UiState
import com.xgetsongs.shared.api.InputKind

@Composable
fun InputPanel(state: UiState, onInput: (String) -> Unit, onResolve: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        OutlinedTextField(
            value = state.input,
            onValueChange = onInput,
            label = { Text("재생목록 ID 또는 영상 주소") },
            singleLine = true,
            modifier = Modifier.weight(1f),
        )
        Button(enabled = state.canResolve && state.input.isNotBlank(), onClick = onResolve) { Text("조회") }
    }
}

@Composable
fun ErrorBanner(message: String, onDismiss: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.weight(1f))
        TextButton(onClick = onDismiss) { Text("닫기") }
    }
}

/** Playlist title, warnings, and the "this video only" shortcut. */
@Composable
fun ResolveInfo(state: UiState, onSwitchToVideoOnly: () -> Unit) {
    val resolved = state.resolved ?: return
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val title = resolved.playlistTitle ?: "단일 영상"
        val count = if (resolved.kind == InputKind.PLAYLIST) " · ${resolved.items.size}개" else ""
        Text(title + count, style = MaterialTheme.typography.titleMedium)
        if (resolved.truncated) {
            Text("999개를 넘어 앞 999개만 표시합니다.", color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(start = 4.dp))
        }
        if (resolved.alsoVideoId != null && state.canResolve) {
            TextButton(onClick = onSwitchToVideoOnly) { Text("이 영상만 받기") }
        }
    }
}
