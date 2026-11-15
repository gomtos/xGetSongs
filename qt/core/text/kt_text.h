#pragma once

#include <QChar>
#include <QString>
#include <QStringView>

#include <concepts>

// Kotlin/JVM character and string semantics, so that the ports of `shared` give exactly the answers of the Kotlin originals
// (Kotlin's standard library on JVM 21). Lengths and indices are UTF-16 units, as Kotlin's String.length. Nothing here depends
// on the locale. qt/tools/differential (kinds X and S) compares these functions with the JVM.
namespace xgs::kt {

bool isWhitespace(QChar c);        // Char.isWhitespace(): Zs, Zl, Zp, U+0009..000D, U+001C..001F
bool isLetter(QChar c);            // Char.isLetter()
bool isLetterOrDigit(QChar c);     // Char.isLetterOrDigit(): a letter (L*) or Nd; a surrogate unit is neither
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

}  // namespace detail

}  // namespace xgs::kt
