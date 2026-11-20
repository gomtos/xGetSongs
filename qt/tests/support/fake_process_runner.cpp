#include "support/fake_process_runner.h"

#include <utility>

namespace xgs::testsupport {

QStringList kotlinLines(const QString& text) {
    QStringList lines;
    QString line;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\r' || c == u'\n') {
            lines.append(line);
            line.clear();
            if (c == u'\r' && i + 1 < text.size() && text.at(i + 1) == u'\n') ++i;
        } else {
            line += c;
        }
    }
    lines.append(line);
    return lines;
}

FakeProcessRunner::FakeProcessRunner(ProcessHandler handler) : m_handler(std::move(handler)) {}

process::RunResult FakeProcessRunner::run(const QStringList& command, const process::LineHandler& onStdout,
                                          const process::LineHandler& onStderr, std::stop_token stop) {
    if (stop.stop_requested()) return {process::RunStatus::Cancelled};
    {
        const std::lock_guard lock(m_mutex);
        m_commands.append(command);
        ++m_active;
        if (m_active > m_maxActive) m_maxActive = m_active;
    }
    struct Leave {
        FakeProcessRunner& self;
        ~Leave() {
            const std::lock_guard lock(self.m_mutex);
            --self.m_active;
        }
    } leave{*this};

    try {
        const int exitCode = m_handler(command, onStdout, onStderr);
        if (stop.stop_requested()) return {process::RunStatus::Cancelled};
        return {process::RunStatus::Exited, exitCode};
    } catch (const StartFailure& failure) {
        return {process::RunStatus::StartFailed, -1, failure.message};
    } catch (const RunFailure& failure) {
        return {process::RunStatus::Failed, -1, failure.message};
    }
}

QList<QStringList> FakeProcessRunner::commands() const {
    const std::lock_guard lock(m_mutex);
    return m_commands;
}

int FakeProcessRunner::maxActive() const {
    const std::lock_guard lock(m_mutex);
    return m_maxActive;
}

}  // namespace xgs::testsupport
