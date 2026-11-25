#pragma once

#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

#include "lyrics/google/browser_channel.h"
#include "lyrics/google/driven_browser.h"
#include "lyrics/google/lyrics_fetcher.h"
#include "lyrics/google/lyrics_result.h"
#include "lyrics/google/rendering_browser.h"
#include "lyrics/lyrics_provider.h"

// Port of engine/.../lyrics/google/GoogleLyricsProvider.kt.
namespace xgs::lyrics::google {

/// Finds lyrics in the lyrics card of a Google search ("artist title lyrics"), read by a browser window that is never shown
/// (WebView2 on the GUI thread, driven through a BrowserChannel). The last resort after the description and LRCLIB, and a
/// fragile one: Google changes its pages, and its terms of service all but forbid automated queries. This is a personal tool,
/// so the lookup is polite (see GoogleLyricsFetcher) and gives up for good at the first sign of a block. Lyrics are
/// copyrighted works; a use beyond personal use needs an official lyrics API (Musixmatch, say) instead of this.
///
/// find never throws; whatever goes wrong is "no lyrics" (also when no agent is attached to the channel), and so is a stop
/// request (Kotlin: a CancellationException). find may be called from any number of threads at once (see
/// GoogleLyricsFetcher); the log is called from whichever of them made the trip. Close it when the application ends: that
/// closes the channel. Destroying it does not close the channel (Kotlin's provider closed only on close()).
class GoogleLyricsProvider final : public LyricsProvider {
public:
    /// The real thing, on the pages channel brings (channel is referenced and must outlive the provider). log gets one line per
    /// trip (counts and result names, never a song or lyrics); when given it takes the place of options.log. browserOptions are
    /// the DrivenBrowser's: its clock and pause stay DrivenBrowser's defaults when empty.
    GoogleLyricsProvider(BrowserChannel& channel, GoogleLyricsLog* log = nullptr, FetcherOptions options = {},
                         DrivenBrowserOptions browserOptions = {});

    /// For tests (Kotlin's internal constructor): the lookup on browser (referenced, must outlive the provider), and onClose is
    /// what close() does.
    GoogleLyricsProvider(RenderingBrowser& browser, std::function<void()> onClose, FetcherOptions options = {});

    GoogleLyricsProvider(const GoogleLyricsProvider&) = delete;
    GoogleLyricsProvider& operator=(const GoogleLyricsProvider&) = delete;

    std::optional<QString> find(const LyricsQuery& query, std::stop_token stop) override;

    /// Closes the channel (or runs the test's close action).
    void close();

private:
    std::unique_ptr<DrivenBrowser> m_ownBrowser;   // the real constructor's browser; declared before the fetcher that uses it
    GoogleLyricsFetcher m_fetcher;
    std::function<void()> m_onClose;
};

}  // namespace xgs::lyrics::google
