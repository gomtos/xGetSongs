#include "lyrics/google/google_lyrics_provider.h"

#include <utility>

#include "text/kt_text.h"

namespace xgs::lyrics::google {

namespace {

FetcherOptions withLog(FetcherOptions options, GoogleLyricsLog* log) {
    if (log) options.log = log;
    return options;
}

}  // namespace

GoogleLyricsProvider::GoogleLyricsProvider(BrowserChannel& channel, GoogleLyricsLog* log, FetcherOptions options,
                                           DrivenBrowserOptions browserOptions)
    : m_ownBrowser(std::make_unique<DrivenBrowser>(channel, std::move(browserOptions))),
      m_fetcher(*m_ownBrowser, withLog(std::move(options), log)),
      m_onClose([&channel] { channel.close(); }) {}

GoogleLyricsProvider::GoogleLyricsProvider(RenderingBrowser& browser, std::function<void()> onClose, FetcherOptions options)
    : m_fetcher(browser, std::move(options)), m_onClose(std::move(onClose)) {}

std::optional<QString> GoogleLyricsProvider::find(const LyricsQuery& query, std::stop_token stop) {
    try {
        if (kt::isBlank(query.artist) || kt::isBlank(query.title)) return std::nullopt;
        const std::optional<GoogleLyricsResult> result = m_fetcher.fetch(query.artist, query.title, stop);
        if (!result || result->kind != GoogleLyricsResult::Kind::Found) return std::nullopt;
        return result->lyrics;
    } catch (...) {
        return std::nullopt;
    }
}

void GoogleLyricsProvider::close() {
    if (m_onClose) m_onClose();
}

}  // namespace xgs::lyrics::google
