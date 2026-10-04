package com.xgetsongs.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.xgetsongs.app.state.ItemRow
import com.xgetsongs.app.state.ItemStatus
import com.xgetsongs.app.state.rankLabel
import com.xgetsongs.app.state.statusLabel

@Composable
fun PreviewList(rows: List<ItemRow>, modifier: Modifier = Modifier) {
    LazyColumn(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        items(rows, key = { "${it.item.rank}-${it.item.videoId}" }) { row -> PreviewRow(row) }
    }
}

@Composable
private fun PreviewRow(row: ItemRow) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(rankLabel(row.item.rank), fontFamily = FontFamily.Monospace, modifier = Modifier.width(40.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                row.fileName ?: row.item.title,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                color = if (row.item.available) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.outline,
            )
            if (row.item.available && row.item.lowConfidence) {
                Text(
                    "⚠ 가수명을 채널명에서 추정했습니다",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.tertiary,
                )
            }
        }
        StatusCell(row.status, modifier = Modifier.width(220.dp))
    }
}

@Composable
private fun StatusCell(status: ItemStatus, modifier: Modifier = Modifier) {
    Column(modifier = modifier) {
        Text(
            statusLabel(status),
            style = MaterialTheme.typography.bodySmall,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            color = if (status is ItemStatus.Failed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface,
        )
        if (status is ItemStatus.Downloading && status.percent != null) {
            LinearProgressIndicator(
                progress = { (status.percent / 100.0).toFloat() },
                modifier = Modifier.fillMaxWidth().padding(top = 2.dp),
            )
        }
    }
}
