#include "ffprobe.h"

#include <QStringList>

#include <mutex>
#include <stdexcept>

#include "fs/file_ops.h"
#include "json/json.h"
#include "paths/paths.h"

using namespace Qt::StringLiterals;

namespace xgs::integration {

namespace {

[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

const json::Object& objectOf(const json::Value& value, const QString& what) {
    const auto* object = std::get_if<json::Object>(&value.data);
    if (!object) fail(what + u" is not a JSON object"_s);
    return *object;
}

const json::Primitive& primitiveOf(const json::Value& value, const QString& what) {
    const auto* primitive = std::get_if<json::Primitive>(&value.data);
    if (!primitive) fail(what + u" is not a JSON primitive"_s);
    return *primitive;
}

// Kotlin's `jsonPrimitive.int`: the content as an Int, or an exception.
int intOf(const json::Value& value, const QString& what) {
    const QString content = primitiveOf(value, what).content;
    bool ok = false;
    const int number = content.toInt(&ok);
    if (!ok) fail(what + u" is not an Int: "_s + content);
    return number;
}

QString optionalInt(const std::optional<int>& value) { return value ? QString::number(*value) : u"null"_s; }

}  // namespace

std::optional<QString> Probed::tag(const QString& key) const {
    const auto it = tags.constFind(key);
    if (it == tags.constEnd()) return std::nullopt;
    return *it;
}

QString describe(const QList<ProbedStream>& streams) {
    QStringList parts;
    for (const ProbedStream& s : streams) {
        parts << u"ProbedStream(codecType=%1, codecName=%2, width=%3, height=%4, attachedPic=%5)"_s.arg(
            s.codecType, s.codecName, optionalInt(s.width), optionalInt(s.height), s.attachedPic ? u"true"_s : u"false"_s);
    }
    return u'[' + parts.join(u", "_s) + u']';
}

QString Probed::describe() const {
    QStringList entries;
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) entries << it.key() + u'=' + it.value();
    return u"Probed(tags={"_s + entries.join(u", "_s) + u"}, streams="_s + integration::describe(streams) + u')';
}

Probed Ffprobe::probe(const QString& file, std::stop_token stop) const {
    QStringList stdoutLines, stderrLines;
    std::mutex mutex;
    const process::RunResult result = m_runner.run(
        {m_ffprobe, u"-v"_s, u"error"_s, u"-of"_s, u"json"_s, u"-show_entries"_s,
         u"format_tags:stream=codec_type,codec_name,width,height:stream_disposition=attached_pic"_s, file},
        [&](const QString& line) { std::lock_guard lock(mutex); stdoutLines << line; },
        [&](const QString& line) { std::lock_guard lock(mutex); stderrLines << line; }, stop);
    if (result.status != process::RunStatus::Exited || result.exitCode != 0) {
        fail(u"ffprobe failed (%1): %2 %3"_s.arg(QString::number(result.exitCode), result.error, stderrLines.join(u", "_s)));
    }

    const std::optional<json::Value> parsed = json::parse(stdoutLines.join(u'\n'));
    if (!parsed) fail(u"ffprobe's output is not JSON"_s);
    const json::Object& root = objectOf(*parsed, u"the root"_s);

    Probed probed;
    if (const json::Value* format = json::find(root, u"format")) {
        if (const json::Value* tags = json::find(objectOf(*format, u"format"_s), u"tags")) {
            for (const auto& [key, value] : objectOf(*tags, u"format.tags"_s)) {
                probed.tags.insert(key, primitiveOf(value, u"tag "_s + key).content);
            }
        }
    }
    if (const json::Value* streams = json::find(root, u"streams")) {
        const auto* array = std::get_if<json::Array>(&streams->data);
        if (!array) fail(u"streams is not a JSON array"_s);
        for (const json::Value& element : *array) {
            const json::Object& stream = objectOf(element, u"a stream"_s);
            const json::Value* codecType = json::find(stream, u"codec_type");
            const json::Value* codecName = json::find(stream, u"codec_name");
            if (!codecType || !codecName) fail(u"a stream has no codec_type or codec_name"_s);
            ProbedStream probedStream;
            probedStream.codecType = primitiveOf(*codecType, u"codec_type"_s).content;
            probedStream.codecName = primitiveOf(*codecName, u"codec_name"_s).content;
            if (const json::Value* width = json::find(stream, u"width")) probedStream.width = intOf(*width, u"width"_s);
            if (const json::Value* height = json::find(stream, u"height")) probedStream.height = intOf(*height, u"height"_s);
            if (const json::Value* disposition = json::find(stream, u"disposition")) {
                if (const json::Value* attached = json::find(objectOf(*disposition, u"disposition"_s), u"attached_pic")) {
                    probedStream.attachedPic = intOf(*attached, u"attached_pic"_s) == 1;
                }
            }
            probed.streams << probedStream;
        }
    }
    return probed;
}

std::optional<QString> Ffprobe::besides(const std::optional<QString>& ffmpeg) {
    if (!ffmpeg) return std::nullopt;
    const std::optional<QString> name = paths::fileName(*ffmpeg);
    if (!name) return std::nullopt;
    QString probeName = *name;
    const qsizetype at = probeName.indexOf(u"ffmpeg"_s);   // replaceFirst: the first occurrence, case-sensitive
    if (at >= 0) probeName.replace(at, 6, u"ffprobe"_s);
    const std::optional<QString> probe = paths::sibling(*ffmpeg, probeName);
    if (probe && fs::isRegularFile(*probe)) return probe;
    return std::nullopt;
}

}  // namespace xgs::integration
