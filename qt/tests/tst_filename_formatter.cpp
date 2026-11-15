#include <QtTest>

#include <optional>
#include <stdexcept>

#include "filename/filename_formatter.h"
#include "text/kt_text.h"

namespace fmt = xgs::filename;
using namespace Qt::StringLiterals;

namespace {

QString format(int rank, const QString& artist, const QString& title, bool includeRank = true) {
    return fmt::format(rank, artist, title, includeRank);
}

// Kotlin's String.removeSuffix(".m4a").
QString removeM4a(const QString& name) {
    return name.endsWith(u".m4a") ? name.chopped(4) : name;
}

// Index of the first high surrogate without a low one after it, or low surrogate without a high one before it; -1 when none.
qsizetype loneSurrogateAt(const QString& text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (xgs::kt::isHighSurrogate(c) && !(i + 1 < text.size() && text.at(i + 1).isLowSurrogate())) return i;
        if (c.isLowSurrogate() && !(i - 1 >= 0 && xgs::kt::isHighSurrogate(text.at(i - 1)))) return i;
    }
    return -1;
}

QStringList deviceNames() {
    QStringList devices{u"CON"_s, u"PRN"_s, u"AUX"_s, u"NUL"_s};
    for (int i = 1; i <= 9; ++i) {
        devices.append(QStringLiteral("COM%1").arg(i));
        devices.append(QStringLiteral("LPT%1").arg(i));
    }
    return devices;
}

std::optional<QString> some(const QString& value) {
    return value;
}

}  // namespace

#define COMPARE_FOLDER(expected, actual) QCOMPARE((actual), some(expected))

class FilenameFormatterTest : public QObject {
    Q_OBJECT

private slots:
    void formatsTheDocumentedExample() {
        QCOMPARE(format(1, u"소연 (SOYEON)"_s, u"퇴사할게여 (Narr. 기안84)"_s), u"001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a"_s);
    }

    void padsRankToThreeDigits() {
        QVERIFY(format(7, u"A"_s, u"B"_s).startsWith(u"007 "));
        QVERIFY(format(42, u"A"_s, u"B"_s).startsWith(u"042 "));
        QVERIFY(format(999, u"A"_s, u"B"_s).startsWith(u"999 "));
    }

