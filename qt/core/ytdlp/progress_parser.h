#pragma once

#include <QString>
#include <QStringView>

#include <optional>

// Port of engine/.../ytdlp/ProgressParser.kt: parses the lines of yt-dlp's `--progress-template` (ProgressPrefix, then
// `|status|downloaded|total|estimated total`).
namespace xgs::ytdlp {

inline constexpr QStringView ProgressPrefix = u"XGSP";   // YtDlpCommands.PROGRESS_PREFIX

// Kotlin ProgressUpdate.Downloading; percent is empty when yt-dlp does not know the total size yet.
struct DownloadProgress {
    std::optional<double> percent;
    bool operator==(const DownloadProgress&) const = default;
};

std::optional<DownloadProgress> parseProgress(const QString& line);

}  // namespace xgs::ytdlp
