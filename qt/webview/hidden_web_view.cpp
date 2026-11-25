#include "webview/hidden_web_view.h"

#include <QDir>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

#include <windows.h>
#include <objbase.h>
#include <wrl.h>

#include <WebView2.h>
// The SDK's options class marks a member with __attribute__((annotate(...))) when __has_attribute is defined, which MSVC's
// C++20 mode defines but whose __attribute__ it does not know. The annotation means nothing here, so it is dropped.
#define __attribute__(annotation)
#include <WebView2EnvironmentOptions.h>
#undef __attribute__

#include <string>
#include <utility>

using namespace Qt::StringLiterals;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using xgs::lyrics::google::PageOutcome;

namespace xgs::webview {

namespace {

const wchar_t HostClass[] = L"xgs-hidden-web-view-host";
constexpr int Width = 1024;
constexpr int Height = 768;

QString hex(HRESULT result) {
    return u"0x%1"_s.arg(quint32(result), 8, 16, u'0');
}

QString unavailable(const QString& why) {
    return u"webview2 unavailable ("_s + why + u')';
}

PageOutcome failed(const QString& text) {
    return PageOutcome{false, text};
}

std::wstring wide(const QString& text) {
    return text.toStdWString();
}

// The window procedure of the host: a close request that reaches it (a tool that closes windows, a stray WM_CLOSE) does not
// destroy it under the controller; the view decides when its windows go.
LRESULT CALLBACK hostProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) return 0;
    return DefWindowProcW(window, message, wParam, lParam);
}

// A top-level window that is never shown (Q0 report): not WS_VISIBLE and never given to ShowWindow, no task-bar button (a
// tool window), never activated, and far off screen in case anything ever showed it.
HWND makeHost() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = hostProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = HostClass;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
        registered = true;
    }
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, HostClass, L"xGetSongs", WS_POPUP, -32000, -32000, Width, Height,
                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// text as a JavaScript string literal (double quotes).
QString javaScriptString(const QString& text) {
    QString out = u"\""_s;
    for (const QChar c : text) {
        const char16_t unit = c.unicode();
        if (unit == u'"' || unit == u'\\') {
            out += u'\\';
            out += c;
        } else if (unit < 0x20 || unit == 0x2028 || unit == 0x2029) {
            out += u"\\u%1"_s.arg(unsigned(unit), 4, 16, u'0');
        } else {
            out += c;
        }
    }
    return out + u'"';
}

}  // namespace

struct HiddenWebView::Window {
    int number = 0;
    HWND host = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    std::vector<std::function<void()>> removers;                // take the event handlers off again
    bool ready = false;
    QElapsedTimer started;                                      // since ensureWindow
    std::vector<std::function<void()>> waiting;                // steps that wait for the window to be made
    std::vector<std::function<void()>> onNavigationCompleted;   // one-shot
};

HiddenWebView::HiddenWebView(HiddenWebViewOptions options, QObject* parent) : QObject(parent), m_options(std::move(options)) {}

HiddenWebView::~HiddenWebView() {
    m_destroying = true;
    std::shared_ptr<Window> window = std::move(m_window);
    m_window.reset();
    // Every call that waits is answered here, once; a done that calls back in finds the view closing (see m_destroying).
    std::map<quint64, Pending> pending = std::move(m_pending);
    m_pending.clear();
    for (auto& [id, waiting] : pending) waiting.done(failed(u"closed"_s));
    if (window) retire(window);
    std::vector<std::shared_ptr<Window>> retiring = std::move(m_retiring);
    for (const std::shared_ptr<Window>& old : retiring) retire(old);
}

bool HiddenWebView::hasWindow() const {
    return m_window != nullptr;
}

bool HiddenWebView::isShownOnScreen() const {
    return m_window && m_window->host && IsWindowVisible(m_window->host);
}

int HiddenWebView::windowsMade() const {
    return m_made;
}

int HiddenWebView::downloadsRefused() const {
    return m_downloadsRefused;
}

QList<qint64> HiddenWebView::creationTimesMs() const {
    return m_creationTimes;
}

std::optional<quint32> HiddenWebView::browserProcessId() const {
    if (!m_window || !m_window->view) return std::nullopt;
    UINT32 pid = 0;
    if (FAILED(m_window->view->get_BrowserProcessId(&pid)) || pid == 0) return std::nullopt;
    return quint32(pid);
}

