#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <climits>
#include <optional>

#include "ytdlp/video_info_file.h"

using namespace Qt::StringLiterals;
using xgs::ytdlp::VideoInfo;

namespace {

QString describe(const std::optional<QString>& value) {
    return value ? u"\""_s + *value + u"\""_s : u"null"_s;
}

QString describe(const std::optional<int>& value) {
    return value ? QString::number(*value) : u"null"_s;
}

QString describe(const VideoInfo& info) {
    return u"VideoInfo(album=%1, description=%2, duration=%3)"_s.arg(describe(info.album), describe(info.description),
                                                                      describe(info.duration));
}

// VideoInfo(album, description, duration = null) as Kotlin writes it
VideoInfo info(std::optional<QString> album, std::optional<QString> description, std::optional<int> duration = std::nullopt) {
    return VideoInfo{std::move(album), std::move(description), duration};
}

}  // namespace

// Kotlin's assertEquals(expected, actual) and assertNull(actual)
#define ASSERT_EQUALS(expected, actual)                                                                          \
    do {                                                                                                         \
        const auto expectedValue = (expected);                                                                   \
        const auto actualValue = (actual);                                                                       \
        QVERIFY2(expectedValue == actualValue,                                                                   \
                 qPrintable(u"expected %1 but was %2"_s.arg(describe(expectedValue), describe(actualValue))));   \
    } while (false)

#define ASSERT_NULL(actual)                                                                       \
    do {                                                                                          \
        const auto actualValue = (actual);                                                        \
        QVERIFY2(!actualValue.has_value(), qPrintable(u"expected null but was %1"_s.arg(describe(actualValue)))); \
    } while (false)

// Port of engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/VideoInfoFileTest.kt. Each test gets its own temporary
// directory (Kotlin makes a new test class instance, and with it a new directory, per test).
class VideoInfoFileTest : public QObject {
    Q_OBJECT

    std::optional<QTemporaryDir> dir_;

    QString dir() const { return dir_->path(); }
    QString file() const { return dir_->filePath(u"vid00000001.info.json"_s); }

    void writeBytes(const QByteArray& bytes) {
        QFile out(file());
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(out.write(bytes), bytes.size());
    }

    // Files.writeString(file, json): UTF-8
    void writeString(const QString& json) { writeBytes(json.toUtf8()); }

    /** Writes [json] as UTF-8 into [file] and reads the album from it. */
    std::optional<QString> albumOf(const QString& json) {
        writeString(json);
        return xgs::ytdlp::readVideoAlbum(file());
    }

    /** Writes [json] as UTF-8 into [file] and reads everything the engine needs from it. */
    VideoInfo infoOf(const QString& json) {
        writeString(json);
        return xgs::ytdlp::readVideoInfo(file());
    }

    const VideoInfo nothing = info(std::nullopt, std::nullopt);

private slots:
    void init() {
        dir_.emplace(QDir::tempPath() + u"/xgs-info-XXXXXX"_s);
        QVERIFY(dir_->isValid());
    }

    void cleanup() { dir_.reset(); }

    void readsTheAlbumOfTheVideo() {
        ASSERT_EQUALS(std::optional<QString>(u"Palette"_s), albumOf(u"{\"id\":\"vid00000001\",\"title\":\"Palette (Official)\",\"album\":\"Palette\"}"_s));
    }

    void trimsTheAlbum() {
        ASSERT_EQUALS(std::optional<QString>(u"Love poem"_s), albumOf(u"{\"album\":\"  Love poem \\t\"}"_s));
    }

    void readsAKoreanAlbum() {
        ASSERT_EQUALS(std::optional<QString>(u"사랑의 시"_s), albumOf(u"{\"album\":\"사랑의 시\"}"_s));
    }

    void decodesEscapesInTheAlbum() {
        ASSERT_EQUALS(std::optional<QString>(u"Love \"poem\" é"_s), albumOf(u"{\"album\":\"Love \\\"poem\\\" \\u00e9\"}"_s));
    }

