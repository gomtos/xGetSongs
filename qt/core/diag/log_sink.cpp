#include "diag/log_sink.h"

#include <windows.h>

#include <typeinfo>

namespace xgs::diag {

QString levelName(Level level) {
    switch (level) {
        case Level::Debug: return QStringLiteral("DEBUG");
        case Level::Info: return QStringLiteral("INFO");
        case Level::Warn: return QStringLiteral("WARN");
        case Level::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString exceptionName(const std::exception& error) {
    QString name = QString::fromLatin1(typeid(error).name());   // MSVC: `class std::runtime_error`
    const qsizetype space = name.lastIndexOf(u' ');
    if (space >= 0) name = name.mid(space + 1);
    const qsizetype colons = name.lastIndexOf(QStringLiteral("::"));
    if (colons >= 0) name = name.mid(colons + 2);
    return name;
}

QString currentExceptionName() {
    try {
        throw;
    } catch (const std::exception& error) {
        return exceptionName(error);
    } catch (...) {
        return QStringLiteral("알 수 없는 예외");
    }
}

namespace {

QString& threadNameSlot() {
    thread_local QString name;
    return name;
}

}  // namespace

QString currentThreadName() {
    const QString& name = threadNameSlot();
    return name.isEmpty() ? QStringLiteral("t") + QString::number(GetCurrentThreadId()) : name;
}

void setCurrentThreadName(const QString& name) {
    threadNameSlot() = name;
}

}  // namespace xgs::diag
