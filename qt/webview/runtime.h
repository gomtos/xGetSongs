#pragma once

#include <QString>

#include <optional>

// Whether the WebView2 runtime is there.
namespace xgs::webview {

/// The version of the WebView2 runtime that a window made now would use (GetAvailableCoreWebView2BrowserVersionString, which
/// honours the WEBVIEW2_* environment variables); nothing when there is none. Shows nothing on screen and starts no process.
std::optional<QString> runtimeVersion();

}  // namespace xgs::webview
