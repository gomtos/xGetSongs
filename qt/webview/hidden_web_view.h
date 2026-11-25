#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "lyrics/google/browser_agent.h"

// WebView2.h declares these as structs; this header keeps COM away from its users.
struct ICoreWebView2Environment;
struct ICoreWebView2Controller;

// Port of shell/src-tauri/src/browser_window.rs (the hidden browser of the Google lyrics lookup) onto WebView2 itself, with the
// two additions of Q3 decision 10.
namespace xgs::webview {

struct HiddenWebViewOptions {
    /// The WebView2 user-data folder (made when missing). The caller chooses it (Q3 decision 11); the checks give a
    /// QTemporaryDir.
    QString userDataFolder;
    /// Extra browser arguments (CoreWebView2EnvironmentOptions::AdditionalBrowserArguments); empty: none. The checks give the
    /// two of Q3 decision 14; the product gives none.
    QString extraBrowserArguments;
    /// How long a script may take to answer (Rust's SCRIPT_LIMIT).
    std::chrono::milliseconds scriptLimit{5000};
};

/// A WebView2 window that is never shown, as the Page of a BrowserAgent. The rules (Q3 decision 10, as browser_window.rs):
///  - the window is made on the first load and reused; close destroys it and the next load makes a new one (windowsMade);
///  - nothing reaches the screen: the controller sits in a top-level window that is never shown (a WS_POPUP tool window, off
///    screen, never given to ShowWindow); the controller itself is visible inside it, so the page's timers keep running;
///  - InPrivate: nothing of a page (cookies, storage) is kept on disk or outlives the window;
///  - no other window (NewWindowRequested is handled with no window), no download (DownloadStarting cancelled; downloads
///    would go into the user-data folder, never the user's Downloads), no permission (PermissionRequested denied), no
///    external program (LaunchingExternalUriScheme cancelled), no script dialog, no context menu, no developer tools;
///  - a navigation goes only where lyrics::google::allowedNavigation allows (`https://`, `http://` to the machine itself,
///    `about:blank`), redirects included; a load of an address that allowedUrl refuses is answered `url not allowed`;
///  - a script answers within scriptLimit or the call fails with `script timed out`;
///  - the browser or render process dying (ProcessFailed) closes the window and fails what was waiting
///    (`browser process failed`);
///  - the User-Agent is WebView2's own.
/// How a call fails, by the text of its PageOutcome: `url not allowed`, `no window`, `script timed out`, `closed` (the window or
/// the view went while the call waited), `browser process failed`, `webview2 unavailable (0x…)` (no runtime, or the window
/// could not be made: the HRESULT in hex), `navigation failed (0x…)`, `script failed (0x…)`.
///
/// Threads: make it, call it and destroy it on one thread, which must be a single-threaded COM apartment with a message loop
/// (the GUI thread of a QGuiApplication; otherwise every load fails). Every `done` is called on that thread, exactly once,
/// inside the call or later; a done may call into the view again (close included). Destroying the view answers every call that
/// waits with `closed` (inside the destructor) and nothing is called after it is gone.
class HiddenWebView final : public QObject, public lyrics::google::Page {
    Q_OBJECT

public:
    explicit HiddenWebView(HiddenWebViewOptions options, QObject* parent = nullptr);
    ~HiddenWebView() override;

    HiddenWebView(const HiddenWebView&) = delete;
    HiddenWebView& operator=(const HiddenWebView&) = delete;

    void load(const QString& url, std::function<void(lyrics::google::PageOutcome)> done) override;
    void evaluate(const QString& script, std::function<void(lyrics::google::PageOutcome)> done) override;
    void reset(std::function<void(lyrics::google::PageOutcome)> done) override;
    void close() override;

    /// Whether a window is there (being made counts).
    bool hasWindow() const;
    /// Whether the window is visible to Windows (IsWindowVisible of its top-level window); false without one. Never true.
    bool isShownOnScreen() const;
    /// How many windows have been made since the view was made.
    int windowsMade() const;
    /// How many downloads a page started and the view refused (the checks see that the refusal was reached).
    int downloadsRefused() const;
    /// The process id of the window's browser process (ICoreWebView2::BrowserProcessId); nothing until the window is made.
    std::optional<quint32> browserProcessId() const;

    /// For the checks: makes html the page's document (parsed as one whole page, as Jsoup.parse did; its scripts are not run),
    /// without a navigation: the window (made when there is none) goes to about:blank and a script puts the parsed document
    /// in. done once that is done.
    void setHtmlForTest(const QString& html, std::function<void(lyrics::google::PageOutcome)> done);

private:
    struct Window;
    using Done = std::function<void(lyrics::google::PageOutcome)>;

    std::shared_ptr<Window> ensureWindow();
    void startWindow(const std::shared_ptr<Window>& window);
    void onEnvironment(const std::weak_ptr<Window>& weak, long result, ICoreWebView2Environment* environment);
    void onController(const std::weak_ptr<Window>& weak, long result, ICoreWebView2Controller* controller);
    void failWindow(const std::shared_ptr<Window>& window, const QString& error);
    void whenReady(const std::shared_ptr<Window>& window, std::function<void()> step);
    quint64 remember(const std::shared_ptr<Window>& window, Done done);
    void answer(quint64 id, const lyrics::google::PageOutcome& outcome);
    void answerAllOf(int windowNumber, const QString& error);
    void retire(const std::shared_ptr<Window>& window);
    void retireLater(std::shared_ptr<Window> window);

    struct Pending {
        int windowNumber;
        Done done;
    };

    HiddenWebViewOptions m_options;
    std::shared_ptr<Window> m_window;
    std::vector<std::shared_ptr<Window>> m_retiring;
    std::map<quint64, Pending> m_pending;
    quint64 m_nextId = 1;
    int m_made = 0;
    int m_downloadsRefused = 0;
    bool m_destroying = false;   // inside the destructor: calls from a done are answered `closed` and make nothing
};

}  // namespace xgs::webview