void HiddenWebView::load(const QString& url, std::function<void(PageOutcome)> done) {
    if (m_destroying) {
        done(failed(u"closed"_s));
        return;
    }
    if (!lyrics::google::allowedUrl(url)) {
        done(failed(u"url not allowed"_s));
        return;
    }
    const std::shared_ptr<Window> window = ensureWindow();
    const quint64 id = remember(window, std::move(done));
    whenReady(window, [this, window, id, url] {
        if (m_window != window) return;
        const HRESULT result = window->view->Navigate(wide(url).c_str());
        answer(id, SUCCEEDED(result) ? PageOutcome{true, QString()} : failed(u"navigation failed ("_s + hex(result) + u')'));
    });
}

void HiddenWebView::evaluate(const QString& script, std::function<void(PageOutcome)> done) {
    if (m_destroying) {
        done(failed(u"closed"_s));
        return;
    }
    if (!m_window) {
        done(failed(u"no window"_s));
        return;
    }
    const std::shared_ptr<Window> window = m_window;
    const quint64 id = remember(window, std::move(done));
    // A page that never gives its thread back (a loop that does not end) never answers: the call fails after scriptLimit (the
    // agent then closes the window). The timer goes with the view; an answer that came first makes it do nothing.
    QTimer::singleShot(m_options.scriptLimit, this, [this, id] { answer(id, failed(u"script timed out"_s)); });
    whenReady(window, [this, window, id, script] {
        if (m_window != window) return;
        const QPointer<HiddenWebView> self(this);
        const HRESULT result = window->view->ExecuteScript(
            wide(script).c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>([self, id](HRESULT code, LPCWSTR json) -> HRESULT {
                                      if (!self) return S_OK;
                                      if (SUCCEEDED(code) && json) self->answer(id, PageOutcome{true, QString::fromWCharArray(json)});
                                      else self->answer(id, failed(u"script failed ("_s + hex(FAILED(code) ? code : E_POINTER) + u')'));
                                      return S_OK;
                                  }).Get());
        if (FAILED(result)) answer(id, failed(u"script failed ("_s + hex(result) + u')'));
    });
}

void HiddenWebView::reset(std::function<void(PageOutcome)> done) {
    if (m_destroying) {
        done(failed(u"closed"_s));
        return;
    }
    if (!m_window) {
        done(PageOutcome{true, QString()});
        return;
    }
    const std::shared_ptr<Window> window = m_window;
    const quint64 id = remember(window, std::move(done));
    whenReady(window, [this, window, id] {
        if (m_window != window) return;
        const HRESULT result = window->view->Navigate(L"about:blank");
        answer(id, SUCCEEDED(result) ? PageOutcome{true, QString()} : failed(u"navigation failed ("_s + hex(result) + u')'));
    });
}

void HiddenWebView::close() {
    if (m_destroying || !m_window) return;
    std::shared_ptr<Window> window = std::move(m_window);
    m_window.reset();
    window->waiting.clear();
    answerAllOf(window->number, u"closed"_s);
    retireLater(std::move(window));
}

void HiddenWebView::setHtmlForTest(const QString& html, std::function<void(PageOutcome)> done) {
    if (m_destroying) {
        done(failed(u"closed"_s));
        return;
    }
    const std::shared_ptr<Window> window = ensureWindow();
    const quint64 id = remember(window, std::move(done));
    // DOMParser parses the text as one whole document (the HTML parsing algorithm, as Jsoup.parse did) and marks its scripts
    // as already started, so the page's scripts are not run; the parsed root then takes the place of the blank page's.
    const QString script =
        u"(function () { var parsed = new DOMParser().parseFromString("_s + javaScriptString(html)
        + u", 'text/html'); document.replaceChild(document.importNode(parsed.documentElement, true), document.documentElement); "
          u"return true; })()"_s;
    whenReady(window, [this, window, id, script] {
        if (m_window != window) return;
        window->onNavigationCompleted.push_back([this, window, id, script] {
            if (m_window != window) return;
            const QPointer<HiddenWebView> self(this);
            const HRESULT result = window->view->ExecuteScript(
                wide(script).c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>([self, id](HRESULT code, LPCWSTR json) -> HRESULT {
                                          if (!self) return S_OK;
                                          const bool ok = SUCCEEDED(code) && json && QString::fromWCharArray(json) == u"true";
                                          self->answer(id, ok ? PageOutcome{true, QString()} : failed(u"script failed"_s));
                                          return S_OK;
                                      }).Get());
            if (FAILED(result)) answer(id, failed(u"script failed ("_s + hex(result) + u')'));
        });
        const HRESULT result = window->view->Navigate(L"about:blank");
        if (FAILED(result)) answer(id, failed(u"navigation failed ("_s + hex(result) + u')'));
    });
}

