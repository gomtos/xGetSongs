#include "ytdlp/ytdlp_commands.h"

#include <stdexcept>

#include "text/kt_text.h"
#include "ytdlp/progress_parser.h"

using namespace Qt::StringLiterals;

namespace xgs::ytdlp {

namespace {

QStringList common() {
    return {u"--ignore-config"_s, u"--no-warnings"_s, u"--encoding"_s, u"utf-8"_s};
}

QString ytDlp(const ToolPaths& tools) {
    if (!tools.ytDlp) throw std::invalid_argument("yt-dlp path is required");
    return *tools.ytDlp;
}

// The part of a path after its last `\` (Path.fileName for a normalised Windows path).
QString fileNameOf(const QString& path) {
    return path.mid(path.lastIndexOf(u'\\') + 1);
}

// A video id is joined into a file path, so it must be a plain file name part: not empty, none of the characters Windows forbids in
// a file name or reads as a separator, no control character.
bool isPlainFileNamePart(const QString& id) {
    if (id.isEmpty()) return false;
    for (const QChar c : id) {
        const char16_t unit = c.unicode();
        if (unit < 0x20 || unit == u'\\' || unit == u'/' || unit == u':' || unit == u'*' || unit == u'?' || unit == u'"' ||
            unit == u'<' || unit == u'>' || unit == u'|')
            return false;
    }
    return true;
}

QStringList jsRuntimeArgs(const ToolPaths& tools) {
    if (!tools.jsRuntime) return {};
    const QString& path = *tools.jsRuntime;
    QString name = kt::lowercase(fileNameOf(path));
    if (kt::endsWith(name, u".exe")) name.chop(4);
    if (name == u"deno" || name == u"node") return {u"--js-runtimes"_s, name + u':' + path};
    return {};
}

}  // namespace

QStringList resolvePlaylistCommand(const ToolPaths& tools, const QString& url) {
    QStringList command{ytDlp(tools)};
    command += common();
    command += {u"--flat-playlist"_s, u"-J"_s, u"--"_s, url};
    return command;
}

QStringList resolveVideoCommand(const ToolPaths& tools, const QString& url) {
    QStringList command{ytDlp(tools)};
    command += common();
    command += jsRuntimeArgs(tools);
    command += {u"--no-playlist"_s, u"-J"_s, u"--"_s, url};
    return command;
}

QStringList downloadCommand(const ToolPaths& tools, const QString& url, const QString& outputDir, const QString& videoId) {
    const QString progress = u"download:"_s + ProgressPrefix + u"|%(progress.status)s|%(progress.downloaded_bytes)s|"_s +
                             u"%(progress.total_bytes)s|%(progress.total_bytes_estimate)s"_s;
    // The order of the two checks differs from Kotlin's (there the path resolve of the id runs before the yt-dlp check), but
    // both are std::invalid_argument here and both are IllegalArgumentException kinds there, so a caller sees the same kind
    // whichever input is wrong.
    QStringList command{ytDlp(tools)};
    if (!isPlainFileNamePart(videoId)) throw std::invalid_argument("video id is not a plain file name part");
    const QString output = outputDir + u'\\' + videoId + u".%(ext)s"_s;
    command += common();
    command += jsRuntimeArgs(tools);
    if (tools.ffmpeg) command += {u"--ffmpeg-location"_s, *tools.ffmpeg};
    command += {u"--no-playlist"_s, u"--newline"_s,
                u"--progress-template"_s, progress,
                u"-f"_s, u"bestaudio[ext=m4a]"_s, u"--fixup"_s, u"never"_s,
                u"--write-thumbnail"_s, u"--convert-thumbnails"_s, u"jpg"_s,
                u"--write-info-json"_s,
                u"-o"_s, output,
                u"--"_s, url};
    return command;
}

QString escapeOutputTemplateDirectory(const QString& dir) {
    QString escaped = dir;
    return escaped.replace(u'%', u"%%"_s);
}

}  // namespace xgs::ytdlp
