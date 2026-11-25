#include <QtTest>

#include <clocale>
#include <initializer_list>
#include <optional>
#include <string>

#include "text/kt_text.h"

namespace kt = xgs::kt;

// Expected values marked "Kotlin's answer" were read from `Oracle.java chartable` (JVM 21.0.9 of the sidecar image) or, for
// the supplementary characters, from the same JVM's String.regionMatches.
class KtTextTest : public QObject {
    Q_OBJECT
private slots:
    void isWhitespaceMatchesKotlinAtTheEdges() {
        for (char16_t unit : std::initializer_list<char16_t>{0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x001C, 0x001D, 0x001E, 0x001F, 0x0020, 0x00A0, 0x1680,
                              0x2003, 0x2007, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000}) {
            QVERIFY2(kt::isWhitespace(QChar(unit)), qPrintable(QString::number(unit, 16)));
        }
        for (char16_t unit : std::initializer_list<char16_t>{0x0085, 0x180E, 0x200B, 0x2060, 0xFEFF, 0x3164, u'a', 0xD83D}) {
            QVERIFY2(!kt::isWhitespace(QChar(unit)), qPrintable(QString::number(unit, 16)));
        }
    }

    void isLetterOrDigitMatchesKotlinAtTheEdges() {
        for (char16_t unit : std::initializer_list<char16_t>{u'a', u'Z', u'7', 0xAC00, 0x00E9, 0x0661}) {
            QVERIFY2(kt::isLetterOrDigit(QChar(unit)), qPrintable(QString::number(unit, 16)));
        }
        for (char16_t unit : std::initializer_list<char16_t>{u'_', u'-', 0x00B2, 0x2163, 0xD83D, 0xDE00}) {
            QVERIFY2(!kt::isLetterOrDigit(QChar(unit)), qPrintable(QString::number(unit, 16)));
        }
        // Kotlin's answer: "3164: 0 1 1 3164 3164" (the Hangul filler is a letter on JVM 21).
        QCOMPARE(kt::isLetterOrDigit(QChar(0x3164)), true);
    }

    void trimUsesKotlinWhitespace() {
        QCOMPARE(kt::trim(QStringLiteral(u"\u001C a  ")), QStringLiteral(u"a"));
        QCOMPARE(kt::trim(QStringLiteral(u"﻿a﻿")), QStringLiteral(u"﻿a﻿"));
        QCOMPARE(kt::trim(QStringLiteral(u"\u0085a")), QStringLiteral(u"\u0085a"));
    }

    void trimEndCharsStopsAtTheFirstOtherCharacter() {
        QCOMPARE(kt::trimEndChars(QStringLiteral(u"a. ."), u". "), QStringLiteral(u"a"));
    }

    void isBlankIsTrueForEmptyAndWhitespaceOnly() {
        QVERIFY(kt::isBlank(QString()));
        QVERIFY(kt::isBlank(QStringLiteral(u" \t　")));
        QVERIFY(!kt::isBlank(QStringLiteral(u"​")));
    }

    void lowercaseAndUppercaseChangeLength() {
        QCOMPARE(kt::uppercase(QStringLiteral(u"ß")), QStringLiteral(u"SS"));
        QCOMPARE(kt::lowercase(QStringLiteral(u"İ")), QStringLiteral(u"i̇"));
        QCOMPARE(kt::uppercase(QStringLiteral(u"ﬁ")), QStringLiteral(u"FI"));
    }

    void lowercaseHandlesFinalSigma() {
        QCOMPARE(kt::lowercase(QStringLiteral(u"ΟΔΟΣ")), QStringLiteral(u"οδος"));
        QCOMPARE(kt::lowercase(QStringLiteral(u"Σ")), QStringLiteral(u"σ"));
    }

