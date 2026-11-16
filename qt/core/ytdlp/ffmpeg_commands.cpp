#include "ytdlp/ffmpeg_commands.h"

#include <stdexcept>

#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::ytdlp {

namespace {

// `\,` keeps the comma inside the filter argument. This is a single argument: no quoting is involved.
QStringList coverOutput() {
    return {u"-c:v"_s, u"mjpeg"_s, u"-q:v"_s, u"2"_s,
            u"-vf"_s, u"crop=min(iw\\,ih):min(iw\\,ih)"_s,
            u"-disposition:v"_s, u"attached_pic"_s,
            u"-metadata:s:v"_s, u"title=Album cover"_s,
            u"-metadata:s:v"_s, u"comment=Cover (front)"_s};
}

// The part of a path after its last `\` (Path.fileName for a normalised Windows path).
QString fileNameOf(const QString& path) {
    return path.mid(path.lastIndexOf(u'\\') + 1);
}

}  // namespace

QStringList tagCommand(const QString& ffmpeg, const QString& input, const std::optional<QString>& cover,
                       const QString& metadataFile, const QString& output) {
    if (!kt::endsWith(fileNameOf(output), u".m4a", kt::CaseMode::Ignore))
        throw std::invalid_argument("output must be an .m4a file");
    // Inputs are numbered in order: the audio file is 0, the cover (when there is one) is 1, the ffmetadata file is last.
    const int metadataIndex = cover ? 2 : 1;
    QStringList command{ffmpeg, u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s};
    command += {u"-i"_s, input};
    if (cover) command += {u"-i"_s, *cover};
    command += {u"-f"_s, u"ffmetadata"_s, u"-i"_s, metadataFile};
    command += {u"-map"_s, u"0:a"_s};
    if (cover) command += {u"-map"_s, u"1:v"_s};
    command += {u"-map_chapters"_s, u"-1"_s, u"-map_metadata"_s, QString::number(metadataIndex)};
    command += {u"-c:a"_s, u"copy"_s};
    if (cover) command += coverOutput();
    command += output;
    return command;
}

}  // namespace xgs::ytdlp
