#include "ytdlp/progress_parser.h"

#include <QStringList>

#include "text/kt_text.h"

namespace xgs::ytdlp {

namespace {

std::optional<double> fieldAsDouble(const QStringList& parts, qsizetype index) {
    if (index >= parts.size()) return std::nullopt;
    return kt::toDoubleOrNull(parts.at(index));
}

// Double.coerceIn(0.0, 100.0): the comparisons decide, so a NaN stays a NaN.
double coerceToPercent(double value) {
    if (value < 0.0) return 0.0;
    if (value > 100.0) return 100.0;
    return value;
}

std::optional<DownloadProgress> parseDownload(const QString& text) {
    const QStringList parts = text.split(u'|');   // keeps empty parts
    if (parts.size() < 2) return std::nullopt;
    const QString& status = parts.at(1);
    if (status == u"finished") return DownloadProgress{100.0};
    if (status == u"downloading") {
        const std::optional<double> downloaded = fieldAsDouble(parts, 2);
        std::optional<double> total = fieldAsDouble(parts, 3);
        if (!total) total = fieldAsDouble(parts, 4);
        std::optional<double> percent;
        if (downloaded && total && *total > 0) {
            // Multiply first: 29 / 100 * 100 is 28.999999999999996 in floating point.
            percent = coerceToPercent(*downloaded * 100.0 / *total);
        }
        return DownloadProgress{percent};
    }
    return std::nullopt;
}

}  // namespace

std::optional<DownloadProgress> parseProgress(const QString& line) {
    const QString text = kt::trim(line);
    const QString prefix = ProgressPrefix.toString() + u'|';
    return kt::startsWith(text, prefix) ? parseDownload(text) : std::nullopt;
}

}  // namespace xgs::ytdlp
