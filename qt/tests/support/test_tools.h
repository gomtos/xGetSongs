#pragma once

#include <QString>

#include <utility>

#include "model/models.h"
#include "tools/tool_locator.h"

// TEST_TOOLS, toolsOf and the tool provider of Fakes.kt.
namespace xgs::testsupport {

inline ToolPaths testTools() {
    return ToolPaths{QStringLiteral("C:\\tools\\yt-dlp.exe"), QStringLiteral("C:\\tools\\ffmpeg.exe"),
                     QStringLiteral("C:\\tools\\node.exe")};
}

/// A ToolPathProvider that always answers paths (Kotlin's `toolsOf(paths)`).
class FixedTools final : public tools::ToolPathProvider {
public:
    explicit FixedTools(ToolPaths paths = testTools()) : m_paths(std::move(paths)) {}
    ToolPaths current() const override { return m_paths; }

private:
    ToolPaths m_paths;
};

inline FixedTools toolsOf(ToolPaths paths = testTools()) { return FixedTools(std::move(paths)); }

}  // namespace xgs::testsupport
