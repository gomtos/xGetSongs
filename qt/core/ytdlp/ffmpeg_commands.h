#pragma once

#include <QString>
#include <QStringList>

#include <optional>

// Port of engine/.../ytdlp/FfmpegCommands.kt: every ffmpeg command line (argument lists, never shell strings).
//
// Paths are the strings the caller passes, used as they are (the Kotlin original takes java.nio.file.Path and so normalises
// them; this port does not): the caller must pass normalised Windows absolute paths, a drive letter and parts separated by `\`,
// without a trailing separator.
namespace xgs::ytdlp {

// Copies the audio of `input` into `output` without re-encoding and writes the MP4 tags (the lyrics and the comment too) read from
// the ffmetadata file. With a cover the picture is cropped to a centered square, re-encoded as JPEG and attached as the front
// cover. Throws std::invalid_argument unless `output` ends in `.m4a` (any case): ffmpeg picks the muxer from the extension.
QStringList tagCommand(const QString& ffmpeg, const QString& input, const std::optional<QString>& cover,
                       const QString& metadataFile, const QString& output);

}  // namespace xgs::ytdlp
