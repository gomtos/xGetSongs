package com.xgetsongs.engine.testutil

/**
 * Reads an ffmetadata file the way ffmpeg does, so a test can see the values that reach the tags: the first line is the
 * header, a backslash makes the next character part of the text (a line break too), an unescaped line feed ends the
 * entry and the first unescaped `=` splits the key from the value.
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
