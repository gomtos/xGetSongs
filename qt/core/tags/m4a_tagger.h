#pragma once

#include <QString>

#include <optional>
#include <stop_token>

#include "process/process_runner.h"
#include "tags/track_tags.h"
#include "tools/tool_locator.h"
#include "ytdlp/error_classifier.h"

// Port of engine/.../tags/M4aTagger.kt: writes the tags and the cover into an m4a with one ffmpeg stream-copy pass. The tag
// text travels in an ffmetadata file next to the m4a, never on the command line. The same pass writes a new container, so the
// fragmented DASH file yt-dlp leaves behind comes out as a regular m4a.
namespace xgs::tags {

enum class TagStatus { Tagged, Failed, Cancelled };

struct TagResult {
    TagStatus status;
    ytdlp::Failure failure;   // Failed only
};

class M4aTagger final {
public:
    M4aTagger(process::ProcessRunner& runner, const tools::ToolPathProvider& tools);

    /// Rewrites file (a result of paths::fromText, an absolute path) in place with tags and, when given, the picture cover.
    /// On a failure or a cancellation file is left untouched. The temporary files next to it (<name>.ffmeta and
    /// <name>.tagged.m4a) are always removed.
    ///  - No ffmpeg: Failed(FATAL, `ffmpeg를 찾을 수 없습니다.`), checked first (as Kotlin, before a cancellation).
    ///  - A stop requested before the ffmetadata file is written: Cancelled, nothing written or run. A stop seen later (after
    ///    the file is written, by the runner, before the output is examined) is Cancelled too; once the output has replaced
    ///    file the result is Tagged.
    ///  - Every other failure is Failed(OTHER, `태그를 쓰지 못했습니다: <detail>`): the detail is the error text of a file
    ///    operation or of a start failure (Windows' text where Kotlin has the JDK exception's message), or, when ffmpeg exits
    ///    with a code other than 0 or writes no output file, its last stderr line that is not blank, trimmed and cut to 200
    ///    UTF-16 units (`알 수 없는 오류` when there is none).
    /// An exception a runner throws propagates (after the temporary files are removed), as in Kotlin.
    TagResult tag(const QString& file, const std::optional<QString>& cover, const TrackTags& tags, std::stop_token stop);

private:
    process::ProcessRunner& m_runner;
    const tools::ToolPathProvider& m_tools;
};

}  // namespace xgs::tags
