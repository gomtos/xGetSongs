#pragma once

#include <QMap>
#include <QString>

// Port of engine/src/test/.../testutil/FfmetadataReader.kt: a simplified reader for an ffmetadata file, so a test can see the
// values that renderFfmetadata intends to hand to ffmpeg: the first line is the header, a backslash makes the next character
// part of the text (a line break too), an unescaped line feed ends the entry and the first unescaped `=` splits the key from
// the value. It is not exactly what ffmpeg does: it decodes the backslash escapes pair by pair, while ffmpeg 8.x joins a line
// that ends in a backslash with the next line even when that backslash is itself escaped. So it cannot catch a regression of
// the trailing-backslash workaround; tst_ffmetadata and the real-ffmpeg integration test cover that.
namespace xgs::testsupport {

/// The entries of an ffmetadata text by key (a later entry of a key replaces an earlier one, as in Kotlin's map). Kotlin's
/// map keeps the order of the entries; no test looks at it.
QMap<QString, QString> readFfmetadata(const QString& text);

}  // namespace xgs::testsupport
