#include "text/kt_text.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace xgs::kt {

namespace {

// The record types of kt_tables.inc: Unicode character data as of the Unicode version of JVM 21, obtained by asking the JVM
// (qt/tools/differential/gen-tables.ps1).
struct CharException {          // a UTF-16 unit where Qt's Unicode data and JVM 21's disagree; the JVM's answer
    char16_t unit;
    bool whitespace;
    bool letter;
    bool letterOrDigit;
    char16_t upper;
    char16_t lower;
};
struct SpecialCase {            // String.uppercase()/lowercase() of one unit when it is not uppercaseChar()/lowercaseChar()
    char16_t unit;
    int length;
    char16_t units[3];
};
struct SupplementaryCase {      // Character.toUpperCase(int)/toLowerCase(int) of a code point above U+FFFF
    char32_t codePoint;
    char32_t upper;
    char32_t lower;
};

#include "kt_tables.inc"

// findEntry below searches these tables with std::lower_bound: each must be strictly ascending by its key.
template <typename Table>
constexpr bool isStrictlyAscending(const Table& table) {
    for (std::size_t i = 1; i < table.size(); ++i) {
        if constexpr (requires { table[i].unit; }) {
            if (!(table[i - 1].unit < table[i].unit)) return false;
        } else {
            if (!(table[i - 1].codePoint < table[i].codePoint)) return false;
        }
    }
    return true;
}
static_assert(isStrictlyAscending(kCharExceptions), "kt_tables.inc: kCharExceptions must be sorted by unit");
static_assert(isStrictlyAscending(kUpperSpecials), "kt_tables.inc: kUpperSpecials must be sorted by unit");
static_assert(isStrictlyAscending(kLowerSpecials), "kt_tables.inc: kLowerSpecials must be sorted by unit");
static_assert(isStrictlyAscending(kSupplementaryCases), "kt_tables.inc: kSupplementaryCases must be sorted by code point");

template <typename Table, typename Key>
auto findEntry(const Table& table, Key key) -> decltype(&table[0]) {
    const auto it = std::lower_bound(table.begin(), table.end(), key, [](const auto& entry, Key value) {
        if constexpr (requires { entry.unit; }) return entry.unit < value;
        else return entry.codePoint < value;
    });
    if (it == table.end()) return nullptr;
    if constexpr (requires { it->unit; }) return it->unit == key ? &*it : nullptr;
    else return it->codePoint == key ? &*it : nullptr;
}

const CharException* charException(char16_t unit) {
    return findEntry(kCharExceptions, unit);
}

constexpr bool isHigh(char16_t unit) { return unit >= 0xD800 && unit <= 0xDBFF; }
constexpr bool isLow(char16_t unit) { return unit >= 0xDC00 && unit <= 0xDFFF; }
constexpr char32_t toCodePoint(char16_t high, char16_t low) {
    return (char32_t(high - 0xD800) << 10) + char32_t(low - 0xDC00) + 0x10000;
}
constexpr qsizetype charCount(char32_t codePoint) { return codePoint > 0xFFFF ? 2 : 1; }

// String.codePointAt / codePointBefore: a lone surrogate is a code point of its own.
char32_t codePointAt(QStringView s, qsizetype index) {
    const char16_t c1 = s[index].unicode();
    if (isHigh(c1) && index + 1 < s.size() && isLow(s[index + 1].unicode())) return toCodePoint(c1, s[index + 1].unicode());
    return c1;
}

char32_t codePointBefore(QStringView s, qsizetype index) {
    const char16_t c2 = s[index - 1].unicode();
    if (isLow(c2) && index - 2 >= 0 && isHigh(s[index - 2].unicode())) return toCodePoint(s[index - 2].unicode(), c2);
    return c2;
}

void appendCodePoint(QString& out, char32_t codePoint) {
    if (codePoint > 0xFFFF) {
        out.append(QChar(char16_t(0xD800 + ((codePoint - 0x10000) >> 10))));
        out.append(QChar(char16_t(0xDC00 + ((codePoint - 0x10000) & 0x3FF))));
    } else {
        out.append(QChar(char16_t(codePoint)));
    }
}

// Character.toUpperCase(int) / toLowerCase(int). Above U+FFFF only the JVM's table counts (Qt knows newer scripts).
char32_t upperCodePoint(char32_t codePoint) {
    if (codePoint <= 0xFFFF) return uppercaseChar(QChar(char16_t(codePoint))).unicode();
    const SupplementaryCase* entry = findEntry(kSupplementaryCases, codePoint);
    return entry ? entry->upper : codePoint;
}

char32_t lowerCodePoint(char32_t codePoint) {
    if (codePoint <= 0xFFFF) return lowercaseChar(QChar(char16_t(codePoint))).unicode();
    const SupplementaryCase* entry = findEntry(kSupplementaryCases, codePoint);
    return entry ? entry->lower : codePoint;
}

// Word_Break MidLetter, MidNumLet and Single_Quote characters (Unicode Standard Annex #29, WordBreakProperty.txt), which
// with the general categories Mn, Me, Cf, Lm and Sk make up Case_Ignorable (Unicode Standard section 3.13,
// DerivedCoreProperties.txt). U+00AD is Cf already.
constexpr char32_t kWordBreakMidCharacters[] = {0x0027, 0x002E, 0x003A, 0x00B7, 0x2018, 0x2019,
                                                0x2024, 0x2027, 0xFE13, 0xFE52, 0xFE55, 0xFF07, 0xFF0E, 0xFF1A};

bool isCaseIgnorable(char32_t codePoint) {
    switch (QChar::category(codePoint)) {
    case QChar::Mark_NonSpacing:
    case QChar::Mark_Enclosing:
    case QChar::Other_Format:
    case QChar::Letter_Modifier:
    case QChar::Symbol_Modifier:
        return true;
    default:
        return std::find(std::begin(kWordBreakMidCharacters), std::end(kWordBreakMidCharacters), codePoint) !=
               std::end(kWordBreakMidCharacters);
    }
}

// Cased (Unicode Standard section 3.13), from Qt's uppercase, lowercase and titlecase properties.
bool isCased(char32_t codePoint) {
    return QChar::isUpper(codePoint) || QChar::isLower(codePoint) || QChar::isTitleCase(codePoint);
}

// An approximation of the Final_Sigma condition of the Unicode Standard (section 3.13, SpecialCasing.txt) for the sigma at
// `index`: a cased letter comes before it and none after it, skipping case-ignorable characters on both sides. It is not
// the full rule: Cased is taken from Qt's uppercase/lowercase/titlecase categories, and case-ignorable is a documented list
// of punctuation (see isCaseIgnorable). Kotlin on the JVM bounds the search by word boundaries instead; this app never
// shows the difference, see qt/README.md.
bool isFinalSigma(QStringView s, qsizetype index) {
    bool casedBefore = false;
    for (qsizetype i = index; i > 0;) {
        const char32_t codePoint = codePointBefore(s, i);
        i -= charCount(codePoint);
        if (isCaseIgnorable(codePoint)) continue;
        casedBefore = isCased(codePoint);
        break;
    }
    if (!casedBefore) return false;
    for (qsizetype i = index + 1; i < s.size();) {
        const char32_t codePoint = codePointAt(s, i);
        i += charCount(codePoint);
        if (isCaseIgnorable(codePoint)) continue;
        return !isCased(codePoint);
    }
    return true;
}

// Two characters are the same ignoring case, as the documentation of String.regionMatches(ignoreCase = true) defines it:
// they are equal, or their uppercase forms are equal, or the lowercase forms of their uppercase forms are equal. The case
// mapping is locale-independent.
bool sameIgnoringCase(char32_t x, char32_t y) {
    if (x == y) return true;
    const char32_t xUpper = upperCodePoint(x);
    const char32_t yUpper = upperCodePoint(y);
    return xUpper == yUpper || lowerCodePoint(xUpper) == lowerCodePoint(yUpper);
}

// The character at `index` inside the region [begin, end) of `s`: a surrogate pair whose two halves both lie in the region
// is one code point, read forward from its high half (`step` = 2) or backward from its low half (`step` = 1); any other
// unit, a lone surrogate included, is a character of its own (`step` = 1).
char32_t characterInRegion(QStringView s, qsizetype index, qsizetype begin, qsizetype end, qsizetype& step) {
    const char16_t unit = s[index].unicode();
    step = 1;
    if (isHigh(unit) && index + 1 < end && isLow(s[index + 1].unicode())) {
        step = 2;
        return toCodePoint(unit, s[index + 1].unicode());
    }
    if (isLow(unit) && index - 1 >= begin && isHigh(s[index - 1].unicode())) return toCodePoint(s[index - 1].unicode(), unit);
    return unit;
}

// String.regionMatches(ignoreCase, aOffset, b, bOffset, length) as documented: false when an offset is negative or a region
// runs past the end of its string, true when `length` is zero or less; without ignoreCase the regions are compared unit by
// unit. With ignoreCase the rule below is what the JVM 21 of the sidecar image answers (probe cases and answers in
// qt/tests/tst_kt_text.cpp, regionMatchesIgnoreCaseMatchesTheObservedJvmAnswers; checked by the differential kinds S, N):
//  - the two regions are walked with a position each; equal units move both positions by one;
//  - otherwise each side reads the character at its position (see characterInRegion: a pair only when both halves are in its
//    region, so "\uD83D" as the region of "😀" is a lone surrogate, and a low half whose high half lies before the
//    region is lone too), the two characters must be the same ignoring case, and each position moves past its own character;
//    so one side may move by two units while the other moves by one;
//  - the comparison ends, with true, as soon as either region is used up, even if the other still has units left
//    (indexOf("\uD83D😀C", "😀B", 0, true) is 0).
bool regionMatchesImpl(QStringView a, qsizetype aOffset, QStringView b, qsizetype bOffset, qsizetype length, bool ignoreCase) {
    if (aOffset < 0 || bOffset < 0 || aOffset > a.size() - length || bOffset > b.size() - length) return false;
    if (length <= 0) return true;
    if (!ignoreCase) return a.sliced(aOffset, length) == b.sliced(bOffset, length);
    const qsizetype aEnd = aOffset + length;
    const qsizetype bEnd = bOffset + length;
    qsizetype i = aOffset;
    qsizetype j = bOffset;
    while (i < aEnd && j < bEnd) {
        if (a[i] == b[j]) {
            ++i;
            ++j;
            continue;
        }
        qsizetype aStep = 1;
        qsizetype bStep = 1;
        const char32_t x = characterInRegion(a, i, aOffset, aEnd, aStep);
        const char32_t y = characterInRegion(b, j, bOffset, bEnd, bStep);
        if (!sameIgnoringCase(x, y)) return false;
        i += aStep;
        j += bStep;
    }
    return true;
}

}  // namespace

