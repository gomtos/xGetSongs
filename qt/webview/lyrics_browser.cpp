#include "webview/lyrics_browser.h"

#include <QMetaObject>

#include <algorithm>
#include <utility>

namespace xgs::webview {

using lyrics::google::BrowserAnswer;
using lyrics::google::BrowserCommand;

LyricsBrowser::LyricsBrowser(lyrics::google::BrowserChannel& channel, HiddenWebViewOptions options, QObject* parent,
                             std::chrono::milliseconds idleClose)
    : QObject(parent), m_channel(channel), m_view(std::move(options)), m_agent(m_view, idleClose) {
    // Looked at four times per idle time, at least every 50 ms and at most every second.
    const auto interval = std::clamp<std::chrono::milliseconds>(idleClose / 4, std::chrono::milliseconds(50), std::chrono::seconds(1));
    connect(&m_idleTimer, &QTimer::timeout, this, [this] { m_agent.idleTick(); });
    m_idleTimer.start(interval);

    auto attached = std::make_shared<int>(0);
    m_attached = attached;
    // Called on the lookup's threads; posts to this object's thread and returns at once (the channel's rule for wake). A post
    // that arrives after this object is gone is dropped by Qt, and detach in the destructor makes sure none is under way.
    m_channel.attach([this, attached] { QMetaObject::invokeMethod(this, [this] { takeCommands(); }, Qt::QueuedConnection); });
}

LyricsBrowser::~LyricsBrowser() {
    m_idleTimer.stop();
    if (!m_attached.expired()) m_channel.detach();
    // m_view goes after m_agent: it answers what still waits with `closed`, and those answers find the channel detached (the
    // wake function, and with it m_attached, is dropped), so they touch it no more.
}

HiddenWebView& LyricsBrowser::view() {
    return m_view;
}

void LyricsBrowser::takeCommands() {
    if (m_attached.expired()) return;
    while (std::optional<BrowserCommand> command = m_channel.takeCommand()) {
        m_agent.handle(*command, [channel = &m_channel, attached = m_attached](const BrowserAnswer& answer) {
            if (const std::shared_ptr<void> stillAttached = attached.lock()) channel->answer(answer);
        });
        if (m_attached.expired()) return;
    }
}

}  // namespace xgs::webview
