#pragma once

#include <QString>

#include "lyrics/google/lyrics_result.h"

// Port of engine/.../lyrics/google/LyricsCardParser.kt, split in two (Q3, decision 5): lyrics_card.js reads the card inside
// the page and reports what it saw; parseCard does what LyricsCardParser.parse did with it.
namespace xgs::lyrics::google {

/// More lines than this in a card answer, or more UTF-16 units of line text in all, is a failed extraction (lyrics_card.js
/// stops collecting at the same limits). Kotlin had no limit: these keep a hostile page from costing unbounded time.
inline constexpr int CardMaxLines = 5000;
inline constexpr qsizetype CardMaxUnits = 1'000'000;

/// Reads the answer of cardScript() (`{"scope": bool, "lines": [{"p": parent number, "t": raw text}, ...]}`):
///   - no lines: NoCard when there was no scope, ExtractionFailed when there was;
///   - every line's text is made what Jsoup 1.23.2's Element.text() gives (see lyrics_card.cpp), then trimmed (Kotlin's
///     trim()); empty lines are dropped. A run of lines with the same parent number is one paragraph (an empty line does not
///     break a run);
///   - lines joined by `\n` and paragraphs by a blank line go through xgs::lyrics::tidy; nothing from tidy is
///     ExtractionFailed. Found carries the tidy text, its count of non-empty lines and of non-blank paragraphs;
///   - an answer that is not of that shape (not JSON, `scope` not a boolean, `lines` not an array, an item without a
///     string `t` or a whole-number `p`) or that goes over CardMaxLines or CardMaxUnits is ExtractionFailed.
GoogleLyricsResult parseCard(const QString& scriptResult);

}  // namespace xgs::lyrics::google
