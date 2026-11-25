#include "net/url_encoding.h"

namespace xgs::net {

namespace {

void appendPercentByte(QString& out, unsigned byte) {
    static constexpr char16_t hex[] = u"0123456789ABCDEF";
    out += u'%';
    out += QChar(hex[(byte >> 4) & 0xF]);
    out += QChar(hex[byte & 0xF]);
}

}  // namespace

QString encodeQueryValue(const QString& value) {
    QString out;
    out.reserve(value.size() * 3);
    const qsizetype size = value.size();
    for (qsizetype i = 0; i < size; ++i) {
        const char16_t unit = value.at(i).unicode();
        if ((unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9') || unit == u'.' ||
            unit == u'-' || unit == u'*' || unit == u'_') {
            out += QChar(unit);
            continue;
        }
        if (unit == u' ') {
            out += u"%20";
            continue;
        }
        char32_t codePoint = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < size && value.at(i + 1).unicode() >= 0xDC00 &&
            value.at(i + 1).unicode() <= 0xDFFF) {
            codePoint = 0x10000 + ((char32_t(unit) - 0xD800) << 10) + (char32_t(value.at(i + 1).unicode()) - 0xDC00);
            ++i;
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            appendPercentByte(out, '?');
            continue;
        }
        if (codePoint < 0x80) {
            appendPercentByte(out, codePoint);
        } else if (codePoint < 0x800) {
            appendPercentByte(out, 0xC0 | (codePoint >> 6));
            appendPercentByte(out, 0x80 | (codePoint & 0x3F));
        } else if (codePoint < 0x10000) {
            appendPercentByte(out, 0xE0 | (codePoint >> 12));
            appendPercentByte(out, 0x80 | ((codePoint >> 6) & 0x3F));
            appendPercentByte(out, 0x80 | (codePoint & 0x3F));
        } else {
            appendPercentByte(out, 0xF0 | (codePoint >> 18));
            appendPercentByte(out, 0x80 | ((codePoint >> 12) & 0x3F));
            appendPercentByte(out, 0x80 | ((codePoint >> 6) & 0x3F));
            appendPercentByte(out, 0x80 | (codePoint & 0x3F));
        }
    }
    return out;
}

}  // namespace xgs::net
