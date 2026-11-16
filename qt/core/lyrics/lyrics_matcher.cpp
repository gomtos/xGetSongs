#include "lyrics/lyrics_matcher.h"

#include <QStringView>

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <utility>

#include "text/kt_text.h"

// The regular expressions of LyricsMatcher.kt are scanners here. Their meaning was taken from the public documentation of
// java.util.regex.Pattern (a Java regular expression without flags: `\s` is [ \t\n\x0B\f\r], CASE_INSENSITIVE alone compares
// US-ASCII letters only, `[0-9]` is the ASCII digits, a match is the leftmost one and findAll/replace go on after it without
// overlap) and was confirmed against the Kotlin original through qt/tools/differential (kind I).
namespace xgs::lyrics {

namespace {

// Two lengths further apart than this (seconds) are two versions of a song, not one.
constexpr double kMaxDurationDifference = 8.0;
// After normalising, an artist this short or shorter says too little to match on.
constexpr qsizetype kMinArtistLength = 2;
constexpr qsizetype kMaxArtistVariants = 3;

// The straight quotes, the typographic single and double quotes, and the low and reversed forms of them.
constexpr QStringView kQuotes = u"'\"\u2018\u2019\u201A\u201B\u201C\u201D\u201E\u201F";

// VERSION_WORD: (?:ver|version|japanese|english|chinese|korean|inst|instrumental|remix|live|acoustic|cover|edit|mix|remaster|demo)
// (?:ed|es|s)?[0-9]*
constexpr QStringView kVersionStems[] = {u"ver", u"version", u"japanese", u"english", u"chinese", u"korean", u"inst", u"instrumental",
                                         u"remix", u"live", u"acoustic", u"cover", u"edit", u"mix", u"remaster", u"demo"};
constexpr QStringView kVersionEndings[] = {u"ed", u"es", u"s"};
// Korean version words: Korean text has no spaces to cut it into words, so these count anywhere inside.
constexpr QStringView kVersionKorean[] = {u"반주", u"일본어", u"영어", u"중국어", u"한국어", u"라이브", u"리믹스", u"어쿠스틱",
                                          u"버전", u"커버", u"데모", u"리마스터", u"믹스", u"에디트", u"인스트"};
// LIVE_LIKE_WORD: (?:live|acoustic)(?:ed|es|s|d)?[0-9]*
constexpr QStringView kLiveLikeStems[] = {u"live", u"acoustic"};
constexpr QStringView kLiveLikeEndings[] = {u"ed", u"es", u"s", u"d"};
constexpr QStringView kLiveLikeKorean[] = {u"라이브", u"어쿠스틱"};

// How well the titles agree; a lower number is a better match.
constexpr int kTitleWhole = 0;
constexpr int kTitleQueryShortened = 1;
constexpr int kTitleCandidateShortened = 2;

constexpr char16_t kFinalSigma = 0x03C2;
constexpr char16_t kSigma = 0x03C3;

// A match of a regular expression: [start, end) in UTF-16 units.
struct Span {
    qsizetype start;
    qsizetype end;
};

// The closing bracket of an opening one of BRACKETED (`(` `[` `{` `<`), or of PARENTHESISED (`(` only); 0 for anything else.
char16_t closerOf(char16_t unit, bool parenthesesOnly) {
    switch (unit) {
        case u'(': return u')';
        case u'[': return parenthesesOnly ? 0 : u']';
        case u'{': return parenthesesOnly ? 0 : u'}';
        case u'<': return parenthesesOnly ? 0 : u'>';
        default: return 0;
    }
}

// The matches of BRACKETED `\([^()]*\)|\[[^\[\]]*\]|\{[^{}]*\}|<[^<>]*>` (or of PARENTHESISED `\(([^()]*)\)`), left to right
// and without overlap: at an opening bracket, the match is there when the first bracket of the same kind after it (opening or
// closing) is the closing one; the next search starts after a match, or one unit further on when there was none. (The class
// [^()] takes any code point, so a surrogate pair inside changes nothing: the brackets are ASCII.)
QList<Span> findPairs(const QString& text, bool parenthesesOnly) {
    QList<Span> found;
    const qsizetype length = text.size();
    qsizetype i = 0;
    while (i < length) {
        const char16_t open = text.at(i).unicode();
        const char16_t close = closerOf(open, parenthesesOnly);
        if (close != 0) {
            qsizetype j = i + 1;
            while (j < length && text.at(j).unicode() != open && text.at(j).unicode() != close) ++j;
            if (j < length && text.at(j).unicode() == close) {
                found.append({i, j + 1});
                i = j + 1;
                continue;
            }
        }
        ++i;
    }
    return found;
}

// Regex.replace(text, replacement) for the given matches.
QString replaceSpans(const QString& text, const QList<Span>& spans, QStringView replacement) {
    QString out;
    qsizetype at = 0;
    for (const Span& span : spans) {
        out += QStringView(text).sliced(at, span.start - at);
        out += replacement;
        at = span.end;
    }
    out += QStringView(text).sliced(at);
    return out;
}

// One US-ASCII letter compared without case, as CASE_INSENSITIVE without UNICODE_CASE does: only A-Z and a-z fold.
char16_t asciiFolded(char16_t unit) {
    return unit >= u'A' && unit <= u'Z' ? char16_t(unit - u'A' + u'a') : unit;
}

bool asciiRegionMatchesIgnoreCase(const QString& text, qsizetype at, QStringView word) {
    if (at + word.size() > text.size()) return false;
    for (qsizetype k = 0; k < word.size(); ++k)
        if (asciiFolded(text.at(at + k).unicode()) != asciiFolded(word.at(k).unicode())) return false;
    return true;
}

bool isAsciiSpaceAt(const QString& text, qsizetype at) {
    return at < text.size() && kt::isAsciiSpace(text.at(at));
}

// CREDIT `\s+(?:feat\.?|ft\.?|prod\.?|narr\.?)\s+.*$` with IGNORE_CASE and DOT_MATCHES_ALL: the start of its leftmost match, or
// nullopt. The match is a run of ASCII white space, one of the words (US-ASCII case-insensitively; a dot after it may be there),
// at least one ASCII white space and everything up to the end (`.` takes line terminators too, so `.*` reaches the end and `$`
// matches there). `\s+` must take its whole run (a letter follows), so a start inside a run reaches the same word as the run's
// first unit, and the scan goes on after the run.
std::optional<qsizetype> findCredit(const QString& text) {
    static constexpr QStringView words[] = {u"feat", u"ft", u"prod", u"narr"};
    const qsizetype length = text.size();
    qsizetype i = 0;
    while (i < length) {
        if (!kt::isAsciiSpace(text.at(i))) {
            ++i;
            continue;
        }
        qsizetype j = i;
        while (j < length && kt::isAsciiSpace(text.at(j))) ++j;
        for (const QStringView word : words) {
            if (!asciiRegionMatchesIgnoreCase(text, j, word)) continue;
            const qsizetype after = j + word.size();
            // \.? is greedy: first with the dot, then without it
            if (after < length && text.at(after) == u'.' && isAsciiSpaceAt(text, after + 1)) return i;
            if (isAsciiSpaceAt(text, after)) return i;
        }
        i = j;
    }
    return std::nullopt;
}

// WHITESPACE `\s+` replaced by one space, then Kotlin's trim().
QString collapse(const QString& text) {
    QString out;
    out.reserve(text.size());
    bool inRun = false;
    for (const QChar c : text) {
        if (kt::isAsciiSpace(c)) {
            if (!inRun) out += u' ';
            inRun = true;
        } else {
            out += c;
            inRun = false;
        }
    }
    return kt::trim(out);
}

// List.distinct(): the first of equal strings, in order.
QStringList distinct(const QStringList& list) {
    QStringList out;
    for (const QString& item : list)
        if (!out.contains(item)) out.append(item);
    return out;
}

QStringList withoutBlank(const QStringList& list) {
    QStringList out;
    for (const QString& item : list)
        if (!kt::isBlank(item)) out.append(item);
    return out;
}

// The words of a text after NFKC and lowercasing (and ς written as σ, the user decision of the header): runs of letters and
// digits (of any script), everything else separates. By code point, as Kotlin's codePoints(): a surrogate pair is one code
// point, an unpaired surrogate is a code point of its own (neither a letter nor a digit).
QStringList words(const QString& text) {
    QString lowered = kt::lowercase(kt::nfkc(text));
    lowered.replace(QChar(kFinalSigma), QChar(kSigma));
    QStringList out;
    QString current;
    const qsizetype length = lowered.size();
    for (qsizetype i = 0; i < length;) {
        const char16_t unit = lowered.at(i).unicode();
        char32_t codePoint = unit;
        qsizetype units = 1;
        if (kt::isHighSurrogate(lowered.at(i)) && i + 1 < length) {
            const char16_t next = lowered.at(i + 1).unicode();
            if (next >= 0xDC00 && next <= 0xDFFF) {
                codePoint = 0x10000 + ((char32_t(unit) - 0xD800) << 10) + (char32_t(next) - 0xDC00);
                units = 2;
            }
        }
        if (kt::isLetterOrDigit(codePoint)) {
            current += QStringView(lowered).sliced(i, units);
        } else if (!current.isEmpty()) {
            out.append(current);
            current.clear();
        }
        i += units;
    }
    if (!current.isEmpty()) out.append(current);
    return out;
}

// Regex.matches: the whole word is one of the stems, one of the endings or none, and ASCII digits ([0-9]*). Every stem and
// ending is tried (`ver` and `version` are both stems).
template <std::size_t S, std::size_t E>
bool wholeWordMatches(const QString& word, const QStringView (&stems)[S], const QStringView (&endings)[E]) {
    const auto onlyAsciiDigits = [](QStringView rest) {
        for (const QChar c : rest)
            if (c.unicode() < u'0' || c.unicode() > u'9') return false;
        return true;
    };
    for (const QStringView stem : stems) {
        if (!kt::startsWith(word, stem)) continue;
        const QStringView rest = QStringView(word).sliced(stem.size());
        if (onlyAsciiDigits(rest)) return true;
        for (const QStringView ending : endings)
            if (kt::startsWith(rest, ending) && onlyAsciiDigits(rest.sliced(ending.size()))) return true;
    }
    return false;
}

bool isVersionWord(const QString& word) {
    return wholeWordMatches(word, kVersionStems, kVersionEndings);
}

bool isLiveLikeWord(const QString& word) {
    return wholeWordMatches(word, kLiveLikeStems, kLiveLikeEndings);
}

bool isLiveLikeKorean(QStringView word) {
    for (const QStringView liveLike : kLiveLikeKorean)
        if (word == liveLike) return true;
    return false;
}

// True when `removed` (a bracketed part or a credit of a title) has a version marker word in it. For the query
// (forCandidate false) `live` and `acoustic` are no marker: a video of a live or acoustic take has the lyrics of the song.
bool hasVersionMarker(const QString& removed, bool forCandidate) {
    if (kt::isBlank(removed)) return false;
    const QString text = kt::lowercase(kt::nfkc(removed));
    for (const QStringView word : kVersionKorean)
        if (kt::contains(text, word) && (forCandidate || !isLiveLikeKorean(word))) return true;
    for (const QString& word : words(text))
        if (isVersionWord(word) && (forCandidate || !isLiveLikeWord(word))) return true;
    return false;
}

// A title and the steps that make it shorter, with the text each step took away.
struct Reduction {
    QString whole;
    QString withoutBrackets;
    QString bracketedParts;
    QString withoutCredit;
    QString credit;
    QString withoutQuotes;
};

// The text with every bracketed part replaced by a space, until no bracket pair is left (so nested pairs go too), and the text
// of the parts that were taken, each followed by a space.
std::pair<QString, QString> removeBrackets(const QString& text) {
    QString current = text;
    QString taken;
    while (true) {
        const QList<Span> parts = findPairs(current, false);
        if (parts.isEmpty()) return {current, taken};
        for (const Span& part : parts) {
            taken += QStringView(current).sliced(part.start, part.end - part.start);
            taken += u' ';
        }
        current = replaceSpans(current, parts, u" ");
    }
}

Reduction reduce(const QString& title) {
    Reduction r;
    r.whole = kt::trim(title);
    const auto [rest, taken] = removeBrackets(r.whole);
    r.withoutBrackets = collapse(rest);
    r.bracketedParts = taken;
    const std::optional<qsizetype> creditAt = findCredit(r.withoutBrackets);
    r.credit = creditAt ? r.withoutBrackets.sliced(*creditAt) : QString();
    r.withoutCredit = collapse(creditAt ? r.withoutBrackets.first(*creditAt) : r.withoutBrackets);
    QString unquoted;
    for (const QChar c : r.withoutCredit)
        if (!kQuotes.contains(c)) unquoted += c;
    r.withoutQuotes = collapse(unquoted);
    return r;
}

// The normalised title as it is, and the normalised shorter forms that are different and not empty.
struct TitleKeys {
    QString whole;
    QStringList shortened;
};

// A shorter form is left out when a bracketed part or the credit it lacks holds a version marker.
TitleKeys titleKeys(const QString& title, bool forCandidate) {
    const Reduction r = reduce(title);
    const QString whole = normalizeForMatching(r.whole);
    const bool bracketsMayGo = !hasVersionMarker(r.bracketedParts, forCandidate);
    const bool creditMayGo = bracketsMayGo && !hasVersionMarker(r.credit, forCandidate);
    QStringList shorter;
    if (bracketsMayGo) shorter.append(r.withoutBrackets);
    if (creditMayGo) {
        shorter.append(r.withoutCredit);
        shorter.append(r.withoutQuotes);
    }
    QStringList keys;
    for (const QString& form : shorter) {
        const QString key = normalizeForMatching(form);
        if (!key.isEmpty() && key != whole) keys.append(key);
    }
    return {whole, distinct(keys)};
}

std::optional<int> titleRank(const QString& wanted, const QString& found) {
    const TitleKeys query = titleKeys(wanted, false);
    const TitleKeys candidate = titleKeys(found, true);
    if (!query.whole.isEmpty() && query.whole == candidate.whole) return kTitleWhole;
    if (!candidate.whole.isEmpty() && query.shortened.contains(candidate.whole)) return kTitleQueryShortened;
    QStringList queryForms{query.whole};
    queryForms += query.shortened;
    for (const QString& form : queryForms)
        if (!form.isEmpty() && candidate.shortened.contains(form)) return kTitleCandidateShortened;
    return std::nullopt;
}

struct ArtistForm {
    QStringList words;
    QString key;
};

// The artist forms that are long enough; a match needs one of each side to be the same name.
QList<ArtistForm> artistForms(const QString& artist) {
    QList<ArtistForm> forms;
    for (const QString& variant : artistVariants(artist)) {
        ArtistForm form{words(variant), normalizeForMatching(variant)};
        if (form.key.size() >= kMinArtistLength) forms.append(std::move(form));
    }
    return forms;
}

// True when `needle` is not empty and its words are some consecutive words of `haystack`.
bool containsRun(const QStringList& haystack, const QStringList& needle) {
    if (needle.isEmpty() || haystack.size() < needle.size()) return false;
    for (qsizetype start = 0; start <= haystack.size() - needle.size(); ++start)
        if (haystack.mid(start, needle.size()) == needle) return true;
    return false;
}

bool sameArtist(const QString& wanted, const QString& found) {
    const QList<ArtistForm> foundForms = artistForms(found);
    for (const ArtistForm& query : artistForms(wanted)) {
        for (const ArtistForm& candidate : foundForms) {
            if (query.key == candidate.key || containsRun(query.words, candidate.words) || containsRun(candidate.words, query.words))
                return true;
        }
    }
    return false;
}

// True when the query has an album (not empty once normalised) and `found` is the same album after normalising.
bool sameAlbum(const std::optional<QString>& wanted, const std::optional<QString>& found) {
    const QString key = wanted ? normalizeForMatching(*wanted) : QString();
    return !key.isEmpty() && found && normalizeForMatching(*found) == key;
}

// The seconds between the length asked for and the candidate's; nullopt when either is not known.
std::optional<double> durationDifference(const LyricsQuery& query, const LyricsCandidate& candidate) {
    if (!query.durationSeconds || !candidate.duration) return std::nullopt;
    return std::abs(double(*query.durationSeconds) - *candidate.duration);
}

// What pick sorts by.
struct Rank {
    int title;
    int album;
    double difference;
};

// The rank of a candidate for the query, or nullopt when it is not acceptable.
std::optional<Rank> rank(const LyricsQuery& query, const LyricsCandidate& candidate) {
    if (!candidate.plainLyrics || kt::isBlank(*candidate.plainLyrics) || candidate.instrumental.value_or(false)) return std::nullopt;
    if (!candidate.trackName || !candidate.artistName) return std::nullopt;
    const std::optional<int> title = titleRank(query.title, *candidate.trackName);
    if (!title) return std::nullopt;
    if (!sameArtist(query.artist, *candidate.artistName)) return std::nullopt;
    const std::optional<double> difference = durationDifference(query, candidate);
    // (a NaN difference is not greater, so it is accepted, as in Kotlin)
    if (difference && *difference > kMaxDurationDifference) return std::nullopt;
    return Rank{*title, sameAlbum(query.album, candidate.albumName) ? 0 : 1, difference.value_or(DBL_MAX)};
}

// Kotlin's Double.compareTo (java.lang.Double.compare): a total order in which NaN equals itself and is greater than every
// other value, +infinity included, and -0.0 is less than 0.0.
int compareDoubles(double a, double b) {
    const bool aNaN = std::isnan(a);
    const bool bNaN = std::isnan(b);
    if (aNaN || bNaN) return aNaN == bNaN ? 0 : (aNaN ? 1 : -1);
    if (a < b) return -1;
    if (a > b) return 1;
    const bool aNegative = std::signbit(a);
    const bool bNegative = std::signbit(b);
    if (aNegative == bNegative) return 0;
    return aNegative ? -1 : 1;
}

// compareBy({ title }, { album }, { difference })
int compareRanks(const Rank& a, const Rank& b) {
    if (a.title != b.title) return a.title < b.title ? -1 : 1;
    if (a.album != b.album) return a.album < b.album ? -1 : 1;
    return compareDoubles(a.difference, b.difference);
}

}  // namespace

QStringList titleVariants(const QString& title) {
    const Reduction r = reduce(title);
    return distinct(withoutBlank({r.whole, r.withoutBrackets, r.withoutCredit, r.withoutQuotes}));
}

QStringList artistVariants(const QString& artist) {
    const QString whole = kt::trim(artist);
    QString outside = whole;
    QStringList parts;
    while (true) {
        const QList<Span> found = findPairs(outside, true);
        if (found.isEmpty()) break;
        for (const Span& span : found) parts.append(kt::trim(outside.sliced(span.start + 1, span.end - span.start - 2)));
        outside = replaceSpans(outside, found, u" ");
    }
    QStringList variants{whole, collapse(outside)};
    variants += parts;
    return distinct(withoutBlank(variants)).mid(0, kMaxArtistVariants);
}

QString normalizeForMatching(const QString& text) {
    return words(text).join(QString());
}

bool isMatch(const LyricsQuery& query, const LyricsCandidate& candidate) {
    return rank(query, candidate).has_value();
}

qsizetype pick(const LyricsQuery& query, const QList<LyricsCandidate>& candidates) {
    // minWithOrNull: the first of the smallest
    qsizetype best = -1;
    Rank bestRank{};
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        const std::optional<Rank> current = rank(query, candidates.at(i));
        if (!current) continue;
        if (best < 0 || compareRanks(bestRank, *current) > 0) {
            best = i;
            bestRank = *current;
        }
    }
    return best;
}

}  // namespace xgs::lyrics
