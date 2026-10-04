package com.xgetsongs.app

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.swing.Swing
import kotlinx.coroutines.withContext
import java.io.File
import javax.swing.JFileChooser

/** Opens Swing's folder chooser on the UI thread. Returns the chosen absolute path or null if cancelled. */
suspend fun pickFolder(initial: String): String? = withContext(Dispatchers.Swing) {
    val chooser = JFileChooser(File(initial).takeIf { it.isDirectory }).apply {
        fileSelectionMode = JFileChooser.DIRECTORIES_ONLY
        dialogTitle = "출력 폴더 선택"
    }
    if (chooser.showOpenDialog(null) == JFileChooser.APPROVE_OPTION) chooser.selectedFile.absolutePath else null
}
