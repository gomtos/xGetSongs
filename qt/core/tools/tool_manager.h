#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <functional>
#include <stop_token>

#include "model/models.h"
#include "outcome.h"
#include "process/process_runner.h"
#include "tools/tool_locator.h"

// Reports tool versions and installs/updates yt-dlp (the port of engine/.../tools/DefaultToolManager.kt). Installing downloads
// a file from GitHub, so callers must ask the user for consent before calling installYtDlp. The download itself is injected
// (Fetch): this code sends no network request.
namespace xgs::tools {

/// Downloads url. A failure carries the text Kotlin's IOException message would (shown after the Korean part); a stop request
/// should make it return cancelled.
using Fetch = std::function<Outcome<QByteArray>(const QString& url, std::stop_token stop)>;

inline constexpr QStringView YtDlpUrl = u"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";

class ToolManager final {
public:
    /// tools and runner are referenced, not owned: they must outlive the manager. binDir is a result of paths::fromText.
    ToolManager(const ToolPathProvider& tools, process::ProcessRunner& runner, QString binDir, Fetch fetch);

    /// A tool that is missing, cannot be started, exits with a code other than 0 or prints no version is not found. A js
    /// runtime older than Deno 2.3.0 / Node 22.0.0 is not found but keeps its version. A run cancelled by stop counts as a
    /// tool that printed nothing (the caller, who requested the stop, discards the answer).
    ToolsStatus status(std::stop_token stop);

    /// Downloads yt-dlp into `<binDir>\yt-dlp.new.exe`, runs it with --version and only then replaces `<binDir>\yt-dlp.exe`.
    /// Ok with the success text, Failed with the text of Kotlin's ToolException, or Cancelled; `yt-dlp.new.exe` never stays.
    Outcome<QString> installYtDlp(std::stop_token stop);

    /// Runs `yt-dlp --ignore-config -U`: Ok with its last non-blank output line (or a default text), Failed with that line when
    /// it exits with a code other than 0, or Cancelled.
    Outcome<QString> updateYtDlp(std::stop_token stop);

private:
    const ToolPathProvider& m_tools;
    process::ProcessRunner& m_runner;
    QString m_binDir;
    Fetch m_fetch;
};

}  // namespace xgs::tools