    void rejectsRankOutsideOneTo999() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, format(0, u"A"_s, u"B"_s));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, format(1000, u"A"_s, u"B"_s));
    }

    void replacesForbiddenCharactersWithFullWidthOnes() {
        QCOMPARE(format(5, u"태연 (TAEYEON)"_s, u"만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"_s),
                 u"005 태연 (TAEYEON) - 만찬가 (晩餐歌 ／ BANSANKA) ： J-POP REMAKE Vol.1.m4a"_s);
        QCOMPARE(format(1, u"A"_s, u"\\/:*?\"<>|"_s), u"001 A - ＼／：＊？＂＜＞｜.m4a"_s);
    }

    void dropsControlCharactersAndTrailingDotsAndSpaces() {
        QCOMPARE(format(1, u"A"_s, u"Mr.\t\n. "_s), u"001 A - Mr.m4a"_s);
        QCOMPARE(format(1, u"A"_s, u"B.C"_s), u"001 A - B.C.m4a"_s);
    }

    void emptyTitleGetsAPlaceholder() {
        QCOMPARE(format(1, u"A"_s, u"  "_s), u"001 A - untitled.m4a"_s);
    }

    void longTitleIsTruncatedWithEllipsisKeepingRankAndArtist() {
        const QString name = format(12, u"ARTIST"_s, u"가"_s.repeated(500));
        const QString base = removeM4a(name);
        QCOMPARE(base.size(), qsizetype(fmt::MaxBaseLength));
        QVERIFY(base.startsWith(u"012 ARTIST - 가"));
        QVERIFY(base.endsWith(u"…"));
    }

    void veryLongArtistIsTruncatedToo() {
        const QString name = format(1, u"A"_s.repeated(300), u"Song"_s);
        const QString base = removeM4a(name);
        QVERIFY(base.size() <= fmt::MaxBaseLength);
        QVERIFY(base.startsWith(u"001 " + u"A"_s.repeated(10)));
        QVERIFY(base.endsWith(u" - Song"));
        QVERIFY(base.contains(u"…"));
    }

    void truncationNeverSplitsASurrogatePair() {
        const QString name = format(1, u"A"_s, u"😀"_s.repeated(200));
        const QString base = removeM4a(name);
        QVERIFY(base.size() <= fmt::MaxBaseLength);
        QCOMPARE(loneSurrogateAt(base), qsizetype(-1));
    }

    // ---- without the rank ----

    void theRankIsIncludedUnlessTheCallerLeavesItOut() {
        QCOMPARE(format(1, u"A"_s, u"B"_s), u"001 A - B.m4a"_s);
        QCOMPARE(format(1, u"A"_s, u"B"_s, true), u"001 A - B.m4a"_s);
        QCOMPARE(format(42, u"소연"_s, u"노래"_s), format(42, u"소연"_s, u"노래"_s, true));
    }

    void withoutTheRankTheNameIsArtistDashTitle() {
        const QString name = format(1, u"A"_s, u"B"_s, false);

        QCOMPARE(name, u"A - B.m4a"_s);
        QVERIFY2(!name.startsWith(u" "), "no leading space where the rank would have been");
    }

    void withoutTheRankTheDocumentedExampleLosesOnlyThePrefix() {
        QCOMPARE(format(1, u"소연 (SOYEON)"_s, u"퇴사할게여 (Narr. 기안84)"_s, false), u"소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a"_s);
    }

    void withoutTheRankTheNameDoesNotDependOnTheRank() {
        QStringList names;
        for (const int rank : {1, 7, 999}) names.append(format(rank, u"A"_s, u"B"_s, false));
        names.removeDuplicates();

        QCOMPARE(names, QStringList{u"A - B.m4a"_s});
    }

    void withoutTheRankTheRankIsStillValidated() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, format(0, u"A"_s, u"B"_s, false));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, format(1000, u"A"_s, u"B"_s, false));
    }

    void withoutTheRankForbiddenCharactersAreStillReplaced() {
        QCOMPARE(format(1, u"A"_s, u"\\/:*?\"<>|"_s, false), u"A - ＼／：＊？＂＜＞｜.m4a"_s);
        QCOMPARE(format(1, u"AC/DC"_s, u"Who Made Who?"_s, false), u"AC／DC - Who Made Who？.m4a"_s);
    }

    void withoutTheRankControlCharactersAndTrailingDotsAndSpacesAreStillDropped() {
        QCOMPARE(format(1, u"A"_s, u"Mr.\t\n. "_s, false), u"A - Mr.m4a"_s);
        QCOMPARE(format(1, u"A"_s, u"B.C"_s, false), u"A - B.C.m4a"_s);
    }

    void withoutTheRankAnEmptyTitleStillGetsAPlaceholder() {
        QCOMPARE(format(1, u"A"_s, u"  "_s, false), u"A - untitled.m4a"_s);
        QCOMPARE(format(1, u"A"_s, u""_s, false), u"A - untitled.m4a"_s);
    }

    void withoutTheRankALongTitleFillsTheWholeBaseBudget() {
        const QString name = format(12, u"ARTIST"_s, u"가"_s.repeated(500), false);
        const QString base = removeM4a(name);

        QCOMPARE(base.size(), qsizetype(fmt::MaxBaseLength));
        QVERIFY2(base.startsWith(u"ARTIST - 가"), qPrintable(base));
        QVERIFY(base.endsWith(u"…"));
        // 180 - "ARTIST - ".length = 171 title units: 170 characters and the ellipsis.
        QCOMPARE(base, u"ARTIST - " + u"가"_s.repeated(170) + u"…");
    }

    void withoutTheRankAnArtistOfEightyCharactersIsKeptAndTheTitleGetsTheRest() {
        const QString artist = u"A"_s.repeated(fmt::MaxArtistLength);

        const QString base = removeM4a(format(1, artist, u"T"_s.repeated(500), false));

        QCOMPARE(base.size(), qsizetype(fmt::MaxBaseLength));
        QVERIFY2(base.startsWith(artist + u" - T"), qPrintable(base));
        QCOMPARE(base, artist + u" - " + u"T"_s.repeated(96) + u"…");
    }

    void withoutTheRankAnArtistOverEightyCharactersIsCutWithAnEllipsis() {
        const QString expected = u"A"_s.repeated(fmt::MaxArtistLength - 1) + u"… - Song.m4a";

        QCOMPARE(format(1, u"A"_s.repeated(300), u"Song"_s, false), expected);
        QCOMPARE(format(1, u"A"_s.repeated(81), u"Song"_s, false), expected);
    }

    void withoutTheRankTruncationNeverSplitsASurrogatePair() {
        const QString base = removeM4a(format(1, u"A"_s, u"😀"_s.repeated(200), false));

        QVERIFY(base.size() <= fmt::MaxBaseLength);
        QVERIFY2(base.startsWith(u"A - 😀"), qPrintable(base));
        QCOMPARE(loneSurrogateAt(base), qsizetype(-1));
    }

    void withoutTheRankADeviceNameAsTheArtistGetsAnUnderscoreAfterTheStem() {
        QCOMPARE(format(1, u"NUL.x"_s, u"Song"_s, false), u"NUL_.x - Song.m4a"_s);
        QCOMPARE(format(1, u"Con."_s, u"Song"_s, false), u"Con_. - Song.m4a"_s);
        QCOMPARE(format(1, u"com1.x"_s, u"Song"_s, false), u"com1_.x - Song.m4a"_s);
        QCOMPARE(format(1, u"LPT9.x"_s, u"Song"_s, false), u"LPT9_.x - Song.m4a"_s);
        QCOMPARE(format(1, u"aux.tar.gz"_s, u"Song"_s, false), u"aux_.tar.gz - Song.m4a"_s);
        QCOMPARE(format(1, u"CON .x"_s, u"Song"_s, false), u"CON_ .x - Song.m4a"_s);
    }

    void withoutTheRankEveryDeviceNameAsTheArtistIsCaught() {
        for (const QString& device : deviceNames()) {
            QCOMPARE(format(1, device + u".x", u"B"_s, false), device + u"_.x - B.m4a");
        }
    }

    void withoutTheRankNamesThatMerelyResembleDeviceNamesStayUnchanged() {
        QCOMPARE(format(1, u"Console"_s, u"Song"_s, false), u"Console - Song.m4a"_s);
        QCOMPARE(format(1, u"Con"_s, u"Song"_s, false), u"Con - Song.m4a"_s);
        QCOMPARE(format(1, u"LPT9"_s, u"Song"_s, false), u"LPT9 - Song.m4a"_s);
        QCOMPARE(format(1, u"NULL"_s, u"Song"_s, false), u"NULL - Song.m4a"_s);
        QCOMPARE(format(1, u"CON TEST"_s, u"Song"_s, false), u"CON TEST - Song.m4a"_s);
        QCOMPARE(format(1, u"COM0"_s, u"Song"_s, false), u"COM0 - Song.m4a"_s);
        QCOMPARE(format(1, u"my.con"_s, u"Song"_s, false), u"my.con - Song.m4a"_s);
    }

    void withTheRankADeviceNameAsTheArtistIsLeftAlone() {
        QCOMPARE(format(1, u"NUL.x"_s, u"Song"_s), u"001 NUL.x - Song.m4a"_s);
        QCOMPARE(format(1, u"Con"_s, u"Song"_s, true), u"001 Con - Song.m4a"_s);
        QCOMPARE(format(42, u"LPT9"_s, u"Song"_s, true), u"042 LPT9 - Song.m4a"_s);
    }

    void theUnderscoreForADeviceNameArtistStillKeepsTheNameWithinTheBudget() {
        const QString artist = u"NUL." + u"A"_s.repeated(300);

        const QString base = removeM4a(format(1, artist, u"T"_s.repeated(500), false));

        QCOMPARE(base.size(), qsizetype(fmt::MaxBaseLength));
        QVERIFY2(base.startsWith(u"NUL_.AAA"), qPrintable(base));
        QVERIFY(base.endsWith(u"…"));
        // The artist is cut to 80 units first, the `_` makes it 81, and the title gets 180 - 81 - " - ".length = 96.
        QCOMPARE(base, u"NUL_." + u"A"_s.repeated(75) + u"… - " + u"T"_s.repeated(95) + u"…");
    }

    void missingOrBlankPlaylistTitleGetsTheUntitledFolderName() {
        QCOMPARE(fmt::folderName(std::nullopt), u"재생목록"_s);
        QCOMPARE(fmt::folderName(u""_s), u"재생목록"_s);
        QCOMPARE(fmt::folderName(u"   "_s), u"재생목록"_s);
        QCOMPARE(fmt::folderName(u"\t\n"_s), u"재생목록"_s);
        QCOMPARE(fmt::folderName(std::nullopt), fmt::untitledPlaylist());
    }

    void ordinaryAndKoreanPlaylistTitlesStayUnchanged() {
        QCOMPARE(fmt::folderName(u"Melon Daily Top 100"_s), u"Melon Daily Top 100"_s);
        QCOMPARE(fmt::folderName(u"멜론 일간 차트 TOP 100"_s), u"멜론 일간 차트 TOP 100"_s);
    }

    void folderNameReplacesForbiddenCharactersWithFullWidthOnes() {
        QCOMPARE(fmt::folderName(u"Best: Hits? <2024>"_s), u"Best： Hits？ ＜2024＞"_s);
        QCOMPARE(fmt::folderName(u"\\/:*?\"<>|"_s), u"＼／：＊？＂＜＞｜"_s);
    }

    void folderNameDropsTrailingDotsAndSpaces() {
        QCOMPARE(fmt::folderName(u"Mix... "_s), u"Mix"_s);
        QCOMPARE(fmt::folderName(u"Mix. . "_s), u"Mix"_s);
        QCOMPARE(fmt::folderName(u"Mix.Vol.1."_s), u"Mix.Vol.1"_s);
    }

    void folderNameThatIsOnlyDotsOrSpacesGetsTheUntitledFolderName() {
        QCOMPARE(fmt::folderName(u"..."_s), u"재생목록"_s);
        QCOMPARE(fmt::folderName(u" . . "_s), u"재생목록"_s);
    }

    void folderNameDropsControlCharacters() {
        QCOMPARE(fmt::folderName(QString::fromUtf16(u"My\u0000Mix\u001F", 7)), u"MyMix"_s);
        QCOMPARE(fmt::folderName(u"\tMy \u007FMix\n"_s), u"My Mix"_s);
        QCOMPARE(fmt::folderName(u"\u0001\u0002"_s), u"재생목록"_s);
    }

    void folderNameIsCutToEightyUnitsWithAnEllipsis() {
        const QString name = fmt::folderName(u"a"_s.repeated(81));
        QCOMPARE(name.size(), qsizetype(fmt::MaxFolderLength));
        QCOMPARE(name, u"a"_s.repeated(79) + u"…");
        QCOMPARE(fmt::folderName(u"a"_s.repeated(80)), u"a"_s.repeated(80));
    }

    void folderNameTruncationNeverEndsInALoneHighSurrogate() {
        const QString name = fmt::folderName(u"🎵"_s.repeated(100));
        QVERIFY(name.size() <= fmt::MaxFolderLength);
        QVERIFY(name.endsWith(u"…"));
        QCOMPARE(loneSurrogateAt(name), qsizetype(-1));
    }

    void reservedWindowsDeviceNamesGetAnUnderscoreAfterTheStem() {
        QCOMPARE(fmt::folderName(u"CON"_s), u"CON_"_s);
        QCOMPARE(fmt::folderName(u"nul"_s), u"nul_"_s);
        QCOMPARE(fmt::folderName(u"Com1"_s), u"Com1_"_s);
        QCOMPARE(fmt::folderName(u"LPT9"_s), u"LPT9_"_s);
        QCOMPARE(fmt::folderName(u"con.txt"_s), u"con_.txt"_s);
        QCOMPARE(fmt::folderName(u"aux.tar.gz"_s), u"aux_.tar.gz"_s);
        QCOMPARE(fmt::folderName(u"CON .txt"_s), u"CON_ .txt"_s);
        for (const QString& device : deviceNames()) {
            QCOMPARE(fmt::folderName(device), device + u"_");
        }
    }

    void namesThatMerelyResembleReservedOnesStayUnchanged() {
        QCOMPARE(fmt::folderName(u"Console"_s), u"Console"_s);
        QCOMPARE(fmt::folderName(u"CON TEST"_s), u"CON TEST"_s);
        QCOMPARE(fmt::folderName(u"NULL"_s), u"NULL"_s);
        QCOMPARE(fmt::folderName(u"COM0"_s), u"COM0"_s);
        QCOMPARE(fmt::folderName(u"COM10"_s), u"COM10"_s);
        QCOMPARE(fmt::folderName(u"my.con"_s), u"my.con"_s);
    }

    void aPlaylistWithoutATypedFolderNameIsSavedInAFolderNamedAfterIt() {
        COMPARE_FOLDER(u"Best： Of？"_s, fmt::destinationFolder(true, u"Best: Of?"_s, std::nullopt));
        COMPARE_FOLDER(u"Mix"_s, fmt::destinationFolder(true, u"Mix"_s, u""_s));
        COMPARE_FOLDER(u"Mix"_s, fmt::destinationFolder(true, u"Mix"_s, u"  \t"_s));
        COMPARE_FOLDER(u"재생목록"_s, fmt::destinationFolder(true, std::nullopt, std::nullopt));
    }

    void aSingleVideoWithoutATypedFolderNameHasNoFolder() {
        QVERIFY(!fmt::destinationFolder(false, u"Mix"_s, std::nullopt).has_value());
        QVERIFY(!fmt::destinationFolder(false, std::nullopt, u"   "_s).has_value());
    }

    void aTypedFolderNameWinsForAPlaylistAndAVideoAndIsMadeSafeLikeAnyFolderName() {
        COMPARE_FOLDER(u"My Songs"_s, fmt::destinationFolder(true, u"Mix"_s, u"My Songs"_s));
        COMPARE_FOLDER(u"My Songs"_s, fmt::destinationFolder(false, u"Mix"_s, u"  My Songs "_s));
        COMPARE_FOLDER(u"a＼b／c： d"_s, fmt::destinationFolder(false, std::nullopt, u"a\\b/c: d"_s));
        COMPARE_FOLDER(u"재생목록"_s, fmt::destinationFolder(false, std::nullopt, u".."_s));
        COMPARE_FOLDER(u"CON_"_s, fmt::destinationFolder(true, u"Mix"_s, u"CON"_s));
    }
};

QTEST_APPLESS_MAIN(FilenameFormatterTest)
#include "tst_filename_formatter.moc"