    void onlyTheAlbumOfTheRootObjectCounts() {
        ASSERT_NULL(albumOf(u"{\"formats\":[{\"album\":\"X\"}],\"meta\":{\"album\":\"Y\"}}"_s));
    }

    void aMissingAlbumKeyGivesNull() {
        ASSERT_NULL(albumOf(u"{\"id\":\"vid00000001\",\"artist\":\"IU\",\"track\":\"Palette\"}"_s));
    }

    void aNullAlbumGivesNull() {
        ASSERT_NULL(albumOf(u"{\"album\":null}"_s));
    }

    void aNumberAlbumGivesNull() {
        ASSERT_NULL(albumOf(u"{\"album\":2024}"_s));
    }

    void anAlbumThatIsNotAStringGivesNull() {
        ASSERT_NULL(albumOf(u"{\"album\":[\"Palette\"]}"_s));
        ASSERT_NULL(albumOf(u"{\"album\":{\"name\":\"Palette\"}}"_s));
        ASSERT_NULL(albumOf(u"{\"album\":true}"_s));
    }

    void aBlankAlbumGivesNull() {
        ASSERT_NULL(albumOf(u"{\"album\":\"\"}"_s));
        ASSERT_NULL(albumOf(u"{\"album\":\"  \\t \"}"_s));
    }

    void theLiteralNAPlaceholderGivesNull() {
        ASSERT_NULL(albumOf(u"{\"album\":\"NA\"}"_s));
        ASSERT_NULL(albumOf(u"{\"album\":\" NA \"}"_s));
    }

    void textThatMerelyContainsNAIsAnAlbum() {
        ASSERT_EQUALS(std::optional<QString>(u"NA Nights"_s), albumOf(u"{\"album\":\"NA Nights\"}"_s));
        ASSERT_EQUALS(std::optional<QString>(u"Na"_s), albumOf(u"{\"album\":\"Na\"}"_s));
    }

    void invalidJsonGivesNull() {
        ASSERT_NULL(albumOf(u"{\"album\":\"Palette\""_s));
        ASSERT_NULL(albumOf(u"not json at all"_s));
    }

    void anEmptyFileGivesNull() {
        ASSERT_NULL(albumOf(QString()));
    }

    void aRootThatIsNotAnObjectGivesNull() {
        ASSERT_NULL(albumOf(u"[{\"album\":\"Palette\"}]"_s));
        ASSERT_NULL(albumOf(u"\"Palette\""_s));
    }

    void aMissingFileGivesNull() {
        ASSERT_NULL(xgs::ytdlp::readVideoAlbum(QDir(dir()).filePath(u"nothing.info.json"_s)));
    }

    void aDirectoryInsteadOfAFileGivesNull() {
        ASSERT_NULL(xgs::ytdlp::readVideoAlbum(dir()));
    }

    void bytesThatAreNotUtf8GiveNull() {
        // 0xC3 0x28 is not valid UTF-8; a lenient decoder would turn it into U+FFFD and still find an album.
        const QByteArray bytes = QByteArray("{\"album\":\"Pal") + QByteArray("\xC3\x28", 2) + QByteArray("ette\"}");
        writeBytes(bytes);

        ASSERT_NULL(xgs::ytdlp::readVideoAlbum(file()));
    }

    // ---- read: the album and the description together ----

    void readReturnsTheAlbumAndTheDescriptionTogether() {
        const VideoInfo result = infoOf(u"{\"id\":\"vid00000001\",\"album\":\" Palette \",\"description\":\"[Lyrics]\\nLine one\\nLine two\"}"_s);

        ASSERT_EQUALS(info(u"Palette"_s, u"[Lyrics]\nLine one\nLine two"_s), result);
    }

