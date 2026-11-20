#pragma once

#include <QString>

#include <optional>
#include <stop_token>

#include "model/models.h"
#include "outcome.h"
#include "process/process_runner.h"
#include "tools/tool_locator.h"

// Port of engine/.../ytdlp/YtDlpResolver.kt: runs yt-dlp to turn a playlist or a video into ranked items, and fetches one
// video's metadata. yt-dlp's JSON is read as Kotlin's `Json { ignoreUnknownKeys = true }` decodes it into RawPlaylist/RawEntry
// (json::ParseOptions::typed). Texts, the order of the checks and MAX_RANK are Kotlin's.
namespace xgs::ytdlp {

/// The metadata of one video that the title parser can use.
struct VideoMeta {
    std::optional<QString> title, channel, artist, track;
    bool operator==(const VideoMeta&) const = default;
};

class VideoMetadataSource {
public:
    virtual ~VideoMetadataSource() = default;
    /// Nothing when the metadata cannot be fetched (cancellation included); callers fall back to what they already know.
    virtual std::optional<VideoMeta> fetch(const QString& videoId, std::stop_token stop) = 0;
};

class YtDlpResolver final : public VideoMetadataSource {
public:
    /// runner and tools are referenced, not owned: they must outlive the resolver.
    YtDlpResolver(process::ProcessRunner& runner, const tools::ToolPathProvider& tools);

    /// Ok with the items (at most filename::MaxRank of them; truncated when there were more), Failed with the message of
    /// Kotlin's ResolveException, or Cancelled when the run was cancelled.
    Outcome<ResolveResult> resolve(const QString& input, std::stop_token stop);

    std::optional<VideoMeta> fetch(const QString& videoId, std::stop_token stop) override;

private:
    process::ProcessRunner& m_runner;
    const tools::ToolPathProvider& m_tools;
};

}  // namespace xgs::ytdlp
