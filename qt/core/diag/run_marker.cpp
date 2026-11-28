#include "diag/run_marker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QStringDecoder>

#include <windows.h>

#include <algorithm>
#include <limits>
#include <type_traits>

#include "fs/file_ops.h"
#include "text/kt_text.h"

namespace xgs::diag {

namespace {

// Kotlin's String.toLongOrNull(): an optional `-` or `+`, then one or more decimal digits (any script's, as Character.digit
// takes them); nullopt for anything else and for a value that does not fit a Long.
std::optional<qint64> toLongOrNull(const QString& s) {
    if (s.isEmpty()) return std::nullopt;
    qsizetype start = 0;
    bool negative = false;
    qint64 limit = -std::numeric_limits<qint64>::max();
    if (s[0].unicode() < u'0') {
        if (s.size() == 1) return std::nullopt;
        start = 1;
        if (s[0] == u'-') {
            negative = true;
            limit = std::numeric_limits<qint64>::min();
        } else if (s[0] != u'+') {
            return std::nullopt;
        }
    }
    const qint64 limitBeforeMul = limit / 10;
    qint64 result = 0;
    for (qsizetype i = start; i < s.size(); ++i) {
        const int digit = kt::digitValue(s[i]);
        if (digit < 0) return std::nullopt;
        if (result < limitBeforeMul) return std::nullopt;
        result *= 10;
        if (result < limit + digit) return std::nullopt;
        result -= digit;
    }
    return negative ? result : -result;
}

// A marker this app wrote holds a time from the last decades; anything else was edited by hand or is damaged. (A time near the
// ends of what an Instant can hold would not even fit a date: it is a hand-edited file, not a run.)
const Instant& earliestStart() {
    static const Instant value = *Instant::parse(QStringLiteral("2000-01-01T00:00:00Z"));
    return value;
}

const Instant& latestStart() {
    static const Instant value = *Instant::parse(QStringLiteral("3000-01-01T00:00:00Z"));
    return value;
}

std::optional<Instant> parseStart(const QString& text) {
    const std::optional<Instant> parsed = Instant::parse(text);
    if (parsed && *parsed >= earliestStart() && *parsed < latestStart()) return parsed;
    return std::nullopt;
}

// Lines split at `\r\n`, `\n` or `\r`, as Kotlin's String.lines().
QStringList splitLines(const QString& text) {
    QStringList lines;
    qsizetype from = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == u'\n' || text[i] == u'\r') {
            lines << text.mid(from, i - from);
            if (text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') ++i;
            from = i + 1;
        }
    }
    lines << text.mid(from);
    return lines;
}

QString two(int value) {
    return QString::number(value).rightJustified(2, u'0');
}

// yyyy-MM-dd HH:mm:ss in zone; the plain instant when it does not fit a date (a marker that was edited by hand). Written by hand:
// the format of a QDateTime follows the digits of the system's language.
QString showTime(const Instant& instant, const QTimeZone& zone) {
    constexpr qint64 FirstOfYear1 = -62'135'596'800LL;       // 0001-01-01T00:00:00Z
    constexpr qint64 LastOfYear9999 = 253'402'300'799LL;     // 9999-12-31T23:59:59Z
    if (instant.epochSecond < FirstOfYear1 + 86'400 || instant.epochSecond > LastOfYear9999 - 86'400) return instant.toString();
    const QDateTime time = QDateTime::fromSecsSinceEpoch(instant.epochSecond, zone);
    if (!time.isValid()) return instant.toString();
    const QDate date = time.date();
    const QTime clock = time.time();
    return QString::number(date.year()).rightJustified(4, u'0') + u'-' + two(date.month()) + u'-' + two(date.day()) + u' ' + two(clock.hour()) +
           u':' + two(clock.minute()) + u':' + two(clock.second());
}

bool writeTempThenMove(const QString& file, const QByteArray& bytes) {
    const qsizetype slash = std::max(file.lastIndexOf(u'\\'), file.lastIndexOf(u'/'));
    if (slash > 0) fs::makeDirectories(file.left(slash));
    const QString temp = file + QStringLiteral(".tmp");
    const auto written = fs::writeBytes(temp, bytes);
    if (!written.isOk()) {
        fs::removeQuietly(temp);
        return false;
    }
    const QString nativeFile = QDir::toNativeSeparators(file);
    const QString nativeTemp = QDir::toNativeSeparators(temp);
    const bool moved = MoveFileExW(reinterpret_cast<const wchar_t*>(nativeTemp.utf16()), reinterpret_cast<const wchar_t*>(nativeFile.utf16()),
                                   MOVEFILE_REPLACE_EXISTING) != 0;
    if (!moved) fs::removeQuietly(temp);   // a leftover temp file would be overwritten by the next write anyway
    return moved;
}

bool overwriteInPlace(const QString& file, const QByteArray& bytes) {
    const qsizetype slash = std::max(file.lastIndexOf(u'\\'), file.lastIndexOf(u'/'));
    if (slash > 0) fs::makeDirectories(file.left(slash));
    return fs::writeBytes(file, bytes).isOk();
}

}  // namespace

// ---- the process table --------------------------------------------------------------------------------------------------

namespace {

std::optional<Instant> creationTime(HANDLE process) {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return std::nullopt;
    const quint64 ticks = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return Instant::fromFileTime(ticks);
}

}  // namespace

qint64 SystemProcesses::ownPid() const {
    return static_cast<qint64>(GetCurrentProcessId());
}

