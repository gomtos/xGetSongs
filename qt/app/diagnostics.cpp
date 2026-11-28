#include "diagnostics.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QObject>
#include <QSysInfo>

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>

#include "app_services.h"
#include "crash_dump.h"
#include "diag/file_logger.h"
#include "diag/log_directory.h"
#include "diag/run_marker.h"
#include "diag/run_record.h"
#include "diag/startup_record.h"
#include "log/log_redaction.h"

using namespace Qt::StringLiterals;

namespace xgs::app {

namespace {

constexpr DWORD WatchJoinMs = 2'000;   // Kotlin waited 500 ms; a hang dump in progress may take a moment longer
const QString HangLoggerName = QString::fromUtf16(diag::HangWatchdog::LoggerName);

// ---- Qt's messages -------------------------------------------------------------------------------------------------------------

std::atomic<diag::FileLogger*> g_messageLogger{nullptr};
QtMessageHandler g_previousHandler = nullptr;

diag::Level levelOf(QtMsgType type) {
    switch (type) {
        case QtDebugMsg: return diag::Level::Debug;
        case QtInfoMsg: return diag::Level::Info;
        case QtWarningMsg: return diag::Level::Warn;
        case QtCriticalMsg:
        case QtFatalMsg: return diag::Level::Error;
    }
    return diag::Level::Info;
}

/// A message to the log file (its category is the logger's name; DEBUG only for the app's own categories, as logback-sidecar.xml
/// had com.xgetsongs at DEBUG and the rest at INFO), its paths removed, then to the handler that was there before (the debugger's
/// output, or Qt Test's).
void onQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (diag::FileLogger* const logger = g_messageLogger.load()) {
        try {
            const diag::Level level = levelOf(type);
            const QString category = context.category != nullptr ? QString::fromLatin1(context.category) : u"default"_s;
            if (level != diag::Level::Debug || category.startsWith(u"xgs."_s)) {
                logger->write(level, diag::currentThreadName(), category, redaction::redactPaths(message));
            }
        } catch (...) {
            // A message that cannot be written is lost; the app goes on.
        }
    }
    if (g_previousHandler != nullptr) g_previousHandler(type, context, message);
}

/// The Windows messages of the end of the session, from every top-level Qt window.
class SessionFilter final : public QAbstractNativeEventFilter {
public:
    explicit SessionFilter(Diagnostics& diagnostics) : m_diagnostics(diagnostics) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override {
        if (eventType == "windows_generic_MSG" && message != nullptr) {
            const MSG* const msg = static_cast<const MSG*>(message);
            if (msg->message == WM_QUERYENDSESSION || msg->message == WM_ENDSESSION) {
                m_diagnostics.handleWindowsMessage(msg->message, static_cast<quintptr>(msg->wParam));
            }
        }
        return false;   // Qt handles them as before
    }

private:
    Diagnostics& m_diagnostics;
};

/// What the hang watch thread uses. Shared with the thread: if the thread cannot be joined in time (a dump that never ends), it
/// keeps this alive and the GUI-thread object its beats are posted to is left too.
struct Watch {
    std::unique_ptr<diag::HangWatchdog> dog;
    QObject* target = nullptr;
};

}  // namespace

struct Diagnostics::State {
    DiagnosticsOptions options;
    QString logDir;
    diag::FileLogger* logger = nullptr;   // never deleted: a late message of another thread may still reach it (it is closed)
    diag::LogSink sink;
    bool messageHandler = false;
    std::unique_ptr<CrashHandler> crash;
    diag::SystemProcesses processes;
    std::unique_ptr<diag::RunRecord> record;
    std::atomic<bool> exitLogged{false};
    std::atomic<bool> sessionAsked{false};
    std::unique_ptr<SessionFilter> filter;

    std::shared_ptr<Watch> watch;
    std::jthread watchThread;
    std::atomic<unsigned long> watchThreadId{0};
};

