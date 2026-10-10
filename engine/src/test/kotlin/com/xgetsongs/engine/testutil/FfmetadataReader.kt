package com.xgetsongs.engine.testutil

/**
 * A simplified reader for an ffmetadata file, so a test can see the values that `Ffmetadata.render` intends to hand to
 * ffmpeg: the first line is the header, a backslash makes the next character part of the text (a line break too), an
 * unescaped line feed ends the entry and the first unescaped `=` splits the key from the value. It is not exactly what
 * ffmpeg does: it decodes the backslash escapes pair by pair, while ffmpeg 8.x joins a line that ends in a backslash
 * with the next line even when that backslash is itself escaped. So it cannot catch a regression of the
 * trailing-backslash workaround; `FfmetadataTest` and `RealFfmpegTaggingIntegrationTest` cover that.
 */
object FfmetadataReader {
    fun read(text: String): Map<String, String> {
        val values = linkedMapOf<String, String>()
        var i = text.indexOf('\n') + 1
        while (i < text.length) {
            val key = StringBuilder()
            val value = StringBuilder()
            var inValue = false
            while (i < text.length) {
                val c = text[i++]
                if (c == '\\' && i < text.length) {
                    (if (inValue) value else key).append(text[i++])
                } else if (c == '\n') {
                    break
                } else if (c == '=' && !inValue) {
                    inValue = true
                } else {
                    (if (inValue) value else key).append(c)
                }
            }
            if (inValue) values[key.toString()] = value.toString()
        }
        return values
    }
}
