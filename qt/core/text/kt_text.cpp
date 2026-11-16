#include "text/kt_text.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

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
struct LetterOrDigitRange {     // code points above U+FFFF where Qt's letter-or-digit answer differs from JVM 21's; the JVM's
    char32_t first;
    char32_t last;
    bool letterOrDigit;
};

#include "kt_tables.inc"

// findEntry below searches these tables with std::lower_bound: each must be strictly ascending by its key.
template <typename Table>
constexpr bool isStrictlyAscending(const Table& table) {
    for (std::size_t i = 1; i < table.size(); ++i) {
        if constexpr (requires { table[i].first; }) {
            if (!(table[i - 1].last < table[i].first)) return false;
        } else if constexpr (requires { table[i].unit; }) {
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
static_assert(isStrictlyAscending(kSupplementaryLetterOrDigit), "kt_tables.inc: kSupplementaryLetterOrDigit must be sorted, disjoint");
constexpr bool rangesAreWellFormed() {
    for (const LetterOrDigitRange& range : kSupplementaryLetterOrDigit) {
        if (range.first > range.last || range.first <= 0xFFFF) return false;
    }
    return true;
}
static_assert(rangesAreWellFormed(), "kt_tables.inc: a range of kSupplementaryLetterOrDigit is reversed or not above U+FFFF");

// The range of a sorted, disjoint range table that holds `codePoint`, or nullptr.
template <typename Table>
auto findRange(const Table& table, char32_t codePoint) -> decltype(&table[0]) {
    const auto it = std::upper_bound(table.begin(), table.end(), codePoint,
                                     [](char32_t value, const auto& range) { return value < range.first; });
    if (it == table.begin()) return nullptr;
    const auto& range = *(it - 1);
    return codePoint <= range.last ? &range : nullptr;
}

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

// Above U+FFFF Qt's categories decide, except where JVM 21's answer differs (kSupplementaryLetterOrDigit: mostly letters of
// scripts that Qt's newer Unicode has and JVM 21's has not).
bool isLetterOrDigit(char32_t codePoint) {
    if (codePoint <= 0xFFFF) return isLetterOrDigit(QChar(char16_t(codePoint)));
    if (codePoint > 0x10FFFF) return false;
    if (const LetterOrDigitRange* range = findRange(kSupplementaryLetterOrDigit, codePoint)) return range->letterOrDigit;
    return detail::qtLetterOrDigit(codePoint);
}

bool isAsciiSpace(QChar c) {
    const char16_t unit = c.unicode();
    return unit == u' ' || unit == u'\t' || unit == u'\n' || unit == 0x000B || unit == u'\f' || unit == u'\r';
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

// Qt's NFKC, told to follow Unicode 15.0, the Unicode version of JVM 21 (the Javadoc of java.lang.Character for Java 21).
// QString::normalized documents the version argument as the version of the standard to normalize by; with it, the code
// points that Qt's newer Unicode added are left alone exactly as JVM 21 leaves its unassigned code points (no decomposition,
// no reordering, no composition across them), and unpaired surrogates stay as they are. The differential kinds Z (every code
// point) and H (strings, with the decompositions and combining marks added after Unicode 15.0) find no difference, so no
// exception table is needed; the one call of QString::normalized in the port is here (qt/tools/forbidden-calls.ps1).
QString nfkc(const QString& s) {
    return s.normalized(QString::NormalizationForm_KC, QChar::Unicode_15_0);
}

namespace {

// Ignoring the sign, a magnitude estimate of a literal whose conversion over- or underflowed: true when it overflowed. The
// literal is digits with an optional point and the exponent, all checked already; `radixLog2` is 0 for decimal (the exponent
// counts powers of 10 and the digits are decimal) and 4 for hexadecimal (the digits are hex and the exponent counts powers
// of 2). The first non-zero digit fixes the order of magnitude. MSVC's std::from_chars reports result_out_of_range only for
// a result that rounds to infinity or to zero (subnormal results come back as values; kind O checks the edges of both), so
// far from 1 that the sign of this estimate cannot be wrong.
bool overflowed(std::string_view digits, long long exponent, int radixLog2) {
    const std::size_t point = digits.find('.');
    const std::string_view integer = digits.substr(0, point);
    const std::size_t firstNonZero = digits.find_first_not_of("0.");
    if (firstNonZero == std::string_view::npos) return false;
    const long long place = firstNonZero < integer.size() ? (long long)(integer.size() - firstNonZero) - 1
                                                          : -(long long)(firstNonZero - integer.size());
    return (radixLog2 == 0 ? place : place * radixLog2) + exponent > 0;
}

// Decimal digits as an exponent value, saturated far beyond any double's range.
long long exponentValue(std::string_view digits, bool negative) {
    long long value = 0;
    for (const char c : digits) value = std::min<long long>(value * 10 + (c - '0'), 1'000'000'000LL);
    return negative ? -value : value;
}

constexpr bool isDecimalDigit(char c) { return c >= '0' && c <= '9'; }
constexpr bool isHexDigit(char c) { return isDecimalDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// Digits with at most one point and at least one digit; returns the length taken from `text` (0 when there are no digits).
template <typename IsDigit>
std::size_t significandLength(std::string_view text, IsDigit isDigit) {
    std::size_t length = 0;
    bool point = false;
    bool digit = false;
    for (; length < text.size(); ++length) {
        const char c = text[length];
        if (isDigit(c)) digit = true;
        else if (c == '.' && !point) point = true;
        else break;
    }
    return digit ? length : 0;
}

// The unsigned part of a floating-point literal of java.lang.Double.valueOf(String) without its suffix: a decimal literal
// (Digits . Digits? Exponent? | . Digits Exponent? | Digits Exponent?) or a hexadecimal one (0x HexDigits . HexDigits? or
// 0x . HexDigits, then p/P, an optional sign and Digits).
std::optional<double> unsignedLiteral(std::string_view text) {
    const bool hex = text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const std::string_view body = hex ? text.substr(2) : text;
    const std::size_t length = significandLength(body, hex ? isHexDigit : isDecimalDigit);
    if (length == 0) return std::nullopt;
    const std::string_view significand = body.substr(0, length);
    std::string_view rest = body.substr(length);
    long long exponent = 0;
    std::string_view exponentDigits;
    bool exponentNegative = false;
    if (!rest.empty()) {
        if (rest[0] != (hex ? 'p' : 'e') && rest[0] != (hex ? 'P' : 'E')) return std::nullopt;
        rest.remove_prefix(1);
        if (!rest.empty() && (rest[0] == '+' || rest[0] == '-')) {
            exponentNegative = rest[0] == '-';
            rest.remove_prefix(1);
        }
        if (rest.empty() || !std::all_of(rest.begin(), rest.end(), isDecimalDigit)) return std::nullopt;
        exponentDigits = rest;
        exponent = exponentValue(exponentDigits, exponentNegative);
    } else if (hex) {
        return std::nullopt;   // a hexadecimal literal needs its binary exponent
    }
    // std::from_chars wants the literal without "0x"; its "e"/"p" exponent reads the same digits.
    std::string literal(significand);
    if (!exponentDigits.empty()) {
        literal += hex ? 'p' : 'e';
        if (exponentNegative) literal += '-';
        literal += exponentDigits;
    }
    double value = 0;
    const auto [end, error] = std::from_chars(literal.data(), literal.data() + literal.size(), value,
                                              hex ? std::chars_format::hex : std::chars_format::general);
    if (error == std::errc::result_out_of_range) {
        return overflowed(significand, exponent, hex ? 4 : 0) ? std::numeric_limits<double>::infinity() : 0.0;
    }
    if (error != std::errc() || end != literal.data() + literal.size()) return std::nullopt;
    return value;
}

}  // namespace

// Which strings are numbers follows the documented grammar of java.lang.Double.valueOf(String) (its Javadoc) and the answers
// that Kotlin's String.toDoubleOrNull() gives on the JVM of the sidecar image (differential kind O): leading and trailing
// characters up to U+0020 are dropped (as String.trim), then an optional sign and "NaN", "Infinity" or a floating-point
// literal with an optional f/F/d/D suffix (which does not change the value). Only ASCII counts as digits, and nothing here
// reads the locale (std::from_chars does not).
std::optional<double> toDoubleOrNull(const QString& s) {
    qsizetype begin = 0;
    qsizetype end = s.size();
    while (begin < end && s[begin].unicode() <= 0x20) ++begin;
    while (end > begin && s[end - 1].unicode() <= 0x20) --end;
    std::string ascii;
    ascii.reserve(std::size_t(end - begin));
    for (qsizetype i = begin; i < end; ++i) {
        const char16_t unit = s[i].unicode();
        if (unit > 0x7E) return std::nullopt;
        ascii += char(unit);
    }
    std::string_view text = ascii;
    bool negative = false;
    if (!text.empty() && (text[0] == '+' || text[0] == '-')) {
        negative = text[0] == '-';
        text.remove_prefix(1);
    }
    if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
    if (text == "Infinity") return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F' || text.back() == 'd' || text.back() == 'D')) text.remove_suffix(1);
    const std::optional<double> value = unsignedLiteral(text);
    if (!value) return std::nullopt;
    return negative ? -*value : *value;
}

QString hexBits(double value) {
    static constexpr char16_t digits[] = u"0123456789abcdef";
    const quint64 bits = std::bit_cast<quint64>(value);
    QString out(16, u'0');
    for (int i = 0; i < 16; ++i) out[i] = QChar(digits[(bits >> (60 - 4 * i)) & 15]);
    return out;
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

bool qtLetterOrDigit(char32_t codePoint) {
    switch (QChar::category(codePoint)) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
    case QChar::Number_DecimalDigit:
        return true;
    default:
        return false;
    }
}

}  // namespace detail

}  // namespace xgs::kt
