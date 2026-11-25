#pragma once

#include <QString>
#include <QStringView>

#include <vector>

// Port of engine/.../lyrics/google/GoogleSearchQuery.kt.
namespace xgs::lyrics::google {

/// Where a search goes unless a check points it at its own fixture server (decision 12 of the Q3 plan; no environment
/// variable sets it).
inline constexpr QStringView DefaultSearchBase = u"https://www.google.com/search";

/// One Google search: its words (text) and the language of the page (language, `ko` or `en`).
struct GoogleSearchRequest {
    QString text;
    QString language;

    /// The address of the result page: `{searchBase}?q={text, UTF-8 percent-encoded}&hl={language}`. A space is `%20`,
    /// never `+` (net::encodeQueryValue, Kotlin's URLEncoder with `+` replaced).
    QString url(QStringView searchBase = DefaultSearchBase) const;
};

/// Turns the artist and title of a song into the searches to make, best first; empty when the artist or the title is blank.
/// The words are `{artist} {title} lyrics`. The title loses its quotes and its credits (`feat.`, `ft.`, `prod.`, `narr.`, as
/// a tail or in brackets) but keeps a bracket that names another version (`(Japanese Ver.)`): Google should look for that
/// version, not for the song. The artist is tried as written first and, only when it holds a parenthesised part, a second
/// time without that part (`RESCENE (리센느)` then `RESCENE`). The language is `ko` when the artist or the title has Hangul,
/// else `en`.
std::vector<GoogleSearchRequest> requests(const QString& artist, const QString& title);

/// What two spellings of one song have in common: the words of the search, NFKC, lowercase, and ς (U+03C2) written as σ
/// (U+03C3). The sigma is not in Kotlin: decision 9 of the Q3 plan (the user counts σ and ς as one letter).
QString cacheKey(const GoogleSearchRequest& request);

}  // namespace xgs::lyrics::google
