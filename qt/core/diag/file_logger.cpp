#include "diag/file_logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#include "fs/file_ops.h"

namespace xgs::diag {

namespace {

const QString ActiveFileName = QStringLiteral("xgetsongs.log");
constexpr qint64 RetryOpenAfterMs = 5'000;     // a log file that could not be opened is tried again this often
constexpr qint64 RetryRollAfterMs = 60'000;    // so is a roll that failed (another program holds the file)

QString two(int value) {
    return QString::number(value).rightJustified(2, u'0');
}

QString dateText(const QDate& date) {
    return QString::number(date.year()).rightJustified(4, u'0') + u'-' + two(date.month()) + u'-' + two(date.day());
}

const wchar_t* wide(const QString& native) {
    return reinterpret_cast<const wchar_t*>(native.utf16());
}

QString joined(const QString& dir, const QString& name) {
    QString base = dir;
    while (base.endsWith(u'\\') || base.endsWith(u'/')) base.chop(1);
    return QDir::toNativeSeparators(base + u'\\' + name);
}

bool asciiDigits(QStringView text, int& value) {
    if (text.isEmpty() || text.size() > 9) return false;
    value = 0;
    for (const QChar c : text) {
        if (c.unicode() < u'0' || c.unicode() > u'9') return false;
        value = value * 10 + (c.unicode() - u'0');
    }
    return true;
}

// One rolled file: `xgetsongs.<yyyy-MM-dd>.<N>.log`.
struct Rolled {
    QString path;
    QDate date;
    int number = 0;
    qint64 size = 0;
};

std::optional<Rolled> parseRolledName(const QString& dir, const QString& name) {
    static const QString prefix = QStringLiteral("xgetsongs.");
    static const QString suffix = QStringLiteral(".log");
    if (!name.startsWith(prefix) || !name.endsWith(suffix)) return std::nullopt;
    const QString middle = name.mid(prefix.size(), name.size() - prefix.size() - suffix.size());   // yyyy-MM-dd.N
    if (middle.size() < 12 || middle[4] != u'-' || middle[7] != u'-' || middle[10] != u'.') return std::nullopt;
    int year = 0;
    int month = 0;
    int day = 0;
    int number = 0;
    if (!asciiDigits(QStringView(middle).mid(0, 4), year) || !asciiDigits(QStringView(middle).mid(5, 2), month) ||
        !asciiDigits(QStringView(middle).mid(8, 2), day) || !asciiDigits(QStringView(middle).mid(11), number)) {
        return std::nullopt;
    }
    const QDate date(year, month, day);
    if (!date.isValid()) return std::nullopt;
    Rolled rolled;
    rolled.path = joined(dir, name);
    rolled.date = date;
    rolled.number = number;
    return rolled;
}

std::vector<Rolled> rolledFiles(const QString& dir) {
    std::vector<Rolled> files;
    const QStringList names = QDir(dir).entryList({QStringLiteral("xgetsongs.*.log")}, QDir::Files | QDir::NoSymLinks);
    for (const QString& name : names) {
        if (auto rolled = parseRolledName(dir, name)) {
            rolled->size = QFileInfo(rolled->path).size();
            files.push_back(*rolled);
        }
    }
    return files;
}

}  // namespace

struct FileLogger::Impl {
    Clock clock;
    FileLoggerOptions options;
    std::mutex mutex;
    QString dir;
    bool dirSet = false;
    QString activePath;
    HANDLE handle = INVALID_HANDLE_VALUE;
    // handle, for writeRaw without the lock: cleared before the handle is closed, set once it is open.
    std::atomic<HANDLE> rawHandle{INVALID_HANDLE_VALUE};
    qint64 size = 0;                   // the length of the active file
    QDate periodDate;                  // the date of the lines in the active file
    qint64 nextOpenAttemptMs = 0;      // wall-clock milliseconds
    qint64 nextRollAttemptMs = 0;

    QDateTime now() const { return clock ? clock() : QDateTime::currentDateTime(); }

    void closeHandle() {
        rawHandle.store(INVALID_HANDLE_VALUE);
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }

    // Opens the active file for appending: others may read it, write it and delete it meanwhile (a viewer must not block the
    // log, and the user may clean the folder).
    bool openActive() {
        closeHandle();
        handle = CreateFileW(wide(activePath), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER length{};
        size = GetFileSizeEx(handle, &length) ? length.QuadPart : 0;
        rawHandle.store(handle);
        return true;
    }

    // The date of the lines that are in the active file when the log is (re)opened: the day it was last written.
    void startPeriod(const QDate& today) {
        periodDate = today;
        if (size > 0) {
            const QDate written = QFileInfo(activePath).lastModified().date();
            if (written.isValid() && written < today) periodDate = written;
        }
    }

    // Deletes the rolled files older than maxHistoryDays and then the oldest ones while they are over the cap together. A file
    // that cannot be deleted stays (and counts); the next one is tried.
    void prune(const QDate& today) {
        std::vector<Rolled> files = rolledFiles(dir);
        const QDate oldest = today.addDays(-options.maxHistoryDays);
        std::vector<Rolled> kept;
        for (const Rolled& file : files) {
            if (file.date < oldest && QFile::remove(file.path)) continue;
            kept.push_back(file);
        }
        std::sort(kept.begin(), kept.end(), [](const Rolled& a, const Rolled& b) {
            return a.date != b.date ? a.date < b.date : a.number < b.number;
        });
        qint64 total = 0;
        for (const Rolled& file : kept) total += file.size;
        for (const Rolled& file : kept) {
            if (total <= options.totalSizeCapBytes) break;
            if (QFile::remove(file.path)) total -= file.size;
        }
    }

    int nextNumber(const QDate& date) const {
        int next = 0;
        for (const Rolled& file : rolledFiles(dir)) {
            if (file.date == date) next = std::max(next, file.number + 1);
        }
        return next;
    }

    // Renames the active file to the first free `xgetsongs.<date>.<N>.log` (never onto a file that exists) and starts a new one.
    // false when it could not be renamed: the active file is then opened again and goes on.
    bool roll(const QDate& date) {
        closeHandle();
        bool renamed = false;
        int number = nextNumber(date);
        for (int attempt = 0; attempt < 100 && !renamed; ++attempt, ++number) {
            const QString target = joined(dir, QStringLiteral("xgetsongs.") + dateText(date) + u'.' + QString::number(number) + QStringLiteral(".log"));
            if (MoveFileExW(wide(activePath), wide(target), 0)) {
                renamed = true;
            } else {
                const DWORD error = GetLastError();
                if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) break;   // anything else: not by trying the next number
            }
        }
        openActive();
        return renamed;
    }

