#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include "process/process_runner.h"

namespace xgs::process {

/// Splits bytes into lines as they arrive, in chunks of any size. A line ends at `\n`, `\r` or `\r\n` (the `\r` and the `\n`
/// of one `\r\n` may arrive in different chunks); the ending is not part of the line. Each line is decoded from UTF-8 as
/// Kotlin's `inputStream.bufferedReader(Charsets.UTF_8).forEachLine` decodes it: kt::decodeUtf8 (text/kt_text.h); a
/// truncated sequence before a line ending is malformed like one at the end of the input.
/// The handler is called from feed and finish, on the caller's thread.
class LineSplitter {
public:
    explicit LineSplitter(LineHandler handler);

    void feed(QByteArrayView bytes);
    /// The end of the input: delivers the last line when it has no line ending (an empty one is not a line).
    void finish();

private:
    void deliver();

    LineHandler m_handler;
    QByteArray m_line;            // the bytes of the current line so far
    bool m_afterCr = false;       // the last byte was a `\r`, so a `\n` next belongs to its ending
};

}  // namespace xgs::process