std::shared_ptr<HiddenWebView::Window> HiddenWebView::ensureWindow() {
    if (m_window) return m_window;
    auto window = std::make_shared<Window>();
    window->number = ++m_made;
    window->started.start();
    m_creationTimes.append(-1);
    m_window = window;
    const std::weak_ptr<Window> weak = window;
    // Made from the event loop, so that no call is ever answered inside load (a failure to start included).
    if (m_held < m_options.holdCreationsForTest) {
        ++m_held;   // a check holds this one back: its callbacks never come
    } else {
        QTimer::singleShot(0, this, [this, weak] {
            if (const std::shared_ptr<Window> made = weak.lock()) startWindow(made);
        });
    }
    // WebView2 may never call back (a runtime that hangs): what waits on the window must not wait for ever, and the next load
    // must get a new window.
    QTimer::singleShot(m_options.createLimit, this, [this, weak] {
        const std::shared_ptr<Window> made = weak.lock();
        if (made && !made->ready) failWindow(made, unavailable(u"timeout"_s));
    });
    return window;
}

void HiddenWebView::startWindow(const std::shared_ptr<Window>& window) {
    if (m_window != window) return;
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    if (FAILED(CoGetApartmentType(&type, &qualifier)) || (type != APTTYPE_STA && type != APTTYPE_MAINSTA)) {
        failWindow(window, unavailable(u"not a single-threaded apartment"_s));
        return;
    }
    if (m_options.userDataFolder.isEmpty()) {
        failWindow(window, unavailable(u"no user-data folder"_s));
        return;
    }
    window->host = makeHost();
    if (!window->host) {
        failWindow(window, unavailable(hex(HRESULT_FROM_WIN32(GetLastError()))));
        return;
    }
    const ComPtr<CoreWebView2EnvironmentOptions> environmentOptions = Make<CoreWebView2EnvironmentOptions>();
    if (!environmentOptions) {
        failWindow(window, unavailable(hex(E_OUTOFMEMORY)));
        return;
    }
    if (!m_options.extraBrowserArguments.isEmpty())
        environmentOptions->put_AdditionalBrowserArguments(wide(m_options.extraBrowserArguments).c_str());
    const QPointer<HiddenWebView> self(this);
    const std::weak_ptr<Window> weak = window;
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, wide(m_options.userDataFolder).c_str(), environmentOptions.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [self, weak](HRESULT code, ICoreWebView2Environment* environment) -> HRESULT {
                if (self) self->onEnvironment(weak, code, environment);
                return S_OK;
            })
            .Get());
    if (FAILED(result)) failWindow(window, unavailable(hex(result)));
}

void HiddenWebView::onEnvironment(const std::weak_ptr<Window>& weak, long result, ICoreWebView2Environment* environment) {
    const std::shared_ptr<Window> window = weak.lock();
    if (!window || m_window != window) return;
    if (FAILED(result) || !environment) {
        failWindow(window, unavailable(hex(FAILED(result) ? result : E_POINTER)));
        return;
    }
    window->environment = environment;
    ComPtr<ICoreWebView2Environment10> environment10;
    if (FAILED(window->environment.As(&environment10))) {
        failWindow(window, unavailable(u"runtime too old"_s));
        return;
    }
    ComPtr<ICoreWebView2ControllerOptions> controllerOptions;
    HRESULT code = environment10->CreateCoreWebView2ControllerOptions(&controllerOptions);
    if (SUCCEEDED(code)) code = controllerOptions->put_IsInPrivateModeEnabled(TRUE);
    if (FAILED(code)) {
        failWindow(window, unavailable(hex(code)));
        return;
    }
    const QPointer<HiddenWebView> self(this);
    code = environment10->CreateCoreWebView2ControllerWithOptions(
        window->host, controllerOptions.Get(),
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [self, weak](HRESULT completed, ICoreWebView2Controller* controller) -> HRESULT {
                if (self) self->onController(weak, completed, controller);
                else if (controller) controller->Close();
                return S_OK;
            })
            .Get());
    if (FAILED(code)) failWindow(window, unavailable(hex(code)));
}

