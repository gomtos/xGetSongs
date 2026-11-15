#include "hidden_webview.h"

#include <QPointer>
#include <QTimer>

#include <wrl.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

const wchar_t kHostClass[] = L"xgs-qt-spike-webview-host";
constexpr int kWidth = 800;
constexpr int kHeight = 600;

QString hrText(HRESULT hr)
{
    QString text = QString("0x%1").arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
    wchar_t *buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                       | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, static_cast<DWORD>(hr), 0,
                                   reinterpret_cast<wchar_t *>(&buf), 0, nullptr);
    if (n && buf)
        text += " " + QString::fromWCharArray(buf, static_cast<int>(n)).trimmed();
    if (buf)
        LocalFree(buf);
    return text;
}

// A top-level window that is never shown: no WS_VISIBLE, no taskbar button (tool window),
// never activated, and placed far off screen in case anything ever shows it.
HWND createHiddenHost()
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kHostClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return nullptr;
        registered = true;
    }
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHostClass, L"xgs-qt-spike",
                           WS_POPUP, -32000, -32000, kWidth, kHeight, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

} // namespace

HiddenWebView::HiddenWebView(const QString &userDataFolder, QObject *parent)
    : QObject(parent), m_userDataFolder(userDataFolder)
{
    // Start from the event loop so that a synchronous failure still reaches connected slots.
    QTimer::singleShot(0, this, &HiddenWebView::start);
}

HiddenWebView::~HiddenWebView()
{
    m_webview.Reset();
    if (m_controller)
        m_controller->Close();
    m_controller.Reset();
    m_env.Reset();
    if (m_host)
        DestroyWindow(m_host);
}

void HiddenWebView::fail(const QString &error)
{
    if (m_failed)
        return;
    m_failed = true;
    emit failed(error);
}

void HiddenWebView::start()
{
    m_host = createHiddenHost();
    if (!m_host) {
        fail("host window: " + hrText(HRESULT_FROM_WIN32(GetLastError())));
        return;
    }

    QPointer<HiddenWebView> self(this);
    const std::wstring folder = m_userDataFolder.toStdWString();
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, folder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [self](HRESULT result, ICoreWebView2Environment *env) -> HRESULT {
                if (self)
                    self->onEnvironment(result, env);
                return S_OK;
            })
            .Get());
    if (FAILED(hr))
        fail("CreateCoreWebView2EnvironmentWithOptions: " + hrText(hr));
}

void HiddenWebView::onEnvironment(HRESULT result, ICoreWebView2Environment *env)
{
    if (FAILED(result) || !env) {
        fail("environment: " + hrText(FAILED(result) ? result : E_POINTER));
        return;
    }
    m_env = env;

    ComPtr<ICoreWebView2Environment10> env10;
    if (FAILED(m_env.As(&env10))) {
        fail("runtime too old: no ICoreWebView2Environment10 (InPrivate)");
        return;
    }
    ComPtr<ICoreWebView2ControllerOptions> options;
    HRESULT hr = env10->CreateCoreWebView2ControllerOptions(&options);
    if (SUCCEEDED(hr))
        hr = options->put_IsInPrivateModeEnabled(TRUE);
    if (FAILED(hr)) {
        fail("controller options: " + hrText(hr));
        return;
    }

    QPointer<HiddenWebView> self(this);
    hr = env10->CreateCoreWebView2ControllerWithOptions(
        m_host, options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [self](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                if (self)
                    self->onController(result, controller);
                return S_OK;
            })
            .Get());
    if (FAILED(hr))
        fail("CreateCoreWebView2ControllerWithOptions: " + hrText(hr));
}

void HiddenWebView::onController(HRESULT result, ICoreWebView2Controller *controller)
{
    if (FAILED(result) || !controller) {
        fail("controller: " + hrText(FAILED(result) ? result : E_POINTER));
        return;
    }
    m_controller = controller;
    m_controller->put_Bounds(RECT{0, 0, kWidth, kHeight});
    // Visible inside a parent that is never shown: nothing reaches the screen, but the page
    // is not put into the "hidden" state that would stop its timers.
    m_controller->put_IsVisible(TRUE);

    HRESULT hr = m_controller->get_CoreWebView2(&m_webview);
    if (FAILED(hr) || !m_webview) {
        fail("get_CoreWebView2: " + hrText(FAILED(hr) ? hr : E_POINTER));
        return;
    }

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(m_webview->get_Settings(&settings))) {
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE); // no alert() boxes
        settings->put_IsStatusBarEnabled(FALSE);
    }

    QPointer<HiddenWebView> self(this);
    EventRegistrationToken token{};
    m_webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [self](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                BOOL ok = FALSE;
                args->get_IsSuccess(&ok);
                if (self)
                    emit self->navigated(ok == TRUE);
                return S_OK;
            })
            .Get(),
        &token);

    // Popups: handled without a new window means the window is not opened.
    m_webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                args->put_Handled(TRUE);
                return S_OK;
            })
            .Get(),
        &token);

    // Downloads: cancelled, and no download UI.
    ComPtr<ICoreWebView2_4> webview4;
    if (SUCCEEDED(m_webview.As(&webview4))) {
        webview4->add_DownloadStarting(
            Callback<ICoreWebView2DownloadStartingEventHandler>(
                [](ICoreWebView2 *, ICoreWebView2DownloadStartingEventArgs *args) -> HRESULT {
                    args->put_Cancel(TRUE);
                    args->put_Handled(TRUE);
                    return S_OK;
                })
                .Get(),
            &token);
    }

    emit ready();

    if (self && m_pendingUrl.isValid()) {
        const QUrl url = m_pendingUrl;
        m_pendingUrl = QUrl();
        open(url);
    }
}

void HiddenWebView::open(const QUrl &url)
{
    if (!m_webview) {
        m_pendingUrl = url;
        return;
    }
    const std::wstring address = url.toString(QUrl::FullyEncoded).toStdWString();
    const HRESULT hr = m_webview->Navigate(address.c_str());
    if (FAILED(hr))
        emit navigated(false);
}

void HiddenWebView::evaluate(const QString &script)
{
    if (!m_webview) {
        emit evaluated(QString());
        return;
    }
    QPointer<HiddenWebView> self(this);
    const std::wstring code = script.toStdWString();
    const HRESULT hr = m_webview->ExecuteScript(
        code.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [self](HRESULT result, LPCWSTR json) -> HRESULT {
                if (self)
                    emit self->evaluated(SUCCEEDED(result) && json ? QString::fromWCharArray(json)
                                                                   : QString());
                return S_OK;
            })
            .Get());
    if (FAILED(hr))
        emit evaluated(QString());
}
