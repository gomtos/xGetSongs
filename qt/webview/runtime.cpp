#include "webview/runtime.h"

#include <windows.h>

#include <WebView2.h>

namespace xgs::webview {

std::optional<QString> runtimeVersion() {
    LPWSTR version = nullptr;
    const HRESULT result = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    std::optional<QString> found;
    if (SUCCEEDED(result) && version && *version) found = QString::fromWCharArray(version);
    if (version) CoTaskMemFree(version);
    return found;
}

}  // namespace xgs::webview
