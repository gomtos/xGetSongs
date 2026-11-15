#pragma once

#include <QString>
#include <QStringView>

#include <optional>

// The string notation of the differential protocol (as shell/tools/differential and Oracle.java): every UTF-16 unit as 4
// lowercase hex digits, "." for the empty string, "-" for null (std::nullopt).
namespace xgs::diff {

inline QString hex(const std::optional<QString>& value) {
    if (!value) return QStringLiteral("-");
    if (value->isEmpty()) return QStringLiteral(".");
    static constexpr char16_t digits[] = u"0123456789abcdef";
    QString out(value->size() * 4, Qt::Uninitialized);
    QChar* write = out.data();
    for (QChar c : *value) {
        const char16_t unit = c.unicode();
        *write++ = QChar(digits[(unit >> 12) & 15]);
        *write++ = QChar(digits[(unit >> 8) & 15]);
        *write++ = QChar(digits[(unit >> 4) & 15]);
        *write++ = QChar(digits[unit & 15]);
    }
    return out;
}

inline std::optional<QString> unhex(QStringView text) {
    if (text == u"-") return std::nullopt;
    if (text == u".") return QString();
    QString out;
    out.reserve(text.size() / 4);
    for (qsizetype i = 0; i + 4 <= text.size(); i += 4) {
        bool ok = false;
        out.append(QChar(char16_t(text.sliced(i, 4).toUInt(&ok, 16))));
    }
    return out;
}

}  // namespace xgs::diff