    void lowercaseAndUppercaseDoNotDependOnTheLocale() {
        const QLocale before;
        const std::string cBefore = std::setlocale(LC_ALL, nullptr);
        QLocale::setDefault(QLocale(QLocale::Turkish));
        std::setlocale(LC_ALL, "tr-TR");
        const QString lower = kt::lowercase(QStringLiteral(u"TITLE"));
        const QString upper = kt::uppercase(QStringLiteral(u"title"));
        QLocale::setDefault(before);
        std::setlocale(LC_ALL, cBefore.c_str());
        QCOMPARE(lower, QStringLiteral(u"title"));
        QCOMPARE(upper, QStringLiteral(u"TITLE"));
    }

    void equalsIgnoreCaseComparesSingleCharacters() {
        QVERIFY(kt::equalsIgnoreCase(QChar(u'k'), QChar(0x212A)));
        QVERIFY(!kt::equalsIgnoreCase(QChar(u'ß'), QChar(u'S')));
        // Kotlin's answer: uppercaseChar of U+0131 is 0049 ("0131: 0 1 1 0049 0131").
        QCOMPARE(kt::equalsIgnoreCase(QChar(u'I'), QChar(0x0131)), true);
    }

    void startsWithEndsWithContainsIgnoreCase() {
        QVERIFY(kt::endsWith(u"Foo - TOPIC", u" - Topic", kt::CaseMode::Ignore));
        QVERIFY(!kt::endsWith(u"x", u"xx", kt::CaseMode::Ignore));
        QVERIFY(kt::contains(u"See HTTPS://a", u"https://", kt::CaseMode::Ignore));
        // Supplementary letters compare as code points, inside the compared region only (Kotlin's answers from the JVM:
        // "𐐀".regionMatches(true, 0, "𐐨", 0, 2) is true, contains("q𐐀z", "\uDC28z", true) is
        // false, endsWith("q𐐀", "\uDC28", true) is false, indexOf("q𐐀", "𐐨", 0, true) is 1).
        QCOMPARE(kt::regionMatches(u"\U00010400", 0, u"\U00010428", kt::CaseMode::Ignore), true);
        QCOMPARE(kt::contains(u"q\U00010400z", QStringView(u"\U00010428z").mid(1), kt::CaseMode::Ignore), false);
        QCOMPARE(kt::endsWith(u"q\U00010400", QStringView(u"\U00010428").mid(1), kt::CaseMode::Ignore), false);
        QCOMPARE(kt::indexOf(u"q\U00010400", u"\U00010428", 0, kt::CaseMode::Ignore), qsizetype(1));
    }

    void indexOfReturnsMinusOneWhenAbsent() {
        QCOMPARE(kt::indexOf(u"abc", u"c", 3), qsizetype(-1));
        QCOMPARE(kt::indexOf(u"abc", u"", 3), qsizetype(3));
    }

    void regionMatchesIsFalseOutOfRange() {
        QCOMPARE(kt::regionMatches(u"ab", 1, u"bc", kt::CaseMode::Sensitive), false);
        QCOMPARE(kt::regionMatches(u"ab", -1, u"a", kt::CaseMode::Sensitive), false);
    }

    void replaceIgnoreCaseIsLeftToRightAndNonOverlapping() {
        QCOMPARE(kt::replace(QStringLiteral(u"aAaA"), u"aa", u"x", kt::CaseMode::Ignore), QStringLiteral(u"xx"));
        QCOMPARE(kt::replace(QStringLiteral(u"abc"), u"B", u"<n>", kt::CaseMode::Ignore), QStringLiteral(u"a<n>c"));
    }

    void ignoreCaseMatchAdvancesEachSideOnItsOwnWhenASurrogatePairIsCompared() {
        // Found by the differential kind S. Kotlin's answers (JVM 21.0.9): both 0.
        // The needle's pair takes two units of the needle but one of the text: D83D D83D DE00 01C4 against D83D DE00 01C6 FB01.
        QCOMPARE(kt::indexOf(u"\xD83D\xDE00ǆﬁ", u"\xD83D\xD83D\xDE00Ǆ", 0, kt::CaseMode::Ignore), qsizetype(0));
        // The text's pair takes two units of the text but one of the needle: D83D DE00 S against D83D D83D DE00 S.
        QCOMPARE(kt::indexOf(u"\xD83D\xD83D\xDE00S", u"\xD83D\xDE00S", 0, kt::CaseMode::Ignore), qsizetype(0));
    }

