#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "model/models.h"

// Port of engine/.../lyrics/LyricsMatcher.kt: decides whether a record of the lyrics service is the song that was asked for,
// and which of several is the best. The order of the checks, the constants and the tie-breaking are Kotlin's. Lengths and
// indexes are UTF-16 units, as in Kotlin. The six Kotlin regular expressions are hand-written scanners with the meaning of
// the Java regular expressions. Only CREDIT is case-insensitive, and Kotlin's IGNORE_CASE folds Unicode case; the scanner folds
// ASCII only, which is the same for CREDIT's letters but would not be for a pattern with s, k or i (lyrics_matcher.cpp).
//
// Cost: bracket removal repeats a scan of the whole title once per nesting level (nesting depth x length). Callers that pass
// text from the network (Q2c: candidate fields of the lyrics service) should cap the field lengths.
//
// One deliberate divergence from Kotlin (a user decision of 2026-11-15): in matching, the Greek letters σ (U+03C3) and ς
// (U+03C2) are the same letter. The normalisation lowercases and then writes ς as σ, so a title typed with σ at the end of a
// word matches one with ς, which Kotlin does not (qt/tests/tst_lyrics_matcher.cpp, sigmaFormsAreTheSameLetterInMatching).
namespace xgs::lyrics {

// A record of the lyrics service, reduced to what the matcher looks at. Every field can be missing in the answer of a service.
// `duration` is in seconds and may be fractional.
struct LyricsCandidate {
    std::optional<QString> trackName;
    std::optional<QString> artistName;
    std::optional<QString> albumName;
    std::optional<double> duration;
    std::optional<bool> instrumental;
    std::optional<QString> plainLyrics;
};

// The spellings of a title to look for, most specific first, equal ones listed once and blank ones never: the title itself
// (trimmed), without its bracketed parts, that without a trailing credit, that without quote characters.
QStringList titleVariants(const QString& title);
// The spellings of an artist: the artist itself, the text outside its parentheses and each parenthesised part; at most three.
QStringList artistVariants(const QString& artist);
// Kotlin LyricsMatcher.normalize: NFKC, lowercase, only the letters and digits (of any script). ς becomes σ (see above).
QString normalizeForMatching(const QString& text);
// True when the candidate can be the song of the query (lyrics, titles, artists and lengths agree).
bool isMatch(const LyricsQuery& query, const LyricsCandidate& candidate);
// The index of the chosen candidate, or -1 when none matches. (Kotlin's pick returns the candidate itself.)
qsizetype pick(const LyricsQuery& query, const QList<LyricsCandidate>& candidates);

}  // namespace xgs::lyrics
