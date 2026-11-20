#include "process/line_splitter.h"

#include <QString>

#include <utility>

#include "text/kt_text.h"

namespace xgs::process {

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
    const QString line = kt::decodeUtf8(m_line);
    m_line.clear();
    if (m_handler) m_handler(line);
}

}  // namespace xgs::process
