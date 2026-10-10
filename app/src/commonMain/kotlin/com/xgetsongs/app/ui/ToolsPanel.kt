package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.FORMAT_FAILURE_HELP
import com.xgetsongs.app.state.Phase
import com.xgetsongs.app.state.UPDATE_YT_DLP_LABEL
import com.xgetsongs.app.state.UiState
import com.xgetsongs.shared.api.ToolInfo

/** Shows which external tools are available and offers to install or update yt-dlp. */
@Composable
fun ToolsPanel(state: UiState, onInstall: () -> Unit, onUpdate: () -> Unit) {
    var confirmInstall by remember { mutableStateOf(false) }
    val tools = state.tools

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (tools == null) {
                Text("도구 확인 중…", style = MaterialTheme.typography.bodyMedium)
            } else {
                Row(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    ToolStatus("yt-dlp", tools.ytDlp)
                    ToolStatus("ffmpeg", tools.ffmpeg)
                    ToolStatus("JS 런타임", tools.jsRuntime)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                    if (!tools.ytDlp.found) {
                        Button(enabled = !state.toolBusy, onClick = { confirmInstall = true }) { Text("yt-dlp 설치") }
                    } else {
                        OutlinedButton(enabled = !state.toolBusy && state.phase != Phase.RUNNING, onClick = onUpdate) {
                            Text(UPDATE_YT_DLP_LABEL)
                        }
                        Text(
                            FORMAT_FAILURE_HELP,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.weight(1f),
                        )
                    }
                }
                if (!tools.ffmpeg.found) {
                    Text(
                        "ffmpeg가 필요합니다. 예: winget install Gyan.FFmpeg",
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
                if (!tools.jsRuntime.found) {
                    Text(
                        "YouTube를 읽으려면 Node.js 22 이상 또는 Deno 2.3 이상이 필요합니다.",
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
            if (state.toolBusy) Text("작업 중…", style = MaterialTheme.typography.bodySmall)
            state.toolMessage?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
        }
    }

    if (confirmInstall) {
        AlertDialog(
            onDismissRequest = { confirmInstall = false },
            title = { Text("yt-dlp 설치") },
            text = {
                Text("GitHub(github.com/yt-dlp/yt-dlp)에서 yt-dlp.exe를 내려받아 이 앱 전용 폴더에 저장합니다. 계속할까요?")
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmInstall = false
                    onInstall()
                }) { Text("내려받기") }
            },
            dismissButton = { TextButton(onClick = { confirmInstall = false }) { Text("취소") } },
        )
    }
}

@Composable
private fun ToolStatus(name: String, info: ToolInfo) {
    val text = if (info.found) "$name ${info.version.orEmpty()} ✓" else "$name ✗"
    Text(
        text,
        style = MaterialTheme.typography.bodyMedium,
        color = if (info.found) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.error,
    )
}
