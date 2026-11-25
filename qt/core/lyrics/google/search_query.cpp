#include "lyrics/google/search_query.h"

#include <QList>

#include <optional>

#include "net/url_encoding.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

// The five regular expressions of GoogleSearchQuery.kt are scanners here (Qt's regular expressions are not used in ported code). Their
// meaning is taken from the public documentation of java.util.regex.Pattern on JVM 21, and the Q3 Task 5 differential compares
// the results with the Kotlin original:
//   - `\s` is the ASCII white space [ \t\n\x0B\f\r] (no UNICODE_CHARACTER_CLASS): kt::isAsciiSpace;
//   - `\b` only ever follows one of the credit words here, whose last character is an ASCII letter. It holds when the next
//     unit is not [a-zA-Z_0-9] (`\w` without UNICODE_CHARACTER_CLASS) and the next code point (a surrogate pair is one) is not
//     a non-spacing mark (general category Mn, kt::isNonSpacingMark), or when there is none. The marks were observed by the
//     Q3 Task 5 differential: after `feat` a U+0301, U+0300 or U+1D167 leaves no boundary, while U+20DD (Me), U+0903 (Mc),
//     U+200C (Cf), non-ASCII letters, digits and symbols do;
//   - `\p{L}` is a code point of general category L: kt::isLetter(char32_t). The lookahead `(?=\p{L})` takes the code point
//     after the quote (a surrogate pair is one code point); the lookbehind `(?<=\p{L})` looks at the one unit before it only,
//     so a letter above U+FFFF before the quote does not count (observed by the Q3 Task 5 differential: `𝒜’a` keeps no
//     apostrophe, `a’𝒜` gets a straight one);
//   - a match is the leftmost one, the greedy quantifiers take what they can, and replace() goes on after a match without
//     overlap, with the lookarounds seeing the original text.
// Case: CREDIT_BRACKET and TRAILING_CREDIT have RegexOption.IGNORE_CASE, which in Kotlin is Unicode case folding. The scanners
// fold ASCII letters only, which gives the same result for `feat`, `ft`, `prod`, `narr`: no character outside ASCII has one of
// a d e f n o p r t as its upper- or lowercase form (the non-ASCII characters that fold to an ASCII letter fold to s, k or i;
// lyrics_matcher.cpp has the same argument for its CREDIT).
namespace xgs::lyrics::google {

namespace {

// Double quotes and the typographic single and double quotes (the straight apostrophe is not among them).
constexpr QStringView kQuotes = u"\"‘’‚‛“”„‟";

constexpr QStringView kCreditWords[] = {u"feat", u"ft", u"prod", u"narr"};

constexpr char16_t kFinalSigma = 0x03C2;
constexpr char16_t kSigma = 0x03C3;

// A match of a regular expression: [start, end) in UTF-16 units.
struct Span {
    qsizetype start;
    qsizetype end;
};

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

bool isAsciiSpaceAt(const QString& text, qsizetype at) {
    return at < text.size() && kt::isAsciiSpace(text.at(at));
}

char16_t asciiFolded(char16_t unit) {
    return unit >= u'A' && unit <= u'Z' ? char16_t(unit - u'A' + u'a') : unit;
}

bool asciiRegionMatchesIgnoreCase(const QString& text, qsizetype at, QStringView word) {
    if (at + word.size() > text.size()) return false;
    for (qsizetype k = 0; k < word.size(); ++k)
        if (asciiFolded(text.at(at + k).unicode()) != asciiFolded(word.at(k).unicode())) return false;
    return true;
}

// `\w` of a Java regular expression without UNICODE_CHARACTER_CLASS.
bool isWordUnit(char16_t unit) {
    return (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9') || unit == u'_';
}

std::optional<char32_t> codePointAt(const QString& text, qsizetype at);

// `\b` right after a credit word (whose last character is a word character): neither a word unit nor a non-spacing mark next.
bool boundaryAfterWord(const QString& text, qsizetype at) {
    if (at >= text.size()) return true;
    if (isWordUnit(text.at(at).unicode())) return false;
    return !kt::isNonSpacingMark(*codePointAt(text, at));
}

bool isBracket(char16_t unit) {
    return unit == u'(' || unit == u')' || unit == u'[' || unit == u']';
}

// The end of the credit word at `at` followed by `\b`; -1 when none of the words is there. The words differ in their first two
// letters, so at most one of them can match.
qsizetype creditWordEnd(const QString& text, qsizetype at) {
    for (const QStringView word : kCreditWords) {
        if (!asciiRegionMatchesIgnoreCase(text, at, word)) continue;
        const qsizetype after = at + word.size();
        if (boundaryAfterWord(text, after)) return after;
    }
    return -1;
}

// CREDIT_BRACKET `[(\[]\s*(?:feat|ft|prod|narr)\b\.?[^()\[\]]*[)\]]` (IGNORE_CASE). At `(` or `[`: the white space run (taken
// whole: a shorter one would leave white space where a letter must be), a credit word with a boundary after it, and then
// everything up to the first bracket of any kind, which must be a closing one. `\.?` changes nothing: a dot that it does not
// take is taken by `[^()\[\]]*`.
QList<Span> findCreditBrackets(const QString& text) {
    QList<Span> found;
    const qsizetype length = text.size();
    qsizetype i = 0;
    while (i < length) {
        const char16_t open = text.at(i).unicode();
        if (open == u'(' || open == u'[') {
            qsizetype j = i + 1;
            while (isAsciiSpaceAt(text, j)) ++j;
            const qsizetype afterWord = creditWordEnd(text, j);
            if (afterWord >= 0) {
                qsizetype k = afterWord;
                while (k < length && !isBracket(text.at(k).unicode())) ++k;
                if (k < length && (text.at(k) == u')' || text.at(k) == u']')) {
                    found.append({i, k + 1});
                    i = k + 1;
                    continue;
                }
            }
        }
        ++i;
    }
    return found;
}

// TRAILING_CREDIT `\s+(?:feat|ft|prod|narr)\b\.?\s+[^()\[\]]*` (IGNORE_CASE). A run of white space (taken whole, so a start
// inside a run reaches the same word as the run's first unit), a credit word, a dot that may be there (greedy: first with
// it, then without), at least one white space, and everything up to the next bracket or the end. `\b` is implied: a dot or a
// white space follows the word.
QList<Span> findTrailingCredits(const QString& text) {
    QList<Span> found;
    const qsizetype length = text.size();
    qsizetype i = 0;
    while (i < length) {
        if (!kt::isAsciiSpace(text.at(i))) {
            ++i;
            continue;
        }
        qsizetype j = i;
        while (isAsciiSpaceAt(text, j)) ++j;
        const qsizetype afterWord = creditWordEnd(text, j);
        if (afterWord >= 0) {
            qsizetype spaceAt = -1;
            if (afterWord < length && text.at(afterWord) == u'.' && isAsciiSpaceAt(text, afterWord + 1))
                spaceAt = afterWord + 1;
            else if (isAsciiSpaceAt(text, afterWord))
                spaceAt = afterWord;
            if (spaceAt >= 0) {
                qsizetype k = spaceAt;
                while (k < length && !isBracket(text.at(k).unicode())) ++k;
                found.append({i, k});
                i = k;
                continue;
            }
        }
        i = j;
    }
    return found;
}

// PARENTHESISED `\([^()]*\)`: at `(`, a match when the first parenthesis after it is `)`.
QList<Span> findParenthesised(const QString& text) {
    QList<Span> found;
    const qsizetype length = text.size();
    qsizetype i = 0;
    while (i < length) {
        if (text.at(i) == u'(') {
            qsizetype j = i + 1;
            while (j < length && text.at(j) != u'(' && text.at(j) != u')') ++j;
            if (j < length && text.at(j) == u')') {
                found.append({i, j + 1});
                i = j + 1;
                continue;
            }
        }
        ++i;
    }
    return found;
}

// What the lookbehind `(?<=\p{L})` looks at: the one unit before `at` (a low surrogate there is no letter, whatever comes
// before it), or nothing at the start.
std::optional<char32_t> unitBefore(const QString& text, qsizetype at) {
    if (at <= 0) return std::nullopt;
    return text.at(at - 1).unicode();
}

// The code point that starts at `at`, or nothing at the end.
std::optional<char32_t> codePointAt(const QString& text, qsizetype at) {
    if (at >= text.size()) return std::nullopt;
    const char16_t first = text.at(at).unicode();
    if (first >= 0xD800 && first <= 0xDBFF && at + 1 < text.size()) {
        const char16_t second = text.at(at + 1).unicode();
        if (second >= 0xDC00 && second <= 0xDFFF) return 0x10000 + ((char32_t(first) - 0xD800) << 10) + (char32_t(second) - 0xDC00);
    }
    return first;
}

bool isLetterCodePoint(const std::optional<char32_t>& codePoint) {
    return codePoint && kt::isLetter(*codePoint);
}

// APOSTROPHE `(?<=\p{L})[‘’](?=\p{L})` replaced by a straight apostrophe.
QString straightenApostrophes(const QString& text) {
    QString out = text;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t unit = text.at(i).unicode();
        if (unit != 0x2018 && unit != 0x2019) continue;
        if (isLetterCodePoint(unitBefore(text, i)) && isLetterCodePoint(codePointAt(text, i + 1))) out[i] = u'\'';
    }
    return out;
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

QString cleanTitle(const QString& title) {
    QString text = straightenApostrophes(title);
    text = replaceSpans(text, findCreditBrackets(text), u" ");
    text = replaceSpans(text, findTrailingCredits(text), u" ");
    QString cleaned;
    cleaned.reserve(text.size());
    for (const QChar c : text)
        if (!kQuotes.contains(c)) cleaned += c;
    const QString collapsed = collapse(cleaned);
    return collapsed.isEmpty() ? collapse(title) : collapsed;
}

// Kotlin's three ranges: Hangul syllables, Hangul Jamo and Hangul Compatibility Jamo (with their unassigned ends).
bool hasHangul(const QString& text) {
    for (const QChar c : text) {
        const char16_t unit = c.unicode();
        if ((unit >= 0xAC00 && unit <= 0xD7A3) || (unit >= 0x1100 && unit <= 0x11FF) || (unit >= 0x3130 && unit <= 0x318F))
            return true;
    }
    return false;
}

}  // namespace

QString GoogleSearchRequest::url(QStringView searchBase) const {
    return searchBase.toString() + u"?q="_s + net::encodeQueryValue(text) + u"&hl="_s + language;
}

std::vector<GoogleSearchRequest> requests(const QString& artist, const QString& title) {
    const QString fullArtist = collapse(artist);
    const QString cleanedTitle = cleanTitle(title);
    if (fullArtist.isEmpty() || cleanedTitle.isEmpty()) return {};
    const QString language = hasHangul(artist) || hasHangul(title) ? u"ko"_s : u"en"_s;
    const QString artistWithoutParentheses = collapse(replaceSpans(fullArtist, findParenthesised(fullArtist), u" "));
    std::vector<GoogleSearchRequest> out;
    QList<QString> seen;
    for (const QString& name : {fullArtist, artistWithoutParentheses}) {
        if (name.isEmpty() || seen.contains(name)) continue;
        seen.append(name);
        out.push_back(GoogleSearchRequest{collapse(name + u' ' + cleanedTitle + u" lyrics"_s), language});
    }
    return out;
}

QString cacheKey(const GoogleSearchRequest& request) {
    QString key = kt::lowercase(kt::nfkc(request.text));
    key.replace(QChar(kFinalSigma), QChar(kSigma));
    return key;
}

}  // namespace xgs::lyrics::google
