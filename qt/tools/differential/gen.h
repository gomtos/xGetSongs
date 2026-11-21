#pragma once

#include <QString>
#include <QStringList>

#include <cstddef>
#include <optional>

// Case generation for xgs-diff: the random source and the token material that every kind's generator mixes in (emoji and
// unpaired surrogates included, so that no port meets them for the first time in a user's title).
namespace xgs::diff {

// mulberry32, the same formula as shell/tools/differential/fuzz.mts: the same seed gives the same cases.
struct Rng {
    quint32 seed;
    double next();                      // [0, 1)
    int between(int low, int high);     // low..high, both included
    bool chance(double p);              // true with probability p
};

// A generator writes the fields of one case, tab-separated, without the kind letter (xgs-diff puts "K\t" in front).
//
// The line of kind W op jb (the whole download job; Oracle.java answerJob and main.cpp answerJb play it, qt/README.md has the
// same), all text as hex ("." empty, "-" null):
//   jb overwrite includeRank album albumOverride searchLyrics existing n
//      [rank videoId title channel artist track lowConfidence] x n   meta ytdlp ffmpeg lyrics put
//   overwrite, includeRank, searchLyrics, lowConfidence: 0 | 1; existing: the names the sink already has, "~" or comma-separated.
//   The five scripts have comma-separated entries (at least one), consumed in call order; once used up, the last entry again
//   (both sides alike):
//     meta    per metadata fetch: - (none) | title/channel/artist/track
//     ytdlp   per yt-dlp run: exit/m4a/cover/info/stdout/stderr: exit code in decimal; m4a, cover 0 | 1 (write <id>.m4a,
//             <id>.jpg into the folder of the -o template); info - (none written) | . (empty file) | the bytes as 2 hex digits
//             each; stdout, stderr "~" (no lines) | lines separated by "+". Files first, then the stdout lines, then stderr.
//     ffmpeg  per tag run: exit/writes/stderr (writes 1: the output file, the last argument, is written)
//     lyrics  per lookup: - (none) | . | text | X (the provider throws)
//     put     per put: S (Stored; the name joins the sink's names) | A<text or .> (AlreadyExists with that error text; Kotlin:
//             FileAlreadyExistsException(fileName)) | F<code>/<text> (Failed with that Win32 code and text; Kotlin: code 5
//             AccessDeniedException, else IOException(text); codes 112 and 39 only with a disk-full marker in the text)
//   exists(name) answers whether the sink's names hold it.
// The answer: E <event>... M <ffmetadata text>... Q <query>... Y <rank>... G <rank>... P <rank/fileName>... R <count>
//   events in order: S,rank,videoId,fileName | P,rank,D|F,percent (- unknown, the 16 hex digits of the bits; the Kotlin side
//   writes N for a percent that is not finite) | D,rank,fileName,DESCRIPTION|ONLINE|NOT_FOUND|SEARCH_OFF|- | K,rank,reason |
//   F,rank,message | J,COMPLETED|CANCELLED|FAILED,succeeded,skipped,failed; the text of the ffmetadata file of each tag run;
//   each lyrics query artist/title/album/durationSeconds; the rank folder of each yt-dlp run (Y) and tag run (G); the rank
//   folder of each put's source and its file name; the entries left in the temporary folder after the job.
using Generator = QString (*)(Rng&);

// Registers the generator of a kind; a later registration of the same kind replaces the earlier.
void registerKind(char kind, Generator gen);
Generator generatorFor(char kind);      // nullptr when the kind has no generator
void registerGenerators();              // the place where each kind calls registerKind (gen.cpp)

QString token(Rng&);
QString text(Rng&, int maxTokens = 40);
std::optional<QString> nullable(Rng&);  // null 8%, empty 8%, white space only 8%, otherwise text()

template <typename T, std::size_t N>
const T& pick(Rng& rng, const T (&items)[N]) {
    return items[rng.between(0, int(N) - 1)];
}

}  // namespace xgs::diff
