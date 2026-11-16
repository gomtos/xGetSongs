#include "ytdlp/video_info_file.h"

#include <QFile>
#include <QStringDecoder>

#include <climits>
#include <cmath>
#include <variant>

#include "json/json.h"
#include "text/kt_text.h"

namespace xgs::ytdlp {

namespace {

// What yt-dlp prints for a field it does not know. It is not an album, whichever way it ends up in the file.
constexpr QStringView NotAvailable = u"NA";

// Files.readString(file): strict UTF-8. A malformed or truncated sequence (an overlong form, an encoded surrogate, a code
// point above U+10FFFF) is an error, not a replacement character, and a BOM at the start stays in the text as U+FEFF (the
// JSON reader then rejects it). Observed on the JVM with kind V of qt/tools/differential.
std::optional<QString> decodeUtf8Strictly(const QByteArray& bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    QString text = decoder.decode(bytes);
    if (decoder.hasError()) return std::nullopt;
    return text;
}

// The value of [key] when it is a JSON string; empty when it is absent, null or of another type.
std::optional<QString> stringOf(const json::Object& info, QStringView key) {
    const json::Value* value = json::find(info, key);
    if (!value) return std::nullopt;
    const json::Primitive* primitive = std::get_if<json::Primitive>(&value->data);
    if (!primitive || !primitive->isString) return std::nullopt;
    return primitive->content;
}

// Kotlin's Double.roundToLong() for a finite value: the nearest Long, ties towards positive infinity; a value beyond the
// Long range gives Long.MIN_VALUE or Long.MAX_VALUE.
qint64 roundToLong(double value) {
    double rounded = std::floor(value);
    if (value - rounded >= 0.5) rounded += 1.0;   // exact: value - floor(value) loses nothing for a double
    constexpr double twoTo63 = 9223372036854775808.0;
    if (rounded >= twoTo63) return LLONG_MAX;
    if (rounded < -twoTo63) return LLONG_MIN;
    return static_cast<qint64>(rounded);
}

// The `duration` of [info] when it is a JSON number that is at least half a second and fits an int once rounded.
std::optional<int> durationOf(const json::Object& info) {
    const json::Value* value = json::find(info, u"duration");
    if (!value) return std::nullopt;
    // JsonNull is a JsonPrimitive in Kotlin, with the content "null", which parses as no number either.
    const json::Primitive* primitive = std::get_if<json::Primitive>(&value->data);
    if (!primitive || primitive->isString) return std::nullopt;
    // `true` and `false` are literals too, and neither of them parses as a number.
    const std::optional<double> seconds = kt::toDoubleOrNull(primitive->content);
    if (!seconds || !std::isfinite(*seconds)) return std::nullopt;
    const qint64 rounded = roundToLong(*seconds);
    if (rounded < 1 || rounded > INT_MAX) return std::nullopt;
    return static_cast<int>(rounded);
}

// Drops the blank lines in front of the first text (its own indentation stays) and all white space after the last text.
QString dropBlankEnds(const QString& text) {
    qsizetype firstText = 0;
    while (firstText < text.size() && kt::isWhitespace(text.at(firstText))) ++firstText;
    if (firstText == text.size()) return QString();
    qsizetype lineStart = firstText;
    while (lineStart > 0 && text.at(lineStart - 1) != u'\n' && text.at(lineStart - 1) != u'\r') --lineStart;
    return kt::trimEnd(text.sliced(lineStart));
}

}  // namespace

VideoInfo parseVideoInfo(const QByteArray& fileBytes) {
    const std::optional<QString> text = decodeUtf8Strictly(fileBytes);
    if (!text) return {};
    const std::optional<json::Value> root = json::parse(*text);
    if (!root) return {};
    const json::Object* info = std::get_if<json::Object>(&root->data);
    if (!info) return {};

    VideoInfo result;
    if (std::optional<QString> album = stringOf(*info, u"album")) {
        QString trimmed = kt::trim(*album);
        if (!trimmed.isEmpty() && trimmed != NotAvailable) result.album = std::move(trimmed);
    }
    if (const std::optional<QString> description = stringOf(*info, u"description")) {
        QString dropped = dropBlankEnds(*description);
        if (!dropped.isEmpty()) result.description = std::move(dropped);
    }
    result.duration = durationOf(*info);
    return result;
}

VideoInfo readVideoInfo(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return {};
    file.close();
    return parseVideoInfo(bytes);
}

std::optional<QString> readVideoAlbum(const QString& filePath) {
    return readVideoInfo(filePath).album;
}

}  // namespace xgs::ytdlp
