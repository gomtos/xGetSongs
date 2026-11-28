#pragma once

#include <QString>
#include <QStringList>

#include <mutex>
#include <vector>

#include "diag/log_sink.h"

// A LogSink that keeps what it is given (Kotlin's LogCapture of the diagnostics tests), for the tests of the diag logic.
namespace xgs::diag::test {

struct LogRecord {
    Level level;
    QString logger;
    QString message;
};

class LogCapture {
public:
    xgs::diag::LogSink sink() {
        return [this](Level level, const QString& logger, const QString& message) {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_records.push_back({level, logger, message});
        };
    }

    void clear() {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_records.clear();
    }

    std::vector<LogRecord> events() const {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_records;
    }

    // The records of one logger (every record when logger is empty).
    std::vector<LogRecord> events(const QString& logger) const {
        std::vector<LogRecord> out;
        for (const LogRecord& record : events()) {
            if (logger.isEmpty() || record.logger == logger) out.push_back(record);
        }
        return out;
    }

    std::vector<LogRecord> at(Level level, const QString& logger = QString()) const {
        std::vector<LogRecord> out;
        for (const LogRecord& record : events(logger)) {
            if (record.level == level) out.push_back(record);
        }
        return out;
    }

    QStringList messages(Level level, const QString& logger = QString()) const {
        QStringList out;
        for (const LogRecord& record : at(level, logger)) out << record.message;
        return out;
    }

    QStringList allMessages(const QString& logger = QString()) const {
        QStringList out;
        for (const LogRecord& record : events(logger)) out << record.message;
        return out;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<LogRecord> m_records;
};

}  // namespace xgs::diag::test
