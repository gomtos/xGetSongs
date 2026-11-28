#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

#include <windows.h>

#include "diag/file_logger.h"

using namespace Qt::StringLiterals;
using xgs::diag::FileLogger;
using xgs::diag::FileLoggerOptions;
using xgs::diag::Level;

namespace {

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray("<unreadable>");
    return file.readAll();
}

QString readText(const QString& path) {
    return QString::fromUtf8(readBytes(path));
}

QStringList linesOf(const QString& path) {
    const QString text = readText(path);
    QStringList lines = text.split(u"\r\n"_s);
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines;
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

QDateTime at(int year, int month, int day, int hour = 12, int minute = 0, int second = 0, int ms = 0) {
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second, ms));
}

// The rolled files and the active one of a folder, sorted by name.
QStringList logFiles(const QString& dir) {
    return QDir(dir).entryList({u"xgetsongs*.log"_s}, QDir::Files, QDir::Name);
}

// A handle that keeps another program's grip on a file: share is the sharing mode it allows others (0 = none).
HANDLE hold(const QString& path, DWORD access, DWORD share) {
    return CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()), access, share, nullptr,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

}  // namespace

// No Kotlin pair: logback does the logging of the Kotlin version. These are the D2 gate of the Q5 plan (the line, rolling, the
// 7 days and 50 MB, lines of many threads not mixing) and its Review Focus 1 (a file another program holds, a folder that cannot
// be used) and 5 (the numbers of rolled files at midnight and at the size limit).
class FileLoggerTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_temp;
    QString m_dir;
    QDateTime m_now = at(2026, 10, 7, 21, 3, 11, 123);
    int m_run = 0;

    FileLogger::Clock clock() { return [this] { return m_now; }; }

    FileLoggerOptions withLimits(qint64 maxFileBytes, int days = 7, qint64 cap = 50LL * 1024 * 1024) {
        FileLoggerOptions options;
        options.maxFileBytes = maxFileBytes;
        options.maxHistoryDays = days;
        options.totalSizeCapBytes = cap;
        return options;
    }

    QString active() const { return m_dir + u"/xgetsongs.log"_s; }
    QString rolled(const QString& date, int number) const { return m_dir + u"/xgetsongs."_s + date + u"."_s + QString::number(number) + u".log"_s; }

    // A message of exactly n characters (made of the letters of the index, so lines differ).
    static QString text(int index, int length) {
        QString out = QString::number(index) + u':';
        while (out.size() < length) out += u'x';
        return out.left(length);
    }

