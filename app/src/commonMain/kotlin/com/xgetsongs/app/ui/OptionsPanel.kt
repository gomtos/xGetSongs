package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Checkbox
import androidx.compose.material3.LocalMinimumInteractiveComponentSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.AppStateHolder
import com.xgetsongs.app.state.Phase
import com.xgetsongs.app.state.UiState
import com.xgetsongs.app.state.albumNameText
import com.xgetsongs.app.state.destinationLabel
import com.xgetsongs.app.state.destinationPath
import com.xgetsongs.app.state.rankFromInput
import com.xgetsongs.app.state.rankInputText
import com.xgetsongs.shared.api.InputKind
import kotlinx.coroutines.launch

/** A checkbox with its label, as one unit that a [FlowRow] never splits. */
@Composable
private fun OptionCheckbox(label: String, checked: Boolean, onChange: (Boolean) -> Unit, enabled: Boolean) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = checked, onCheckedChange = onChange, enabled = enabled)
        Text(label)
    }
}

/**
 * The options under the preview. [pickFolder] opens the platform folder chooser; [openFolder] shows the given
 * destination path in the platform's file manager (the platform opens the nearest existing folder when it is missing).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun OptionsPanel(
    state: UiState,
    holder: AppStateHolder,
    pickFolder: suspend (String) -> String?,
    openFolder: (String) -> Unit,
) {
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
        destinationLabel(state)?.let { label ->
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(
                    label,
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f),
                )
                // At the far right, under the 폴더 선택 button. Always enabled: looking at the folder is harmless while a job runs.
                destinationPath(state)?.let { path ->
                    // A compact button: without these the button reserves 40dp (48dp touch target) and the row would grow.
                    CompositionLocalProvider(LocalMinimumInteractiveComponentSize provides 0.dp) {
                        OutlinedButton(
                            onClick = { openFolder(path) },
                            modifier = Modifier.heightIn(min = 28.dp),
                            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp),
                        ) { Text("탐색기에서 보기", style = MaterialTheme.typography.bodySmall) }
                    }
                }
            }
        }
        // The field shows the default once something is resolved; leaving it alone (or clearing it) keeps the old
        // behaviour. The explanation is a supporting text, not a placeholder: a placeholder only shows while focused.
        OutlinedTextField(
            value = albumNameText(state),
            onValueChange = holder::onAlbumName,
            label = { Text("앨범명 (폴더명)") },
            supportingText = { Text("저장 폴더 이름과 앨범 태그가 모두 이 이름입니다. 비우면 재생목록 제목(영상 1개는 폴더 없이 영상 자체의 앨범)") },
            singleLine = true,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth(),
        )
        // A checkbox moves to the next line together with its label when the window is too narrow for all three.
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalArrangement = Arrangement.spacedBy(0.dp),
        ) {
            OptionCheckbox("기존 파일 덮어쓰기", state.overwrite, holder::onOverwrite, enabled)
            OptionCheckbox("파일명에 순번 포함", state.includeRank, holder::onIncludeRank, enabled)
            OptionCheckbox("가사가 없으면 인터넷에서 검색", state.searchLyricsOnline, holder::onSearchLyricsOnline, enabled)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("동시 다운로드", modifier = Modifier.widthIn(min = 100.dp), maxLines = 1, softWrap = false)
            Text("코어 수의 70% (자동)", style = MaterialTheme.typography.bodyMedium, maxLines = 1, softWrap = false)
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
