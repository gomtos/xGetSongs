#include <QtTest>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>

#include <variant>

#include "diag/file_logger.h"
#include "diag/job_log.h"

using namespace Qt::StringLiterals;
using xgs::InputKind;
using xgs::JobEvent;
using xgs::JobOptions;
using xgs::diag::FileLogger;
using xgs::diag::JobLogLine;
using xgs::diag::Level;
namespace JobLog = xgs::diag::JobLog;

namespace {

// The names of the files and titles in these events are recognisable markers: none of them may ever show up in a log line.

JobLogLine line(const JobEvent& event, const QHash<int, QString>& fileNames = {}) {
    const auto described = JobLog::describe(event, fileNames);
    return described ? *described : JobLogLine{Level::Error, u"<no line>"_s};
}

bool has(const QString& text, const QString& part) {
    return text.contains(part);
}

xgs::JobSummary summary(int succeeded, int skipped, int failed) {
    xgs::JobSummary s;
    s.succeeded = succeeded;
    s.skipped = skipped;
    s.failed = failed;
    return s;
}

}  // namespace

class JobLogTest : public QObject {
    Q_OBJECT

private slots:
    // ---- the lines -------------------------------------------------------------------------

    void anItemThatStartsIsADebugLineWithRankAndVideoIdOnly() {
        const auto described = JobLog::describe(xgs::ItemStarted{7, u"vid00000007"_s, u"FILENAME-MARKER.mp3"_s});

        QVERIFY(described.has_value());
        QCOMPARE(described->level, Level::Debug);
        QVERIFY2(has(described->text, u"순위 7"_s) && has(described->text, u"vid00000007"_s), qPrintable(described->text));
        QVERIFY2(!has(described->text, u"FILENAME-MARKER"_s), qPrintable(described->text));
    }

    void progressIsNotLogged() {
        QVERIFY(!JobLog::describe(xgs::Progress{1, xgs::Stage::Downloading, 42.0}).has_value());
        QVERIFY(!JobLog::describe(xgs::Progress{1, xgs::Stage::Finishing, std::nullopt}).has_value());
    }

    void aFinishedItemIsAnInfoLineWithTheRankOnly() {
        const auto described = JobLog::describe(xgs::ItemDone{12, u"FILENAME-MARKER.mp3"_s, std::nullopt});

        QVERIFY(described.has_value());
        QCOMPARE(described->level, Level::Info);
        QCOMPARE(described->text, u"항목 완료: 순위 12"_s);
    }

    void aFinishedItemNamesItsLyricsOutcomeButNotItsFileName() {
        const std::pair<xgs::LyricsOutcome, QString> outcomes[] = {
            {xgs::LyricsOutcome::Description, u"DESCRIPTION"_s},
            {xgs::LyricsOutcome::Online, u"ONLINE"_s},
            {xgs::LyricsOutcome::NotFound, u"NOT_FOUND"_s},
            {xgs::LyricsOutcome::SearchOff, u"SEARCH_OFF"_s},
        };
        for (const auto& [outcome, name] : outcomes) {
            const auto described = JobLog::describe(xgs::ItemDone{3, u"FILENAME-MARKER.mp3"_s, outcome});

            QVERIFY(described.has_value());
            QCOMPARE(described->level, Level::Info);
            QCOMPARE(described->text, u"항목 완료: 순위 3, 가사 "_s + name);
            QVERIFY2(!has(described->text, u"FILENAME-MARKER"_s), qPrintable(described->text));
        }
        QCOMPARE(JobLog::describe(xgs::ItemDone{3, u"x.mp3"_s, xgs::LyricsOutcome::Online})->text, u"항목 완료: 순위 3, 가사 ONLINE"_s);
    }

    void aFinishedItemWithoutAKnownOutcomeAddsNothingToTheLine() {
        const auto described = JobLog::describe(xgs::ItemDone{3, u"FILENAME-MARKER.mp3"_s, std::nullopt});

        QVERIFY(described.has_value());
        QCOMPARE(described->level, Level::Info);
        QCOMPARE(described->text, u"항목 완료: 순위 3"_s);
    }

