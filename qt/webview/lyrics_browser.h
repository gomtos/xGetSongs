#pragma once

#include <QObject>
#include <QTimer>

#include <chrono>
#include <memory>

#include "lyrics/google/browser_agent.h"
#include "lyrics/google/browser_channel.h"
#include "webview/hidden_web_view.h"

namespace xgs::webview {

/// The hidden browser of the Google lyrics lookup on the GUI thread: a HiddenWebView, the BrowserAgent that carries out the
/// commands on it, and the channel's agent end. Made, it attaches to the channel (from then on the lookup's worker threads
/// reach it); destroyed, it detaches (their waiting calls end with `closed` and later ones with `no-agent`) and its window and
/// browser processes go. The channel's `wake` posts a queued call to this object's thread, which takes every command there
/// is; a timer lets the agent close a window that has had no command for idleClose (Rust: 60 seconds).
///
/// Threads: made, used and destroyed on the GUI thread (see HiddenWebView). The channel may be called from any thread. The
/// channel should outlive it; if the channel goes first, that must happen on this object's thread (the browser then notices
/// that its wake function was dropped and stops touching the channel).
class LyricsBrowser final : public QObject {
    Q_OBJECT

public:
    LyricsBrowser(lyrics::google::BrowserChannel& channel, HiddenWebViewOptions options, QObject* parent = nullptr,
                  std::chrono::milliseconds idleClose = std::chrono::seconds(60));
    ~LyricsBrowser() override;

    LyricsBrowser(const LyricsBrowser&) = delete;
    LyricsBrowser& operator=(const LyricsBrowser&) = delete;

    /// The window the commands act on (the checks look at it).
    HiddenWebView& view();

private:
    void takeCommands();

    lyrics::google::BrowserChannel& m_channel;
    HiddenWebView m_view;
    lyrics::google::BrowserAgent m_agent;
    QTimer m_idleTimer;
    // Alive for as long as the channel holds this browser's wake function (detach, close and the channel's end drop it).
    std::weak_ptr<void> m_attached;
};

}  // namespace xgs::webview
