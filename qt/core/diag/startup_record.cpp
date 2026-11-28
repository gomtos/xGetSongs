#include "diag/startup_record.h"

namespace xgs::diag {

QString startupRecord(const StartupInfo& info) {
    const auto value = [](const std::optional<QString>& text) { return text ? *text : QStringLiteral("?"); };
    QString out;
    out += QStringLiteral("시작 정보\n");
    out += QStringLiteral("  앱: xGetSongs") + (info.appVersion ? QStringLiteral(" ") + *info.appVersion : QString()) + u'\n';
    out += QStringLiteral("  Qt: ") + value(info.qtVersion) + u'\n';
    out += QStringLiteral("  OS: ") + value(info.osName) + u' ' + value(info.osVersion) + u' ' + value(info.osArchitecture) + u'\n';
    out += QStringLiteral("  프로세서 ") + QString::number(info.processors) + QStringLiteral("개\n");
    out += QStringLiteral("  작업 폴더: ") + info.workingDir + u'\n';
    out += QStringLiteral("  로그 폴더: ") + info.logDir;
    if (info.headless) out += QStringLiteral("\n  headless=") + (*info.headless ? QStringLiteral("true") : QStringLiteral("false"));
    return out;
}

}  // namespace xgs::diag
