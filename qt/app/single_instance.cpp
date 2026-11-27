#include "single_instance.h"

#include <QCoreApplication>
#include <QDir>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QStringList>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <string>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcSingle, "xgs.single")

namespace xgs::app {

namespace {

/// The WM_COPYDATA tag of the plugin (WMCOPYDATA_SINGLE_INSTANCE_DATA).
constexpr ULONG_PTR kCopyDataTag = 1542;
/// How long a second copy waits for the first to take the message (the plugin's FORWARD_TIMEOUT_MS), and how long it waits for
/// a first copy that does not listen to end.
constexpr UINT kForwardTimeoutMs = 10000;
constexpr auto kWaitLimit = std::chrono::milliseconds(10000);
/// How often a copy that waits on the mutex looks for the first copy's window again (the first makes it right after the mutex).
constexpr DWORD kLookAgainMs = 100;

std::wstring wide(const QString& text) {
    return text.toStdWString();
}

LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COPYDATA) {
        const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        auto* instance = reinterpret_cast<SingleInstance*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (copy && copy->dwData == kCopyDataTag && instance && instance->secondLaunchReported()) return TRUE;
        return FALSE;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool acquired(DWORD wait) {
    return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
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
    if (mutex == nullptr) {
        // Without the mutex this copy cannot know whether another runs; two copies must not, so it does not run.
        qCWarning(lcSingle) << "단일 인스턴스 뮤텍스를 만들지 못함, 오류" << error;
        return;
    }
    if (error != ERROR_ALREADY_EXISTS) {
        becomeFirst();
        return;
    }
    tellTheFirst();
}

SingleInstance::~SingleInstance() {
    stopListening();
    if (m_mutex != nullptr) {
        if (m_first) ReleaseMutex(static_cast<HANDLE>(m_mutex));
        CloseHandle(static_cast<HANDLE>(m_mutex));
    }
}

void SingleInstance::becomeFirst() {
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
}

void SingleInstance::stopListening() {
    m_callback = {};
    if (m_window != nullptr) {
        SetWindowLongPtrW(static_cast<HWND>(m_window), GWLP_USERDATA, 0);
        DestroyWindow(static_cast<HWND>(m_window));
        m_window = nullptr;
    }
    if (m_classRegistered) {
        UnregisterClassW(wide(m_id + u"-sic"_s).c_str(), GetModuleHandleW(nullptr));
        m_classRegistered = false;
    }
}

void SingleInstance::onSecondLaunch(std::function<void()> callback) {
    m_callback = std::move(callback);
}

bool SingleInstance::secondLaunchReported() {
    if (m_window == nullptr) return false;
    QMetaObject::invokeMethod(
        this,
        [this] {
            const std::function<void()> callback = m_callback;  // a copy: the callback may replace itself
            if (callback) callback();
        },
        Qt::QueuedConnection);
    return true;
}

void SingleInstance::tellTheFirst() {
    const HANDLE mutex = static_cast<HANDLE>(m_mutex);
    const std::wstring className = wide(m_id + u"-sic"_s);
    const std::wstring windowName = wide(m_id + u"-siw"_s);
    const auto deadline = std::chrono::steady_clock::now() + kWaitLimit;
    auto left = [deadline] {
        const auto rest = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        return DWORD(std::max<long long>(rest.count(), 0));
    };
    for (;;) {
        const HWND window = FindWindowW(className.c_str(), windowName.c_str());
        if (window != nullptr) {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid != 0) AllowSetForegroundWindow(pid);
            QStringList parts{QDir::toNativeSeparators(QDir::currentPath())};
            if (QCoreApplication::instance()) parts.append(QCoreApplication::arguments());
            QByteArray data = parts.join(u'|').toUtf8();
            data.append('\0');
            COPYDATASTRUCT copy{kCopyDataTag, DWORD(data.size()), data.data()};
            DWORD_PTR result = 0;
            const LRESULT sent =
                SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy), SMTO_NORMAL, kForwardTimeoutMs, &result);
            if (sent != 0 && result == TRUE) {
                qCInfo(lcSingle) << "다른 xGetSongs가 떠 있어 그 창을 앞으로 가져오게 하고 끝냄";
                return;
            }
            // The first copy did not take it (it is shutting down, or hangs): wait for it to end.
            if (acquired(WaitForSingleObject(mutex, left()))) break;
            qCInfo(lcSingle) << "다른 xGetSongs가 답하지 않고 끝나지도 않아 이 실행은 끝냄";
            return;
        }
        // No window: the first copy is starting or no longer listens. It may end meanwhile.
        if (acquired(WaitForSingleObject(mutex, std::min(kLookAgainMs, left())))) break;
        if (left() == 0) {
            qCInfo(lcSingle) << "다른 xGetSongs의 창을 찾지 못했고 끝나지도 않아 이 실행은 끝냄";
            return;
        }
    }
    qCInfo(lcSingle) << "앞의 xGetSongs가 끝나서 이 실행이 첫 실행이 됨";
    becomeFirst();
}

}  // namespace xgs::app
