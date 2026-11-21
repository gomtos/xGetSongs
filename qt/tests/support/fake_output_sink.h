#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <mutex>
#include <utility>

#include "output/output_sink.h"

namespace xgs::testing {

/// An OutputSink whose answers are functions; records every call. Safe from several threads. The Kotlin tests write such sinks
/// as `object : OutputSink`; an exception a Kotlin put throws is a PutResult here (author decision 5 of the Q2d plan).
class FakeOutputSink final : public output::OutputSink {
public:
    using Exists = std::function<bool(const QString& fileName)>;
    using Put = std::function<output::PutResult(const QString& fileName, const QString& source, bool overwrite)>;

    struct PutCall {
        QString fileName;
        QString source;
        bool overwrite = false;
    };

    /// An empty exists answers false; an empty put answers Stored without touching any file.
    explicit FakeOutputSink(Exists exists = {}, Put put = {}) : m_exists(std::move(exists)), m_put(std::move(put)) {}

    bool exists(const QString& fileName) override {
        {
            const std::lock_guard lock(m_mutex);
            m_existsCalls.append(fileName);
        }
        return m_exists ? m_exists(fileName) : false;
    }

    output::PutResult put(const QString& fileName, const QString& source, bool overwrite) override {
        {
            const std::lock_guard lock(m_mutex);
            m_putCalls.append(PutCall{fileName, source, overwrite});
        }
        return m_put ? m_put(fileName, source, overwrite) : output::PutResult{output::PutStatus::Stored, {}, 0};
    }

    QStringList existsCalls() const {
        const std::lock_guard lock(m_mutex);
        return m_existsCalls;
    }

    QList<PutCall> putCalls() const {
        const std::lock_guard lock(m_mutex);
        return m_putCalls;
    }

private:
    Exists m_exists;
    Put m_put;
    mutable std::mutex m_mutex;
    QStringList m_existsCalls;
    QList<PutCall> m_putCalls;
};

}  // namespace xgs::testing