    void ignoreCaseMatchEndsWhenEitherRegionIsUsedUp() {
        // Found by the differential kind N (LogRedaction.redact). Kotlin's answers (JVM 21.0.9): 0 and "PC". The text's pair
        // uses up the text's region of 3 units (D83D, D83D DE00) while the needle has compared D83D DE00 only; its last unit
        // (B) is never compared.
        QCOMPARE(kt::indexOf(u"\xD83D\xD83D\xDE00" u"C", u"\xD83D\xDE00" u"B", 0, kt::CaseMode::Ignore), qsizetype(0));
        QCOMPARE(kt::replace(QStringLiteral(u"\xD83D\xD83D\xDE00" u"C"), u"\xD83D\xDE00" u"B", u"P", kt::CaseMode::Ignore), QStringLiteral(u"PC"));
    }

    // The 25 probe cases from which the ignore-case region comparison (regionMatchesImpl in kt_text.cpp) was written, with
    // the answers of the JVM 21.0.9 of the sidecar image (Oracle.java, as differential case lines `S op text other [from]`).
    // The rows keep the probe numbers.
    void regionMatchesIgnoreCaseMatchesTheObservedJvmAnswers_data() {
        QTest::addColumn<QString>("op");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("other");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("jvm");
        const auto row = [](const char* name, const char* op, const char* text, const char* other, int from, int jvm) {
            QTest::newRow(name) << QString::fromLatin1(op) << QString::fromLatin1(text) << QString::fromLatin1(other) << from << jvm;
        };
        row("1", "ci-starts", "D801DC00", "D801DC28", 0, 1);
        row("2", "ci-contains", "0071D801DC00007A", "DC28007A", 0, 0);
        row("3", "ci-ends", "0071D801DC00", "DC28", 0, 0);
        row("4", "ci-index", "0071D801DC00", "D801DC28", 0, 1);
        row("5", "ci-index", "D83DDE0001C6FB01", "D83DD83DDE0001C4", 0, 0);
        row("6", "ci-index", "D83DD83DDE000053", "D83DDE000053", 0, 0);
        row("7", "ci-index", "D83DD83DDE000043", "D83DDE000042", 0, 0);
        row("8", "ci-starts", "D83DD83DDE00", "D83DDE00", 0, 0);
        row("9", "ci-ends", "D801DC000079", "D801DC28", 0, 0);
        row("10", "ci-index", "D83DD83DDE00", "D83DDE00", 0, 1);
        row("11", "ci-starts", "D83DDE00", "D83D", 0, 1);
        row("12", "ci-starts", "D800", "D801", 0, 0);
        row("13", "ci-starts", "DC00", "DC28", 0, 0);
        row("14", "ci-starts", "006B", "212A", 0, 1);
        row("15", "ci-starts", "0069", "0130", 0, 1);
        row("16", "ci-starts", "0069", "0131", 0, 1);
        row("17", "ci-starts", "00DF", "0053", 0, 0);
        row("18", "ci-starts", "01C4", "01C5", 0, 1);
        row("19", "ci-starts", "03C2", "03A3", 0, 1);
        row("20", "ci-starts", "1E9E", "00DF", 0, 1);
        row("21", "ci-starts", "D83ADD00", "D83ADD22", 0, 1);
        row("22", "ci-starts", "DC00DC00", "DC00DC28", 0, 0);
        row("23", "ci-starts", "D801DC00", "D801", 0, 1);
        row("24", "ci-starts", "D801DC00", "D802DC00", 0, 0);
        row("25", "eqic", "0069", "0130", 0, 1);
    }

