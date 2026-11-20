#pragma once

#include <QString>

#include <optional>

#include "model/models.h"

// Finds the external tools (the port of engine/.../tools/ToolLocator.kt).
namespace xgs::tools {

class ToolPathProvider {
public:
    virtual ~ToolPathProvider() = default;
    virtual ToolPaths current() const = 0;
};

/// The app's own appBinDir (a result of paths::fromText) wins over PATH so an app-managed yt-dlp is preferred. Re-evaluated on
/// every call so a freshly installed tool is picked up immediately.
class ToolLocator final : public ToolPathProvider {
public:
    /// Without pathEnv the environment variable PATH is read (once, here).
    explicit ToolLocator(QString appBinDir, std::optional<QString> pathEnv = std::nullopt,
                         std::optional<QString> ffmpegOverride = std::nullopt);
    ToolPaths current() const override;

private:
    std::optional<QString> find(const QString& name) const;

    QString m_appBinDir;
    QString m_pathEnv;
    std::optional<QString> m_ffmpegOverride;
};

}  // namespace xgs::tools
