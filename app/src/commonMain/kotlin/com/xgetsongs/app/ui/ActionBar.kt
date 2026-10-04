package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.state.Phase
import com.xgetsongs.app.state.UiState
import com.xgetsongs.app.state.summaryText

@Composable
fun ActionBar(state: UiState, holder: AppStateHolder) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        when (state.phase) {
            Phase.PREVIEW -> {
                Button(
                    enabled = state.rows.any { it.item.available },
                    onClick = holder::startDownload,
                ) { Text("다운로드 시작") }
                OutlinedButton(onClick = holder::reset) { Text("새로 시작") }
            }
            Phase.RUNNING -> Button(onClick = holder::cancel) { Text("취소") }
            Phase.FINISHED -> {
                summaryText(state)?.let { Text(it) }
                if (state.failedRanks.isNotEmpty()) {
                    Button(onClick = holder::retryFailed) { Text("실패 항목 재시도") }
                }
                OutlinedButton(onClick = holder::startDownload) { Text("다시 다운로드") }
                OutlinedButton(onClick = holder::reset) { Text("새로 시작") }
            }
            Phase.IDLE, Phase.RESOLVING -> Unit
        }
    }
}