    void readKeepsTheInnerLineBreaksAndDecodesEscapesOfTheDescription() {
        const VideoInfo result = infoOf(u"{\"description\":\"첫 줄\\r\\n\\\"둘째\\\" 줄\\n\\né 🎵\"}"_s);

        ASSERT_EQUALS(std::optional<QString>(u"첫 줄\r\n\"둘째\" 줄\n\né 🎵"_s), result.description);
        ASSERT_NULL(result.album);
    }

    void readDropsALeadingAndTrailingBlankRunOfTheDescriptionAndNothingElse() {
        ASSERT_EQUALS(std::optional<QString>(u"  Line one\nLine two"_s), infoOf(u"{\"description\":\"\\n \\n  Line one\\nLine two\\n\\n  \\t\\n\"}"_s).description);
        ASSERT_EQUALS(std::optional<QString>(u"a  b"_s), infoOf(u"{\"description\":\"a  b\"}"_s).description);
    }

    void readGivesAnAlbumWithoutADescriptionAndTheOtherWayRound() {
        ASSERT_EQUALS(info(u"Palette"_s, std::nullopt), infoOf(u"{\"album\":\"Palette\"}"_s));
        ASSERT_EQUALS(info(std::nullopt, u"Some text"_s), infoOf(u"{\"description\":\"Some text\"}"_s));
    }

    void aMissingDescriptionGivesNull() {
        ASSERT_NULL(infoOf(u"{\"id\":\"vid00000001\",\"album\":\"Palette\"}"_s).description);
    }

    void aNullDescriptionGivesNull() {
        ASSERT_NULL(infoOf(u"{\"description\":null}"_s).description);
    }

    void aDescriptionThatIsNotAStringGivesNull() {
        ASSERT_NULL(infoOf(u"{\"description\":2024}"_s).description);
        ASSERT_NULL(infoOf(u"{\"description\":true}"_s).description);
        ASSERT_NULL(infoOf(u"{\"description\":[\"Line one\"]}"_s).description);
        ASSERT_NULL(infoOf(u"{\"description\":{\"text\":\"Line one\"}}"_s).description);
    }

    void aBlankDescriptionGivesNull() {
        ASSERT_NULL(infoOf(u"{\"description\":\"\"}"_s).description);
        ASSERT_NULL(infoOf(u"{\"description\":\"  \\t \\n \\r\\n \"}"_s).description);
    }

    void onlyTheDescriptionOfTheRootObjectCounts() {
        const VideoInfo result = infoOf(u"{\"formats\":[{\"description\":\"X\"}],\"meta\":{\"description\":\"Y\"},\"album\":\"Palette\"}"_s);

        ASSERT_EQUALS(info(u"Palette"_s, std::nullopt), result);
    }

    void readGivesNothingForAnyFileItCannotUse() {
        ASSERT_EQUALS(nothing, infoOf(u"{\"album\":\"Palette\",\"description\":\"Line one\""_s));
        ASSERT_EQUALS(nothing, infoOf(u"not json at all"_s));
        ASSERT_EQUALS(nothing, infoOf(QString()));
        ASSERT_EQUALS(nothing, infoOf(u"[{\"album\":\"Palette\",\"description\":\"Line one\"}]"_s));
        ASSERT_EQUALS(nothing, xgs::ytdlp::readVideoInfo(QDir(dir()).filePath(u"nothing.info.json"_s)));
        ASSERT_EQUALS(nothing, xgs::ytdlp::readVideoInfo(dir()));
        writeBytes(QByteArray("{\"album\":\"Pal") + QByteArray("\xC3\x28", 2) + QByteArray("ette\",\"description\":\"Line one\"}"));
        ASSERT_EQUALS(nothing, xgs::ytdlp::readVideoInfo(file()));
    }

