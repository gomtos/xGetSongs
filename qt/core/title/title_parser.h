#pragma once

#include <QString>

#include <optional>

// Port of shared/.../title/TitleParser.kt: splits a YouTube video title into artist and song title. Order: title patterns
// (`Artist - Title`, `Artist 'Title'`) -> yt-dlp `artist`/`track` metadata -> channel name. Text is kept as written; only
// quotes around the title and noise such as `Official MV` are removed. Lengths and indices are UTF-16 units, as in Kotlin.
namespace xgs {

enum class Confidence { High, Medium, Low };

struct ParsedTrack {
    QString artist;
    QString title;
    Confidence confidence;
    bool operator==(const ParsedTrack&) const = default;
};

// The artist when neither the title, the metadata nor the channel gives one ("Unknown Artist").
QString unknownArtist();

ParsedTrack parseTitle(const QString& rawTitle,
                       const std::optional<QString>& channel = std::nullopt,
                       const std::optional<QString>& metaArtist = std::nullopt,
                       const std::optional<QString>& metaTrack = std::nullopt);

}  // namespace xgs
