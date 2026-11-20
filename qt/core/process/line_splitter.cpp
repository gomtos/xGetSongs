#include "process/line_splitter.h"

#include <QString>

#include <utility>

namespace xgs::process {

namespace {

constexpr char16_t kReplacement = 0xFFFD;

bool isContinuation(unsigned char byte) {
    return byte >= 0x80 && byte <= 0xBF;
}

// UTF-8 to UTF-16 with the replacement rules of line_splitter.h (each observed through the oracle, kind W op ln).
QString decode(QByteArrayView bytes) {
    QString out(bytes.size(), Qt::Uninitialized);   // never more units than bytes
    char16_t* write = reinterpret_cast<char16_t*>(out.data());
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const qsizetype size = bytes.size();
    qsizetype i = 0;
    while (i < size) {
        const unsigned char lead = p[i];
        if (lead < 0x80) {
            *write++ = lead;
            ++i;
            continue;
        }
        int needed = 0;                      // continuation bytes after the lead
        unsigned char low = 0x80, high = 0xBF;   // the range of the second byte
        char32_t value = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            needed = 1;
            value = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            needed = 2;
            value = lead & 0x0F;
            if (lead == 0xE0) low = 0xA0;    // (ED keeps 80..BF: an encoded surrogate is rejected whole, below)
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            needed = 3;
            value = lead & 0x07;
            if (lead == 0xF0) low = 0x90;
            if (lead == 0xF4) high = 0x8F;
        } else {
            *write++ = kReplacement;         // 80..C1, F5..FF cannot start a sequence
            ++i;
            continue;
        }
        qsizetype at = i + 1;
        int got = 0;
        while (got < needed && at < size) {
            const unsigned char byte = p[at];
            const bool fits = got == 0 ? byte >= low && byte <= high : isContinuation(byte);
            if (!fits) break;
            value = (value << 6) | (byte & 0x3F);
            ++got;
            ++at;
        }
        i = at;
        if (got < needed || (value >= 0xD800 && value <= 0xDFFF)) {
            *write++ = kReplacement;         // truncated (the offending byte is looked at again), or an encoded surrogate
            continue;
        }
        if (value >= 0x10000) {
            *write++ = char16_t(0xD800 + ((value - 0x10000) >> 10));
            *write++ = char16_t(0xDC00 + ((value - 0x10000) & 0x3FF));
        } else {
            *write++ = char16_t(value);
        }
    }
    out.truncate(write - reinterpret_cast<const char16_t*>(out.constData()));
    return out;
}

}  // namespace

LineSplitter::LineSplitter(LineHandler handler) : m_handler(std::move(handler)) {}

void LineSplitter::feed(QByteArrayView bytes) {
    const char* data = bytes.data();
    const qsizetype size = bytes.size();
    qsizetype start = 0;   // the first byte of this chunk not yet in m_line
    for (qsizetype i = 0; i < size; ++i) {
        const char byte = data[i];
        if (byte != '\r' && byte != '\n') continue;
        const bool secondHalf = byte == '\n' && m_afterCr && i == start && m_line.isEmpty();
        m_line.append(data + start, i - start);
        start = i + 1;
        if (secondHalf) {
            m_afterCr = false;   // the `\n` of a `\r\n` whose `\r` ended the line already
            continue;
        }
        m_afterCr = byte == '\r';
        deliver();
    }
    if (start < size) {
        m_line.append(data + start, size - start);
        m_afterCr = false;
    }
}

void LineSplitter::finish() {
    if (!m_line.isEmpty()) deliver();
    m_afterCr = false;
}

void LineSplitter::deliver() {
    const QString line = decode(m_line);
    m_line.clear();
    if (m_handler) m_handler(line);
}

}  // namespace xgs::process