    void rollIfNeeded(const QDateTime& time) {
        const QDate today = time.date();
        const bool dayChanged = periodDate.isValid() && today > periodDate;
        const bool full = size >= options.maxFileBytes;
        if (!((dayChanged && size > 0) || full)) {
            if (dayChanged) periodDate = today;   // nothing in the file from the earlier day
            return;
        }
        if (time.toMSecsSinceEpoch() < nextRollAttemptMs) return;
        if (roll(periodDate.isValid() ? periodDate : today)) {
            periodDate = today;
            prune(today);
        } else {
            nextRollAttemptMs = time.toMSecsSinceEpoch() + RetryRollAfterMs;
        }
    }
};

FileLogger::FileLogger(Clock clock, FileLoggerOptions options) : m_impl(std::make_unique<Impl>()) {
    m_impl->clock = std::move(clock);
    m_impl->options = options;
}

FileLogger::~FileLogger() {
    close();
}

bool FileLogger::open(const QString& dir) {
    try {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        Impl& s = *m_impl;
        s.closeHandle();
        s.dir = dir;
        s.dirSet = true;
        s.activePath = joined(dir, ActiveFileName);
        const QDateTime now = s.now();
        fs::makeDirectories(dir);
        if (!s.openActive()) {
            s.nextOpenAttemptMs = now.toMSecsSinceEpoch() + RetryOpenAfterMs;
            return false;
        }
        s.startPeriod(now.date());
        s.prune(now.date());
        return true;
    } catch (...) {
        return false;
    }
}

QString FileLogger::formatLine(const QDateTime& time, Level level, const QString& threadName, const QString& loggerName, const QString& message) {
    const QDate date = time.date();
    const QTime clock = time.time();
    return QString::number(date.year()).rightJustified(4, u'0') + u'-' + two(date.month()) + u'-' + two(date.day()) + u' ' + two(clock.hour()) +
           u':' + two(clock.minute()) + u':' + two(clock.second()) + u'.' + QString::number(clock.msec()).rightJustified(3, u'0') + u' ' +
           levelName(level).leftJustified(5, u' ') + QStringLiteral(" [") + threadName + QStringLiteral("] ") + loggerName + QStringLiteral(" - ") + message;
}

void FileLogger::write(Level level, const QString& threadName, const QString& loggerName, const QString& message) noexcept {
    try {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        Impl& s = *m_impl;
        if (!s.dirSet) return;
        const QDateTime now = s.now();
        const qint64 nowMs = now.toMSecsSinceEpoch();
        if (s.handle == INVALID_HANDLE_VALUE) {
            if (nowMs < s.nextOpenAttemptMs) return;
            fs::makeDirectories(s.dir);
            if (!s.openActive()) {
                s.nextOpenAttemptMs = nowMs + RetryOpenAfterMs;
                return;
            }
            s.startPeriod(now.date());
            s.prune(now.date());
        }
        s.rollIfNeeded(now);
        if (s.handle == INVALID_HANDLE_VALUE) {
            s.nextOpenAttemptMs = nowMs + RetryOpenAfterMs;
            return;
        }
        const QByteArray bytes = (formatLine(now, level, threadName, loggerName, message) + QStringLiteral("\r\n")).toUtf8();
        DWORD written = 0;
        if (WriteFile(s.handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == static_cast<DWORD>(bytes.size())) {
            s.size += written;
        } else {
            s.size += written;
            s.closeHandle();    // opened again by a later write
            s.nextOpenAttemptMs = nowMs + RetryOpenAfterMs;
        }
    } catch (...) {
        // Logging never throws: a line that cannot be written is lost.
    }
}

bool FileLogger::writeRaw(const char* bytes, unsigned long size) const noexcept {
    const HANDLE handle = m_impl->rawHandle.load();
    if (handle == INVALID_HANDLE_VALUE || bytes == nullptr) return false;
    DWORD written = 0;
    return WriteFile(handle, bytes, size, &written, nullptr) && written == size;
}

void FileLogger::close() noexcept {
    try {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->closeHandle();
        m_impl->dirSet = false;
    } catch (...) {
    }
}

bool FileLogger::isOpen() const {
    try {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->handle != INVALID_HANDLE_VALUE;
    } catch (...) {
        return false;
    }
}

}  // namespace xgs::diag
