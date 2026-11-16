#include <QtTest>

#include <optional>

#include "ytdlp/progress_parser.h"

using xgs::ytdlp::DownloadProgress;
using namespace Qt::StringLiterals;

namespace {

std::optional<DownloadProgress> parse(const QString& line) {
    return xgs::ytdlp::parseProgress(line);
}

QString describe(const std::optional<DownloadProgress>& update) {
    if (!update) return u"null"_s;
    return update->percent ? u"Downloading(percent=%1)"_s.arg(*update->percent) : u"Downloading(percent=null)"_s;
}

}  // namespace

// Kotlin's assertEquals(expected, actual) on ProgressUpdate.Downloading?
#define ASSERT_UPDATE(expected, actual)                                                                       \
    do {                                                                                                      \
        const std::optional<DownloadProgress> expectedUpdate = (expected);                                    \
        const std::optional<DownloadProgress> actualUpdate = (actual);                                        \
        QVERIFY2(expectedUpdate == actualUpdate,                                                              \
                 qPrintable(u"expected %1 but was %2"_s.arg(describe(expectedUpdate), describe(actualUpdate)))); \
    } while (false)

class ProgressParserTest : public QObject {
    Q_OBJECT

private slots:
    void percentFromDownloadedAndTotalBytes() {
        const std::optional<DownloadProgress> update = parse(u"XGSP|downloading|500|1000|NA"_s);
        ASSERT_UPDATE(DownloadProgress{50.0}, update);
    }

    void fallsBackToTheEstimatedTotal() {
        const std::optional<DownloadProgress> update = parse(u"XGSP|downloading|250|NA|1000"_s);
        QVERIFY(update.has_value());   // Kotlin's !!
        QCOMPARE(update->percent, std::optional<double>(25.0));
    }

    void percentIsUnknownWithoutAnyTotal() {
        ASSERT_UPDATE(DownloadProgress{std::nullopt}, parse(u"XGSP|downloading|500|NA|NA"_s));
    }

    void percentIsCappedAt100() {
        ASSERT_UPDATE(DownloadProgress{100.0}, parse(u"XGSP|downloading|2000|1000|NA"_s));
    }

    void finishedMeansHundredPercent() {
        ASSERT_UPDATE(DownloadProgress{100.0}, parse(u"XGSP|finished|1000|1000|NA"_s));
    }

    // yt-dlp's post-processor lines are not read any more: the downloader reports the finishing stage itself.
    void postprocessorLinesAreIgnored() {
        QVERIFY(!parse(u"XGSPP|started|ExtractAudio"_s).has_value());
        QVERIFY(!parse(u"XGSPP|started|MoveFiles"_s).has_value());
        QVERIFY(!parse(u"XGSPP|finished|ThumbnailsConvertor"_s).has_value());
    }

    void otherLinesAreIgnored() {
        QVERIFY(!parse(u"[youtube] Extracting URL"_s).has_value());
        QVERIFY(!parse(QString()).has_value());
    }
};

QTEST_APPLESS_MAIN(ProgressParserTest)
#include "tst_progress_parser.moc"