    // Kotlin's answers from `Oracle.java lettertable` (Character.isLetterOrDigit(int), JVM 21.0.9 of the sidecar image).
    void codePointIsLetterOrDigitHandlesSupplementaryLetters() {
        QCOMPARE(kt::isLetterOrDigit(char32_t(0x1D49C)), true);    // 𝒜 MATHEMATICAL SCRIPT CAPITAL A
        QCOMPARE(kt::isLetterOrDigit(char32_t(0x10400)), true);    // DESERET CAPITAL LONG I
        QCOMPARE(kt::isLetterOrDigit(char32_t(0x1F600)), false);   // 😀
        QCOMPARE(kt::isLetterOrDigit(char32_t(0x20000)), true);    // CJK Extension B
        QCOMPARE(kt::isLetterOrDigit(char32_t(0x0661)), true);     // ARABIC-INDIC DIGIT ONE
        QCOMPARE(kt::isLetterOrDigit(char32_t(0x00B2)), false);    // SUPERSCRIPT TWO (No)
        QCOMPARE(kt::isLetterOrDigit(char32_t(0x2163)), false);    // ROMAN NUMERAL FOUR (Nl)
        QCOMPARE(kt::isLetterOrDigit(char32_t(0xD83D)), false);    // a surrogate code point
    }

    // Character.isLetter(int) by the general categories of Unicode 15.0 (not yet observed on the JVM; the Q3 Task 5
    // differential compares `\p{L}` around apostrophes, which uses it).
    void codePointIsLetterTellsSupplementaryLettersFromDigits() {
        QCOMPARE(kt::isLetter(char32_t(0x1D49C)), true);    // 𝒜 MATHEMATICAL SCRIPT CAPITAL A (Lu)
        QCOMPARE(kt::isLetter(char32_t(0x10428)), true);    // DESERET SMALL LONG I (Ll)
        QCOMPARE(kt::isLetter(char32_t(0x20000)), true);    // CJK Extension B (Lo)
        QCOMPARE(kt::isLetter(char32_t(0x1D7CE)), false);   // MATHEMATICAL BOLD DIGIT ZERO (Nd)
        QCOMPARE(kt::isLetter(char32_t(0x104A0)), false);   // OSMANYA DIGIT ZERO (Nd)
        QCOMPARE(kt::isLetter(char32_t(0x1F600)), false);   // 😀 (So)
        QCOMPARE(kt::isLetter(char32_t(0x0041)), true);     // A
        QCOMPARE(kt::isLetter(char32_t(0x0661)), false);    // ARABIC-INDIC DIGIT ONE (Nd)
        QCOMPARE(kt::isLetter(char32_t(0xD83D)), false);    // a surrogate code point
        QCOMPARE(kt::isLetter(char32_t(0x110000)), false);  // beyond Unicode
    }

