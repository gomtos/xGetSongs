#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

// Port of engine/.../ytdlp/VideoInfoFile.kt: reads the info file `<videoId>.info.json` that yt-dlp writes next to the
// audio (`--write-info-json`). The engine only needs the album, the description and the duration.
namespace xgs::ytdlp {

// Kotlin VideoInfo: a field is empty when the file has no usable value for it.
struct VideoInfo {
    std::optional<QString> album;
    std::optional<QString> description;
    std::optional<int> duration;   // whole seconds
    bool operator==(const VideoInfo&) const = default;
};

/// VideoInfoFile.read: the VideoInfo of the video in the file at [filePath] (used as given), from the keys of the root
/// object only:
///  - `album`, trimmed; empty when it is absent, null, not a string, blank or yt-dlp's `NA` placeholder;
///  - `description` as written, with the blank lines in front of the first text and the white space after the last text
///    dropped; empty when it is absent, null, not a string or blank;
///  - `duration`, a JSON number rounded to whole seconds (ties towards positive infinity); empty when it is absent, null,
///    not a number (a string such as "258" is not), not positive once rounded or larger than INT_MAX.
/// All are empty when the file is missing, unreadable, a directory, not valid UTF-8 (a BOM is not skipped) or not valid
/// JSON (xgs::json), or its root is not an object. Never throws.
VideoInfo readVideoInfo(const QString& filePath);

/// The same from the bytes of a file (what readVideoInfo does once it has read the file).
VideoInfo parseVideoInfo(const QByteArray& fileBytes);

/// VideoInfoFile.readAlbum: the album of readVideoInfo.
std::optional<QString> readVideoAlbum(const QString& filePath);

}  // namespace xgs::ytdlp