bool isWhitespace(QChar c) {
    if (const CharException* e = charException(c.unicode())) return e->whitespace;
    return detail::qtCharFacts(c.unicode()).whitespace;
}

bool isLetter(QChar c) {
    if (const CharException* e = charException(c.unicode())) return e->letter;
    return detail::qtCharFacts(c.unicode()).letter;
}

bool isLetterOrDigit(QChar c) {
    if (const CharException* e = charException(c.unicode())) return e->letterOrDigit;
    return detail::qtCharFacts(c.unicode()).letterOrDigit;
}

bool isHighSurrogate(QChar c) {
    return isHigh(c.unicode());
}

bool isSurrogate(QChar c) {
    return c.unicode() >= 0xD800 && c.unicode() <= 0xDFFF;
}

QChar uppercaseChar(QChar c) {
    if (const CharException* e = charException(c.unicode())) return QChar(e->upper);
    return QChar(detail::qtCharFacts(c.unicode()).upper);
}

QChar lowercaseChar(QChar c) {
    if (const CharException* e = charException(c.unicode())) return QChar(e->lower);
    return QChar(detail::qtCharFacts(c.unicode()).lower);
}

bool equalsIgnoreCase(QChar a, QChar b) {
    if (a == b) return true;
    const QChar aUpper = uppercaseChar(a);
    const QChar bUpper = uppercaseChar(b);
    return aUpper == bUpper || lowercaseChar(aUpper) == lowercaseChar(bUpper);
}