    void aSkippedItemIsAnInfoLineWithRankAndReason() {
        const auto described = JobLog::describe(xgs::ItemSkipped{2, u"이미 파일이 있음"_s});

        QVERIFY(described.has_value());
        QCOMPARE(described->level, Level::Info);
        QCOMPARE(described->text, u"항목 건너뜀: 순위 2, 사유: 이미 파일이 있음"_s);
    }

    void aFailedItemIsAWarningWithRankAndReason() {
        const auto described = JobLog::describe(xgs::ItemFailed{3, u"yt-dlp가 비정상 종료했습니다 (코드 1)"_s});

        QVERIFY(described.has_value());
        QCOMPARE(described->level, Level::Warn);
        QCOMPARE(described->text, u"항목 실패: 순위 3, 사유: yt-dlp가 비정상 종료했습니다 (코드 1)"_s);
    }

    void theSummaryIsAnInfoLineWithTheStatusAndTheThreeCounts() {
        const auto described = JobLog::describe(xgs::JobDone{xgs::JobStatus::Cancelled, summary(5, 2, 1)});

        QVERIFY(described.has_value());
        QCOMPARE(described->level, Level::Info);
        QCOMPARE(described->text, u"작업 종료: 상태=CANCELLED, 성공 5, 건너뜀 2, 실패 1"_s);
        QCOMPARE(JobLog::describe(xgs::JobDone{xgs::JobStatus::Completed, summary(0, 0, 0)})->text, u"작업 종료: 상태=COMPLETED, 성공 0, 건너뜀 0, 실패 0"_s);
        QCOMPARE(JobLog::describe(xgs::JobDone{xgs::JobStatus::Failed, summary(0, 0, 0)})->text, u"작업 종료: 상태=FAILED, 성공 0, 건너뜀 0, 실패 0"_s);
    }

