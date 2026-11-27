#include "single_instance.h"

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QStringList>

#include <windows.h>

#include <string>
#include <thread>

using namespace Qt::StringLiterals;

namespace xgs::app {

namespace {

/// The WM_COPYDATA tag of the plugin (WMCOPYDATA_SINGLE_INSTANCE_DATA).
constexpr ULONG_PTR kCopyDataTag = 1542;
/// How long a second copy waits for the first to take the message (the plugin's FORWARD_TIMEOUT_MS).
constexpr UINT kForwardTimeoutMs = 10000;
/// How long a second copy looks for the first copy's window: the first makes it right after the mutex, so this only covers
/// the moment in between. (The plugin does not wait; it would then run as a second copy.)
constexpr int kFindTries = 20;
constexpr auto kFindPause = std::chrono::milliseconds(100);

std::wstring wide(const QString& text) {
    return text.toStdWString();
}

LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COPYDATA) {
        const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        auto* instance = reinterpret_cast<SingleInstance*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (copy && copy->dwData == kCopyDataTag && instance) {
            instance->secondLaunchReported();
            return TRUE;
        }
        return FALSE;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

QString SingleInstance::productId() {
    return u"com.xgetsongs.shell"_s;
}

SingleInstance::SingleInstance(QString id, QObject* parent) : QObject(parent), m_id(std::move(id)) {
    const std::wstring mutexName = wide(m_id + u"-sim"_s);
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    const DWORD error = GetLastError();
    m_mutex = mutex;
    if (mutex != nullptr && error != ERROR_ALREADY_EXISTS) {
        m_first = true;
        const std::wstring className = wide(m_id + u"-sic"_s);
        const std::wstring windowName = wide(m_id + u"-siw"_s);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = messageWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = className.c_str();
        m_classRegistered = RegisterClassExW(&windowClass) != 0;
        const HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW, className.c_str(),
                                            windowName.c_str(), WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window != nullptr) SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        m_window = window;
        return;
    }
    // Another copy runs (or the mutex could not be made at all: then too this copy does not run, as two copies must not).
    tellTheFirst();
}

SingleInstance::~SingleInstance() {
    if (m_window != nullptr) {
        SetWindowLongPtrW(static_cast<HWND>(m_window), GWLP_USERDATA, 0);
        DestroyWindow(static_cast<HWND>(m_window));
    }
    if (m_classRegistered) UnregisterClassW(wide(m_id + u"-sic"_s).c_str(), GetModuleHandleW(nullptr));
    if (m_mutex != nullptr) {
        if (m_first) ReleaseMutex(static_cast<HANDLE>(m_mutex));
        CloseHandle(static_cast<HANDLE>(m_mutex));
    }
}

void SingleInstance::onSecondLaunch(std::function<void()> callback) {
    m_callback = std::move(callback);
}

void SingleInstance::secondLaunchReported() {
    QMetaObject::invokeMethod(
        this,
        [this] {
            const std::function<void()> callback = m_callback;  // a copy: the callback may replace itself
            if (callback) callback();
        },
        Qt::QueuedConnection);
}

void SingleInstance::tellTheFirst() {
    const std::wstring className = wide(m_id + u"-sic"_s);
    const std::wstring windowName = wide(m_id + u"-siw"_s);
    HWND window = nullptr;
    for (int attempt = 0; attempt < kFindTries; ++attempt) {
        window = FindWindowW(className.c_str(), windowName.c_str());
        if (window != nullptr) break;
        std::this_thread::sleep_for(kFindPause);
    }
    if (window == nullptr) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != 0) AllowSetForegroundWindow(pid);
    QStringList parts{QDir::toNativeSeparators(QDir::currentPath())};
    if (QCoreApplication::instance()) parts.append(QCoreApplication::arguments());
    QByteArray data = parts.join(u'|').toUtf8();
    data.append('\0');
    COPYDATASTRUCT copy{kCopyDataTag, DWORD(data.size()), data.data()};
    DWORD_PTR result = 0;
    SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy), SMTO_NORMAL, kForwardTimeoutMs, &result);
}

}  // namespace xgs::app