// Kotlin's String.lowercase(): the simple mapping of each code point, except U+0130 and U+03A3 (approximating Unicode's
// Final_Sigma, see isFinalSigma).
QString lowercase(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size();) {
        const char32_t codePoint = codePointAt(s, i);
        if (codePoint == 0x03A3) {
            out.append(QChar(char16_t(isFinalSigma(s, i) ? 0x03C2 : 0x03C3)));
        } else if (const SpecialCase* special = codePoint <= 0xFFFF ? findEntry(kLowerSpecials, char16_t(codePoint)) : nullptr) {
            out.append(reinterpret_cast<const QChar*>(special->units), special->length);
        } else {
            appendCodePoint(out, lowerCodePoint(codePoint));
        }
        i += charCount(codePoint);
    }
    return out;
}

// java.lang.String.toUpperCase(Locale.ROOT): the full (SpecialCasing) mapping of each code point, with no context.
QString uppercase(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size();) {
        const char32_t codePoint = codePointAt(s, i);
        if (const SpecialCase* special = codePoint <= 0xFFFF ? findEntry(kUpperSpecials, char16_t(codePoint)) : nullptr) {
            out.append(reinterpret_cast<const QChar*>(special->units), special->length);
        } else {
            appendCodePoint(out, upperCodePoint(codePoint));
        }
        i += charCount(codePoint);
    }
    return out;
}

QString trim(const QString& s) {
    return trimEnd(trimStart(s));
}

QString trimStart(const QString& s) {
    qsizetype start = 0;
    while (start < s.size() && isWhitespace(s[start])) ++start;
    return s.sliced(start);
}

