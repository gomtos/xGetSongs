#include "tags/m4a_tagger.h"

#include <QStringList>

#include <mutex>
#include <stdexcept>

#include "fs/file_ops.h"
#include "paths/paths.h"
#include "tags/ffmetadata.h"
#include "text/kt_text.h"
#include "ytdlp/ffmpeg_commands.h"

namespace xgs::tags {

namespace {

using namespace Qt::StringLiterals;
using ytdlp::Failure;
using ytdlp::FailureKind;

constexpr qsizetype MaxDetail = 200;

TagResult failed(const QString& detail) {
    return {TagStatus::Failed, Failure{FailureKind::Other, u"태그를 쓰지 못했습니다: "_s + detail}};
}

TagResult unknownFailure() {
    return failed(u"알 수 없는 오류"_s);
}

TagResult cancelled() {
    return {TagStatus::Cancelled, {}};
}

// Kotlin's finally block: the temporary files go on every way out, an exception included. A leftover is harmless (the job's
// work folder is removed), so a failure to delete is ignored and never hides the real result.
struct RemoveOnExit {
    QString metadataFile;
    QString output;
    ~RemoveOnExit() {
        fs::removeQuietly(metadataFile);
        fs::removeQuietly(output);
    }
};

}  // namespace

M4aTagger::M4aTagger(process::ProcessRunner& runner, const tools::ToolPathProvider& tools) : m_runner(runner), m_tools(tools) {}

TagResult M4aTagger::tag(const QString& file, const std::optional<QString>& cover, const TrackTags& tags, std::stop_token stop) {
    const std::optional<QString> ffmpeg = m_tools.current().ffmpeg;
    if (!ffmpeg) return {TagStatus::Failed, Failure{FailureKind::Fatal, u"ffmpeg를 찾을 수 없습니다."_s}};
    if (stop.stop_requested()) return cancelled();

    const QString name = paths::nameWithoutExtension(file);
    const std::optional<QString> metadataFile = paths::sibling(file, name + u".ffmeta"_s);
    const std::optional<QString> output = paths::sibling(file, name + u".tagged.m4a"_s);
    // Kotlin's file is a Path, so these always exist there; a file that is not a path cannot be tagged.
    if (!metadataFile || !output) return unknownFailure();
    const RemoveOnExit removeOnExit{*metadataFile, *output};

    const Outcome<std::monostate> written = fs::writeUtf8(*metadataFile, renderFfmetadata(tags));
    if (!written.isOk()) return failed(written.error);
    if (stop.stop_requested()) return cancelled();

    QStringList command;
    try {
        command = ytdlp::tagCommand(*ffmpeg, file, cover, *metadataFile, *output);
    } catch (const std::invalid_argument& e) {
        // Unreachable: the output always ends in `.m4a`. Kotlin would let this IllegalArgumentException propagate; here it is
        // a failure like any other.
        return failed(QString::fromUtf8(e.what()));
    }

    std::mutex stderrLock;
    QStringList stderrLines;
    const process::RunResult run = m_runner.run(
        command, [](const QString&) {},
        [&](const QString& line) {
            const std::lock_guard lock(stderrLock);
            stderrLines.append(line);
        },
        stop);
    if (run.status == process::RunStatus::Cancelled) return cancelled();
    if (run.status == process::RunStatus::StartFailed) return failed(run.error);   // Kotlin: the IOException's message
    if (stop.stop_requested()) return cancelled();

    if (run.exitCode != 0 || !fs::isRegularFile(*output)) {
        const std::lock_guard lock(stderrLock);
        for (qsizetype i = stderrLines.size() - 1; i >= 0; --i) {
            if (!kt::isBlank(stderrLines.at(i))) return failed(kt::trim(stderrLines.at(i)).left(MaxDetail));
        }
        return unknownFailure();
    }
    if (stop.stop_requested()) return cancelled();

    const fs::MoveResult moved = fs::moveFile(*output, file, true);
    if (moved.status != fs::MoveStatus::Moved) return failed(moved.error);
    return {TagStatus::Tagged, {}};
}

}  // namespace xgs::tags