    void readAlbumGivesTheAlbumOfRead() {
        writeString(u"{\"album\":\"  Palette \",\"description\":\"Line one\"}"_s);

        ASSERT_EQUALS(xgs::ytdlp::readVideoInfo(file()).album, xgs::ytdlp::readVideoAlbum(file()));
        ASSERT_EQUALS(std::optional<QString>(u"Palette"_s), xgs::ytdlp::readVideoAlbum(file()));
    }

    void readAppliesTheAlbumRulesOfReadAlbum() {
        ASSERT_NULL(infoOf(u"{\"album\":\"NA\",\"description\":\"Line one\"}"_s).album);
        ASSERT_NULL(infoOf(u"{\"album\":\"  \",\"description\":\"Line one\"}"_s).album);
        ASSERT_NULL(infoOf(u"{\"album\":2024,\"description\":\"Line one\"}"_s).album);
        ASSERT_EQUALS(std::optional<QString>(u"Line one"_s), infoOf(u"{\"album\":\"NA\",\"description\":\"Line one\"}"_s).description);
    }

    // ---- the duration ----

    void aWholeNumberDurationIsReadAsSeconds() {
        ASSERT_EQUALS(std::optional<int>(258), infoOf(u"{\"id\":\"vid00000001\",\"duration\":258}"_s).duration);
        ASSERT_EQUALS(std::optional<int>(1), infoOf(u"{\"duration\":1}"_s).duration);
    }

    void aDecimalDurationIsRounded() {
        ASSERT_EQUALS(std::optional<int>(258), infoOf(u"{\"duration\":258.4}"_s).duration);
        ASSERT_EQUALS(std::optional<int>(259), infoOf(u"{\"duration\":258.5}"_s).duration);
        ASSERT_EQUALS(std::optional<int>(259), infoOf(u"{\"duration\":258.6}"_s).duration);
        ASSERT_EQUALS(std::optional<int>(19), infoOf(u"{\"duration\":19.0}"_s).duration);
        ASSERT_EQUALS(std::optional<int>(258), infoOf(u"{\"duration\":2.584E2}"_s).duration);
    }

    void aMissingOrNullDurationGivesNull() {
        ASSERT_NULL(infoOf(u"{\"id\":\"vid00000001\",\"album\":\"Palette\"}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":null}"_s).duration);
    }

    void aDurationThatIsNotANumberGivesNull() {
        ASSERT_NULL(infoOf(u"{\"duration\":\"258\"}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":\"NA\"}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":\"\"}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":true}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":[258]}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":{\"seconds\":258}}"_s).duration);
    }

    void aZeroOrNegativeDurationGivesNull() {
        ASSERT_NULL(infoOf(u"{\"duration\":0}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":0.0}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":-5}"_s).duration);
        ASSERT_NULL(infoOf(u"{\"duration\":-0.4}"_s).duration);
        const std::optional<int> roundsToZero = infoOf(u"{\"duration\":0.4}"_s).duration;
        QVERIFY2(!roundsToZero.has_value(), "it rounds to zero, which is no length");
    }

    void aDurationThatDoesNotFitAnIntGivesNullAndTheOtherFieldsAreStillRead() {
        const VideoInfo result = infoOf(u"{\"album\":\"Palette\",\"duration\":1e30,\"description\":\"Line one\"}"_s);

        ASSERT_NULL(result.duration);
        ASSERT_EQUALS(std::optional<QString>(u"Palette"_s), result.album);
        ASSERT_EQUALS(std::optional<QString>(u"Line one"_s), result.description);
        ASSERT_NULL(infoOf(u"{\"duration\":4294967296}"_s).duration);
        ASSERT_EQUALS(std::optional<int>(INT_MAX), infoOf(u"{\"duration\":2147483647}"_s).duration);
    }

    void onlyTheDurationOfTheRootObjectCounts() {
        ASSERT_NULL(infoOf(u"{\"formats\":[{\"duration\":200}],\"meta\":{\"duration\":300},\"album\":\"Palette\"}"_s).duration);
        ASSERT_EQUALS(std::optional<int>(19), infoOf(u"{\"formats\":[{\"duration\":200}],\"duration\":19}"_s).duration);
    }