QString trimEnd(const QString& s) {
    qsizetype end = s.size();
    while (end > 0 && isWhitespace(s[end - 1])) --end;
    return s.first(end);
}

QString trimEndChars(const QString& s, QStringView chars) {
    qsizetype end = s.size();
    while (end > 0 && chars.contains(s[end - 1])) --end;
    return s.first(end);
}

QString trimStartChars(const QString& s, QStringView chars) {
    qsizetype start = 0;
    while (start < s.size() && chars.contains(s[start])) ++start;
    return s.sliced(start);
}

bool isBlank(const QString& s) {
    return std::all_of(s.begin(), s.end(), [](QChar c) { return isWhitespace(c); });
}

bool startsWith(QStringView s, QStringView prefix, qsizetype from, CaseMode mode) {
    return regionMatchesImpl(s, from, prefix, 0, prefix.size(), mode == CaseMode::Ignore);
}

bool startsWith(QStringView s, QStringView prefix, CaseMode mode) {
    return startsWith(s, prefix, 0, mode);
}

bool endsWith(QStringView s, QStringView suffix, CaseMode mode) {
    return regionMatchesImpl(s, s.size() - suffix.size(), suffix, 0, suffix.size(), mode == CaseMode::Ignore);
}

bool contains(QStringView s, QStringView needle, CaseMode mode) {
    return indexOf(s, needle, 0, mode) >= 0;
}

qsizetype indexOf(QStringView s, QStringView needle, qsizetype from, CaseMode mode) {
    if (mode == CaseMode::Sensitive) {
        // java.lang.String.indexOf(String, int)
        if (from >= s.size()) return needle.isEmpty() ? s.size() : -1;
        return s.indexOf(needle, std::max<qsizetype>(from, 0));
    }
    // Kotlin's CharSequence.indexOf(other, startIndex, endIndex = length, ignoreCase = true): this loop follows the
    // algorithm of the Kotlin standard library (kotlin-stdlib, Apache License 2.0, (c) JetBrains s.r.o. and Kotlin
    // Programming Language contributors); see THIRD-PARTY-NOTICES.md at the repository root.
    for (qsizetype index = std::max<qsizetype>(from, 0); index <= s.size(); ++index) {
        if (regionMatchesImpl(needle, 0, s, index, needle.size(), true)) return index;
    }
    return -1;
}

qsizetype indexOf(QStringView s, QStringView needle, CaseMode mode) {
    return indexOf(s, needle, 0, mode);
}

bool regionMatches(QStringView s, qsizetype at, QStringView other, CaseMode mode) {
    return regionMatchesImpl(s, at, other, 0, other.size(), mode == CaseMode::Ignore);
}

// Kotlin's String.replace(oldValue, newValue, ignoreCase). It follows the algorithm of the Kotlin standard library step for
// step (kotlin-stdlib, Apache License 2.0, (c) JetBrains s.r.o. and Kotlin Programming Language contributors); see
// THIRD-PARTY-NOTICES.md at the repository root.
QString replace(const QString& s, QStringView oldValue, QStringView newValue, CaseMode mode) {
    qsizetype occurrence = indexOf(s, oldValue, 0, mode);
    if (occurrence < 0) return s;
    const qsizetype oldLength = oldValue.size();
    const qsizetype searchStep = std::max<qsizetype>(oldLength, 1);
    QString out;
    out.reserve(s.size() - oldLength + newValue.size());
    qsizetype i = 0;
    do {
        out.append(QStringView(s).sliced(i, occurrence - i)).append(newValue);
        i = occurrence + oldLength;
        if (occurrence >= s.size()) break;
        occurrence = indexOf(s, oldValue, occurrence + searchStep, mode);
    } while (occurrence > 0);
    out.append(QStringView(s).sliced(i));
    return out;
}

namespace detail {

CharFacts qtCharFacts(char16_t unit) {
    const QChar c(unit);
    const QChar::Category category = c.category();
    const bool letter = category == QChar::Letter_Uppercase || category == QChar::Letter_Lowercase ||
                        category == QChar::Letter_Titlecase || category == QChar::Letter_Modifier ||
                        category == QChar::Letter_Other;
    const bool whitespace = category == QChar::Separator_Space || category == QChar::Separator_Line ||
                            category == QChar::Separator_Paragraph || (unit >= 0x0009 && unit <= 0x000D) ||
                            (unit >= 0x001C && unit <= 0x001F);
    return {whitespace, letter, letter || category == QChar::Number_DecimalDigit, c.toUpper().unicode(), c.toLower().unicode()};
}

}  // namespace detail

}  // namespace xgs::kt
