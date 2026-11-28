#include "diag/log_sink.h"

#include <windows.h>

#include <typeinfo>

namespace xgs::diag {

QString levelName(Level level) {
    switch (level) {
        case Level::Debug: return QStringLiteral("DEBUG");
        case Level::Info: return QStringLiteral("INFO");
        case Level::Warn: return QStringLiteral("WARN");
        case Level::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString exceptionName(const std::exception& error) {
    QString name = QString::fromLatin1(typeid(error).name());   // MSVC: `class std::runtime_error`
    const qsizetype space = name.lastIndexOf(u' ');
    if (space >= 0) name = name.mid(space + 1);
    const qsizetype colons = name.lastIndexOf(QStringLiteral("::"));
    if (colons >= 0) name = name.mid(colons + 2);
    return name;
}

QString currentExceptionName() {
    try {
        throw;
    } catch (const std::exception& error) {
        return exceptionName(error);
    } catch (...) {
        return QStringLiteral("알 수 없는 예외");
    }
}

namespace {

QString& threadNameSlot() {
    thread_local QString name;
    return name;
}

}  // namespace

QString currentThreadName() {
    const QString& name = threadNameSlot();
    return name.isEmpty() ? QStringLiteral("t") + QString::number(GetCurrentThreadId()) : name;
}

void setCurrentThreadName(const QString& name) {
    threadNameSlot() = name;
}

int currentThreadNameUtf8(char* out, int capacity) noexcept {
    if (out == nullptr || capacity <= 0) return 0;
    int size = 0;
    // Appends the bytes of one character, or nothing when they do not all fit (the zero needs its byte).
    const auto put = [&](const char* bytes, int count) {
        if (size + count > capacity - 1) return false;
        for (int i = 0; i < count; ++i) out[size++] = bytes[i];
        return true;
    };
    const QString& name = threadNameSlot();   // a reference: no copy, no allocation
    if (name.isEmpty()) {
        char digits[16];
        int count = 0;
        unsigned long id = GetCurrentThreadId();
        do {
            digits[count++] = static_cast<char>('0' + id % 10);
            id /= 10;
        } while (id != 0 && count < 16);
        if (put("t", 1)) {
            while (count > 0 && put(&digits[count - 1], 1)) --count;
        }
        out[size] = 0;
        return size;
    }
    const char16_t* text = reinterpret_cast<const char16_t*>(name.utf16());
    const qsizetype length = name.size();
    for (qsizetype i = 0; i < length; ++i) {
        char32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < length && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;   // a lone surrogate
        }
        char bytes[4];
        int count = 0;
        if (c < 0x80) {
            bytes[count++] = static_cast<char>(c);
        } else if (c < 0x800) {
            bytes[count++] = static_cast<char>(0xC0 | (c >> 6));
            bytes[count++] = static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            bytes[count++] = static_cast<char>(0xE0 | (c >> 12));
            bytes[count++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            bytes[count++] = static_cast<char>(0x80 | (c & 0x3F));
        } else {
            bytes[count++] = static_cast<char>(0xF0 | (c >> 18));
            bytes[count++] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            bytes[count++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            bytes[count++] = static_cast<char>(0x80 | (c & 0x3F));
        }
        if (!put(bytes, count)) break;
    }
    out[size] = 0;
    return size;
}

}  // namespace xgs::diag