Instant SystemProcesses::ownStart() const {
    static const Instant start = creationTime(GetCurrentProcess()).value_or(Instant::now());
    return start;
}

std::optional<Instant> SystemProcesses::startOf(qint64 pid) const {
    if (pid <= 0 || pid > static_cast<qint64>(std::numeric_limits<DWORD>::max())) return std::nullopt;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) return std::nullopt;
    std::optional<Instant> start;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE) start = creationTime(process);
    CloseHandle(process);
    return start;
}

// ---- the text ------------------------------------------------------------------------------------------------------------

std::optional<RunMarker> parseRunMarker(const QString& text) {
    QHash<QString, QString> values;
    const QString body = text.startsWith(QChar(0xFEFF)) ? text.mid(1) : text;
    for (const QString& line : splitLines(body)) {
        const qsizetype at = line.indexOf(u'=');
        if (at > 0) values.insert(kt::trim(line.left(at)), kt::trim(line.mid(at + 1)));
    }
    std::optional<qint64> pid;
    if (const auto it = values.constFind(QStringLiteral("pid")); it != values.constEnd()) {
        const std::optional<qint64> parsed = toLongOrNull(*it);
        if (parsed && *parsed > 0) pid = parsed;
    }
    std::optional<Instant> started;
    if (const auto it = values.constFind(QStringLiteral("started")); it != values.constEnd()) started = parseStart(*it);

    const auto state = values.constFind(QStringLiteral("state"));
    if (state == values.constEnd()) return std::nullopt;
    if (*state == QStringLiteral("running")) {
        if (pid && started) return RunMarker{RunState::Running, pid, started};
        return std::nullopt;
    }
    if (*state == QStringLiteral("exited")) return RunMarker{RunState::Exited, pid, started};
    return std::nullopt;
}

QString renderRunning(qint64 pid, const Instant& started) {
    return QStringLiteral("state=running\npid=") + QString::number(pid) + QStringLiteral("\nstarted=") + started.toString() + u'\n';
}

QString renderExited(qint64 pid, const Instant& started, bool userRequested, const Instant& time) {
    return QStringLiteral("state=exited\npid=") + QString::number(pid) + QStringLiteral("\nstarted=") + started.toString() +
           QStringLiteral("\nreason=") + (userRequested ? QStringLiteral("user") : QStringLiteral("other")) + QStringLiteral("\ntime=") +
           time.toString() + u'\n';
}

PreviousRun judgePreviousRun(const std::optional<RunMarker>& previous, const std::function<std::optional<Instant>(qint64)>& startOf) {
    if (!previous || previous->state != RunState::Running) return PreviousRunNone{};
    if (!previous->pid || !previous->started) return PreviousRunNone{};
    const qint64 pid = *previous->pid;
    const Instant started = *previous->started;
    if (startOf(pid) == std::optional<Instant>(started)) return PreviousRunStillRunning{pid};
    return PreviousRunUnclean{pid, started};
}

std::optional<PreviousRunNote> describePreviousRun(const PreviousRun& run, const QTimeZone& zone) {
    if (const auto* still = std::get_if<PreviousRunStillRunning>(&run)) {
        return PreviousRunNote{false, QStringLiteral("다른 실행이 아직 실행 중임 (PID ") + QString::number(still->pid) + QStringLiteral(")")};
    }
    if (const auto* unclean = std::get_if<PreviousRunUnclean>(&run)) {
        return PreviousRunNote{true, QStringLiteral("이전 실행(PID ") + QString::number(unclean->pid) + QStringLiteral(", 시작 ") +
                                         showTime(unclean->started, zone) +
                                         QStringLiteral(")이 정상 종료 기록 없이 끝났음: 강제 종료, 크래시, 전원 차단 등의 가능성")};
    }
    return std::nullopt;
}

// ---- the file ------------------------------------------------------------------------------------------------------------

RunMarkerFile::RunMarkerFile(QString file, qint64 pid, Instant started, Writer replace, Writer overwrite)
    : m_file(std::move(file)),
      m_pid(pid),
      m_started(started),
      m_replace(replace ? std::move(replace) : Writer(writeTempThenMove)),
      m_overwrite(overwrite ? std::move(overwrite) : Writer(overwriteInPlace)) {}

std::optional<RunMarker> RunMarkerFile::read() const {
    try {
        QFile file(m_file);
        if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
        const QByteArray bytes = file.readAll();
        QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        const QString text = decoder(bytes);
        if (decoder.hasError()) return std::nullopt;
        return parseRunMarker(text);
    } catch (...) {
        return std::nullopt;
    }
}

void RunMarkerFile::markRunning() noexcept {
    try {
        write(renderRunning(m_pid, m_started));
    } catch (...) {
    }
}

void RunMarkerFile::markExited(bool userRequested) noexcept {
    markExited(userRequested, Instant::now());
}

void RunMarkerFile::markExited(bool userRequested, const Instant& time) noexcept {
    try {
        write(renderExited(m_pid, m_started, userRequested, time));
    } catch (...) {
    }
}

// Whatever fails (also something that is not an exception): the exit record has nobody to report to, and the app must not
// suffer from a marker.
void RunMarkerFile::write(const QString& text) noexcept {
    try {
        const QByteArray bytes = text.toUtf8();
        try {
            if (m_replace(m_file, bytes)) return;
        } catch (...) {
            // The second way.
        }
        try {
            m_overwrite(m_file, bytes);
        } catch (...) {
            // The next run learns nothing from this one; nothing else suffers.
        }
    } catch (...) {
    }
}

}  // namespace xgs::diag
