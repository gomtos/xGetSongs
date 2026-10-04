package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilterChip
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.state.Phase
import com.xgetsongs.app.state.UiState
import com.xgetsongs.app.state.rankFromInput
import com.xgetsongs.app.state.rankInputText
import com.xgetsongs.shared.api.InputKind
import kotlinx.coroutines.launch

@Composable
fun OptionsPanel(state: UiState, holder: AppStateHolder, pickFolder: suspend (String) -> String?) {
    val scope = rememberCoroutineScope()
    val enabled = state.phase != Phase.RUNNING
    // The raw text is kept here so the field can be empty while the user retypes the number.
    var rankText by remember { mutableStateOf(state.singleRank.toString()) }
    LaunchedEffect(state.singleRank) {
        if (rankFromInput(rankText) != state.singleRank) rankText = state.singleRank.toString()
    }

    Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OutlinedTextField(
                value = state.outputDir,
                onValueChange = holder::onOutputDir,
                label = { Text("출력 폴더") },
                singleLine = true,
                enabled = enabled,
                modifier = Modifier.weight(1f),
            )
            OutlinedButton(
                enabled = enabled,
                onClick = { scope.launch { pickFolder(state.outputDir)?.let(holder::onOutputDir) } },
            ) { Text("폴더 선택") }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Checkbox(checked = state.overwrite, onCheckedChange = holder::onOverwrite, enabled = enabled)
            Text("기존 파일 덮어쓰기")
            Text("동시 다운로드", modifier = Modifier.width(100.dp))
            (1..4).forEach { n ->
                FilterChip(
                    selected = state.concurrency == n,
                    onClick = { holder.onConcurrency(n) },
                    label = { Text("$n") },
                    enabled = enabled,
                )
            }
            if (state.resolved?.kind == InputKind.VIDEO) {
                OutlinedTextField(
                    value = rankText,
                    onValueChange = { raw ->
                        rankText = rankInputText(raw)
                        rankFromInput(rankText)?.let(holder::onSingleRank)
                    },
                    label = { Text("순위 번호") },
                    singleLine = true,
                    enabled = enabled,
                    modifier = Modifier.width(120.dp),
                )
            }
        }
    }
}