    void aReasonIsKeptOnOneLineAndCutWhenItIsLong() {
        const QString multi = u"first line\r\nsecond line\n\n\tthird"_s + QChar(0) + u"line"_s;
        const auto multiLine = JobLog::describe(xgs::ItemFailed{1, multi});
        QVERIFY(multiLine.has_value());
        QCOMPARE(multiLine->text, u"항목 실패: 순위 1, 사유: first line second line third line"_s);

        const auto longLine = JobLog::describe(xgs::ItemFailed{1, QString(2'000, u'x')});
        QVERIFY(longLine.has_value());
        const QString reason = longLine->text.mid(longLine->text.indexOf(u"사유: "_s) + 4);
        QCOMPARE(reason.size(), JobLog::MAX_REASON_LENGTH + 1);
        QVERIFY(reason.endsWith(u"…"_s));
    }

    // ---- paths and file names --------------------------------------------------------------

    void aNoSuchFileExceptionShapedReasonLosesBothPathsWhatEverTheSpacesInThem() {
        const QString reason = u"NoSuchFileException: C:\\Users\\Some User\\AppData\\Roaming\\xGetSongs\\work\\job-1\\001 A - B.f251.webm -> "_s
                               u"D:\\Music\\My Playlist\\001 A - B.mp3"_s;

        QCOMPARE(JobLog::redact(reason, {}), u"NoSuchFileException: <경로> -> <경로>"_s);
    }

    void aFileSystemExceptionKeepsItsSystemReason() {
        const QString reason = u"FileSystemException: C:\\w\\job 1\\001 A - B.mp3 -> D:\\Music\\My Playlist\\001 A - B.mp3: "_s
                               u"The process cannot access the file because it is being used by another process"_s;

        QCOMPARE(JobLog::redact(reason, {}),
                 u"FileSystemException: <경로> -> <경로>: The process cannot access the file because it is being used by another process"_s);
    }

    void aUncPathIsRedactedToo() {
        QCOMPARE(JobLog::redact(u"AccessDeniedException: \\\\NAS\\Music Share\\Lists\\a.mp3: Access is denied"_s, {}),
                 u"AccessDeniedException: <경로>: Access is denied"_s);
    }

    void theKnownFileNameIsReplacedWhereverItAppearsIgnoringCase() {
        const QString redacted = JobLog::redact(u"yt-dlp could not write 001 A - B.MP3 (while merging)"_s, {u"001 A - B.mp3"_s});

        QCOMPARE(redacted, u"yt-dlp could not write <파일명> (while merging)"_s);
    }

    void textWithoutPathsOrKnownNamesIsUnchanged() {
        for (const QString& text : {u"연결이 끊어졌습니다"_s, u"HTTP 429: Too Many Requests"_s, u"ERROR: [youtube] abc: Video unavailable"_s, u""_s}) {
            QCOMPARE(JobLog::redact(text, {u"001 A - B.mp3"_s}), text);
        }
    }

    void aFailedItemLosesPathsAndTheFileNameOfItsRank() {
        const xgs::ItemFailed event{3, u"FileSystemException: C:\\Users\\me\\Music\\MARKER-SONG.mp3 -> D:\\Out\\MARKER-SONG.mp3: Access is denied (MARKER-SONG.mp3)"_s};

        const auto described = JobLog::describe(event, {{3, u"MARKER-SONG.mp3"_s}});

        QVERIFY(described.has_value());
        QCOMPARE(described->level, Level::Warn);
        QCOMPARE(described->text, u"항목 실패: 순위 3, 사유: FileSystemException: <경로> -> <경로>: Access is denied (<파일명>)"_s);
    }

    void aSkippedItemLosesThemToo() {
        const auto described = JobLog::describe(xgs::ItemSkipped{2, u"이미 있음: D:\\Out\\MARKER-SONG.mp3"_s}, {{2, u"MARKER-SONG.mp3"_s}});

        QVERIFY(described.has_value());
        QCOMPARE(described->text, u"항목 건너뜀: 순위 2, 사유: 이미 있음: <경로>"_s);
    }

    void theFileNameOfAnotherRankIsNotTouched() {
        const auto described = JobLog::describe(xgs::ItemFailed{3, u"failed: other-name.mp3"_s}, {{4, u"other-name.mp3"_s}});

        QVERIFY(described.has_value());
        QCOMPARE(described->text, u"항목 실패: 순위 3, 사유: failed: other-name.mp3"_s);
    }

    void aPathIsRedactedBeforeTheReasonIsCutSoNoPartOfItSurvives() {
        QString longPath = u"C:\\"_s;
        for (int i = 0; i < 40; ++i) longPath += u"very long folder name\\"_s;
        longPath += u"file.mp3"_s;

        const auto described = JobLog::describe(xgs::ItemFailed{1, u"failed: "_s + longPath}, {});

        QVERIFY(described.has_value());
        QCOMPARE(described->text, u"항목 실패: 순위 1, 사유: failed: <경로>"_s);
    }

    void theLineForADroppedEventConnectionNamesTheJob() {
        QCOMPARE(JobLog::eventsDisconnected(u"0123456789abcdef"_s), u"이벤트 연결이 끊어짐 (작업 01234567)"_s);
    }

    void theStartLineHasKindCountAndOptionsAndNoTitles() {
        JobOptions options;
        options.outputDir = u"C:\\Music\\out"_s;
        options.overwrite = true;
        options.includeRank = false;
        options.searchLyricsOnline = false;
        JobOptions noDir = options;
        noDir.outputDir = std::nullopt;

        const QString playlist = JobLog::started(u"0123456789abcdef"_s, InputKind::Playlist, 40, options, 3);
        const QString video = JobLog::started(u"0123456789abcdef"_s, InputKind::Video, 1, noDir, 3);

        QCOMPARE(playlist,
                 u"작업 시작: id=0123456789abcdef, 종류=재생목록, 항목=40개, overwrite=true, includeRank=false, concurrency=3, searchLyricsOnline=false, outputDir=C:\\Music\\out"_s);
        QVERIFY2(has(video, u"종류=영상"_s) && has(video, u"항목=1개"_s) && has(video, u"outputDir=없음"_s), qPrintable(video));
    }

    void theStartLineSaysThatAnAlbumNameWasTypedButNeverWhatItIs() {
        JobOptions plain;
        plain.outputDir = u"C:\\Music\\out"_s;
        JobOptions typed = plain;
        typed.albumName = u"비밀 앨범"_s;
        JobOptions blank = plain;
        blank.albumName = u"  "_s;
        const QString id = u"0123456789abcdef"_s;

        const QString text = JobLog::started(id, InputKind::Playlist, 2, typed, 3);

        QVERIFY2(text.endsWith(u", outputDir=C:\\Music\\out, albumName=지정"_s), qPrintable(text));
        QVERIFY2(!has(text, u"비밀"_s), qPrintable(text));
        QCOMPARE(JobLog::started(id, InputKind::Playlist, 2, plain, 3), JobLog::started(id, InputKind::Playlist, 2, blank, 3));
        QVERIFY(!has(JobLog::started(id, InputKind::Playlist, 2, plain, 3), u"albumName"_s));
    }

    void theShortIdIsTheFirstEightCharacters() {
        QCOMPARE(JobLog::shortId(u"0123456789abcdef"_s), u"01234567"_s);
        QCOMPARE(JobLog::shortId(u"abc"_s), u"abc"_s);
    }

    // ---- through the log file ---------------------------------------------------------------

    // Kotlin's version goes through the HTTP routes and checks the token too; here the lines go through FileLogger into the
    // file, which is what the user may send to someone (Review Focus 4 of the Q5 plan).
    void noLogLineHoldsATitleAFileNameOrTheToken() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QList<JobEvent> events = {
            xgs::ItemStarted{1, u"vid00000001"_s, u"TITLE-MARKER-ONE.mp3"_s},
            xgs::ItemDone{1, u"TITLE-MARKER-ONE.mp3"_s, xgs::LyricsOutcome::Description},
            xgs::ItemStarted{3, u"vid00000003"_s, u"TITLE-MARKER-THREE.mp3"_s},
            xgs::ItemFailed{3,
                            u"FileSystemException: C:\\Users\\Some User\\Music\\TITLE-MARKER-THREE.mp3 -> D:\\Out\\TITLE-MARKER-THREE.mp3: "_s
                            u"Access is denied while writing title-marker-three.mp3"_s},
            xgs::JobDone{xgs::JobStatus::Completed, summary(1, 0, 1)},
        };
        const QString jobId = u"0123456789abcdef"_s;
        const QString tag = u"["_s + JobLog::shortId(jobId) + u"] "_s;

        FileLogger logger;
        QVERIFY(logger.open(dir.path()));
        QHash<int, QString> fileNames;
        for (const JobEvent& event : events) {
            if (const auto* started = std::get_if<xgs::ItemStarted>(&event)) fileNames.insert(started->rank, started->fileName);
            if (const auto described = JobLog::describe(event, fileNames)) {
                logger.write(described->level, u"worker"_s, JobLog::LOGGER_NAME, tag + described->text);
            }
        }
        logger.close();

        QFile file(dir.filePath(u"xgetsongs.log"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString everything = QString::fromUtf8(file.readAll());
        QVERIFY(!everything.isEmpty());
        QVERIFY2(!has(everything, u"TITLE-MARKER"_s), qPrintable(everything));
        QVERIFY2(!has(everything, u"title-marker"_s), qPrintable(everything));
        QVERIFY2(!has(everything, u"Some User"_s), qPrintable(u"no path: "_s + everything));
        const QStringList lines = everything.split(u"\r\n"_s, Qt::SkipEmptyParts);
        QString failure;
        for (const QString& l : lines) {
            if (has(l, u" WARN "_s)) failure = l;
        }
        QVERIFY2(failure.endsWith(u"항목 실패: 순위 3, 사유: FileSystemException: <경로> -> <경로>: Access is denied while writing <파일명>"_s),
                 qPrintable(failure));
    }
};

QTEST_GUILESS_MAIN(JobLogTest)
#include "tst_job_log.moc"
