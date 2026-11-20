#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include "process/process_runner.h"

namespace xgs::process {

/// Splits bytes into lines as they arrive, in chunks of any size. A line ends at `\n`, `\r` or `\r\n` (the `\r` and the `\n`
/// of one `\r\n` may arrive in different chunks); the ending is not part of the line. Each line is decoded from UTF-8 as
/// Kotlin's `inputStream.bufferedReader(Charsets.UTF_8).forEachLine` decodes it (observed through the differential oracle,
/// kind W op ln):
///   - every malformed sequence becomes one U+FFFD; a malformed sequence is a lead byte with the continuation bytes that may
///     follow it so far (a truncated sequence, also at the end of the input or before a line ending), or a single byte that
///     cannot start a sequence (80..C1, F5..FF);
///   - the second byte after E0 must be A0..BF, after F0 90..BF, after F4 80..8F (else the lead alone is malformed); after ED
///     it may be 80..BF, and a complete ED A0..BF xx (an encoded surrogate) is one U+FFFD for all three bytes;
///   - a byte order mark is kept (U+FEFF), NUL is kept.
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