private slots:
    void initTestCase() { QVERIFY(m_temp.isValid()); }

    void init() {
        m_dir = m_temp.filePath(u"run%1"_s.arg(++m_run));
        m_now = at(2026, 10, 7, 21, 3, 11, 123);
    }

    // ---- the line --------------------------------------------------------------------------

    void theLineHasTheLogbackShape() {
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));

        logger.write(Level::Info, u"main"_s, u"xgs.test"_s, u"hello"_s);
        logger.close();

        QCOMPARE(readBytes(active()), QByteArray("2026-10-07 21:03:11.123 INFO  [main] xgs.test - hello\r\n"));
    }

    void levelsArePaddedLikeLogbackAndTheMillisecondsHaveThreeDigits() {
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));
        m_now = at(2026, 1, 2, 3, 4, 5, 6);

        logger.write(Level::Debug, u"t"_s, u"a"_s, u"d"_s);
        logger.write(Level::Info, u"t"_s, u"a"_s, u"i"_s);
        logger.write(Level::Warn, u"t"_s, u"a"_s, u"w"_s);
        logger.write(Level::Error, u"t"_s, u"a"_s, u"e"_s);
        logger.close();

        QCOMPARE(linesOf(active()), (QStringList{
                                        u"2026-01-02 03:04:05.006 DEBUG [t] a - d"_s,
                                        u"2026-01-02 03:04:05.006 INFO  [t] a - i"_s,
                                        u"2026-01-02 03:04:05.006 WARN  [t] a - w"_s,
                                        u"2026-01-02 03:04:05.006 ERROR [t] a - e"_s,
                                    }));
    }

    void aMessageWithLineBreaksIsOneRecordKeptAsItIs() {
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));

        logger.write(Level::Info, u"main"_s, u"xgs.test"_s, u"시작 정보\n  앱: xGetSongs"_s);
        logger.close();

        QCOMPARE(readBytes(active()), QByteArray("2026-10-07 21:03:11.123 INFO  [main] xgs.test - ") +
                                          QString(u"시작 정보\n  앱: xGetSongs"_s).toUtf8() + QByteArray("\r\n"));
    }

    void theFileIsUtf8WithoutAByteOrderMark() {
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));

        logger.write(Level::Info, u"main"_s, u"xgs.test"_s, u"한글 ✓"_s);
        logger.close();

        const QByteArray bytes = readBytes(active());
        QVERIFY(bytes.startsWith("2026-"));
        QVERIFY(bytes.contains(QString(u"한글 ✓"_s).toUtf8()));
    }

    void theLineIsInTheFileWhenWriteReturnsWithoutAnyFlush() {
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));

        logger.write(Level::Warn, u"main"_s, u"xgs.test"_s, u"before a crash"_s);

        // Read through another handle while the logger still holds the file: a kill now loses nothing.
        QVERIFY(readText(active()).contains(u"before a crash"_s));
    }

    void openMakesTheFolderAndAppendsToAnExistingFile() {
        {
            FileLogger first(clock());
            QVERIFY(first.open(m_dir + u"/a/b"_s));
            first.write(Level::Info, u"t"_s, u"l"_s, u"one"_s);
        }
        FileLogger second(clock());
        QVERIFY(second.open(m_dir + u"/a/b"_s));
        second.write(Level::Info, u"t"_s, u"l"_s, u"two"_s);
        second.close();

        const QStringList lines = linesOf(m_dir + u"/a/b/xgetsongs.log"_s);
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines[0].endsWith(u" - one"_s) && lines[1].endsWith(u" - two"_s));
    }

    void aWriteBeforeOpenAndAfterCloseDoesNothingAndThrowsNothing() {
        FileLogger logger(clock());
        logger.write(Level::Info, u"t"_s, u"l"_s, u"too early"_s);
        QVERIFY(!logger.isOpen());

        QVERIFY(logger.open(m_dir));
        QVERIFY(logger.isOpen());
        logger.close();
        QVERIFY(!logger.isOpen());
        logger.write(Level::Info, u"t"_s, u"l"_s, u"too late"_s);

        QVERIFY(readBytes(active()).isEmpty());
    }

    void aClockThatThrowsDoesNotMakeWriteThrow() {
        bool fail = false;
        FileLogger logger([&]() -> QDateTime {
            if (fail) throw std::runtime_error("clock");
            return m_now;
        });
        QVERIFY(logger.open(m_dir));
        fail = true;

        logger.write(Level::Info, u"t"_s, u"l"_s, u"x"_s);  // must not throw
        fail = false;
        logger.write(Level::Info, u"t"_s, u"l"_s, u"y"_s);

        QCOMPARE(linesOf(active()).size(), 1);
    }

    void theDefaultLimitsAreThoseOfLogbackSidecarXml() {
        const FileLoggerOptions options;

        QCOMPARE(options.maxFileBytes, qint64(5 * 1024 * 1024));
        QCOMPARE(options.maxHistoryDays, 7);
        QCOMPARE(options.totalSizeCapBytes, qint64(50 * 1024 * 1024));
    }

    // ---- rolling ----------------------------------------------------------------------------

    // The file is rolled once it has reached the limit, before the line that would follow (logback's SizeAndTimeBased policy
    // asks the file's length first): a line may take the file over the limit.
    void aFileIsRolledOnceItHasReachedTheLimitNotBefore() {
        FileLogger logger(clock(), withLimits(100));
        QVERIFY(logger.open(m_dir));
        const QString message = text(1, 30);  // a line of 41 + 30 + 2 = about 80 bytes... measured below

        logger.write(Level::Info, u"t"_s, u"l"_s, message);
        const qint64 oneLine = QFileInfo(active()).size();
        QVERIFY(oneLine > 0 && oneLine < 100);
        logger.write(Level::Info, u"t"_s, u"l"_s, text(2, 30));
        QVERIFY2(QFileInfo(active()).size() >= 100, "the second line took the file over the limit");
        QCOMPARE(logFiles(m_dir), QStringList{u"xgetsongs.log"_s});

        logger.write(Level::Info, u"t"_s, u"l"_s, text(3, 30));  // rolls first
        logger.close();

        QCOMPARE(logFiles(m_dir), (QStringList{u"xgetsongs.2026-10-07.0.log"_s, u"xgetsongs.log"_s}));
        QCOMPARE(linesOf(rolled(u"2026-10-07"_s, 0)).size(), 2);
        const QStringList now = linesOf(active());
        QCOMPARE(now.size(), 1);
        QVERIFY(now.first().endsWith(u" - "_s + text(3, 30)));
    }

    void rolledFilesOfOneDayCountUpFromZero() {
        FileLogger logger(clock(), withLimits(100));
        QVERIFY(logger.open(m_dir));

        for (int i = 0; i < 13; ++i) logger.write(Level::Info, u"t"_s, u"l"_s, text(i, 60));
        logger.close();

        const QStringList files = logFiles(m_dir);
        QVERIFY2(files.contains(u"xgetsongs.2026-10-07.0.log"_s) && files.contains(u"xgetsongs.2026-10-07.1.log"_s) &&
                     files.contains(u"xgetsongs.2026-10-07.2.log"_s) && files.contains(u"xgetsongs.2026-10-07.5.log"_s),
                 qPrintable(files.join(u'|')));
        // every line exactly once, in order, over the rolled files and the active one
        QStringList all;
        for (int n = 0; QFileInfo::exists(rolled(u"2026-10-07"_s, n)); ++n) all += linesOf(rolled(u"2026-10-07"_s, n));
        all += linesOf(active());
        QCOMPARE(all.size(), 13);
        for (int i = 0; i < 13; ++i) QVERIFY2(all[i].endsWith(u" - "_s + text(i, 60)), qPrintable(all[i]));
    }

    void aNewDayRollsTheLinesOfTheOldDayUnderTheOldDate() {
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));
        logger.write(Level::Info, u"t"_s, u"l"_s, u"day one"_s);

        m_now = at(2026, 10, 8, 0, 0, 1);
        logger.write(Level::Info, u"t"_s, u"l"_s, u"day two"_s);
        logger.close();

        QCOMPARE(logFiles(m_dir), (QStringList{u"xgetsongs.2026-10-07.0.log"_s, u"xgetsongs.log"_s}));
        QVERIFY(readText(rolled(u"2026-10-07"_s, 0)).contains(u"day one"_s));
        QVERIFY(!readText(rolled(u"2026-10-07"_s, 0)).contains(u"day two"_s));
        QVERIFY(readText(active()).contains(u"day two"_s));
        QVERIFY(readText(active()).startsWith(u"2026-10-08 00:00:01"_s));
    }

    void aNewDayWithNothingInTheFileRollsNothing() {
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));

        m_now = at(2026, 10, 9);
        logger.write(Level::Info, u"t"_s, u"l"_s, u"first of the day"_s);
        logger.close();

        QCOMPARE(logFiles(m_dir), QStringList{u"xgetsongs.log"_s});
    }

    // Review Focus 5: the size roll of the evening and the midnight roll must not pick the same number.
    void midnightAfterASizeRollOfTheSameDayTakesTheNextNumber() {
        FileLogger logger(clock(), withLimits(100));
        QVERIFY(logger.open(m_dir));
        m_now = at(2026, 10, 7, 23, 59, 58);
        logger.write(Level::Info, u"t"_s, u"l"_s, text(1, 30));
        logger.write(Level::Info, u"t"_s, u"l"_s, text(2, 30));
        logger.write(Level::Info, u"t"_s, u"l"_s, text(3, 30));  // size roll: 2026-10-07.0 holds lines 1 and 2
        QVERIFY(QFileInfo::exists(rolled(u"2026-10-07"_s, 0)));
        const QByteArray first = readBytes(rolled(u"2026-10-07"_s, 0));

        m_now = at(2026, 10, 8, 0, 0, 1);
        logger.write(Level::Info, u"t"_s, u"l"_s, text(4, 5));  // day roll: line 3 goes to 2026-10-07.1
        logger.close();

        QCOMPARE(readBytes(rolled(u"2026-10-07"_s, 0)), first);  // not overwritten
        QVERIFY(QFileInfo::exists(rolled(u"2026-10-07"_s, 1)));
        QVERIFY(readText(rolled(u"2026-10-07"_s, 1)).contains(u" - "_s + text(3, 30)));
        QVERIFY(readText(active()).contains(u" - "_s + text(4, 5)));
        QCOMPARE(logFiles(m_dir).size(), 3);
    }

    void aRolledFileThatAlreadyExistsIsNeverOverwritten() {
        QVERIFY(QDir().mkpath(m_dir));
        writeFile(rolled(u"2026-10-07"_s, 0), "old zero");
        writeFile(rolled(u"2026-10-07"_s, 1), "old one");
        writeFile(rolled(u"2026-10-07"_s, 4), "old four");
        FileLogger logger(clock(), withLimits(60));
        QVERIFY(logger.open(m_dir));

        logger.write(Level::Info, u"t"_s, u"l"_s, text(1, 60));
        logger.write(Level::Info, u"t"_s, u"l"_s, text(2, 60));  // rolls
        logger.close();

        QCOMPARE(readBytes(rolled(u"2026-10-07"_s, 0)), QByteArray("old zero"));
        QCOMPARE(readBytes(rolled(u"2026-10-07"_s, 1)), QByteArray("old one"));
        QCOMPARE(readBytes(rolled(u"2026-10-07"_s, 4)), QByteArray("old four"));
        QVERIFY(readText(rolled(u"2026-10-07"_s, 5)).contains(u" - "_s + text(1, 60)));
    }

    void aFileLeftByAnEarlierRunOnAnEarlierDayIsRolledUnderItsOwnDate() {
        QVERIFY(QDir().mkpath(m_dir));
        writeFile(active(), "2026-10-05 10:00:00.000 INFO  [main] xgs.test - from the earlier run\r\n");
        QFile file(active());
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(at(2026, 10, 5, 10), QFileDevice::FileModificationTime));
        file.close();
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));

        logger.write(Level::Info, u"t"_s, u"l"_s, u"today"_s);
        logger.close();

        QVERIFY(readText(rolled(u"2026-10-05"_s, 0)).contains(u"from the earlier run"_s));
        QVERIFY(!readText(active()).contains(u"from the earlier run"_s));
        QVERIFY(readText(active()).contains(u"today"_s));
    }

    // ---- 7 days and 50 MB -------------------------------------------------------------------

    void rolledFilesOlderThanSevenDaysAreDeletedAndOthersAreLeftAlone() {
        QVERIFY(QDir().mkpath(m_dir));
        writeFile(rolled(u"2026-09-29"_s, 0), "eight days old");   // the 7th of October minus 8
        writeFile(rolled(u"2026-09-30"_s, 3), "seven days old");
        writeFile(rolled(u"2026-10-06"_s, 0), "yesterday");
        writeFile(rolled(u"2026-01-01"_s, 12), "very old");
        writeFile(m_dir + u"/notes.txt"_s, "not a log");
        writeFile(m_dir + u"/xgetsongs.foo.log"_s, "not a rolled name");
        writeFile(m_dir + u"/xgetsongs.2026-13-45.0.log"_s, "not a date");
        writeFile(m_dir + u"/last-run.txt"_s, "state=exited");
        FileLogger logger(clock());

        QVERIFY(logger.open(m_dir));

        QVERIFY(!QFileInfo::exists(rolled(u"2026-09-29"_s, 0)));
        QVERIFY(!QFileInfo::exists(rolled(u"2026-01-01"_s, 12)));
        QVERIFY(QFileInfo::exists(rolled(u"2026-09-30"_s, 3)));
        QVERIFY(QFileInfo::exists(rolled(u"2026-10-06"_s, 0)));
        QVERIFY(QFileInfo::exists(m_dir + u"/notes.txt"_s));
        QVERIFY(QFileInfo::exists(m_dir + u"/xgetsongs.foo.log"_s));
        QVERIFY(QFileInfo::exists(m_dir + u"/xgetsongs.2026-13-45.0.log"_s));
        QVERIFY(QFileInfo::exists(m_dir + u"/last-run.txt"_s));
        QVERIFY(QFileInfo::exists(active()));
    }

    void theHistoryIsCleanedAfterEveryRollToo() {
        FileLogger logger(clock(), withLimits(50));
        QVERIFY(logger.open(m_dir));
        writeFile(rolled(u"2026-09-01"_s, 0), "old");  // appears after the open

        logger.write(Level::Info, u"t"_s, u"l"_s, text(1, 60));
        logger.write(Level::Info, u"t"_s, u"l"_s, text(2, 60));  // rolls and cleans
        logger.close();

        QVERIFY(!QFileInfo::exists(rolled(u"2026-09-01"_s, 0)));
        QVERIFY(QFileInfo::exists(rolled(u"2026-10-07"_s, 0)));
    }

    void theOldestRolledFilesGoWhileTheTotalIsOverTheCap() {
        QVERIFY(QDir().mkpath(m_dir));
        writeFile(rolled(u"2026-10-04"_s, 0), QByteArray(100, 'a'));
        writeFile(rolled(u"2026-10-05"_s, 0), QByteArray(100, 'b'));
        writeFile(rolled(u"2026-10-05"_s, 1), QByteArray(100, 'c'));
        writeFile(rolled(u"2026-10-06"_s, 0), QByteArray(100, 'd'));
        FileLogger logger(clock(), withLimits(5 * 1024 * 1024, 7, 250));

        QVERIFY(logger.open(m_dir));

        // 400 bytes over a cap of 250: the oldest two go (by date, then by number), 200 are left
        QVERIFY(!QFileInfo::exists(rolled(u"2026-10-04"_s, 0)));
        QVERIFY(!QFileInfo::exists(rolled(u"2026-10-05"_s, 0)));
        QVERIFY(QFileInfo::exists(rolled(u"2026-10-05"_s, 1)));
        QVERIFY(QFileInfo::exists(rolled(u"2026-10-06"_s, 0)));
    }

    void theNumbersAreComparedAsNumbersWhenTheOldestIsChosen() {
        QVERIFY(QDir().mkpath(m_dir));
        writeFile(rolled(u"2026-10-06"_s, 2), QByteArray(100, 'a'));
        writeFile(rolled(u"2026-10-06"_s, 10), QByteArray(100, 'b'));
        FileLogger logger(clock(), withLimits(5 * 1024 * 1024, 7, 150));

        QVERIFY(logger.open(m_dir));

        QVERIFY2(!QFileInfo::exists(rolled(u"2026-10-06"_s, 2)), "number 2 is older than number 10");
        QVERIFY(QFileInfo::exists(rolled(u"2026-10-06"_s, 10)));
    }

    void aFileThatCannotBeDeletedDoesNotStopTheCleaning() {
        QVERIFY(QDir().mkpath(m_dir));
        writeFile(rolled(u"2026-10-04"_s, 0), QByteArray(100, 'a'));
        writeFile(rolled(u"2026-10-05"_s, 0), QByteArray(100, 'b'));
        writeFile(rolled(u"2026-10-06"_s, 0), QByteArray(100, 'c'));
        const HANDLE held = hold(rolled(u"2026-10-04"_s, 0), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);  // no delete sharing
        QVERIFY(held != INVALID_HANDLE_VALUE);
        FileLogger logger(clock(), withLimits(5 * 1024 * 1024, 7, 150));

        QVERIFY(logger.open(m_dir));  // must not throw or stop at the locked file
        CloseHandle(held);

        QVERIFY(QFileInfo::exists(rolled(u"2026-10-04"_s, 0)));    // could not go
        QVERIFY(!QFileInfo::exists(rolled(u"2026-10-05"_s, 0)));   // the next oldest went instead
        QVERIFY(!QFileInfo::exists(rolled(u"2026-10-06"_s, 0)));
    }

    // ---- Review Focus 1: files and folders that cannot be used ------------------------------

    void aLogFileAnotherProgramHoldsExclusivelyIsNotOpenedAndLoggingIsSilent() {
        QVERIFY(QDir().mkpath(m_dir));
        const HANDLE held = hold(active(), GENERIC_READ | GENERIC_WRITE, 0);  // nobody else may open it
        QVERIFY(held != INVALID_HANDLE_VALUE);
        FileLogger logger(clock());

        QVERIFY(!logger.open(m_dir));
        QVERIFY(!logger.isOpen());
        logger.write(Level::Error, u"t"_s, u"l"_s, u"nobody sees this"_s);  // must not throw
        logger.write(Level::Error, u"t"_s, u"l"_s, u"nor this"_s);
        CloseHandle(held);

        // the opening is tried again a few seconds later, and then the lines go to the file
        m_now = m_now.addSecs(10);
        logger.write(Level::Info, u"t"_s, u"l"_s, u"after the other program let go"_s);
        QVERIFY(logger.isOpen());
        const QString text = readText(active());
        QVERIFY(text.contains(u"after the other program let go"_s));
        QVERIFY(!text.contains(u"nobody sees this"_s));
    }

    void aReadOnlyLogFileIsNotOpenedAndNothingIsThrown() {
        QVERIFY(QDir().mkpath(m_dir));
        writeFile(active(), "an old line\r\n");
        QVERIFY(QFile::setPermissions(active(), QFileDevice::ReadOwner));
        FileLogger logger(clock());

        const bool opened = logger.open(m_dir);
        logger.write(Level::Info, u"t"_s, u"l"_s, u"cannot be written"_s);  // must not throw
        logger.close();
        QFile::setPermissions(active(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        QVERIFY(!opened);
        QCOMPARE(readBytes(active()), QByteArray("an old line\r\n"));
    }

    void aFolderThatCannotBeMadeIsNotOpenedAndNothingIsThrown() {
        QVERIFY(QDir().mkpath(m_temp.path()));
        const QString blocker = m_dir + u"-a-file"_s;
        writeFile(blocker, "not a folder");
        FileLogger logger(clock());

        QVERIFY(!logger.open(blocker + u"/logs"_s));
        logger.write(Level::Info, u"t"_s, u"l"_s, u"nowhere"_s);  // must not throw
        logger.close();

        QCOMPARE(readBytes(blocker), QByteArray("not a folder"));
    }

    void aLogFileThatCannotBeRenamedKeepsTakingLinesAndIsRolledWhenItCan() {
        FileLogger logger(clock(), withLimits(100));
        QVERIFY(logger.open(m_dir));
        logger.write(Level::Info, u"t"_s, u"l"_s, text(1, 70));
        // another program (a viewer without delete sharing) reads the active file: a rename of it fails
        HANDLE held = hold(active(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);
        QVERIFY(held != INVALID_HANDLE_VALUE);

        logger.write(Level::Info, u"t"_s, u"l"_s, text(2, 70));
        logger.write(Level::Info, u"t"_s, u"l"_s, text(3, 70));  // would roll: cannot, goes on in the same file
        logger.write(Level::Info, u"t"_s, u"l"_s, text(4, 70));

        QCOMPARE(logFiles(m_dir), QStringList{u"xgetsongs.log"_s});
        QCOMPARE(linesOf(active()).size(), 4);  // nothing lost
        CloseHandle(held);

        m_now = m_now.addSecs(120);  // the next try is not at once
        logger.write(Level::Info, u"t"_s, u"l"_s, text(5, 70));
        logger.close();

        QVERIFY(QFileInfo::exists(rolled(u"2026-10-07"_s, 0)));
        QStringList all = linesOf(rolled(u"2026-10-07"_s, 0));
        all += linesOf(active());
        QCOMPARE(all.size(), 5);
        for (int i = 0; i < 5; ++i) QVERIFY2(all[i].endsWith(u" - "_s + text(i + 1, 70)), qPrintable(all[i]));
    }

    // ---- many threads -----------------------------------------------------------------------

    void linesOfManyThreadsNeverMix() {
        constexpr int Threads = 8;
        constexpr int PerThread = 400;
        FileLogger logger;
        QVERIFY(logger.open(m_dir));

        std::vector<std::jthread> threads;
        for (int t = 0; t < Threads; ++t) {
            threads.emplace_back([&logger, t] {
                const QString name = u"worker-%1"_s.arg(t);
                for (int i = 0; i < PerThread; ++i) {
                    logger.write(Level::Info, name, u"xgs.many"_s, u"t%1 n%2 "_s.arg(t).arg(i) + QString(200, QChar(u'a' + t)) + u" 한글"_s);
                }
            });
        }
        threads.clear();  // joins
        logger.close();

        const QStringList lines = linesOf(active());
        QCOMPARE(lines.size(), Threads * PerThread);
        std::vector<int> next(Threads, 0);
        for (const QString& line : lines) {
            // "yyyy-MM-dd HH:mm:ss.SSS INFO  [worker-T] xgs.many - tT nN aaaa... 한글"
            const int open = line.indexOf(u'[');
            const int close = line.indexOf(u"] xgs.many - t"_s);
            QVERIFY2(open == 30 && close > open, qPrintable(line.left(80)));
            const int t = line.mid(open + 8, close - open - 8).toInt();  // worker- is 7 characters
            QVERIFY2(t < Threads, qPrintable(line.left(80)));
            const QString message = line.mid(close + 13);
            const QString expected = u"t%1 n%2 "_s.arg(t).arg(next[t]) + QString(200, QChar(u'a' + t)) + u" 한글"_s;
            QCOMPARE(message, expected);  // whole, and the threads' own order is kept
            ++next[t];
        }
    }

    void linesOfManyThreadsAreNotLostOrTornAcrossRolls() {
        constexpr int Threads = 4;
        constexpr int PerThread = 150;
        FileLogger logger({}, withLimits(3'000));
        QVERIFY(logger.open(m_dir));

        std::vector<std::jthread> threads;
        for (int t = 0; t < Threads; ++t) {
            threads.emplace_back([&logger, t] {
                for (int i = 0; i < PerThread; ++i) {
                    logger.write(Level::Warn, u"w%1"_s.arg(t), u"xgs.roll"_s, u"t%1 n%2 end"_s.arg(t).arg(i) + QString(40, u'.'));
                }
            });
        }
        threads.clear();
        logger.close();

        QStringList all;
        for (const QString& name : logFiles(m_dir)) {
            if (name != u"xgetsongs.log"_s) all += linesOf(m_dir + u"/"_s + name);
        }
        all += linesOf(active());
        QVERIFY2(logFiles(m_dir).size() > 3, qPrintable(logFiles(m_dir).join(u'|')));
        QCOMPARE(all.size(), Threads * PerThread);
        QSet<QString> seen;
        for (const QString& line : all) {
            const int at = line.indexOf(u" - t"_s);
            QVERIFY2(at > 0 && line.endsWith(QString(40, u'.')), qPrintable(line));
            QVERIFY2(!seen.contains(line.mid(at)), qPrintable(u"twice: "_s + line));
            seen.insert(line.mid(at));
        }
        QCOMPARE(seen.size(), Threads * PerThread);
    }

    // ---- the crash path (Q5 Task 2: what a crash handler may use) -----------------------------

    void aRawWriteAppendsToTheActiveFileAfterTheLines() {
        FileLogger logger(clock());
        QVERIFY(logger.open(m_dir));
        logger.write(Level::Info, u"main"_s, u"xgs.test"_s, u"before"_s);

        const QByteArray raw("raw line\r\n");
        QVERIFY(logger.writeRaw(raw.constData(), static_cast<unsigned long>(raw.size())));
        logger.write(Level::Info, u"main"_s, u"xgs.test"_s, u"after"_s);
        logger.close();

        QCOMPARE(linesOf(active()),
                 (QStringList{u"2026-10-07 21:03:11.123 INFO  [main] xgs.test - before"_s, u"raw line"_s,
                              u"2026-10-07 21:03:11.123 INFO  [main] xgs.test - after"_s}));
    }

    void aRawWriteWithoutAnOpenFileFailsQuietly() {
        FileLogger never(clock());
        QVERIFY(!never.writeRaw("x\r\n", 3));

        FileLogger closed(clock());
        QVERIFY(closed.open(m_dir));
        closed.close();
        QVERIFY(!closed.writeRaw("x\r\n", 3));
        QCOMPARE(readBytes(active()), QByteArray());
    }

    void aRawWriteGoesOnWhileAnotherThreadHoldsTheLock() {
        // A crash handler runs while any thread may be inside write(): a clock that blocks keeps write() inside the lock.
        std::atomic<bool> block{false};
        std::atomic<bool> inside{false};
        std::atomic<bool> release{false};
        FileLogger logger([&] {
            if (block.load()) {
                inside = true;
                while (!release.load()) std::this_thread::yield();
            }
            return m_now;
        });
        QVERIFY(logger.open(m_dir));
        block = true;
        // A failing check returns from here: the writer is let go first, so its join does not hang the test.
        std::jthread writer([&] { logger.write(Level::Info, u"t"_s, u"xgs.test"_s, u"held"_s); });
        const auto letGo = qScopeGuard([&] { release = true; });   // after the writer: it runs before the join
        while (!inside.load()) std::this_thread::yield();

        QVERIFY(logger.writeRaw("raw\r\n", 5));
        release = true;
        writer.join();
        logger.close();

        QCOMPARE(linesOf(active()), (QStringList{u"raw"_s, u"2026-10-07 21:03:11.123 INFO  [t] xgs.test - held"_s}));
    }

    void theThreadNameComesAsUtf8AndIsCutAtACharacter() {
        // Taken on the threads, compared here (Qt Test's checks belong to the test's thread).
        QByteArray whole;
        QByteArray cut;
        bool terminated = false;
        std::jthread named([&] {
            xgs::diag::setCurrentThreadName(u"작업-1"_s);
            char buffer[64];
            const int size = xgs::diag::currentThreadNameUtf8(buffer, sizeof buffer);
            whole = QByteArray(buffer, size);
            char room[5];   // one Hangul syllable (3 bytes) fits, the next one does not
            const int part = xgs::diag::currentThreadNameUtf8(room, sizeof room);
            cut = QByteArray(room, part);
            terminated = buffer[size] == '\0' && room[part] == '\0';
        });
        named.join();
        QByteArray plain;
        DWORD plainId = 0;
        std::jthread unnamed([&] {
            char buffer[32];
            plain = QByteArray(buffer, xgs::diag::currentThreadNameUtf8(buffer, sizeof buffer));
            plainId = GetCurrentThreadId();
        });
        unnamed.join();

        QCOMPARE(whole, u"작업-1"_s.toUtf8());
        QCOMPARE(cut, u"작"_s.toUtf8());
        QVERIFY(terminated);
        QCOMPARE(QString::fromUtf8(plain), u"t"_s + QString::number(plainId));
    }
};

QTEST_GUILESS_MAIN(FileLoggerTest)
#include "tst_file_logger.moc"