std::unique_ptr<Diagnostics> Diagnostics::start(const AppPaths& paths, DiagnosticsOptions options) noexcept {
    std::unique_ptr<Diagnostics> diagnostics;
    try {
        auto state = std::make_unique<State>();
        State& s = *state;
        s.options = std::move(options);
        diagnostics.reset(new Diagnostics(std::move(state)));
        diag::setCurrentThreadName(u"main"_s);

        const QString temp = s.options.tempDir.isEmpty() ? QDir::tempPath() : s.options.tempDir;
        s.logDir = diag::chooseLogDirectory(diag::logDirectoryCandidates(paths.appData, QDir::toNativeSeparators(temp)));
        s.logger = new diag::FileLogger();
        s.logger->open(s.logDir);
        diag::FileLogger* const logger = s.logger;
        s.sink = [logger](diag::Level level, const QString& name, const QString& message) {
            logger->write(level, diag::currentThreadName(), name, message);
        };

        if (s.options.messageHandler) {
            g_messageLogger.store(logger);
            g_previousHandler = qInstallMessageHandler(onQtMessage);
            s.messageHandler = true;
        }
        if (s.options.crashHandler) s.crash = std::make_unique<CrashHandler>(s.logDir, logger);

        diag::StartupInfo info;
        const QString version = QCoreApplication::applicationVersion();
        if (!version.isEmpty()) info.appVersion = version;
        info.qtVersion = QString::fromLatin1(qVersion());
        info.osName = QSysInfo::prettyProductName();
        info.osVersion = QSysInfo::kernelVersion();
        info.osArchitecture = QSysInfo::currentCpuArchitecture();
        info.processors = static_cast<int>(std::thread::hardware_concurrency());
        info.workingDir = QDir::toNativeSeparators(QDir::currentPath());
        info.logDir = s.logDir;
        s.record = std::make_unique<diag::RunRecord>(s.logDir, s.sink, s.processes, diag::startupRecord(info));

        if (QCoreApplication::instance() != nullptr) {
            s.filter = std::make_unique<SessionFilter>(*diagnostics);
            QCoreApplication::instance()->installNativeEventFilter(s.filter.get());
        }

        // The hang watch: a beat posted to the GUI thread once a second (HangWatchdog: only when the last one has run).
        auto watch = std::make_shared<Watch>();
        watch->target = new QObject();
        QObject* const target = watch->target;
        const QString dumpPath = s.logDir + u'\\' + HangDumpFileName;
        const diag::LogSink sink = s.sink;
        watch->dog = std::make_unique<diag::HangWatchdog>(
            [target](std::function<void()> beat) {
                if (!QMetaObject::invokeMethod(target, std::move(beat), Qt::QueuedConnection)) throw std::runtime_error("post");
            },
            [] { return diag::monotonicMillis(); }, sink, s.options.hang);
        watch->dog->setOnHung([sink, dumpPath](const diag::Hung& hung) {
            const QString seconds = QString::number(hung.silentMs / 1'000);
            if (hung.repeat) {
                sink(diag::Level::Error, HangLoggerName, u"화면 스레드가 "_s + seconds + u"초 동안 응답하지 않음 (계속)"_s);
                return;
            }
            // One dump per hang: the first report.
            const bool dumped = writeMiniDump(dumpPath);
            sink(diag::Level::Error, HangLoggerName,
                 u"화면 스레드가 "_s + seconds + u"초 동안 응답하지 않음 ("_s + (dumped ? u"덤프: "_s + HangDumpFileName : u"덤프 없음"_s) + u')');
        });
        watch->dog->setOnRecovered([sink](const diag::Recovered& recovered) {
            sink(diag::Level::Warn, HangLoggerName, u"화면 스레드 응답 회복 (총 "_s + QString::number(recovered.totalMs / 1'000) + u"초)"_s);
        });
        s.watch = watch;
        const auto interval = std::chrono::milliseconds(s.options.hangIntervalMs);
        s.watchThread = std::jthread([watch, interval](std::stop_token stop) {
            diag::setCurrentThreadName(u"xgs-hang-watchdog"_s);
            std::mutex mutex;
            std::condition_variable_any wake;
            while (!stop.stop_requested()) {
                watch->dog->tick();   // never throws
                std::unique_lock lock(mutex);
                wake.wait_for(lock, stop, interval, [] { return false; });
            }
        });
        s.watchThreadId = GetThreadId(s.watchThread.native_handle());
    } catch (...) {
        // What was made so far stays (a log without a hang watch, say); the app runs either way.
    }
    return diagnostics;
}

Diagnostics::Diagnostics(std::unique_ptr<State> state) : m_state(std::move(state)) {}

Diagnostics::~Diagnostics() {
    State& s = *m_state;
    stopWatching();
    if (s.filter && QCoreApplication::instance() != nullptr) QCoreApplication::instance()->removeNativeEventFilter(s.filter.get());
    if (s.messageHandler) {
        qInstallMessageHandler(g_previousHandler);
        diag::FileLogger* expected = s.logger;
        g_messageLogger.compare_exchange_strong(expected, nullptr);
    }
    s.crash.reset();
    s.record.reset();
    if (s.logger != nullptr) s.logger->close();
}

QString Diagnostics::logDir() const {
    return m_state->logDir;
}

diag::LogSink Diagnostics::sink() const {
    return m_state->sink;
}

void Diagnostics::setRunningJobs(std::function<int()> runningJobs) {
    if (m_state->record) m_state->record->setRunningJobs(std::move(runningJobs));
}

void Diagnostics::markUserExit() {
    if (m_state->record) m_state->record->markUserExit();
}

void Diagnostics::stopWatching() noexcept {
    State& s = *m_state;
    if (!s.watchThread.joinable()) return;
    s.watchThread.request_stop();
    if (WaitForSingleObject(s.watchThread.native_handle(), WatchJoinMs) == WAIT_OBJECT_0) {
        s.watchThread.join();
        if (s.watch) {
            delete s.watch->target;   // the thread is gone: no beat is posted any more (a queued one goes with the object)
            s.watch->target = nullptr;
        }
    } else {
        // Stuck in a dump: left to end with the process. It holds the Watch; its target object is left alive for it.
        s.watchThread.detach();
    }
    s.watch.reset();
    s.watchThreadId = 0;
}

bool Diagnostics::watching() const {
    return m_state->watchThread.joinable();
}

unsigned long Diagnostics::watchThreadId() const {
    return m_state->watchThreadId.load();
}

void Diagnostics::logExit() noexcept {
    if (m_state->exitLogged.exchange(true)) return;
    if (m_state->record) m_state->record->logExit();
}

void Diagnostics::handleWindowsMessage(unsigned message, quintptr wParam) noexcept {
    try {
        State& s = *m_state;
        if (message == WM_QUERYENDSESSION) {
            if (!s.sessionAsked.exchange(true) && s.sink) {
                s.sink(diag::Level::Info, diag::DIAGNOSTICS_LOGGER_NAME, u"Windows가 로그오프·종료를 묻음"_s);
            }
        } else if (message == WM_ENDSESSION && wParam != 0) {
            // The session ends: Windows may end the process as soon as this message returns.
            logExit();
        }
    } catch (...) {
    }
}

}  // namespace xgs::app
