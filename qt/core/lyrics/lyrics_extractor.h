#pragma once

#include <QString>

#include <optional>

// Port of shared/.../lyrics/LyricsExtractor.kt: finds the lyrics in a YouTube video description and tidies lyrics text.
// Uploaders put the lyrics after a line that is only a heading (`[Lyrics]`, `가사`, `Lyrics:`); the first heading line wins
// (a line of hashtags is never a heading) and the block runs up to the first line that cannot be a lyric (a separator, a
// link, a copyright or hashtag line, boilerplate, the heading of another section) or to the end. Anything that does not
// look like a lyrics block gives nullopt: better no lyrics than somebody's links. Lengths are UTF-16 units, as in Kotlin.
namespace xgs::lyrics {

// The lyrics found in description, lines joined with `\n`, trailing whitespace of every line gone, no blank line at either
// end and no run of more than two blank lines, at most 30000 units (cut at a line boundary). nullopt when description is
// null or blank, has no heading line, or its first block has fewer than three non-blank lines.
std::optional<QString> extract(const std::optional<QString>& description);

// text cleaned up as lyrics with the rules of extract, without looking for a heading or an end. nullopt when text is null
// or blank or has fewer than three non-blank lines.
std::optional<QString> tidy(const std::optional<QString>& text);

}  // namespace xgs::lyrics
