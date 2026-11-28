#pragma once

#include <QString>

#include <optional>

// The startup record: one block of text, so it is one log record. Port of Diagnostics.kt's startupRecord with the Java lines
// replaced (the author's choice, Q5 Task 1): `Qt: <version>`, `OS: <name> <version> <architecture>` and `프로세서 N개`
// (no heap figure: there is no JVM heap).
namespace xgs::diag {

struct StartupInfo {
    std::optional<QString> appVersion;     // left out of the `앱:` line when absent
    std::optional<QString> qtVersion;      // `?` when absent, like the other values
    std::optional<QString> osName;
    std::optional<QString> osVersion;
    std::optional<QString> osArchitecture;
    int processors = 0;
    std::optional<bool> headless;          // the `headless=` line is left out when absent
    QString workingDir;
    QString logDir;
};

QString startupRecord(const StartupInfo& info);

}  // namespace xgs::diag
