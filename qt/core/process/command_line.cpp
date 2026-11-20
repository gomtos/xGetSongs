#include "process/command_line.h"

namespace xgs::process {

namespace {

bool needsQuotes(const QString& argument) {
    if (argument.isEmpty()) return true;
    for (QChar c : argument) {
        const char16_t unit = c.unicode();
        if (unit == u' ' || unit == u'\t' || unit == u'\n' || unit == u'\v' || unit == u'"') return true;
    }
    return false;
}

void appendQuoted(QString& out, const QString& argument) {
    out += u'"';
    qsizetype backslashes = 0;   // the run of backslashes just written
    for (QChar c : argument) {
        if (c == u'\\') {
            ++backslashes;
            out += c;
            continue;
        }
        if (c == u'"') {
            // n backslashes before a quote: 2n + 1 of them make n backslashes and a literal quote
            out += QString(backslashes + 1, u'\\');
            out += c;
        } else {
            out += c;
        }
        backslashes = 0;
    }
    // before the closing quote, 2n backslashes make n
    out += QString(backslashes, u'\\');
    out += u'"';
}

}  // namespace

QString windowsCommandLine(const QStringList& arguments) {
    QString out;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        if (i > 0) out += u' ';
        const QString& argument = arguments.at(i);
        if (needsQuotes(argument)) appendQuoted(out, argument);
        else out += argument;
    }
    return out;
}

}  // namespace xgs::process
