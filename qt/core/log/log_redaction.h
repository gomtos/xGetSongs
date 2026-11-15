#pragma once

#include <QString>
#include <QStringList>

// Port of shared/.../log/LogRedaction.kt: keeps file names and paths out of the log files. Messages from the file system or
// from yt-dlp name the files they worked on, and a song's file name is its title: neither belongs in a log the user may send
// to someone. Paths are found by a small scanner (no regular expression, no recursion), so a path of thousands of parts
// cannot overflow a stack. Lengths are UTF-16 units, as in Kotlin; qt/tools/differential (kinds R and N) compares them.
namespace xgs::redaction {

// Longer text is cut to this many units (and ends with `…`) before it is looked at.
inline constexpr int MaxTextLength = 4000;

QString pathPlaceholder();        // "<경로>"
QString fileNamePlaceholder();    // "<파일명>"

// Every Windows path in text replaced by pathPlaceholder(), after a text over MaxTextLength is cut. A path is a drive (`C:`)
// followed by `\` or `/` and one or more parts (separated by one or two `\` or `/`), also behind `\\?\` or `\\.\` and as a
// `file:///C:/...` URI; or a UNC path `\\host\...`, also as `\\?\UNC\host\...`. A part ends at a character Windows forbids
// in a name, a line break, ` ->` and the start of another drive. Relative paths are left alone.
QString redactPaths(const QString& text);

// redactPaths, then each of knownNames and the same name without its extension (names shorter than 3 units or blank are
// skipped), longest first, wherever it appears and whatever the case, replaced by fileNamePlaceholder(). Plain text, not
// patterns.
QString redact(const QString& text, const QStringList& knownNames);

}  // namespace xgs::redaction
