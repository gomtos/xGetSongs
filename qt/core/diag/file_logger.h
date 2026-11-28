#pragma once

#include <QDateTime>
#include <QString>

#include <functional>
#include <memory>

#include "diag/log_sink.h"

// The log file of the app, in the shape logback-sidecar.xml gives the sidecar's: the active file is always `xgetsongs.log`;
// a file that is full (5 MB) or that holds yesterday's lines is renamed `xgetsongs.<yyyy-MM-dd>.<N>.log` (the date of the lines
// in it, N counting up from 0 for the files of one date, and never the name of a file that exists); rolled files older than
// 7 days go, and the oldest go while they are together over 50 MB. A line is
//   `yyyy-MM-dd HH:mm:ss.SSS LEVEL [thread] logger - message` + CRLF (logback's `%n` on Windows), UTF-8, written to the file at
// once (no buffer of ours: a crash or a kill does not lose the last lines). Every failure (a folder or file that cannot be
// opened, a file another program holds, a rename or a delete that fails) is swallowed: logging goes on in the active file or,
// when that cannot be opened at all, silently does nothing and tries again every few seconds.
namespace xgs::diag {

struct FileLoggerOptions {
    qint64 maxFileBytes = 5LL * 1024 * 1024;
    int maxHistoryDays = 7;
    qint64 totalSizeCapBytes = 50LL * 1024 * 1024;
};

class FileLogger {
public:
    /// Wall-clock time (local), asked once per write. Empty: QDateTime::currentDateTime.
    using Clock = std::function<QDateTime()>;

    explicit FileLogger(Clock clock = {}, FileLoggerOptions options = {});
    ~FileLogger();
    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    /// Makes the folder if needed, opens (or creates) `xgetsongs.log` in it for appending and tidies the rolled files. false
    /// when the file could not be opened; writes are then dropped (and the opening is tried again every 5 seconds).
    bool open(const QString& dir);

    /// Writes one line. Any thread; the whole line is written under a lock, so lines of different threads never mix. Never
    /// throws.
    void write(Level level, const QString& threadName, const QString& loggerName, const QString& message) noexcept;

    /// Appends bytes (a whole line, UTF-8 with its line end) to the active file with one WriteFile, without the lock, without
    /// rolling and without touching the heap: for a crash handler, whose thread may hold the lock. false when no file is open or
    /// the write fails. Any thread; a line of write() and this one do not mix (both are appends of one WriteFile).
    bool writeRaw(const char* bytes, unsigned long size) const noexcept;
    /// Closes the file; later writes are dropped. Never throws.
    void close() noexcept;

    bool isOpen() const;

    /// The line as it is written, without the line end (a test and the console use it).
    static QString formatLine(const QDateTime& time, Level level, const QString& threadName, const QString& loggerName,
                              const QString& message);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace xgs::diag