void HiddenWebView::onController(const std::weak_ptr<Window>& weak, long result, ICoreWebView2Controller* controller) {
    const std::shared_ptr<Window> window = weak.lock();
    if (!window || m_window != window) {
        // The window was closed while it was being made: this controller is nobody's.
        if (controller) controller->Close();
        return;
    }
    if (FAILED(result) || !controller) {
        failWindow(window, unavailable(hex(FAILED(result) ? result : E_POINTER)));
        return;
    }
    window->controller = controller;
    window->controller->put_Bounds(RECT{0, 0, Width, Height});
    // Visible inside a parent that is never shown: nothing reaches the screen, and the page is not put into the hidden state
    // that would slow its timers down.
    window->controller->put_IsVisible(TRUE);
    HRESULT code = window->controller->get_CoreWebView2(&window->view);
    if (FAILED(code) || !window->view) {
        failWindow(window, unavailable(hex(FAILED(code) ? code : E_POINTER)));
        return;
    }
    const ComPtr<ICoreWebView2> view = window->view;

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(view->get_Settings(&settings)) && settings) {
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);   // alert() and the like raise ScriptDialogOpening below
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
    }

    const QPointer<HiddenWebView> self(this);
    EventRegistrationToken token{};

    // Where the page may go: allowedNavigation, for every navigation of the page (redirects included), as wry's on_navigation.
    code = view->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                const bool allowed = SUCCEEDED(args->get_Uri(&uri)) && uri && lyrics::google::allowedNavigation(QString::fromWCharArray(uri));
                if (uri) CoTaskMemFree(uri);
                if (!allowed) args->put_Cancel(TRUE);
                return S_OK;
            })
            .Get(),
        &token);
    if (FAILED(code)) {
        failWindow(window, unavailable(hex(code)));
        return;
    }
    window->removers.push_back([view, token] { view->remove_NavigationStarting(token); });

    code = view->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>([weak](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
            const std::shared_ptr<Window> completed = weak.lock();
            if (!completed || completed->onNavigationCompleted.empty()) return S_OK;
            // Only setHtmlForTest waits for a navigation, the one to the blank page.
            LPWSTR source = nullptr;
            const bool blank = SUCCEEDED(sender->get_Source(&source)) && source && QString::fromWCharArray(source) == u"about:blank";
            if (source) CoTaskMemFree(source);
            if (!blank) return S_OK;
            std::vector<std::function<void()>> steps = std::move(completed->onNavigationCompleted);
            completed->onNavigationCompleted.clear();
            for (const std::function<void()>& step : steps) step();
            return S_OK;
        }).Get(),
        &token);
    if (SUCCEEDED(code)) window->removers.push_back([view, token] { view->remove_NavigationCompleted(token); });

    // No other window: handled, and no window given.
    code = view->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            args->put_Handled(TRUE);
            return S_OK;
        }).Get(),
        &token);
    if (FAILED(code)) {
        failWindow(window, unavailable(hex(code)));
        return;
    }
    window->removers.push_back([view, token] { view->remove_NewWindowRequested(token); });

    // No permission (camera, microphone, location, notifications, ...).
    code = view->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
            return S_OK;
        }).Get(),
        &token);
    if (FAILED(code)) {
        failWindow(window, unavailable(hex(code)));
        return;
    }
    window->removers.push_back([view, token] { view->remove_PermissionRequested(token); });

    // No script dialog: with the default dialogs off, a dialog the handler does not accept is dismissed when it returns.
    code = view->add_ScriptDialogOpening(
        Callback<ICoreWebView2ScriptDialogOpeningEventHandler>([](ICoreWebView2*, ICoreWebView2ScriptDialogOpeningEventArgs*) -> HRESULT {
            return S_OK;
        }).Get(),
        &token);
    if (SUCCEEDED(code)) window->removers.push_back([view, token] { view->remove_ScriptDialogOpening(token); });

    // The browser or the page's process died: the window is of no use any more (Q3 decision 10).
    code = view->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>([self, weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
            if (FAILED(args->get_ProcessFailedKind(&kind))) return S_OK;
            if (kind != COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED && kind != COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED)
                return S_OK;
            const std::shared_ptr<Window> broken = weak.lock();
            if (self && broken) self->failWindow(broken, u"browser process failed"_s);
            return S_OK;
        }).Get(),
        &token);
    if (FAILED(code)) {
        failWindow(window, unavailable(hex(code)));
        return;
    }
    window->removers.push_back([view, token] { view->remove_ProcessFailed(token); });

    // No download: cancelled, without the download UI.
    ComPtr<ICoreWebView2_4> view4;
    if (FAILED(view.As(&view4))) {
        failWindow(window, unavailable(u"runtime too old"_s));
        return;
    }
    code = view4->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>([self](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
            args->put_Cancel(TRUE);
            args->put_Handled(TRUE);
            if (self) ++self->m_downloadsRefused;
            return S_OK;
        }).Get(),
        &token);
    if (FAILED(code)) {
        failWindow(window, unavailable(hex(code)));
        return;
    }
    window->removers.push_back([view4, token] { view4->remove_DownloadStarting(token); });
    // And should a download ever get past that, it lands in the user-data folder, not in the user's Downloads. The path must be
    // in Windows form: with a forward slash in it, WebView2 dropped every download before DownloadStarting (observed with
    // runtime 158), which would also hide the refusal above from the checks.
    ComPtr<ICoreWebView2_13> view13;
    if (SUCCEEDED(view.As(&view13))) {
        ComPtr<ICoreWebView2Profile> profile;
        if (SUCCEEDED(view13->get_Profile(&profile)) && profile) {
            const QString folder = QDir(m_options.userDataFolder).filePath(u"refused-downloads"_s);
            if (QDir().mkpath(folder)) profile->put_DefaultDownloadFolderPath(wide(QDir::toNativeSeparators(folder)).c_str());
        }
    }

    // No program outside the browser for an address of another scheme (WebView2 would ask in a dialog).
    ComPtr<ICoreWebView2_18> view18;
    if (SUCCEEDED(view.As(&view18))) {
        code = view18->add_LaunchingExternalUriScheme(
            Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
                [](ICoreWebView2*, ICoreWebView2LaunchingExternalUriSchemeEventArgs* args) -> HRESULT {
                    args->put_Cancel(TRUE);
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(code)) window->removers.push_back([view18, token] { view18->remove_LaunchingExternalUriScheme(token); });
    }

    // No sign-in dialog for a page that asks for HTTP authentication.
    ComPtr<ICoreWebView2_10> view10;
    if (SUCCEEDED(view.As(&view10))) {
        code = view10->add_BasicAuthenticationRequested(
            Callback<ICoreWebView2BasicAuthenticationRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2BasicAuthenticationRequestedEventArgs* args) -> HRESULT {
                    args->put_Cancel(TRUE);
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(code)) window->removers.push_back([view10, token] { view10->remove_BasicAuthenticationRequested(token); });
    }

    window->ready = true;
    if (window->number - 1 < m_creationTimes.size()) m_creationTimes[window->number - 1] = window->started.elapsed();
    std::vector<std::function<void()>> steps = std::move(window->waiting);
    window->waiting.clear();
    for (const std::function<void()>& step : steps) step();
}

