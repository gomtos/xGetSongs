#pragma once

#include <QString>
#include <QStringList>

#include "model/models.h"

// Port of engine/.../ytdlp/YtDlpCommands.kt: every yt-dlp command line. Commands are argument lists (never shell strings) and
// the URL always comes last, after `--`, so it can never be read as an option.
//
// Paths are the strings the caller passes, used as they are (the Kotlin original takes java.nio.file.Path and so normalises
// them; this port does not): the caller must pass normalised Windows absolute paths, a drive letter and parts separated by `\`,
// without a trailing separator. A video id is joined to the output directory as it is (downloadCommand refuses one that is not a plain
// file name part; a real id is 11 characters of [A-Za-z0-9_-]).
namespace xgs::ytdlp {

// Every function throws std::invalid_argument when tools.ytDlp is absent.
QStringList resolvePlaylistCommand(const ToolPaths& tools, const QString& url);   // the entries of a playlist, flat (fast)
QStringList resolveVideoCommand(const ToolPaths& tools, const QString& url);      // the full metadata of one video

// Downloads the AAC audio stream of `url` as it is (itag 140, no re-encoding) as `<outputDir>\<videoId>.m4a`, with the thumbnail
// converted to JPEG as `<outputDir>\<videoId>.jpg` and the video's info as `<outputDir>\<videoId>.info.json`.
// Also throws std::invalid_argument when videoId is empty or contains one of `\ / : * ? " < > |` or a code unit below U+0020: the
// id comes from yt-dlp's output and is joined into a file path. Kotlin (Path.resolve) throws for `* ? " < > |`, for `:` alone and
// for most control characters next to other text; this port is STRICTER where Kotlin does not throw: for an empty id, `\`, `/`
// (Kotlin changes the directory or the separators) and `:` or a control character inside an id (Kotlin reads `a:b` as a drive-
// relative path and drops outputDir).
QStringList downloadCommand(const ToolPaths& tools, const QString& url, const QString& outputDir, const QString& videoId);

}  // namespace xgs::ytdlp
