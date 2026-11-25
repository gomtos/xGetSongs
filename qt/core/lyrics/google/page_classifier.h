#pragma once

#include <QString>

#include <optional>

#include "lyrics/google/lyrics_result.h"

// Port of engine/.../lyrics/google/PageClassifier.kt.
namespace xgs::lyrics::google {

/// Tells Google's block pages from result pages. Pure. The address says most: a consent page lives on ConsentHost, the
/// CAPTCHA page at SorryPath. The visible text is read only when the page is short (BlockPageMaxText UTF-16 units or fewer),
/// so that a result page that merely talks about "too many requests" is not taken for a block. statusCode is nothing when
/// the browser cannot tell it.
///
/// Consent, Captcha or RateLimited; nothing when the page is no block.
std::optional<GoogleLyricsResult::Kind> blockOf(const QString& url, std::optional<int> statusCode, const QString& bodyText);

}  // namespace xgs::lyrics::google
