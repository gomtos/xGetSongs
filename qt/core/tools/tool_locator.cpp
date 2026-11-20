#include "tools/tool_locator.h"

#include <QStringList>

#include <utility>

#include "fs/file_ops.h"
#include "paths/paths.h"
#include "text/kt_text.h"

namespace xgs::tools {

namespace {

// The semicolon-separated PATH entries: blank ones dropped, the outer double quotes removed (all of them, like Kotlin's
// trim('"')), those that are not paths skipped.
QStringList pathDirectories(const QString& pathEnv) {
    QStringList dirs;
    for (const QString& entry : pathEnv.split(u';')) {
        if (kt::isBlank(entry)) continue;
        qsizetype begin = 0;
        qsizetype end = entry.size();
        while (begin < end && entry[begin] == u'"') ++begin;
        while (end > begin && entry[end - 1] == u'"') --end;
        if (const auto dir = paths::fromText(entry.mid(begin, end - begin))) dirs.append(*dir);
    }
    return dirs;
}

}  // namespace

ToolLocator::ToolLocator(QString appBinDir, std::optional<QString> pathEnv, std::optional<QString> ffmpegOverride)
    : m_appBinDir(std::move(appBinDir)),
      m_pathEnv(pathEnv ? std::move(*pathEnv) : qEnvironmentVariable("PATH")),
      m_ffmpegOverride(std::move(ffmpegOverride)) {}

ToolPaths ToolLocator::current() const {
    ToolPaths result;
    result.ytDlp = find(QStringLiteral("yt-dlp"));
    if (m_ffmpegOverride && fs::isRegularFile(*m_ffmpegOverride)) result.ffmpeg = m_ffmpegOverride;
    else result.ffmpeg = find(QStringLiteral("ffmpeg"));
    result.jsRuntime = find(QStringLiteral("deno"));
    if (!result.jsRuntime) result.jsRuntime = find(QStringLiteral("node"));
    return result;
}

std::optional<QString> ToolLocator::find(const QString& name) const {
    QStringList dirs{m_appBinDir};
    dirs.append(pathDirectories(m_pathEnv));
    for (const QString& dir : dirs) {
        for (const QString& candidate : {name + QStringLiteral(".exe"), name}) {
            const auto path = paths::child(dir, candidate);
            if (path && fs::isRegularFile(*path)) return path;
        }
    }
    return std::nullopt;
}

}  // namespace xgs::tools
