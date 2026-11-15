#include <QtTest>

#include <clocale>
#include <initializer_list>
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
