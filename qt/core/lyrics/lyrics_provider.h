#pragma once

#include <QString>

#include <optional>
#include <stop_token>
#include <vector>

#include "model/models.h"

// Port of engine/.../lyrics/LyricsProvider.kt and FallbackLyricsProvider.kt. (LyricsQuery itself is in model/models.h.)
namespace xgs::lyrics {

/// Finds the lyrics of a song somewhere else than in the video description.
class LyricsProvider {
public:
    virtual ~LyricsProvider() = default;
    /// The tidied lyrics, or nothing when none were found, the lookup did not work, or stop was requested. Never throws.
    ///
    /// The lyrics of the song described by `query`, cleaned up (see lyrics::tidy), or nothing when none were found or the
    /// lookup did not work: a lookup that fails is a song without lyrics, not a failed download. Where Kotlin throws a
    /// CancellationException (the calling coroutine is cancelled), this returns nothing once `stop` is requested.
    virtual std::optional<QString> find(const LyricsQuery& query, std::stop_token stop) = 0;
};

/// Looks nothing up. The engine's default: it never sends anything to anybody.
class NoLyricsProvider final : public LyricsProvider {
public:
    std::optional<QString> find(const LyricsQuery& query, std::stop_token stop) override;
};

/// Asks its providers one after the other and answers with the first lyrics found; nothing when none has any.
class FallbackLyricsProvider final : public LyricsProvider {
public:
    /// The providers are referenced, not owned (they must outlive this one), and asked in order.
    explicit FallbackLyricsProvider(std::vector<LyricsProvider*> providers);
    std::optional<QString> find(const LyricsQuery& query, std::stop_token stop) override;

private:
    std::vector<LyricsProvider*> m_providers;
};

}  // namespace xgs::lyrics