void HiddenWebView::failWindow(const std::shared_ptr<Window>& window, const QString& error) {
    if (m_window != window) return;
    m_window.reset();
    window->waiting.clear();
    answerAllOf(window->number, error);
    retireLater(window);
}

void HiddenWebView::whenReady(const std::shared_ptr<Window>& window, std::function<void()> step) {
    if (window->ready) step();
    else window->waiting.push_back(std::move(step));
}

quint64 HiddenWebView::remember(const std::shared_ptr<Window>& window, Done done) {
    const quint64 id = m_nextId++;
    m_pending.emplace(id, Pending{window->number, std::move(done)});
    return id;
}

void HiddenWebView::answer(quint64 id, const PageOutcome& outcome) {
    const auto found = m_pending.find(id);
    if (found == m_pending.end()) return;
    Done done = std::move(found->second.done);
    m_pending.erase(found);
    done(outcome);
}

void HiddenWebView::answerAllOf(int windowNumber, const QString& error) {
    std::vector<quint64> ids;
    for (const auto& [id, pending] : m_pending)
        if (pending.windowNumber == windowNumber) ids.push_back(id);
    for (const quint64 id : ids) answer(id, failed(error));
}

void HiddenWebView::retire(const std::shared_ptr<Window>& window) {
    for (const std::function<void()>& remove : window->removers) remove();
    window->removers.clear();
    window->waiting.clear();
    window->onNavigationCompleted.clear();
    if (window->controller) window->controller->Close();
    window->view.Reset();
    window->controller.Reset();
    window->environment.Reset();
    if (window->host) DestroyWindow(window->host);
    window->host = nullptr;
}

void HiddenWebView::retireLater(std::shared_ptr<Window> window) {
    // Not inside the call that closed it: that may be a handler of the window's own WebView2 events.
    m_retiring.push_back(std::move(window));
    QTimer::singleShot(0, this, [this] {
        std::vector<std::shared_ptr<Window>> retiring = std::move(m_retiring);
        m_retiring.clear();
        for (const std::shared_ptr<Window>& old : retiring) retire(old);
    });
}

}  // namespace xgs::webview
