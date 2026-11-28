#include "diag/log_directory.h"

#include <cstdio>

#include "fs/file_ops.h"

namespace xgs::diag {

namespace {

// dir + `\` + name, without doubling a separator at the end of dir (the paths of the app use backslashes).
QString joined(const QString& dir, const QString& name) {
    QString base = dir;
    while (base.endsWith(u'\\') || base.endsWith(u'/')) base.chop(1);
    return base + u'\\' + name;
}

void toStderr(const QString& line) {
    const QByteArray bytes = line.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
    std::fputc('\n', stderr);
}

void say(const Report& report, const QString& line) {
    try {
        if (report) report(line);
        else toStderr(line);
    } catch (...) {
        // A message about a failure must not become a failure.
    }
}

}  // namespace

QStringList logDirectoryCandidates(const QString& appData, const QString& tempDir) {
    return {joined(appData, QStringLiteral("logs")), joined(tempDir, QStringLiteral("xgetsongs-logs"))};
}

bool ensureLogDirectory(const QString& dir, const Report& report) {
    try {
        quint32 code = 0;
        if (fs::makeDirectories(dir, code)) return true;
        say(report, QStringLiteral("로그 폴더를 만들지 못함: ") + dir + QStringLiteral(" (") +
                        (code != 0 ? fs::errorText(code) : QStringLiteral("알 수 없는 오류")) + QStringLiteral(")"));
    } catch (...) {
        say(report, QStringLiteral("로그 폴더를 만들지 못함: ") + dir);
    }
    return false;
}

QString chooseLogDirectory(const QStringList& candidates, const Report& report) {
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        if (ensureLogDirectory(candidates[index], report)) return candidates[index];
        if (index + 1 < candidates.size()) say(report, QStringLiteral("로그를 대신 이 폴더에 씀: ") + candidates[index + 1]);
    }
    return candidates.isEmpty() ? QString() : candidates.last();
}

}  // namespace xgs::diag