    void theDurationComesWithTheAlbumAndTheDescription() {
        const VideoInfo result = infoOf(u"{\"album\":\" Palette \",\"duration\":258.2,\"description\":\"[Lyrics]\\nLine one\\nLine two\"}"_s);

        ASSERT_EQUALS(info(u"Palette"_s, u"[Lyrics]\nLine one\nLine two"_s, 258), result);
    }

    void noDurationIsReadFromAFileThatCannotBeUsed() {
        ASSERT_EQUALS(nothing, infoOf(u"{\"duration\":258"_s));
        ASSERT_NULL(xgs::ytdlp::readVideoInfo(QDir(dir()).filePath(u"nothing.info.json"_s)).duration);
        ASSERT_NULL(infoOf(u"[{\"duration\":258}]"_s).duration);
    }

    void aLargeFileWithManyOtherFieldsStillWorks() {
        // A real info file is several hundred KB because of the formats list.
        QStringList entries;
        for (int i = 1; i <= 1000; ++i) {
            entries.append(u"{\"format_id\":\"%1\",\"url\":\"https://example.invalid/videoplayback?id=%1&sig=%2\",\"height\":%1}"_s
                               .arg(QString::number(i), u"x"_s.repeated(150)));
        }
        const QString json = u"{\"id\":\"vid00000001\",\"formats\":["_s + entries.join(u',') + u"],\"album\":\"Palette\",\"description\":\""_s
                             + u"d"_s.repeated(50'000) + u"\"}"_s;
        writeString(json);
        const qint64 size = QFileInfo(file()).size();
        QVERIFY2(size > 200'000, qPrintable(u"the test file must be large: %1 bytes"_s.arg(size)));

        ASSERT_EQUALS(std::optional<QString>(u"Palette"_s), xgs::ytdlp::readVideoAlbum(file()));
        ASSERT_EQUALS(info(u"Palette"_s, u"d"_s.repeated(50'000)), xgs::ytdlp::readVideoInfo(file()));
    }

    // Added (not in the Kotlin class): a 10 MB info file, mostly a formats list, is read within the time limit.
    void readsATenMegabyteInfoFileQuickly() {
        QByteArray bytes = "{\"formats\":[";
        const QByteArray entry =
            "{\"format_id\":\"251\",\"tbr\":NaN,\"filesize\":1234567,\"url\":\"https://example.invalid/videoplayback?a=1&b=2\",\"album\":\"X\",\"duration\":1},";
        while (bytes.size() < 10 * 1024 * 1024) bytes += entry;
        bytes += "{}],\"album\":\"A\",\"description\":\"";
        const QString description = u"가사\n첫 줄\n둘째 줄"_s;
        bytes += QString(description).replace(u"\n"_s, u"\\n"_s).toUtf8();
        bytes += "\",\"duration\":200}";
        writeBytes(bytes);

        QElapsedTimer timer;
        timer.start();
        const VideoInfo result = xgs::ytdlp::readVideoInfo(file());
        const qint64 elapsed = timer.elapsed();
#ifdef NDEBUG
        // The time limit is about the optimized build; an unoptimized Debug build with Qt's checks on is many times slower.
        QVERIFY2(elapsed < 3000, qPrintable(u"reading %1 bytes took %2 ms"_s.arg(bytes.size()).arg(elapsed)));
#else
        Q_UNUSED(elapsed);
#endif
        ASSERT_EQUALS(std::optional<QString>(u"A"_s), result.album);
        ASSERT_EQUALS(std::optional<int>(200), result.duration);
        ASSERT_EQUALS(std::optional<QString>(description), result.description);
    }
};

QTEST_APPLESS_MAIN(VideoInfoFileTest)
#include "tst_video_info_file.moc"
