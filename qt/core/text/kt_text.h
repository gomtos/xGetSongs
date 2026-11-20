#pragma once

#include <QByteArrayView>
#include <QChar>
#include <QString>
#include <QStringView>

#include <concepts>
#include <optional>

// Kotlin/JVM character and string semantics, so that the ports of `shared` give exactly the answers of the Kotlin originals
// (Kotlin's standard library on JVM 21). Lengths and indices are UTF-16 units, as Kotlin's String.length. Nothing here depends
// on the locale. qt/tools/differential (kinds X and S) compares these functions with the JVM.
namespace xgs::kt {

bool isWhitespace(QChar c);        // Char.isWhitespace(): Zs, Zl, Zp, U+0009..000D, U+001C..001F
bool isLetter(QChar c);            // Char.isLetter()
bool isLetterOrDigit(QChar c);     // Char.isLetterOrDigit(): a letter (L*) or Nd; a surrogate unit is neither
bool isLetterOrDigit(char32_t codePoint);   // Character.isLetterOrDigit(int); a surrogate code point is neither
bool isDigit(QChar c);             // Char.isDigit() (inline-only: Character.isDigit(char)): Nd; a surrogate unit is not
int digitValue(QChar c);           // Character.digit(c, 10) of an isDigit unit (what toIntOrNull adds per digit), else -1
bool isAsciiSpace(QChar c);        // \s of a Java regular expression without flags: ' ', \t, \n, U+000B, \f, \r
bool isHighSurrogate(QChar c);
bool isSurrogate(QChar c);
QChar uppercaseChar(QChar c);      // Char.uppercaseChar()
QChar lowercaseChar(QChar c);      // Char.lowercaseChar()
bool equalsIgnoreCase(QChar a, QChar b);   // Char.equals(other, ignoreCase = true)
QString lowercase(const QString& s);       // String.lowercase(): the length may change (U+0130); the sigma form approximates
                                           // Unicode's Final_Sigma (qt/README.md)
QString uppercase(const QString& s);       // String.uppercase(): the length may change (U+00DF -> "SS")
QString trim(const QString& s);            // String.trim()
QString trimStart(const QString& s);
QString trimEnd(const QString& s);
QString trimEndChars(const QString& s, QStringView chars);     // trimEnd(vararg chars)
QString trimStartChars(const QString& s, QStringView chars);
bool isBlank(const QString& s);                                // empty or all isWhitespace

// java.text.Normalizer.normalize(s, Normalizer.Form.NFKC) with the Unicode version of JVM 21 (15.0): code points assigned
// only by Qt's newer Unicode and unpaired surrogates stay as they are.
QString nfkc(const QString& s);
// Kotlin's String.toDoubleOrNull() on the JVM: the grammar of java.lang.Double.valueOf(String) (decimal and hexadecimal
// floating-point literals, an optional f/F/d/D suffix, "NaN", "Infinity", an optional sign, and leading and trailing
// characters up to U+0020 ignored); std::nullopt for anything else. The value is the nearest double (ties to even).
std::optional<double> toDoubleOrNull(const QString& s);
QString hexBits(double value);     // the 16 lowercase hex digits of the bit pattern (qt/tools/differential, kind O)

// UTF-8 bytes as the JVM decodes them with REPLACE, as Kotlin's `inputStream.bufferedReader(Charsets.UTF_8)` does (observed
// through the differential oracle, kind W op ln; process::LineSplitter decodes each line with it):
//   - every malformed sequence becomes one U+FFFD; a malformed sequence is a lead byte with the continuation bytes that may
//     follow it so far (a truncated sequence, also at the end of the input), or a single byte that cannot start a sequence
//     (80..C1, F5..FF);
//   - the second byte after E0 must be A0..BF, after F0 90..BF, after F4 80..8F (else the lead alone is malformed); after ED
//     it may be 80..BF, and a complete ED A0..BF xx (an encoded surrogate) is one U+FFFD for all three bytes;
//   - a byte order mark is kept (U+FEFF), NUL is kept.
// The LRCLIB provider decodes a whole body with it, where Kotlin uses String(bytes, UTF_8); the two decoders agree on every
// malformed body of the Q2c Task 4 differential (kind W op lf, on the JVM).
QString decodeUtf8(QByteArrayView bytes);

// Kotlin's `ignoreCase` argument. An enum, not a bool, so that a position and a case mode can never be swapped silently;
// the arguments follow Kotlin's order: (..., from = 0, mode = Sensitive).
enum class CaseMode { Sensitive, Ignore };

bool startsWith(QStringView s, QStringView prefix, qsizetype from = 0, CaseMode mode = CaseMode::Sensitive);
bool startsWith(QStringView s, QStringView prefix, CaseMode mode);
bool endsWith(QStringView s, QStringView suffix, CaseMode mode = CaseMode::Sensitive);
bool contains(QStringView s, QStringView needle, CaseMode mode = CaseMode::Sensitive);
qsizetype indexOf(QStringView s, QStringView needle, qsizetype from = 0, CaseMode mode = CaseMode::Sensitive);  // -1 when absent
qsizetype indexOf(QStringView s, QStringView needle, CaseMode mode);
// A bool where a position belongs (the old `ignoreCase` argument) would silently become position 0 or 1: refuse it.
template <typename B>
    requires std::same_as<B, bool>
bool startsWith(QStringView, QStringView, B, CaseMode = CaseMode::Sensitive) = delete;
template <typename B>
    requires std::same_as<B, bool>
qsizetype indexOf(QStringView, QStringView, B, CaseMode = CaseMode::Sensitive) = delete;
bool regionMatches(QStringView s, qsizetype at, QStringView other, CaseMode mode = CaseMode::Sensitive);  // false out of range
QString replace(const QString& s, QStringView oldValue, QStringView newValue,
                CaseMode mode = CaseMode::Sensitive);                                             // left to right, no overlap

// For qt/tools/differential only; the ports do not use these.
namespace detail {

// What Qt's own Unicode data says, before the exceptions of kt_tables.inc (gen-tables.ps1 compares it with the JVM).
struct CharFacts {
    bool whitespace;
    bool letter;
    bool letterOrDigit;
    char16_t upper;
    char16_t lower;
};
CharFacts qtCharFacts(char16_t unit);
bool qtLetterOrDigit(char32_t codePoint);   // a letter (L*) or Nd by Qt's categories alone (gen-tables.ps1)

}  // namespace detail

}  // namespace xgs::kt
