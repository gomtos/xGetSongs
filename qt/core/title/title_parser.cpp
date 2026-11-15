#include "title/title_parser.h"

#include <QStringList>

#include <algorithm>
#include <utility>

#include "text/kt_text.h"

namespace xgs {

namespace {

namespace kt = xgs::kt;

constexpr QStringView kTopicSuffix = u" - Topic";
constexpr QStringView kVevoSuffix = u"vevo";
constexpr QStringView kSeparators[] = {u" - ", u" – ", u" — ", u"_ "};
constexpr QStringView kSingleQuotes = u"'‘’";
constexpr QStringView kDoubleQuotes = u"\"“”";

// Lowercased, longest first so "official music video" is removed as a whole, not just "music video". Kotlin's
// sortedByDescending is stable: entries of the same length keep the order of the source list.
const QStringList& trailingNoise() {
    static const QStringList tokens = [] {
        QStringList list;
        for (const QStringView token : {u"Official Music Video", u"Official Video", u"Official Audio", u"Official MV",
                                        u"Official M/V", u"Performance Video", u"Special Video", u"Special Clip",
                                        u"Lyric Video", u"Music Video", u"Live Clip", u"Visualizer", u"Audio", u"M/V", u"MV"}) {
            list.append(kt::lowercase(token.toString()));
        }
        std::stable_sort(list.begin(), list.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
        return list;
    }();
    return tokens;
}

// Kotlin's `.ifEmpty { fallback }`.
QString ifEmpty(const QString& value, const QString& fallback) {
    return value.isEmpty() ? fallback : value;
}

// ---- cleaning -------------------------------------------------------------------------

QString collapseWhitespace(const QString& s) {
    QString sb;
    sb.reserve(s.size());
    bool previousWasSpace = false;
    for (const QChar c : s) {
        if (kt::isWhitespace(c)) {
            if (!previousWasSpace) sb.append(u' ');
            previousWasSpace = true;
        } else {
            sb.append(c);
            previousWasSpace = false;
        }
    }
    return kt::trim(sb);
}

// Drops leading emoji and pictographs (and whitespace); anything else, such as `&`, `*` or `$`, is kept. When every unit is
// such junk, nothing is dropped (Kotlin's indexOfFirst gives -1).
QString stripLeadingJunk(const QString& s) {
    qsizetype i = -1;
    for (qsizetype k = 0; k < s.size(); ++k) {
        const QChar c = s.at(k);
        const char16_t u = c.unicode();
        const bool junk = kt::isWhitespace(c) || kt::isSurrogate(c) || u == 0xFE0F || u == 0x200D || (u >= 0x2190 && u <= 0x2BFF);
        if (!junk) {
            i = k;
            break;
        }
    }
    return i <= 0 ? s : s.mid(i);
}

// Drops a leading `[...]` block of any kind; stacked tags are removed one per pass of clean(). A block that is the whole
// text is kept.
QString stripLeadingTag(const QString& s) {
    if (!s.startsWith(u'[')) return s;
    const qsizetype end = s.indexOf(u']');
    if (end < 0) return s;
    return ifEmpty(kt::trim(s.mid(end + 1)), s);
}

QString cutAtPipe(const QString& s) {
    const qsizetype i = kt::indexOf(s, u" | ");
    return i <= 0 ? s : kt::trim(s.left(i));
}

QString stripTrailingBracketBlock(const QString& s) {
    if (!s.endsWith(u']')) return s;
    const qsizetype start = s.lastIndexOf(u'[');
    if (start <= 0) return s;
    return ifEmpty(kt::trim(s.left(start)), s);
}

QString stripTrailingNoiseParen(const QString& s) {
    if (!s.endsWith(u')')) return s;
    const qsizetype start = s.lastIndexOf(u'(');
    if (start <= 0) return s;
    const QString inner = kt::lowercase(kt::trim(s.mid(start + 1, s.size() - 1 - (start + 1))));
    return trailingNoise().contains(inner) ? ifEmpty(kt::trim(s.left(start)), s) : s;
}

// Kotlin's trimEnd { it.isWhitespace() || it == '-' || it == '–' || it == '—' }.
QString trimEndWhitespaceAndDashes(const QString& s) {
    qsizetype end = s.size();
    while (end > 0) {
        const QChar c = s.at(end - 1);
        if (!(kt::isWhitespace(c) || c == u'-' || c == u'–' || c == u'—')) break;
        --end;
    }
    return s.left(end);
}

QString stripTrailingNoiseToken(const QString& s) {
    for (const QString& token : trailingNoise()) {
        if (s.size() > token.size() && kt::endsWith(s, token, kt::CaseMode::Ignore) && kt::isWhitespace(s.at(s.size() - token.size() - 1))) {
            return ifEmpty(trimEndWhitespaceAndDashes(s.left(s.size() - token.size())), s);
        }
    }
    return s;
}

QString clean(const QString& raw) {
    QString s = collapseWhitespace(raw);
    while (true) {
        const QString next = collapseWhitespace(
            stripTrailingNoiseToken(stripTrailingNoiseParen(stripTrailingBracketBlock(cutAtPipe(stripLeadingTag(stripLeadingJunk(s)))))));
        if (next == s) return s;
        s = next;
    }
}

// ---- splitting ------------------------------------------------------------------------

struct Span {
    qsizetype first;
    qsizetype second;
};

// (index, length) of the earliest separator, ignoring position 0.
std::optional<Span> findSeparator(const QString& s) {
    std::optional<Span> best;
    for (const QStringView separator : kSeparators) {
        const qsizetype i = kt::indexOf(s, separator, 1);
        if (i > 0 && (!best || i < best->first)) best = Span{i, separator.size()};
    }
    return best;
}

// The quote family of c (the single or the double quotes), or an empty view when c is not a quote.
QStringView quoteFamily(QChar c) {
    if (kSingleQuotes.contains(c)) return kSingleQuotes;
    if (kDoubleQuotes.contains(c)) return kDoubleQuotes;
    return {};
}

std::optional<qsizetype> findCloser(const QString& s, qsizetype open, QStringView family) {
    for (qsizetype j = open + 2; j < s.size(); ++j) {
        if (family.contains(s.at(j)) && (j == s.size() - 1 || !kt::isLetterOrDigit(s.at(j + 1)))) return j;
    }
    return std::nullopt;
}

// The first quoted span at or after `from` as (openIndex, closeIndex). An opening quote must follow whitespace or the start
// of the text ("Girls'"); a closing quote must not be followed by a letter or digit ("Don't").
std::optional<Span> findQuotedSpan(const QString& s, qsizetype from) {
    for (qsizetype i = from; i < s.size(); ++i) {
        const QStringView family = quoteFamily(s.at(i));
        if (family.isEmpty()) continue;
        if (i > 0 && !kt::isWhitespace(s.at(i - 1))) continue;
        const std::optional<qsizetype> close = findCloser(s, i, family);
        if (!close) continue;
        return Span{i, *close};
    }
    return std::nullopt;
}

QString unwrapQuotes(const QString& s) {
    const std::optional<Span> span = findQuotedSpan(s, 0);
    return span && span->first == 0 ? kt::trim(s.mid(1, span->second - 1)) : s;
}

// Splits at whichever comes first: a separator (`Artist - Title`) or an opening quote (`Artist 'Title'`).
std::optional<std::pair<QString, QString>> split(const QString& text) {
    const std::optional<Span> separator = findSeparator(text);
    const std::optional<Span> quote = findQuotedSpan(text, 0);
    std::pair<QString, QString> result;
    if (separator && (!quote || separator->first < quote->first)) {
        result = {kt::trim(text.left(separator->first)), unwrapQuotes(kt::trim(text.mid(separator->first + separator->second)))};
    } else if (quote && quote->first > 0) {
        result = {kt::trim(text.left(quote->first)), kt::trim(text.mid(quote->first + 1, quote->second - (quote->first + 1)))};
    } else {
        return std::nullopt;
    }
    if (result.first.isEmpty() || result.second.isEmpty()) return std::nullopt;
    return result;
}

QString artistFromChannel(const std::optional<QString>& channel) {
    QString name = channel ? kt::trim(*channel) : QString();
    if (kt::endsWith(name, kTopicSuffix, kt::CaseMode::Ignore)) name.chop(kTopicSuffix.size());
    if (name.size() > kVevoSuffix.size() && kt::endsWith(name, kVevoSuffix, kt::CaseMode::Ignore)) name.chop(kVevoSuffix.size());
    return ifEmpty(kt::trim(name), unknownArtist());
}

}  // namespace

QString unknownArtist() {
    return QStringLiteral("Unknown Artist");
}

ParsedTrack parseTitle(const QString& rawTitle, const std::optional<QString>& channel, const std::optional<QString>& metaArtist,
                       const std::optional<QString>& metaTrack) {
    const QString text = clean(rawTitle);

    // Auto-generated "Artist - Topic" channels carry the bare track name, so " - " in the title belongs to the track and must
    // not be treated as an artist separator.
    const bool isTopicChannel = channel && kt::endsWith(kt::trim(*channel), kTopicSuffix, kt::CaseMode::Ignore);
    if (!isTopicChannel) {
        if (const auto parts = split(text)) return ParsedTrack{parts->first, parts->second, Confidence::High};
    }

    const QString title = ifEmpty(unwrapQuotes(text), kt::trim(rawTitle));
    const QString artist = metaArtist ? kt::trim(*metaArtist) : QString();
    if (!artist.isEmpty()) {
        const QString track = ifEmpty(metaTrack ? kt::trim(*metaTrack) : QString(), title);
        return ParsedTrack{artist, track, Confidence::Medium};
    }
    return ParsedTrack{artistFromChannel(channel), title, Confidence::Low};
}

}  // namespace xgs
