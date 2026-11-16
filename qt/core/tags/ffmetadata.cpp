#include "tags/ffmetadata.h"

#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::tags {

namespace {

constexpr char16_t kFullwidthBackslash = u'＼';   // replaces a trailing backslash, like file names do for forbidden characters

bool needsBackslash(QChar c) {
    const char16_t unit = c.unicode();
    return unit == u'=' || unit == u';' || unit == u'#' || unit == u'\\' || unit == u'\n' || unit == u'\r';
}

// Two things in a value would corrupt the file. A NUL (or any other control character except tab and the line breaks, which are
// escaped) can end a line and so inject another tag: those are dropped. And ffmpeg joins a line that ends in a backslash with the
// next one, even when that backslash is escaped, so a trailing backslash becomes a full-width one.
QString sanitize(const QString& value) {
    QString printable;
    printable.reserve(value.size());
    for (const QChar c : value) {
        const char16_t unit = c.unicode();
        if (unit >= u' ' || unit == u'\t' || unit == u'\n' || unit == u'\r') printable.append(c);
    }
    qsizetype keptLength = printable.size();
    while (keptLength > 0 && printable.at(keptLength - 1) == u'\\') --keptLength;
    QString result = printable.left(keptLength);
    result.append(QString(printable.size() - keptLength, QChar(kFullwidthBackslash)));
    return result;
}

void entry(QString& out, QStringView key, const QString& value) {
    out.append(key).append(u'=');
    for (const QChar c : sanitize(value)) {
        if (needsBackslash(c)) out.append(u'\\');
        out.append(c);
    }
    out.append(u'\n');
}

bool hasText(const std::optional<QString>& value) {
    return value && !kt::isBlank(*value);
}

}  // namespace

QString renderFfmetadata(const TrackTags& tags) {
    QString out = u";FFMETADATA1\n"_s;
    entry(out, u"title", tags.title);
    entry(out, u"artist", tags.artist);
    entry(out, u"album_artist", tags.albumArtist);
    if (hasText(tags.album)) entry(out, u"album", *tags.album);
    entry(out, u"track", QString::number(tags.trackNumber));
    if (hasText(tags.comment)) entry(out, u"comment", *tags.comment);
    if (hasText(tags.lyrics)) entry(out, u"lyrics", *tags.lyrics);
    return out;
}

}  // namespace xgs::tags
