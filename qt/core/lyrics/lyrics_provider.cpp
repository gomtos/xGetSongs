#include "lyrics/lyrics_provider.h"

#include <utility>

namespace xgs::lyrics {

std::optional<QString> NoLyricsProvider::find(const LyricsQuery&, std::stop_token) {
    return std::nullopt;
}

FallbackLyricsProvider::FallbackLyricsProvider(std::vector<LyricsProvider*> providers) : m_providers(std::move(providers)) {}

std::optional<QString> FallbackLyricsProvider::find(const LyricsQuery& query, std::stop_token stop) {
    for (LyricsProvider* provider : m_providers) {
        if (std::optional<QString> lyrics = provider->find(query, stop)) return lyrics;
    }
    return std::nullopt;
}

}  // namespace xgs::lyrics
