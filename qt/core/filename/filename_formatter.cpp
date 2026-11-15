#include "filename/filename_formatter.h"

#include <QStringList>

#include <stdexcept>
#include <string>

#include "text/kt_text.h"

namespace xgs::filename {

namespace {

namespace kt = xgs::kt;

constexpr QStringView kEllipsis = u"…";
constexpr QStringView kExtension = u".m4a";
constexpr QStringView kEmptyTitlePlaceholder = u"untitled";
constexpr QStringView kTrailingDotsAndSpaces = u". ";

// Windows device names that cannot be a file or folder name, with or without an extension.
bool isReservedDeviceName(const QString& upperStem) {
    static const QStringList names = [] {
        QStringList list{QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL")};
        for (int i = 1; i <= 9; ++i) {
            list.append(QStringLiteral("COM%1").arg(i));
            list.append(QStringLiteral("LPT%1").arg(i));
        }
        return list;
    }();
    return names.contains(upperStem);
}

// Full-width look-alike of a forbidden character, or the character itself.
QChar fullWidth(QChar c) {
    switch (c.unicode()) {
        case u'\\': return QChar(u'＼');
        case u'/': return QChar(u'／');
        case u':': return QChar(u'：');
        case u'*': return QChar(u'＊');
        case u'?': return QChar(u'？');
        case u'"': return QChar(u'＂');
        case u'<': return QChar(u'＜');
        case u'>': return QChar(u'＞');
        case u'|': return QChar(u'｜');
        default: return c;
    }
}

// Cuts text to at most max UTF-16 units, ending with an ellipsis, never splitting a surrogate pair.
QString truncate(const QString& text, qsizetype max) {
    if (text.size() <= max) return text;
    qsizetype end = max - kEllipsis.size();
    if (end > 0 && kt::isHighSurrogate(text.at(end - 1))) --end;
    if (end < 0) end = 0;
    return kt::trimEnd(text.left(end)) + kEllipsis;
}

// The text before the first '.' of a name (trailing spaces ignored) is a device to Windows when it is a reserved name,
// whatever the case. Puts a `_` right after that stem then; other names are returned as is.
QString escapeDeviceName(const QString& name) {
    const qsizetype dot = name.indexOf(u'.');
    // Kotlin trimEnd(' '): only U+0020, not every white space character.
    const QString stem = kt::trimEndChars(dot < 0 ? name : name.left(dot), u" ");
    if (isReservedDeviceName(kt::uppercase(stem))) return stem + u'_' + name.mid(stem.size());
    return name;
}

}  // namespace

QString untitledPlaylist() {
    return QStringLiteral(u"재생목록");
}

QString format(int rank, const QString& artist, const QString& title, bool includeRank) {
    if (rank < MinRank || rank > MaxRank) {
        throw std::invalid_argument("rank must be in " + std::to_string(MinRank) + ".." + std::to_string(MaxRank) + " but was " +
                                    std::to_string(rank));
    }
    const QString cutArtist = truncate(sanitize(artist), MaxArtistLength);
    const QString cleanArtist = (includeRank || !cutArtist.contains(u'.')) ? cutArtist : escapeDeviceName(cutArtist);
    const QString rankPrefix = includeRank ? QStringLiteral("%1 ").arg(rank, 3, 10, QLatin1Char('0')) : QString();
    const QString prefix = rankPrefix + cleanArtist + u" - ";
    QString cleanTitle = sanitize(title);
    if (cleanTitle.isEmpty()) cleanTitle = kEmptyTitlePlaceholder.toString();
    const qsizetype titleBudget = MaxBaseLength - prefix.size();
    const QString base = kt::trimEndChars(prefix + truncate(cleanTitle, titleBudget), kTrailingDotsAndSpaces);
    return base + kExtension;
}

QString folderName(const std::optional<QString>& title) {
    if (!title || kt::isBlank(*title)) return untitledPlaylist();
    const QString name = kt::trimStartChars(kt::trimEndChars(truncate(sanitize(*title), MaxFolderLength), kTrailingDotsAndSpaces), u" ");
    if (name.isEmpty()) return untitledPlaylist();
    return escapeDeviceName(name);
}

std::optional<QString> destinationFolder(bool isPlaylist, const std::optional<QString>& playlistTitle,
                                         const std::optional<QString>& typed) {
    if (typed && !kt::isBlank(*typed)) return folderName(typed);
    if (isPlaylist) return folderName(playlistTitle);
    return std::nullopt;
}

QString sanitize(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F) continue;
        out.append(fullWidth(c));
    }
    return kt::trim(out);
}

}  // namespace xgs::filename
