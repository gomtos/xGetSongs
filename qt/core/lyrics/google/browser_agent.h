#pragma once

#include <QString>
#include <QStringView>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include "lyrics/google/browser_channel.h"
#include "lyrics/google/page_watch.h"

// Port of the agent of sidecar-host/src/browser.rs (the trait Page, `handle`, `allowed_url`, `allowed_navigation`,
// `decode_script_result` and the idle close of `serve`). The agent no longer polls a server over HTTP (Q3, decision 1): whoever
// owns it (the hidden browser on the GUI thread) hands it each command the channel gives and passes the answer back.
namespace xgs::lyrics::google {

/// How one page call went: `ok`, and then `text` is what the call gave (for evaluate: the script's result as the JSON text
/// WebView2 answers, `"\"abc\""` for a string, `null`); otherwise `text` is the short name of what went wrong.
struct PageOutcome {
    bool ok = false;
    QString text;
};

/// The window the commands act on: the hidden WebView2 window on the GUI thread. Every call answers through `done`, exactly
/// once, either inside the call or later (WebView2 answers through the message loop).
class Page {
public:
    virtual ~Page() = default;
    /// Opens url, making the window when there is none; done once the load has started.
    virtual void load(const QString& url, std::function<void(PageOutcome)> done) = 0;
    /// Runs script in the page. Without a window: fails with `no window`.
    virtual void evaluate(const QString& script, std::function<void(PageOutcome)> done) = 0;
    /// Back to `about:blank`; the window stays. Without a window: succeeds.
    virtual void reset(std::function<void(PageOutcome)> done) = 0;
    /// Destroys the window and everything it holds. Does nothing when there is none. The agent closes a page that failed from
    /// inside that call's `done`, which may itself run inside the page's own load, evaluate or reset (a page that answers at
    /// once): close must cope with being called there (Rust called it after the page call had returned), and the page call
    /// must not touch the window after its `done` has returned.
    virtual void close() = 0;
};

/// What the browser may open: `https://` addresses with an authority (not empty, no `@`), and `http://` only to the machine
/// itself (`127.0.0.1` or `localhost`, with or without a port of ASCII digits), the fixed pages of the checks. An address with
/// a control character or white space in it is refused. The same answers as Rust's allowed_url; one addition: an unpaired
/// surrogate (which a Rust string cannot hold, so Rust never saw one) is refused too.
bool allowedUrl(QStringView url);

/// Where a navigation may go: allowedUrl, and the blank page.
bool allowedNavigation(QStringView url);

/// WebView2 answers a script with JSON text. A string is the text itself, `null` is nothing, any other JSON value is kept as its
/// JSON text (without the white space around it). Text that is not JSON is nothing. See the .cpp for how "JSON" is decided.
std::optional<QString> decodeScriptResult(const QString& jsonText);

/// Carries the channel's commands out on a Page. Single-threaded: every function, and every `done` of the page, on one thread
/// (the page's). Knows nothing about Google.
///
/// A page call that fails closes the page (the next load makes a new window, which is what a window stuck on a page that never
/// answers needs). A load of an address that allowedUrl refuses is answered `url not allowed` without touching the page. An
/// opened page that has had no command for idleClose is closed by idleTick, so that its processes are given back.
class BrowserAgent {
public:
    BrowserAgent(Page& page, std::chrono::nanoseconds idleClose = std::chrono::seconds(60), Clock clock = {});

    /// Carries out one command and calls done with its answer exactly once, inside this call or later (when the page answers
    /// later). A `done` of the page that comes after this agent is gone still answers, and closes the page on a failure.
    void handle(const BrowserCommand& command, std::function<void(BrowserAnswer)> done);

    /// Closes the page when it is open, no command is underway and none came for idleClose. Rust looked after every poll that
    /// brought nothing; the owner calls this on a timer.
    void idleTick();

private:
    struct State {
        Page* page = nullptr;
        bool opened = false;
        int underway = 0;
    };

    std::chrono::nanoseconds now() const;

    std::shared_ptr<State> m_state;
    std::chrono::nanoseconds m_idleClose;
    Clock m_clock;
    std::chrono::nanoseconds m_lastCommand;
};

}  // namespace xgs::lyrics::google
