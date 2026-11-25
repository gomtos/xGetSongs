#pragma once

#include <QStringView>

#include <array>
#include <cstddef>

// Port of engine/.../lyrics/google/GoogleLyricsSelectors.kt. Everything that depends on how Google writes its result page, in
// one place. Google changes these obfuscated attributes from time to time; when it does, this is the only file to touch (what
// notices is a real lookup: the log line "구글 가사 카드: 카드는 있으나 줄을 읽지 못함" or "카드 없음" for songs that are
// known to have a card).
namespace xgs::lyrics::google {

/// Where the lyrics card is, outermost first: the first one that matches limits the search for lines.
inline constexpr std::array<QStringView, 2> CardScopes{u"div[data-lyricid]", u"[jsname=WbKHeb]"};

/// One line of the lyrics. Searched inside the scope, or in the whole page when there is no scope.
inline constexpr QStringView Line = u"span[jsname=YS01Ge]";

/// Matches a page that shows a lyrics card or the first sign of one; the browser stops waiting at it. The card scopes and the
/// line, joined by ", " (Kotlin's `(CARD_SCOPES + LINE).joinToString(", ")`).
inline constexpr QStringView CardPresence = u"div[data-lyricid], [jsname=WbKHeb], span[jsname=YS01Ge]";

namespace detail {
// Whether CardPresence is the scopes and the line joined by ", ", so that the three can never drift apart.
constexpr bool cardPresenceIsTheJoin() {
    const std::array<QStringView, 3> parts{CardScopes[0], CardScopes[1], Line};
    qsizetype at = 0;
    for (std::size_t p = 0; p < parts.size(); ++p) {
        if (p > 0) {
            if (at + 2 > CardPresence.size() || CardPresence[at] != u',' || CardPresence[at + 1] != u' ') return false;
            at += 2;
        }
        for (qsizetype k = 0; k < parts[p].size(); ++k, ++at)
            if (at >= CardPresence.size() || CardPresence[at] != parts[p][k]) return false;
    }
    return at == CardPresence.size();
}
static_assert(cardPresenceIsTheJoin(), "CardPresence must be CardScopes and Line joined by \", \"");
}  // namespace detail

/// The address of Google's block page ("we have detected unusual traffic").
inline constexpr QStringView SorryPath = u"/sorry/";

/// The host of Google's consent page.
inline constexpr QStringView ConsentHost = u"consent.google";

/// Lowercase. Block pages say this in English or Korean.
inline constexpr std::array<QStringView, 2> UnusualTrafficTexts{u"unusual traffic", u"비정상적인 트래픽"};

/// Lowercase.
inline constexpr std::array<QStringView, 1> TooManyRequestsTexts{u"too many requests"};

/// The text of a page longer than this (UTF-16 units, Kotlin's String.length) is not read for block signs: a block page says
/// little, a result page a lot.
inline constexpr int BlockPageMaxText = 2000;

}  // namespace xgs::lyrics::google
