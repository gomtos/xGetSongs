#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <limits>
#include <stdexcept>
#include <vector>

#include "diag/run_marker.h"

using namespace Qt::StringLiterals;
using xgs::diag::describePreviousRun;
using xgs::diag::Instant;
using xgs::diag::judgePreviousRun;
using xgs::diag::parseRunMarker;
using xgs::diag::PreviousRun;
using xgs::diag::PreviousRunNone;
using xgs::diag::PreviousRunNote;
using xgs::diag::PreviousRunStillRunning;
using xgs::diag::PreviousRunUnclean;
using xgs::diag::renderExited;
using xgs::diag::renderRunning;
using xgs::diag::RunMarker;
using xgs::diag::RunMarkerFile;
using xgs::diag::RunState;
using xgs::diag::SystemProcesses;

namespace {

struct NotAStdException {};   // as an `Error` of the JVM is not an Exception

Instant instant(const QString& text) {
    const auto parsed = Instant::parse(text);
    return parsed ? *parsed : Instant{};
}

QString windowsPath(const QString& path) {
    QString out = path;
    return out.replace(u'/', u'\\');
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return u"<unreadable>"_s;
    return QString::fromUtf8(file.readAll());
}

QString show(const std::optional<RunMarker>& marker) {
    if (!marker) return u"null"_s;
    return u"RunMarker(%1, pid=%2, started=%3)"_s
        .arg(marker->state == RunState::Running ? u"RUNNING"_s : u"EXITED"_s)
        .arg(marker->pid ? QString::number(*marker->pid) : u"null"_s)
        .arg(marker->started ? marker->started->toString() : u"null"_s);
}

}  // namespace

class RunMarkerTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    const Instant m_started = instant(u"2026-10-07T12:15:40.123Z"_s);
    const RunMarker m_running{RunState::Running, 1234, m_started};

    QString file(const QString& name = u"last-run.txt"_s) const { return m_dir.filePath(name); }

    RunMarkerFile marker(const QString& name = u"last-run.txt"_s) const { return RunMarkerFile(file(name), 1234, m_started); }

    // The marker as text for the checks that compare text: a failed QCOMPARE then shows both.
    void checkParsed(const QString& text, const std::optional<RunMarker>& expected) {
        QCOMPARE(show(parseRunMarker(text)), show(expected));
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void init() {
        // Every test starts with an empty folder.
        QDir dir(m_dir.path());
        for (const QFileInfo& entry : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (entry.isDir()) QDir(entry.absoluteFilePath()).removeRecursively();
            else QFile::remove(entry.absoluteFilePath());
        }
    }

    // ---- parsing and writing the text ------------------------------------------------------

    void aRunningMarkerIsParsed() {
        checkParsed(u"state=running\npid=1234\nstarted=2026-10-07T12:15:40.123Z\n"_s, m_running);
    }

    void anExitedMarkerIsParsedAndTheReasonAndTimeAreOnlyForReaders() {
        const QString text = u"state=exited\npid=1234\nstarted=2026-10-07T12:15:40.123Z\nreason=user\ntime=2026-10-07T12:20:00Z\n"_s;

        checkParsed(text, RunMarker{RunState::Exited, 1234, m_started});
    }

    void anExitedMarkerNeedsNothingButTheState() {
        checkParsed(u"state=exited"_s, RunMarker{RunState::Exited, std::nullopt, std::nullopt});
    }

    void windowsLineEndsSpacesUnknownKeysBlankLinesAndABomAreAllRight() {
        const QString text = u"﻿\r\n state = running \r\n\r\npid= 1234\r\nstarted=2026-10-07T12:15:40.123Z\r\nfuture-key=whatever\r\n# not a pair\r\n"_s;

        checkParsed(text, m_running);
    }

    void theLastValueOfAKeyWins() {
        checkParsed(u"state=exited\nstate=running\npid=1234\nstarted=2026-10-07T12:15:40.123Z"_s, m_running);
    }

    void textThatIsNotAMarkerIsNull() {
        const QStringList texts = {
            u""_s,
            u"   \n\n"_s,
            u"hello world"_s,
            u"state=weird\npid=1\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"state=running"_s,  // nothing to tell the process by
            u"state=running\npid=1234"_s,
            u"state=running\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"state=running\npid=abc\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"state=running\npid=-5\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"state=running\npid=1234\nstarted=yesterday"_s,
            u"pid=1234\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"\x01\x02 binary \xFF"_s,
            QString(QChar(0)) + u"\x01"_s,
        };
        for (const QString& text : texts) {
            QVERIFY2(!parseRunMarker(text), qPrintable(text));
        }
    }

    void absurdValuesMakeTheMarkerUnusableInsteadOfBreakingAnything() {
        const QStringList texts = {
            u"state=running\npid=1234\nstarted=+1000000000-12-31T23:59:59Z"_s,  // does not fit a date
            u"state=running\npid=1234\nstarted=-1000000000-01-01T00:00:00Z"_s,
            u"state=running\npid=1234\nstarted=1970-01-01T00:00:00Z"_s,  // long before any run of this app
            u"state=running\npid=1234\nstarted=3000-01-01T00:00:00Z"_s,
            u"state=running\npid=99999999999999999999\nstarted=2026-10-07T12:15:40.123Z"_s,  // not a Long
            u"state=running\npid=0\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"state=running\npid=-1234\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"state=running\npid=1e3\nstarted=2026-10-07T12:15:40.123Z"_s,
        };
        for (const QString& text : texts) {
            QVERIFY2(!parseRunMarker(text), qPrintable(text));
        }
    }

    void aHugeButValidPidIsKept() {
        const auto parsed = parseRunMarker(u"state=running\npid=%1\nstarted=2026-10-07T12:15:40.123Z"_s.arg(std::numeric_limits<qint64>::max()));

        QCOMPARE(show(parsed), show(RunMarker{RunState::Running, std::numeric_limits<qint64>::max(), m_started}));
    }

    void anExitedMarkerWithAnAbsurdStartTimeJustLacksIt() {
        checkParsed(u"state=exited\npid=1234\nstarted=+1000000000-12-31T23:59:59Z"_s, RunMarker{RunState::Exited, 1234, std::nullopt});
    }

    void theRunningMarkerIsWrittenAsKeyValueLinesAndReadBack() {
        const QString text = renderRunning(1234, m_started);

        QCOMPARE(text, u"state=running\npid=1234\nstarted=2026-10-07T12:15:40.123Z\n"_s);
        checkParsed(text, m_running);
    }

    void theExitedMarkerNamesTheReasonAndTheTime() {
        const Instant time = instant(u"2026-10-07T12:20:00Z"_s);

        QCOMPARE(renderExited(1234, m_started, true, time),
                 u"state=exited\npid=1234\nstarted=2026-10-07T12:15:40.123Z\nreason=user\ntime=2026-10-07T12:20:00Z\n"_s);
        QVERIFY(renderExited(1234, m_started, false, time).contains(u"reason=other\n"_s));
        checkParsed(renderExited(1234, m_started, false, time), RunMarker{RunState::Exited, 1234, m_started});
    }

    // ---- what the marker says about the previous run ---------------------------------------

    void noMarkerMeansNothingToSay() {
        const auto judged = judgePreviousRun(std::nullopt, [](qint64) { return std::optional<Instant>(); });

        QVERIFY(std::holds_alternative<PreviousRunNone>(judged));
    }

    void aCleanExitMeansNothingToSay() {
        const auto noProcess = [](qint64) { return std::optional<Instant>(); };

        QVERIFY(std::holds_alternative<PreviousRunNone>(judgePreviousRun(RunMarker{RunState::Exited, 1234, m_started}, noProcess)));
        QVERIFY(std::holds_alternative<PreviousRunNone>(judgePreviousRun(RunMarker{RunState::Exited, std::nullopt, std::nullopt}, noProcess)));
    }

    void aRunningMarkerWhoseProcessIsGoneIsAnUncleanEnd() {
        std::vector<qint64> asked;

        const auto judged = judgePreviousRun(m_running, [&](qint64 pid) {
            asked.push_back(pid);
            return std::optional<Instant>();
        });

        QVERIFY(judged == PreviousRun(PreviousRunUnclean{1234, m_started}));
        QVERIFY(asked == std::vector<qint64>{1234});
    }

    void aRunningMarkerWhoseProcessLivesWithTheSameStartTimeIsAnotherRunStillGoing() {
        const auto judged = judgePreviousRun(m_running, [&](qint64) { return std::optional<Instant>(m_started); });

        QVERIFY(judged == PreviousRun(PreviousRunStillRunning{1234}));
    }

    void aReusedProcessNumberWithAnotherStartTimeIsAnUncleanEnd() {
        const auto judged = judgePreviousRun(m_running, [&](qint64) { return std::optional<Instant>(m_started.plusSeconds(3'600)); });

        QVERIFY(judged == PreviousRun(PreviousRunUnclean{1234, m_started}));
    }

    void theRealProcessTableRecognisesThisProcessByNumberAndStartTime() {
        const SystemProcesses processes;
        const RunMarker own{RunState::Running, processes.ownPid(), processes.ownStart()};
        const auto startOf = [&](qint64 pid) { return processes.startOf(pid); };

        QVERIFY(processes.startOf(processes.ownPid()) == std::optional<Instant>(processes.ownStart()));
        QVERIFY(judgePreviousRun(own, startOf) == PreviousRun(PreviousRunStillRunning{processes.ownPid()}));
        QVERIFY2(!processes.startOf(std::numeric_limits<qint64>::max()), "no process has that number");
        RunMarker other = own;
        other.started = processes.ownStart().minusSeconds(60);
        // the same number with another start time is another process
        QVERIFY(judgePreviousRun(other, startOf) ==
                PreviousRun(PreviousRunUnclean{processes.ownPid(), processes.ownStart().minusSeconds(60)}));
    }

    void theWordsForTheTwoCases() {
        const auto still = describePreviousRun(PreviousRunStillRunning{1234}, QTimeZone::UTC);
        QVERIFY(still.has_value());
        QCOMPARE(still->warn, false);
        QCOMPARE(still->text, u"다른 실행이 아직 실행 중임 (PID 1234)"_s);

        const auto unclean = describePreviousRun(PreviousRunUnclean{1234, m_started}, QTimeZone::UTC);
        QVERIFY(unclean.has_value());
        QCOMPARE(unclean->warn, true);
        QCOMPARE(unclean->text,
                 u"이전 실행(PID 1234, 시작 2026-10-07 12:15:40)이 정상 종료 기록 없이 끝났음: 강제 종료, 크래시, 전원 차단 등의 가능성"_s);
        QVERIFY(!describePreviousRun(PreviousRunNone{}, QTimeZone::UTC).has_value());
    }

    void anUncleanRunWhoseStartTimeCannotBeShownStillGetsItsWarning() {
        for (const Instant extreme : {Instant::max(), Instant::min()}) {
            const auto note = describePreviousRun(PreviousRunUnclean{7, extreme}, QTimeZone::UTC);

            QVERIFY2(note.has_value() && note->warn && note->text.contains(u"이전 실행(PID 7, 시작 "_s) &&
                         note->text.contains(u"정상 종료 기록 없이 끝났음"_s),
                     note ? qPrintable(note->text) : "no note");
        }
    }

    void theStartTimeIsShownInTheZoneOfTheMachine() {
        // Asia/Seoul (UTC+9, no daylight saving); a fixed offset when the platform does not know the name.
        QTimeZone seoul(QByteArray("Asia/Seoul"));
        if (!seoul.isValid()) seoul = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);

        const auto note = describePreviousRun(PreviousRunUnclean{1, m_started}, seoul);

        QVERIFY(note.has_value());
        QVERIFY2(note->text.contains(u"시작 2026-10-07 21:15:40)"_s), qPrintable(note->text));
    }

    // ---- the file --------------------------------------------------------------------------

    void aMissingFileReadsAsNull() {
        QVERIFY(!marker().read());
    }

    void aFileThatIsNotAMarkerReadsAsNull() {
        writeFile(file(), "garbage");
        QVERIFY(!marker().read());

        writeFile(file(), QByteArray("\x73\x74\xFF\xFE", 4));  // not UTF-8
        QVERIFY(!marker().read());
    }

    void somethingThatCannotBeReadAsAFileReadsAsNull() {
        QVERIFY(QDir(m_dir.path()).mkdir(u"last-run.txt"_s));

        QVERIFY(!marker().read());
    }

    void markingRunningWritesTheMarkerAtomicallyWithoutLeavingATempFile() {
        RunMarkerFile marker = this->marker();

        marker.markRunning();

        QCOMPARE(show(marker.read()), show(m_running));
        QCOMPARE(QDir(m_dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{u"last-run.txt"_s});
    }

    void markingExitedReplacesTheRunningMarker() {
        RunMarkerFile marker = this->marker();
        marker.markRunning();

        marker.markExited(true, instant(u"2026-10-07T12:20:00Z"_s));

        const QString text = readText(file());
        QVERIFY2(text.contains(u"state=exited"_s) && text.contains(u"reason=user"_s) && text.contains(u"time=2026-10-07T12:20:00Z"_s),
                 qPrintable(text));
        QVERIFY(marker.read() && marker.read()->state == RunState::Exited);

        marker.markExited(false);
        QVERIFY(readText(file()).contains(u"reason=other"_s));
    }

    void whenTheMoveOverTheMarkerFailsTheFileIsOverwrittenInPlace() {
        RunMarkerFile marker(file(), 1234, m_started,
                             [](const QString&, const QByteArray&) -> bool { throw std::runtime_error("locked by another process"); });

        marker.markRunning();
        QCOMPARE(show(marker.read()), show(m_running));

        marker.markExited(true, instant(u"2026-10-07T12:20:00Z"_s));
        const QString text = readText(file());
        QVERIFY2(text.contains(u"state=exited"_s) && text.contains(u"reason=user"_s), qPrintable(text));
    }

    void theInPlaceOverwriteIsOnlyTheSecondStrategy() {
        int overwrites = 0;
        RunMarkerFile marker(
            file(), 1234, m_started,
            [](const QString& target, const QByteArray& bytes) {
                QFile out(target);
                return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size();
            },
            [&](const QString&, const QByteArray&) {
                ++overwrites;
                return true;
            });

        marker.markRunning();

        QCOMPARE(overwrites, 0);
        QCOMPARE(show(marker.read()), show(m_running));
    }

    void whenBothWaysOfWritingFailNothingIsThrown() {
        RunMarkerFile marker(
            file(), 1234, m_started, [](const QString&, const QByteArray&) -> bool { throw std::runtime_error("first"); },
            [](const QString&, const QByteArray&) -> bool { throw std::runtime_error("second"); });

        marker.markRunning();       // must not throw
        marker.markExited(false);   // nor this

        QVERIFY(!marker.read());
    }

    void anErrorFromAWritingStrategyIsNotThrownEither() {
        RunMarkerFile marker(file(), 1234, m_started, [](const QString&, const QByteArray&) -> bool { throw NotAStdException{}; });

        marker.markRunning();  // must not throw

        QCOMPARE(show(marker.read()), show(m_running));  // the second strategy still wrote it
    }

    void theFileHoldsOnlyAscii() {
        marker().markRunning();

        QFile in(file());
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray bytes = in.readAll();
        QVERIFY(!bytes.isEmpty());
        for (const char byte : bytes) QVERIFY(static_cast<unsigned char>(byte) <= 127);
    }

    void theFolderIsCreatedWhenItIsMissing() {
        RunMarkerFile marker(windowsPath(m_dir.filePath(u"a/b/last-run.txt"_s)), 1234, m_started);

        marker.markRunning();

        QCOMPARE(show(marker.read()), show(m_running));
    }

    void writingNeverThrowsWhateverGoesWrong() {
        writeFile(file(u"a-file"_s), "not a folder");
        const QString inFile = windowsPath(m_dir.filePath(u"a-file/last-run.txt"_s));
        RunMarkerFile marker(inFile, 1234, m_started);

        marker.markRunning();       // must not throw
        marker.markExited(true);    // nor this

        QVERIFY(!marker.read());
        QVERIFY(!QFileInfo::exists(inFile));
    }
};

QTEST_GUILESS_MAIN(RunMarkerTest)
#include "tst_run_marker.moc"