    // Kotlin's answers from Oracle.java (case lines `H text`: Normalizer.normalize(text, Form.NFKC), JVM 21.0.9).
    void nfkcComposesHangulJamoAndFoldsFullWidth() {
        QCOMPARE(kt::nfkc(QStringLiteral(u"가")), QStringLiteral(u"가"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"가")), QStringLiteral(u"가"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"각")), QStringLiteral(u"각"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"ＡＢＣ１２３")), QStringLiteral(u"ABC123"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"ﬁ")), QStringLiteral(u"fi"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"é")), QStringLiteral(u"é"));    // é composed
        QCOMPARE(kt::nfkc(QStringLiteral(u"é")), QStringLiteral(u"é"));   // é decomposed
        QCOMPARE(kt::nfkc(QStringLiteral(u"①")), QStringLiteral(u"1"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"㈜")), QStringLiteral(u"(주)"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"ｶﾞ")), QStringLiteral(u"ガ"));
    }

    void nfkcLeavesLoneSurrogatesAlone() {
        QCOMPARE(kt::nfkc(QStringLiteral(u"\xD800")), QStringLiteral(u"\xD800"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"a\xDC00" u"b")), QStringLiteral(u"a\xDC00" u"b"));
        QCOMPARE(kt::nfkc(QStringLiteral(u"\xDBFF́")), QStringLiteral(u"\xDBFF́"));
    }

    // Kotlin's answers from Oracle.java (case lines `O text`: String.toDoubleOrNull(), JVM 21.0.9), as the bit patterns.
    void toDoubleOrNullAcceptsWhatKotlinAccepts_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("bits");
        const auto row = [](const char16_t* text, const char* bits) {
            const QString value = QString::fromUtf16(text);
            QTest::newRow(qPrintable(value)) << value << QString::fromLatin1(bits);
        };
        row(u"1", "3ff0000000000000");
        row(u"1.5", "3ff8000000000000");
        row(u"-0", "8000000000000000");
        row(u".5", "3fe0000000000000");
        row(u"5.", "4014000000000000");
        row(u"1e3", "408f400000000000");
        row(u"1E-3", "3f50624dd2f1a9fc");
        row(u" 1 ", "3ff0000000000000");
        row(u"1f", "3ff0000000000000");
        row(u"1d", "3ff0000000000000");
        row(u"0x1p3", "4020000000000000");
        row(u"NaN", "7ff8000000000000");
        row(u"Infinity", "7ff0000000000000");
        row(u"-Infinity", "fff0000000000000");
        row(u"+1", "3ff0000000000000");
        // out of range for std::from_chars (it reports result_out_of_range); the bits are the oracle's answers
        row(u"4.9e-324", "0000000000000001");   // the smallest subnormal
        row(u"1e-310", "000012688b70e62b");     // a subnormal in the middle
        row(u"1e-400", "0000000000000000");     // rounds to zero
        row(u"-1e-400", "8000000000000000");
        row(u"1e400", "7ff0000000000000");      // overflows to infinity
        row(u"-1e400", "fff0000000000000");
    }

    void toDoubleOrNullAcceptsWhatKotlinAccepts() {
        QFETCH(QString, text);
        QFETCH(QString, bits);
        const std::optional<double> value = kt::toDoubleOrNull(text);
        QVERIFY(value.has_value());
        QCOMPARE(kt::hexBits(*value), bits);
    }

    void toDoubleOrNullRejectsWhatKotlinRejects() {
        for (const char16_t* text : {u"", u" ", u"NA", u"1,5", u"1 2", u"abc", u"0x10", u"١", u"1e", u"--1", u"infinity", u"nan"}) {
            QVERIFY2(!kt::toDoubleOrNull(QString::fromUtf16(text)).has_value(), qPrintable(QString::fromUtf16(text)));
        }
    }

    void isAsciiSpaceIsExactlySixCharacters() {
        for (char16_t unit : std::initializer_list<char16_t>{u' ', u'\t', u'\n', 0x000B, u'\f', u'\r'}) {
            QVERIFY2(kt::isAsciiSpace(QChar(unit)), qPrintable(QString::number(unit, 16)));
        }
        for (char16_t unit : std::initializer_list<char16_t>{0x00A0, 0x3000, 0x2028, 0x0085, 0x001C}) {
            QVERIFY2(!kt::isAsciiSpace(QChar(unit)), qPrintable(QString::number(unit, 16)));
        }
    }

    void regionMatchesIgnoreCaseMatchesTheObservedJvmAnswers() {
        QFETCH(QString, op);
        QFETCH(QString, text);
        QFETCH(QString, other);
        QFETCH(int, from);
        QFETCH(int, jvm);
        const auto decode = [](const QString& hex) {
            QString units;
            for (qsizetype i = 0; i + 4 <= hex.size(); i += 4) units.append(QChar(char16_t(hex.mid(i, 4).toUShort(nullptr, 16))));
            return units;
        };
        const QString a = decode(text);
        const QString b = decode(other);
        int answer = -2;
        if (op == u"ci-starts") answer = kt::startsWith(a, b, kt::CaseMode::Ignore) ? 1 : 0;
        else if (op == u"ci-ends") answer = kt::endsWith(a, b, kt::CaseMode::Ignore) ? 1 : 0;
        else if (op == u"ci-contains") answer = kt::contains(a, b, kt::CaseMode::Ignore) ? 1 : 0;
        else if (op == u"ci-index") answer = int(kt::indexOf(a, b, from, kt::CaseMode::Ignore));
        else if (op == u"eqic") answer = kt::equalsIgnoreCase(a.at(0), b.at(0)) ? 1 : 0;
        QCOMPARE(answer, jvm);
    }
};

QTEST_APPLESS_MAIN(KtTextTest)
#include "tst_kt_text.moc"
