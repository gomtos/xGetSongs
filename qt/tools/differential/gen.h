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
