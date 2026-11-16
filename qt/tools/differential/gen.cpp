#include "gen.h"

#include <QHash>
#include <QStringView>

#include <algorithm>
#include <bit>
#include <iterator>
#include <limits>
#include <optional>

#include "codec.h"
#include "text/kt_text.h"

namespace xgs::diff {

using namespace Qt::StringLiterals;

double Rng::next() {
    seed += 0x6D2B79F5u;
    quint32 t = seed;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return double(t ^ (t >> 14)) / 4294967296.0;
}

int Rng::between(int low, int high) {
    return low + int(next() * double(high - low + 1));
}

bool Rng::chance(double p) {
    return next() < p;
}

namespace {

QHash<char, Generator>& generators() {
    static QHash<char, Generator> table;
    return table;
}

// The material of shell/tools/differential/fuzz.mts, plus the letters whose case mapping is tricky.
constexpr QStringView kAscii = u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr QStringView kPunct = u" .-_()[]{}'&,;!@#$%^+=~`";
constexpr QStringView kForbidden = u"\\/:*?\"<>|";
// (QStringView stops a literal at its first NUL, so U+0000 gets an explicit length.)
constexpr QStringView kControl[] = {QStringView(u"\0", 1), u"\u0001", u"\t", u"\n", u"\r", u"\u001b", u"\u001c", u"\u001f", u"\u007f"};
constexpr QStringView kSpaces[] = {u" ", u"　", u" ", u" ", u"﻿",
                                   u"​", u"᠎", u"\u0085", u" ", u" "};
constexpr QStringView kKorean = u"가나다라마바사아자차카타파하소연퇴사할게여기안";
constexpr QStringView kEmoji[] = {u"😀", u"🎵", u"🇰🇷", u"👨‍👩‍👧", u"𝒜"};
constexpr QStringView kLone[] = {u"\xD83D", u"\xDE00", u"\xDBFF"};
constexpr QStringView kDevices[] = {u"CON", u"PRN", u"AUX", u"NUL", u"COM1", u"LPT9",
                                    u"con", u"Nul", u"com0", u"LPT10", u"COM9", u"lpt1"};
constexpr QStringView kDeviceTails[] = {u"", u".", u" .", u". ", u".x", u"..", u" "};
constexpr QStringView kDeviceExtensions[] = {u"", u".", u".x", u" .txt", u". ", u".tar.gz"};
constexpr QStringView kTricky[] = {u"ß", u"İ", u"ı", u"Σ", u"ς", u"σ", u"ǅ", u"ﬁ", u"ŉ", u"Ǆ", u"K", u"Å"};
constexpr QStringView kRunUnits[] = {u"a", u"가", u"😀", u"A.", u"🎵", u"ab"};
// Kotlin white space only (Char.isWhitespace), for the "white space only" strings of nullable().
constexpr QStringView kWhitespace[] = {u" ", u"\t", u"\n", u"\r", u"\u000b", u"\u001c", u"\u001f",
                                       u" ", u"　", u" ", u" ", u" ", u" "};

QString charOf(Rng& rng, QStringView set) {
    return QString(set[rng.between(0, int(set.size()) - 1)]);
}

// ---- kind S: Kotlin's String.lowercase/uppercase/trim/trimStart/trimEnd/isBlank and the ignoreCase family ----
constexpr QStringView kStringOps[] = {u"lower", u"upper", u"trim", u"trimStart", u"trimEnd", u"blank",
                                      u"ci-starts", u"ci-ends", u"ci-contains", u"ci-index", u"ci-replace", u"eqic",
                                      u"index", u"replace", u"ci-starts-from"};

// The alphabet of the ignoreCase ops: few letters (so that matches happen), both cases, the tricky letters, a supplementary
// cased pair (Deseret U+10400 / U+10428), an emoji, Korean and unpaired surrogates.
constexpr QStringView kCiPieces[] = {u"a", u"A", u"b", u"B", u"k", u"K", u"s", u"S", u"i", u"I", u"\u00df", u"\u0130", u"\u0131",
                                     u"\u03a3", u"\u03c2", u"\u03c3", u"\u01c5", u"\u01c6", u"\u01c4", u"\ufb01", u"\u0149",
                                     u"\u212a", u"\u212b", u"\u00e5", u"\U00010400", u"\U00010428", u"\U00010400", u"\U00010428",
                                     u"\U0001f600", u"\uac00", u"\xD83D", u"\xDC00", u"."};
// Single units for eqic (a supplementary letter is two units, so it cannot be one).
constexpr char16_t kCiUnits[] = {u'a', u'A', u'b', u'B', u'k', u'K', u's', u'S', u'i', u'I', u'\u00df', u'\u0130', u'\u0131',
                                 u'\u03a3', u'\u03c2', u'\u03c3', u'\u01c5', u'\u01c6', u'\u01c4', u'\ufb01', u'\u0149', u'\u212a',
                                 u'\u212b', u'\u00e5', u'\uac00', u'\xD83D', u'\xDC00', u'.', u'\u00e9', u'\u00c9'};

QString ciText(Rng& rng) {
    if (rng.chance(0.04)) return text(rng, 8);
    QString result;
    const int length = rng.between(0, 14);
    for (int i = 0; i < length; ++i) result += pick(rng, kCiPieces).toString();
    return result;
}

// The same letters in the other case: all upper, all lower, or each single unit at random (surrogates stay).
QString flipCase(Rng& rng, const QString& value) {
    const int mode = rng.between(0, 2);
    if (mode == 0) return kt::uppercase(value);
    if (mode == 1) return kt::lowercase(value);
    QString result = value;
    for (QChar& c : result) {
        if (c.isSurrogate()) continue;
        c = rng.chance(0.5) ? kt::uppercaseChar(c) : kt::lowercaseChar(c);
    }
    return result;
}

// A needle that is likely to match: a slice of the text, its case sometimes flipped; sometimes empty, sometimes longer than
// the text, sometimes unrelated.
QString ciNeedle(Rng& rng, const QString& value) {
    const double roll = rng.next();
    if (roll < 0.08) return QString();
    if (roll < 0.15) return flipCase(rng, value) + pick(rng, kCiPieces).toString();
    if (roll < 0.25) return ciText(rng);
    if (value.isEmpty()) return pick(rng, kCiPieces).toString();
    const int from = rng.between(0, int(value.size()) - 1);
    const int length = rng.between(1, int(std::min<qsizetype>(value.size() - from, rng.chance(0.7) ? 4 : 14)));
    QString slice = value.mid(from, length);
    // anchor the slice at an end now and then, so that starts/ends match
    if (rng.chance(0.25)) slice = value.left(length);
    else if (rng.chance(0.25)) slice = value.right(length);
    // now and then a slice whose last unit is another one: the JVM's ignoreCase match stops comparing when either region is
    // used up, so after a surrogate pair on one side the other side's last unit may never be looked at
    if (rng.chance(0.12)) slice[slice.size() - 1] = QChar(pick(rng, kCiUnits));
    return rng.chance(0.55) ? flipCase(rng, slice) : slice;
}

QString generateS(Rng& rng) {
    const QString op = pick(rng, kStringOps).toString();
    if (op == u"eqic") {
        const char16_t a = pick(rng, kCiUnits);
        const double roll = rng.next();
        char16_t b = pick(rng, kCiUnits);
        if (roll < 0.15) b = a;
        else if (roll < 0.5) b = rng.chance(0.5) ? kt::uppercaseChar(QChar(a)).unicode() : kt::lowercaseChar(QChar(a)).unicode();
        return op + u'\t' + hex(QString(QChar(a))) + u'\t' + hex(QString(QChar(b)));
    }
    if (op == u"ci-starts-from") {
        // startsWith(prefix, from, ignoreCase = true): a prefix sliced from the text at `from` (sometimes case-flipped or with
        // another last unit), sometimes empty or unrelated; `from` in -2..length+2. The pieces bring surrogate pairs, lone
        // surrogates and cased supplementary letters; now and then the lone-high-before-a-pair shape sits at `from`.
        QString value = ciText(rng);
        if (rng.chance(0.1)) {
            static constexpr QStringView pairs[] = {u"\U0001F600", u"\U00010400", u"\U00010428"};
            const QString pair = pick(rng, pairs).toString();
            value = ciText(rng).left(4) + (rng.chance(0.5) ? QString(pair.at(0)) + pair : pair) + ciText(rng).left(3);
        }
        const int length = int(value.size());
        const int from = rng.between(-2, length + 2);
        QString prefix;
        const double roll = rng.next();
        if (roll < 0.08) {
            prefix = QString();
        } else if (roll < 0.2) {
            prefix = ciText(rng).left(rng.between(1, 4));
        } else {
            const int at = std::clamp(from, 0, length);
            prefix = value.mid(at, rng.between(1, rng.chance(0.7) ? 4 : 10));
            if (prefix.isEmpty()) prefix = pick(rng, kCiPieces).toString();
            if (rng.chance(0.12)) prefix[prefix.size() - 1] = QChar(pick(rng, kCiUnits));
            if (rng.chance(0.1)) prefix += pick(rng, kCiPieces).toString();
            if (rng.chance(0.55)) prefix = flipCase(rng, prefix);
        }
        return op + u'\t' + hex(value) + u'\t' + hex(prefix) + u'\t' + QString::number(from);
    }
    if (op.startsWith(u"ci-") || op == u"index" || op == u"replace") {
        QString value = ciText(rng);
        QString needle = ciNeedle(rng, value);
        if (rng.chance(0.08)) {
            // A lone high surrogate right before a pair with the same high half on one side, the pair and one more unit on
            // the other: the JVM's ignoreCase match ends when either region is used up, so the shorter side's last unit is
            // never compared (kt::regionMatches must do the same). Either side may be the text; the shape sits at the start,
            // the end or inside.
            static constexpr QStringView pairs[] = {u"\U0001F600", u"\U00010400", u"\U00010428", u"\U0001F3B5"};
            const QString pair = pick(rng, pairs).toString();
            const QString loneThenPair = QString(pair.at(0)) + pair + QChar(pick(rng, kCiUnits));
            const QString pairThenOne = pair + QChar(pick(rng, kCiUnits));
            const bool textHasLone = rng.chance(0.5);
            const QString inText = textHasLone ? loneThenPair : pairThenOne;
            needle = textHasLone ? pairThenOne : loneThenPair;
            const double where = rng.next();
            value = (where < 0.35 ? QString() : ciText(rng).left(4)) + inText + (where >= 0.35 && where < 0.7 ? QString() : ciText(rng).left(4));
        }
        QString out = op + u'\t' + hex(value) + u'\t' + hex(needle);
        if (op == u"ci-index" || op == u"index") out += u'\t' + QString::number(rng.between(-2, int(value.size()) + 2));
        if (op == u"ci-replace" || op == u"replace") out += u'\t' + hex(rng.chance(0.15) ? QString() : ciText(rng));
        return out;
    }
    if (op == u"blank" && rng.chance(0.4)) {
        // isBlank answers true for empty and white space only strings: give that answer a floor of its own (text() is
        // seldom blank). One in four of them gets a last unit that is not white space, so the answer flips.
        QString value;
        const int length = rng.between(0, 5);
        for (int i = 0; i < length; ++i) value += pick(rng, kWhitespace).toString();
        if (rng.chance(0.25)) value += token(rng);
        return op + u'\t' + hex(value);
    }
    return op + u'\t' + hex(text(rng));
}

// ---- kind C: URL-like input for the classifier (classifierInput() of shell/tools/differential/fuzz.mts) ----
constexpr QStringView kIdChars = u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";

QString idOf(Rng& rng, int length) {
    QString id;
    for (int i = 0; i < length; ++i) id += kIdChars[rng.between(0, int(kIdChars.size()) - 1)];
    return id;
}

QString videoIdOf(Rng& rng) {
    if (rng.chance(0.85)) return idOf(rng, 11);
    return idOf(rng, rng.between(0, 14)) + (rng.chance(0.2) ? QString(QChar(0x00e9)) : QString());
}

QString playlistIdOf(Rng& rng) {
    // fuzz.mts picks among PL UU LL FL OL RD PL XX; RD (an automatic mix) is more likely here, for the statistics
    static constexpr QStringView prefixes[] = {u"PL", u"UU", u"LL", u"FL", u"OL", u"RD", u"PL", u"XX", u"RD", u"RD"};
    return pick(rng, prefixes).toString() + idOf(rng, rng.between(8, 36));
}

// What may surround an address: an ordinary space, a Kotlin or JavaScript white space, a control.
QString padOf(Rng& rng) {
    const int spaces = int(std::size(kSpaces));
    const int count = 3 + spaces + int(std::size(kControl));
    const int index = rng.between(0, count - 1);
    if (index < 3) return index == 2 ? QStringLiteral("\t") : QStringLiteral(" ");
    if (index < 3 + spaces) return kSpaces[index - 3].toString();
    return kControl[index - 3 - spaces].toString();
}

QString generateC(Rng& rng) {
    const double shape = rng.next();
    if (shape < 0.12) {
        // nothing but white space (possibly nothing at all): rejected as empty
        QString blank;
        const int length = rng.between(0, 3);
        for (int i = 0; i < length; ++i) blank += padOf(rng);
        return hex(blank);
    }
    if (shape < 0.2) {
        static constexpr QStringView garbage[] = {u"hello world", u"PLshort", u"PLabc; rm -rf /", u"youtu.be", u"a.b", u"/", u".", u"RD", u"x"};
        return hex(pick(rng, garbage).toString());
    }
    if (shape < 0.36) {
        const double kind = rng.next();
        const QString id = kind < 0.33 ? playlistIdOf(rng) : kind < 0.66 ? videoIdOf(rng) : u"RD"_s + idOf(rng, rng.between(10, 30));
        return hex((rng.chance(0.1) ? padOf(rng) : QString()) + id + (rng.chance(0.1) ? padOf(rng) : QString()));
    }
    static constexpr QStringView schemes[] = {u"https://", u"http://", u"", u"HTTPS://", u"ftp://", u"//"};
    static constexpr QStringView hosts[] = {u"www.youtube.com", u"youtube.com", u"m.youtube.com", u"music.youtube.com", u"youtu.be",
                                            u"WWW.YOUTUBE.COM", u"www.youtube.com.evil.com", u"evil.com", u"youtube.com@evil.com",
                                            u"www.youtube.com:443", u"user@youtu.be", u"youtube.co", u"",
                                            u"www.youtube.com", u"youtube.com", u"youtu.be"};
    const QString scheme = pick(rng, schemes).toString();
    const QString host = pick(rng, hosts).toString();
    const QString paths[] = {u"/watch"_s, u"/playlist"_s, u"/shorts/"_s + videoIdOf(rng), u"/embed/"_s + videoIdOf(rng),
                             u"/live/"_s + videoIdOf(rng), u"/"_s, QString(), u"/"_s + videoIdOf(rng), u"/watch/"_s,
                             u"//watch"_s, u"/shorts"_s, u"/playlist"_s, u"/watch"_s};
    const QString path = pick(rng, paths);
    QStringList params;
    if (rng.chance(0.7)) params.append(u"v="_s + videoIdOf(rng));
    if (rng.chance(0.6)) params.append(u"list="_s + playlistIdOf(rng));
    static constexpr QStringView extras[] = {u"t=10s", u"index=3", u"si=abc", u"&", u"=", u"v", u"list=", u"v=", u"x=y=z"};
    for (const QStringView extra : extras) {
        if (rng.chance(0.15)) params.append(extra.toString());
    }
    for (int index = int(params.size()) - 1; index > 0; --index) {
        const int other = rng.between(0, index);
        params.swapItemsAt(index, other);
    }
    const QString query = params.isEmpty() ? QString() : u"?"_s + params.join(u'&');
    const QString fragment = rng.chance(0.1) ? u"#frag"_s : QString();
    const QString leading = rng.chance(0.1) ? padOf(rng) : QString();
    const QString trailing = rng.chance(0.1) ? padOf(rng) : QString();
    return hex(leading + scheme + host + path + query + fragment + trailing);
}

// ---- kinds F, D, G: file names and folder names (the F/D/G loops of shell/tools/differential/fuzz.mts) ----
// The limits count UTF-16 units (80 for an artist or a folder, 180 for a whole name), so the generators also make texts whose
// length lands within +-3 of a limit, with an emoji, an unpaired surrogate, a dot or a space at the cut. The filler has no
// white space, so the length of the text is the length the formatter sees.
constexpr QStringView kPairs[] = {u"😀", u"🎵", u"𝒜"};

QString nearLimit(Rng& rng, int limit) {
    const int target = limit + rng.between(-3, 3);
    QString value;
    value.reserve(target + 2);
    while (value.size() < target) {
        const double roll = rng.next();
        if (roll < 0.5) value += charOf(rng, kAscii);
        else if (roll < 0.85) value += charOf(rng, kKorean);
        else value += rng.chance(0.5) ? QChar(u'.') : QChar(u'-');
    }
    const double kind = rng.next();
    if (kind >= 0.85) return value;
    int at = limit - 1 - rng.between(0, 3);
    if (kind < 0.4) {
        at = std::clamp(at, 0, target - 2);
        return value.left(at) + pick(rng, kPairs).toString() + value.mid(at + 2);
    }
    at = std::clamp(at, 0, target - 1);
    QString mark;
    if (kind < 0.75) mark = pick(rng, kLone).toString();
    else mark = rng.chance(0.5) ? QStringLiteral(" ") : QStringLiteral(".");
    return value.left(at) + mark + value.mid(at + 1);
}

// nullable() (null, empty and white space only 8% each), with some of the texts cut near a limit.
std::optional<QString> nullableNear(Rng& rng, int limit) {
    const double roll = rng.next();
    if (roll < 0.08) return std::nullopt;
    if (roll < 0.16) return QString();
    if (roll < 0.24) {
        QString blank;
        const int length = rng.between(1, 4);
        for (int i = 0; i < length; ++i) blank += pick(rng, kWhitespace);
        return blank;
    }
    if (roll < 0.5) return nearLimit(rng, limit);
    return text(rng);
}

QString generateF(Rng& rng) {
    static constexpr int badRanks[] = {0, 1000, -5, 5000};
    const int rank = rng.chance(0.03) ? pick(rng, badRanks) : rng.between(1, 999);
    const bool includeRank = rng.chance(0.5);
    const QString artist = rng.chance(0.4) ? nearLimit(rng, 80) : text(rng);
    QString title;
    if (rng.chance(0.4)) {
        const int prefix = (includeRank ? 4 : 0) + int(std::min<qsizetype>(artist.size(), 80)) + 3;
        title = nearLimit(rng, 180 - prefix);
    } else {
        title = text(rng);
    }
    return QString::number(rank) + u'\t' + (includeRank ? u"1"_s : u"0"_s) + u'\t' + hex(artist) + u'\t' + hex(title);
}

QString generateD(Rng& rng) {
    return hex(nullableNear(rng, 80));
}

QString generateG(Rng& rng) {
    const bool isPlaylist = rng.chance(0.5);
    const std::optional<QString> title = nullableNear(rng, 80);
    const std::optional<QString> typed = nullableNear(rng, 80);
    return (isPlaylist ? u"1"_s : u"0"_s) + u'\t' + hex(title) + u'\t' + hex(typed);
}

// ---- kind T: YouTube titles for the title parser, built from the pieces it looks for ----
constexpr QStringView kLeadEmoji[] = {u"🎤", u"🎶", u"❤️", u"♬", u"👨‍👩‍👧", u"⭐", u"←", u"⯿",
                                      u"Ⰰ", u"‍", u"️", u"\xD83D", u"\xDE00", u"🇰🇷"};
constexpr QStringView kBracketTags[] = {u"[MV]", u"[LIVE]", u"[#2024MAMA]", u"[Official MV]", u"[선공개]", u"[Ballad]",
                                        u"[DJ티비씨]", u"[]", u"[4K]", u"[M/V]"};
constexpr QStringView kTitleSeparators[] = {u" - ", u" – ", u" — ", u"_ "};
constexpr QStringView kSingleQuoteChars[] = {u"'", u"‘", u"’"};
constexpr QStringView kDoubleQuoteChars[] = {u"\"", u"“", u"”"};
constexpr QStringView kApostropheWords[] = {u"Don't", u"Girls'", u"It’s", u"rock'n'roll", u"'90s", u"you`re"};
constexpr QStringView kNoiseTokens[] = {u"Official Music Video", u"Official Video", u"Official Audio", u"Official MV",
                                        u"Official M/V", u"Performance Video", u"Special Video", u"Special Clip",
                                        u"Lyric Video", u"Music Video", u"Live Clip", u"Visualizer", u"Audio", u"M/V", u"MV"};
constexpr QStringView kTailBrackets[] = {u"[4K]", u"[유희열의 스케치북]", u"[Live]", u"[MV]", u"[]"};
constexpr QStringView kOtherParens[] = {u"(feat. Someone)", u"(Full. ver)", u"(with 적재)", u"()", u"( mv )"};
// Look-alikes that Kotlin's per-character ignoreCase comparison matches but String.lowercase() does not: U+0130 (İ),
// U+017F (long s, uppercase S), U+212A (Kelvin sign) and U+0131 (dotless i, uppercase I).
QString mixCase(Rng& rng, const QString& value) {
    QString result = value;
    const int mode = rng.between(0, 3);
    for (QChar& c : result) {
        if (mode == 0) c = kt::uppercaseChar(c);
        else if (mode == 1) c = kt::lowercaseChar(c);
        else if (mode == 2) c = rng.chance(0.5) ? kt::uppercaseChar(c) : kt::lowercaseChar(c);
    }
    if (rng.chance(0.12)) {
        // one look-alike, when the token has a letter it can stand for
        for (int tries = 0; tries < 4 && !result.isEmpty(); ++tries) {
            QChar& c = result[rng.between(0, int(result.size()) - 1)];
            if (c == u'i' || c == u'I') c = rng.chance(0.5) ? QChar(u'İ') : QChar(u'ı');
            else if (c == u's' || c == u'S') c = QChar(u'ſ');
            else if (c == u'k' || c == u'K') c = QChar(u'K');
            else continue;
            break;
        }
    }
    return result;
}

// A run of spaces: mostly one ASCII space, sometimes a tab, U+3000, another white space, or several.
QString spaceRun(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.75) return QStringLiteral(" ");
    if (roll < 0.83) return QStringLiteral("\t");
    if (roll < 0.9) return QStringLiteral(u"　");
    if (roll < 0.95) return pick(rng, kWhitespace).toString();
    return QString(u' ').repeated(rng.between(2, 4));
}

// Replaces each ASCII space with a spaceRun().
QString scatterSpaces(Rng& rng, const QString& value) {
    if (!rng.chance(0.35)) return value;
    QString result;
    result.reserve(value.size() + 8);
    for (const QChar c : value) {
        if (c == u' ') result += spaceRun(rng);
        else result += c;
    }
    return result;
}

QString titleLead(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.45) return QString();
    if (roll < 0.63) {
        QString lead = pick(rng, kLeadEmoji).toString();
        if (rng.chance(0.3)) lead += pick(rng, kLeadEmoji).toString();
        if (rng.chance(0.5)) lead += u' ';
        if (rng.chance(0.2)) lead += pick(rng, kBracketTags).toString() + u' ';
        return lead;
    }
    QString tags = pick(rng, kBracketTags).toString() + (rng.chance(0.85) ? u" "_s : QString());
    if (rng.chance(0.35)) tags += pick(rng, kBracketTags).toString() + u' ';
    if (rng.chance(0.1)) tags = u"["_s + text(rng, 4) + u"] ";
    return tags;
}

QString songTitle(Rng& rng) {
    QString title = text(rng, 8);
    if (rng.chance(0.25)) {
        const QString word = pick(rng, kApostropheWords).toString();
        title = rng.chance(0.5) ? word + u' ' + title : title + u' ' + word;
    }
    return title;
}

// The artist/title joint: none 30%, one of the four separators, or the title in quotes (closed with the same family, now and
// then left open or closed by a quote of the other family).
QString joinArtistAndTitle(Rng& rng, const QString& artist, const QString& title) {
    const double roll = rng.next();
    if (roll < 0.3) return artist + (rng.chance(0.6) ? u" "_s : QString()) + title;
    if (roll < 0.62) {
        const QString separator = pick(rng, kTitleSeparators).toString();
        if (rng.chance(0.2)) {
            // `Artist - 'Title'`
            const bool single = rng.chance(0.6);
            const QString open = single ? pick(rng, kSingleQuoteChars).toString() : pick(rng, kDoubleQuoteChars).toString();
            const QString close = single ? pick(rng, kSingleQuoteChars).toString() : pick(rng, kDoubleQuoteChars).toString();
            return artist + separator + open + title + close;
        }
        return artist + separator + title;
    }
    const bool single = rng.chance(0.6);
    const QString open = single ? pick(rng, kSingleQuoteChars).toString() : pick(rng, kDoubleQuoteChars).toString();
    const double shape = rng.next();
    QString close;
    if (shape < 0.8) close = single ? pick(rng, kSingleQuoteChars).toString() : pick(rng, kDoubleQuoteChars).toString();
    else if (shape < 0.9) close = single ? pick(rng, kDoubleQuoteChars).toString() : pick(rng, kSingleQuoteChars).toString();
    const QString before = rng.chance(0.9) ? u" "_s : QString();
    QString after = rng.chance(0.15) ? charOf(rng, kAscii) : QString();   // a letter right after the quote: not a closer
    if (rng.chance(0.25)) after += u" "_s + text(rng, 4);
    return artist + before + open + title + close + after;
}

QString titleTail(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.3) return QString();
    if (roll < 0.55) {
        const QString noise = mixCase(rng, pick(rng, kNoiseTokens).toString());
        return (rng.chance(0.9) ? u" "_s : QString()) + noise + (rng.chance(0.15) ? u" "_s + pick(rng, kNoiseTokens).toString() : QString());
    }
    if (roll < 0.7) {
        const QString inner = rng.chance(0.75) ? mixCase(rng, pick(rng, kNoiseTokens).toString()) : pick(rng, kOtherParens).toString();
        const QString padded = rng.chance(0.2) ? u" "_s + inner + u" " : inner;
        return u" ("_s + (inner.startsWith(u'(') ? inner.mid(1).chopped(1) : padded) + u")";
    }
    if (roll < 0.82) return (rng.chance(0.85) ? u" "_s : QString()) + pick(rng, kTailBrackets).toString();
    if (roll < 0.92) return u" | "_s + text(rng, 6);
    // noise then a pipe, or a dash before the noise
    return rng.chance(0.5) ? u" - "_s + mixCase(rng, pick(rng, kNoiseTokens).toString())
                           : u" "_s + pick(rng, kNoiseTokens).toString() + u" | " + text(rng, 4);
}

std::optional<QString> channelOf(Rng& rng) {
    std::optional<QString> channel = nullable(rng);
    if (channel && !kt::isBlank(*channel) && rng.chance(0.3)) {
        if (rng.chance(0.55)) {
            *channel += mixCase(rng, u" - Topic"_s);
            if (rng.chance(0.15)) *channel = pick(rng, kWhitespace).toString() + *channel + pick(rng, kWhitespace).toString();
        } else {
            *channel += rng.chance(0.5) ? u"VEVO"_s : mixCase(rng, u"Vevo"_s);
        }
    } else if (rng.chance(0.03)) {
        static constexpr QStringView bare[] = {u" - Topic", u"VEVO", u"vevo - Topic", u"  - TOPIC  ", u"ArianaGrandeVevo"};
        channel = pick(rng, bare).toString();
    }
    return channel;
}

QString generateT(Rng& rng) {
    const QString raw = rng.chance(0.04)
                            ? text(rng)
                            : scatterSpaces(rng, titleLead(rng) + joinArtistAndTitle(rng, text(rng, 6), songTitle(rng)) + titleTail(rng));
    return hex(raw) + u'\t' + hex(channelOf(rng)) + u'\t' + hex(nullable(rng)) + u'\t' + hex(nullable(rng));
}

// ---- kinds L and Y: video descriptions for LyricsExtractor.extract, lyrics texts for LyricsExtractor.tidy ----
// A description is made of lines: a head (text, links, hashtags, now and then a fake heading), a heading line, separator
// lines, the body, a terminator line of one of the seven kinds (or a near miss, or none) and a tail. The lines are joined
// with `\n`, `\r\n` or `\r` (one style per case, or a mix). 4% of the cases have a body of more than 30000 units: most of
// those are made of clean lines whose joined length lands within 3 units of the 30000 limit, with a surrogate pair at the
// ends of the lines next to the cut and sometimes a blank line right before it.

// One spelling for each of the ten markers (their keys are exactly the markers).
constexpr QStringView kHeadingWords[] = {u"Lyrics", u"Lyric", u"가사", u"歌詞", u"歌词", u"Lyrics / 가사", u"가사 Lyrics",
                                         u"Lyrics (Korean)", u"Lyrics - Kor", u"Lyrics (Original)"};
constexpr QStringView kHeadingFrames[] = {u"%1", u"%1", u"[%1]", u"[ %1 ]", u"【%1】", u"%1:", u"%1 :", u"- %1 -", u"■ %1 ■",
                                          u"🎵 %1 🎵", u"# %1", u"♬ %1 ♬"};
// Lines that look like a heading and are not one (a hashtag line, or more than a heading).
constexpr QStringView kFakeHeadings[] = {u"#Lyrics #가사", u"#가사", u"#lyrics", u"##가사", u"#Lyrics\t#가사", u"  #Lyrics  ",
                                         u"Lyrics: some words", u"Lyricsmith", u"[Lyrics] 2024", u"Lyrics (English)", u"가사집",
                                         u"Lyric video", u"Lyrics & Music", u"Korean lyrics"};
constexpr QStringView kSeparatorUnits = u"=-_*~#.+━─═\u3161\u2014\u25AC\uFF1D";
// The characters normalise() removes (zero width) or turns into a space (U+3164).
constexpr QStringView kInvisibles[] = {u"\u200B", u"\u200C", u"\u200D", u"\u2060", u"\uFEFF", u"\u3164"};
// What a blank line is made of (never a line break).
constexpr QStringView kBlankPieces[] = {u" ", u"\t", u"\u3000", u"\u3164", u"\u200B", u"\uFEFF", u"\u00A0", u"\u2003",
                                        u"\u000b", u"\u001f"};
// Lyric lines that look like a terminator or a heading and are not one.
constexpr QStringView kNearMisses[] = {u"(Follow me)", u"(Romantic)", u"(English version part)", u"(English Ver.)", u"(Info)",
                                       u"(Rom)", u"(Lyrics)", u"(가사)", u"(Credits to you)", u"(Ooh)", u"[Chorus]", u"[Verse 1]",
                                       u"[Bridge: Name]", u"[후렴]", u"# Line two", u"#", u"==", u"=-", u"..", u"===x===",
                                       u"--- ---", u"Romance in the air", u"Info is what I need", u"Lyrics by 소연", u"(번역가)",
                                       u"[]", u"()", u"[", u"(c", u"ⓒ", u"©"};
// The terminators, by kind (separator lines are made by separatorLine()).
constexpr QStringView kLinkLines[] = {u"https://example.invalid/a", u"http://example.invalid", u"Follow: www.example.invalid",
                                      u"Visit HTTPS://EXAMPLE.INVALID", u"(https://example.invalid)", u"Instagram: Http://x.invalid",
                                      u"WWW.EXAMPLE.INVALID", u"sns→https://example.invalid"};
constexpr QStringView kCopyrightLines[] = {u"© 2024 Example Label", u"  ©2024", u"ⓒ 2024 Example", u"(c) 2024 Example",
                                           u"(C) 2024 Example", u"Copyright 2024 Example", u"COPYRIGHT: Example",
                                           u"All rights reserved.", u"ALL RIGHTS RESERVED", u"copyright"};
constexpr QStringView kHashtagLines[] = {u"#example", u"#example #song", u"  #태그", u"##double", u"#🎵", u"#가사", u"#Lyrics",
                                         u"#1"};
constexpr QStringView kBoilerplateLines[] = {u"Provided to YouTube by Example Label", u"Released on: 2024-01-01",
                                             u"Auto-generated by YouTube.", u"  PROVIDED TO YOUTUBE",
                                             u"(Provided to YouTube by Example Label)", u"[Released on 2024]",
                                             u"Auto generated", u"provided"};
constexpr QStringView kBracketedLines[] = {u"[Rom]", u"[Romanized]", u"[Romanization]", u"[English Translation]", u"[Eng]",
                                           u"【번역】", u"[Translated]", u"[Credits]", u"[Info]", u"[Staff]", u"[Links]",
                                           u"[SNS]", u"[Follow us]", u"[번역]", u"[해석]", u"[영문]", u"[로마자]", u"[크레딧]",
                                           u"[정보]", u"  [ROM]  ", u"[Lyrics]", u"【가사】", u"[Lyrics / 가사]", u"[English Ver.]",
                                           u"【Romanized】", u"[Credits to you]"};
constexpr QStringView kParenthesisedLines[] = {u"(English)", u"(English Translation)", u"(Translation)", u"(Romanized)",
                                               u"(Romanization)", u"(번역)", u"(해석)", u"(영문)", u"(로마자)", u"(Credit)",
                                               u"(Credits)", u"( ENGLISH )", u"  (English)  ", u"(english translation)",
                                               u"(English-Translation)", u"(Roman-ized)"};
constexpr QStringView kHeadLines[] = {u"Example Artist - Example Song (Official Audio)", u"Listen on every platform.",
                                      u"Stream: https://example.invalid/s", u"#example #song", u"=======", u"Lyrics by 소연",
                                      u"Music video out now 🎬"};
constexpr QStringView kCleanUnits = u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789가나다라마바사아자차카타파하";

QString insertInvisible(Rng& rng, const QString& value) {
    const int at = rng.between(0, int(value.size()));
    return value.left(at) + pick(rng, kInvisibles).toString() + value.mid(at);
}

QString blankLine(Rng& rng) {
    if (rng.chance(0.5)) return QString();
    QString blank;
    const int length = rng.between(1, 4);
    for (int i = 0; i < length; ++i) blank += pick(rng, kBlankPieces).toString();
    return blank;
}

// 2..12 separator characters in any mix (2 is one short of a separator), now and then padded with spaces.
QString separatorLine(Rng& rng, int minLength = 2) {
    QString line;
    const bool one = rng.chance(0.5);
    const QChar first = kSeparatorUnits[rng.between(0, int(kSeparatorUnits.size()) - 1)];
    const int length = rng.between(minLength, 12);
    for (int i = 0; i < length; ++i) line += one ? first : kSeparatorUnits[rng.between(0, int(kSeparatorUnits.size()) - 1)];
    if (rng.chance(0.2)) line = spaceRun(rng) + line + spaceRun(rng);
    return line;
}

QString headingLine(Rng& rng) {
    QString line = pick(rng, kHeadingFrames).toString().arg(mixCase(rng, pick(rng, kHeadingWords).toString()));
    if (rng.chance(0.3)) line = insertInvisible(rng, line);
    if (rng.chance(0.1)) line = insertInvisible(rng, line);
    if (rng.chance(0.15)) line = spaceRun(rng) + line;
    if (rng.chance(0.15)) line += rng.chance(0.5) ? spaceRun(rng) : u"\u3164"_s;
    return line;
}

QString fakeHeadingLine(Rng& rng) {
    if (rng.chance(0.25)) return mixCase(rng, u"Lyrics by "_s) + text(rng, 4);
    QString line = pick(rng, kFakeHeadings).toString();
    if (rng.chance(0.4)) line = mixCase(rng, line);
    if (rng.chance(0.15)) line = insertInvisible(rng, line);
    return line;
}

// One lyric line: text, blank (empty, white space, U+3164 or zero width), a near miss, emoji and unpaired surrogates,
// trailing white space and indentation.
QString bodyLine(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.15) return blankLine(rng);
    if (roll < 0.25) return pick(rng, kNearMisses).toString();
    QString line = text(rng, 8);
    if (roll < 0.33) line += pick(rng, kEmoji).toString() + (rng.chance(0.5) ? pick(rng, kLone).toString() : QString());
    if (rng.chance(0.15)) line = pick(rng, kBlankPieces).toString() + line;
    if (rng.chance(0.2)) line += blankLine(rng);
    if (rng.chance(0.1)) line = insertInvisible(rng, line);
    return line;
}

// The terminator line: one of the seven kinds (separator, link, copyright, hashtag, boilerplate, bracketed heading,
// parenthesised heading), a near miss, or none (an empty optional).
std::optional<QString> terminatorLine(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.1) return std::nullopt;
    if (roll < 0.18) return pick(rng, kNearMisses).toString();
    QString line;
    switch (rng.between(0, 6)) {
        case 0: line = separatorLine(rng, 3); break;
        case 1: line = pick(rng, kLinkLines).toString(); break;
        case 2: line = pick(rng, kCopyrightLines).toString(); break;
        case 3: line = pick(rng, kHashtagLines).toString(); break;
        case 4: line = pick(rng, kBoilerplateLines).toString(); break;
        case 5: line = pick(rng, kBracketedLines).toString(); break;
        default: line = pick(rng, kParenthesisedLines).toString(); break;
    }
    if (rng.chance(0.35)) line = mixCase(rng, line);
    if (rng.chance(0.08)) line = insertInvisible(rng, line);
    if (rng.chance(0.05)) line += QChar(u'\0');
    return line;
}

// A line of exactly `length` units (a surrogate pair counts two) without white space at its ends, controls, invisible
// characters or anything a terminator looks for; a surrogate pair at its start or end when asked.
QString cleanLine(Rng& rng, int length, bool pairAtStart, bool pairAtEnd) {
    QString line;
    line.reserve(length);
    if (pairAtStart && length >= 2) line += pick(rng, kPairs).toString();
    const int body = length - ((pairAtEnd && length - line.size() >= 2) ? 2 : 0);
    while (line.size() < body) {
        const double roll = rng.next();
        const bool inside = !line.isEmpty() && line.size() < body - 1;
        if (roll < 0.12 && inside && !line.endsWith(u' ')) line += u' ';
        else if (roll < 0.18 && line.size() + 2 <= body) line += pick(rng, kPairs).toString();
        else line += kCleanUnits[rng.between(0, int(kCleanUnits.size()) - 1)];
    }
    if (line.size() < length) line += pick(rng, kPairs).toString();
    return line;
}

// A long, messy line: text with many tokens, or a run of one unit.
QString messyLongLine(Rng& rng) {
    if (rng.chance(0.4)) return pick(rng, kRunUnits).toString().repeated(rng.between(30, 300));
    QString line = text(rng, 120);
    if (rng.chance(0.3)) line += pick(rng, kEmoji).toString();
    return line;
}

// A body of more than 30000 units (see the comment of this section).
QStringList longBody(Rng& rng) {
    QStringList lines;
    qsizetype joined = 0;   // the length of the lines joined with `\n`
    const auto add = [&](const QString& line) {
        joined += (lines.isEmpty() ? 0 : 1) + line.size();
        lines.append(line);
    };
    if (rng.chance(0.35)) {
        const int until = 30000 + rng.between(0, 1500);
        add(messyLongLine(rng));
        while (joined < until) add(rng.chance(0.1) ? blankLine(rng) : messyLongLine(rng));
        return lines;
    }
    const int target = 30000 + rng.between(-3, 3);
    const bool pairs = rng.chance(0.6);
    add(cleanLine(rng, rng.between(100, 600), false, pairs && rng.chance(0.5)));
    while (target - joined - 1 > 700) {
        // a blank line now and then, never two in a row (a run would be collapsed and change the length)
        if (!lines.last().isEmpty() && rng.chance(0.06)) add(QString());
        else add(cleanLine(rng, rng.between(100, 600), pairs && rng.chance(0.3), pairs && rng.chance(0.3)));
    }
    if (!lines.last().isEmpty() && rng.chance(0.25)) add(QString());
    add(cleanLine(rng, int(target - joined - 1), pairs && rng.chance(0.5), pairs));   // the line that lands on the target
    const int more = rng.between(1, 3);
    for (int i = 0; i < more; ++i) add(cleanLine(rng, rng.between(50, 400), pairs && rng.chance(0.6), pairs && rng.chance(0.3)));
    return lines;
}

// Null, empty and white space only 6% each.
std::optional<QString> blankKind(Rng& rng, double roll) {
    if (roll < 0.06) return std::nullopt;
    if (roll < 0.12) return QString();
    QString blank;
    const int length = rng.between(1, 4);
    for (int i = 0; i < length; ++i) blank += pick(rng, kWhitespace);
    return blank;
}

QString joinWithLineEnds(Rng& rng, const QStringList& lines) {
    static constexpr QStringView ends[] = {u"\n", u"\r\n", u"\r"};
    // 3% of the line breaks are odd: a zero-width character, U+3164 or a control between `\r` and `\n` (normalise removes
    // the zero-width characters before it unifies the line breaks, and the controls after it), `\n\r`, `\r\r\n`.
    static constexpr QStringView odd[] = {u"\r\x200B\n", u"\r\xFEFF\n", u"\r\x2060\n", QStringView(u"\r\0\n", 3), u"\r\x0085\n",
                                          u"\r\x0001\n", u"\n\r", u"\r\r\n", u"\r\x3164\n"};
    const int style = rng.between(0, 3);   // 3: a mix
    const auto lineEnd = [&]() -> QString {
        if (rng.chance(0.03)) return pick(rng, odd).toString();
        return (style == 3 ? pick(rng, ends) : ends[style]).toString();
    };
    QString result;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (i > 0) result += lineEnd();
        result += lines.at(i);
    }
    if (rng.chance(0.3)) result += lineEnd();
    return result;
}

QString generateL(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.18) return hex(blankKind(rng, roll));
    QStringList lines;
    const int head = rng.between(0, 5);
    for (int i = 0; i < head; ++i) {
        const double kind = rng.next();
        if (kind < 0.5) lines.append(text(rng, 10));
        else if (kind < 0.7) lines.append(pick(rng, kLinkLines).toString());
        else if (kind < 0.85) lines.append(pick(rng, kHashtagLines).toString());
        else lines.append(pick(rng, kHeadLines).toString());
    }
    // a long body mostly comes after a real heading, so that the limit is reached
    const bool longCase = rng.chance(0.04);
    const double heading = longCase && rng.chance(0.85) ? 0.5 : rng.next();
    if (heading < 0.15) lines.append(fakeHeadingLine(rng));
    if (heading < 0.7) lines.append(headingLine(rng));
    else if (heading < 0.85) lines.append(fakeHeadingLine(rng));
    const int separators = rng.between(0, 2);
    for (int i = 0; i < separators; ++i) lines.append(rng.chance(0.2) ? blankLine(rng) : separatorLine(rng));
    if (longCase) {
        lines.append(longBody(rng));
    } else {
        const int body = rng.between(0, 12);
        for (int i = 0; i < body; ++i) lines.append(bodyLine(rng));
    }
    if (const std::optional<QString> terminator = terminatorLine(rng)) lines.append(*terminator);
    const int tail = rng.between(0, 3);
    for (int i = 0; i < tail; ++i) {
        const double kind = rng.next();
        if (kind < 0.4) lines.append(text(rng, 10));
        else if (kind < 0.55) lines.append(pick(rng, kLinkLines).toString());
        else if (kind < 0.7) lines.append(pick(rng, kCopyrightLines).toString());
        else if (kind < 0.85) lines.append(headingLine(rng));   // a second heading: the first one wins
        else lines.append(bodyLine(rng));
    }
    return hex(joinWithLineEnds(rng, lines));
}

QString generateY(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.18) return hex(blankKind(rng, roll));
    QStringList lines;
    const int lead = rng.chance(0.3) ? rng.between(1, 3) : 0;
    for (int i = 0; i < lead; ++i) lines.append(blankLine(rng));
    if (rng.chance(0.04)) {
        lines.append(longBody(rng));
    } else {
        const int body = rng.between(0, 14);
        for (int i = 0; i < body; ++i) {
            lines.append(bodyLine(rng));
            if (rng.chance(0.2)) {
                const int run = rng.between(1, 5);
                for (int j = 0; j < run; ++j) lines.append(blankLine(rng));
            }
        }
    }
    const int trail = rng.chance(0.3) ? rng.between(1, 3) : 0;
    for (int i = 0; i < trail; ++i) lines.append(blankLine(rng));
    return hex(joinWithLineEnds(rng, lines));
}

// ---- kinds R and N: log lines for LogRedaction.redactPaths and LogRedaction.redact ----
// A log line is made of fragments joined by list punctuation, arrows, reasons and line breaks. A fragment is plain text, a
// drive path (`\`, `/`, doubled or mixed separators, now and then a trailing or a tripled separator), a UNC or verbatim path
// (`\\host\share`, `\\?\C:\`, `\\?\UNC\host`, `\\.\C:\`, and broken prefixes), a `file:///` URI in mixed case (also with the
// look-alikes U+0130 and U+0131 that the JVM's ignoreCase comparison matches), or a decoy that is not a path. 12% of the
// lines are decoys and safe words only. 4% are longer than 4000 units: a surrogate pair (or a lone surrogate) at unit 3999,
// a path that crosses the cut, or just long.
constexpr QStringView kPathPieces[] = {u"Music", u"My Playlist", u"Some User", u"001 A - B.mp3", u"가수 - 제목.m4a", u"a",
                                       u"x.y.z", u"job-1", u"f251.webm", u"$1", u"🎵", u"😀", u"\xD83D", u"\xDE00", u"노래 모음",
                                       u".", u"..", u" ", u"a.b", u"(feat. B) [x]", u"İ", u"ß", u"AppData", u"Roaming", u"1",
                                       u"song - ", u"x-", u",", u";", u" , ", u"%20"};
constexpr QStringView kHosts[] = {u"NAS", u"server", u"h", u"192.168.0.1", u"호스트", u"srv-1", u"Music Share"};
constexpr QStringView kDecoys[] = {u"https://a.b/c", u"Music\\a.mp3", u"..\\x", u"/usr/lib", u"abC:\\x", u"http://example.com/a/b",
                                   u"https://example.com:8080/x", u"ftp://host/dir", u".\\a\\b.mp3", u"a/b/c.mp3",
                                   u"\\just\\absolute\\drive.mp3", u"a\\b", u"C:", u"9C:\\x", u"x1d:/y", u"가:\\x", u"1:\\x",
                                   u"file://C:/x", u"file:///x/y", u"\\\\", u"\\\\\\x", u"C:\\\\\\x", u"wordZ:/a"};
constexpr QStringView kLogWords[] = {u"NoSuchFileException: ", u"ERROR: ", u"open ", u"failed: ", u"cannot read ",
                                     u"at 10:30 the job (a:b) ended", u"HTTP 403: Forbidden", u"연결이 끊어졌습니다", u"'", u"\"",
                                     u"[Errno 13] Permission denied: ", u"FileSystemException: ", u"-> arrow ->", u"5 / 3 | 2"};
constexpr QStringView kJoiners[] = {u", ", u"; ", u" -> ", u": 이유", u": Access is denied", u"\n", u"\r\n", u" ", u"", u",",
                                    u" | ", u" (", u") ", u", ", u"; ", u" -> ", u": The process cannot access the file"};
constexpr QStringView kSafeJoiners[] = {u" ", u", ", u"\n", u" and "};

QString pathPart(Rng& rng) {
    QString part;
    const int pieces = rng.between(1, 3);
    for (int i = 0; i < pieces; ++i) part += pick(rng, kPathPieces).toString();
    if (rng.chance(0.12)) part += token(rng);
    if (rng.chance(0.06)) part += text(rng, 3);
    return part;
}

QString driveLetter(Rng& rng) {
    static constexpr QStringView letters = u"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    return charOf(rng, letters);
}

// A separator by style: 0 `\`, 1 `/`, 2 `\\`, 3 `//`, 4 mixed (now and then a tripled one).
QString separatorOf(Rng& rng, int style) {
    static constexpr QStringView mixed[] = {u"\\", u"/", u"\\\\", u"\\/", u"/\\", u"\\", u"/"};
    switch (style) {
        case 0: return u"\\"_s;
        case 1: return u"/"_s;
        case 2: return u"\\\\"_s;
        case 3: return u"//"_s;
        default: return rng.chance(0.06) ? u"\\\\\\"_s : pick(rng, mixed).toString();
    }
}

// The separators and parts after a prefix: `\part\part...`, now and then ending with a separator.
QString partsOf(Rng& rng, int style, int maxParts = 6) {
    QString out;
    const int parts = rng.between(1, maxParts);
    for (int i = 0; i < parts; ++i) out += separatorOf(rng, style) + pathPart(rng);
    if (rng.chance(0.08)) out += separatorOf(rng, style);
    return out;
}

QString drivePath(Rng& rng) {
    const double roll = rng.next();
    const int style = roll < 0.45 ? 0 : roll < 0.6 ? 1 : roll < 0.72 ? 2 : roll < 0.77 ? 3 : 4;
    return driveLetter(rng) + u':' + partsOf(rng, style);
}

QString mixedCaseOf(Rng& rng, QStringView value) {
    QString out;
    for (const QChar c : value) out += rng.chance(0.5) ? kt::uppercaseChar(c) : kt::lowercaseChar(c);
    return out;
}

QString uncPath(Rng& rng) {
    const int style = rng.chance(0.75) ? 0 : 4;
    const double roll = rng.next();
    if (roll < 0.3) return u"\\\\"_s + pick(rng, kHosts).toString() + partsOf(rng, style);
    if (roll < 0.5) return u"\\\\?\\"_s + driveLetter(rng) + u':' + partsOf(rng, style);
    if (roll < 0.7) {
        const QString unc = rng.chance(0.5) ? u"UNC"_s : mixedCaseOf(rng, u"unc");
        return u"\\\\?\\"_s + unc + u'\\' + pick(rng, kHosts).toString() + partsOf(rng, style);
    }
    if (roll < 0.85) return u"\\\\.\\"_s + driveLetter(rng) + u':' + partsOf(rng, style);
    if (roll < 0.9) return u"\\\\.\\UNC\\"_s + pick(rng, kHosts).toString() + partsOf(rng, style);
    // broken prefixes: no host, nothing after UNC, a verbatim prefix without a drive, a host without parts
    static constexpr QStringView broken[] = {u"\\\\\\x\\y", u"\\\\?\\UNC\\", u"\\\\?\\UNC\\\\x", u"\\\\?\\x\\y", u"\\\\.\\",
                                             u"\\\\host", u"\\\\?\\C:", u"\\\\?C:\\x", u"\\\\h/x", u"\\\\?\\C:/x"};
    return pick(rng, broken).toString();
}

QString fileUri(Rng& rng) {
    QString prefix;
    const double roll = rng.next();
    if (roll < 0.35) prefix = u"file:///"_s;
    else if (roll < 0.85) prefix = mixedCaseOf(rng, u"file:///");
    else prefix = rng.chance(0.5) ? u"f\u0130le:///"_s : u"F\u0131LE:///"_s;
    return prefix + driveLetter(rng) + u':' + partsOf(rng, rng.chance(0.8) ? 1 : 4);
}

QString plainText(Rng& rng) {
    if (rng.chance(0.5)) return pick(rng, kLogWords).toString();
    return text(rng, 10);
}

QString decoyOnlyText(Rng& rng) {
    QString out;
    const int fragments = rng.between(1, 4);
    for (int i = 0; i < fragments; ++i) {
        if (i > 0) out += pick(rng, kSafeJoiners).toString();
        out += rng.chance(0.6) ? pick(rng, kDecoys).toString() : pick(rng, kLogWords).toString();
    }
    return out;
}

QString logFragment(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.24) return plainText(rng);
    if (roll < 0.5) return drivePath(rng);
    if (roll < 0.7) return uncPath(rng);
    if (roll < 0.8) return fileUri(rng);
    return pick(rng, kDecoys).toString();
}

QString logText(Rng& rng) {
    if (rng.chance(0.12)) return decoyOnlyText(rng);
    QString out;
    const int fragments = rng.between(1, 6);
    for (int i = 0; i < fragments; ++i) {
        if (i > 0) out += pick(rng, kJoiners).toString();
        out += logFragment(rng);
    }
    return out;
}

// Log text of at least `length` units.
QString filler(Rng& rng, qsizetype length) {
    QString out;
    while (out.size() < length) out += logText(rng) + pick(rng, kJoiners).toString();
    return out;
}

// A path whose parts are long, so that it crosses the cut.
QString longPartsPath(Rng& rng) {
    QString path = rng.chance(0.7) ? driveLetter(rng) + u":" : u"\\\\"_s + pick(rng, kHosts).toString();
    const int parts = rng.between(2, 8);
    for (int i = 0; i < parts; ++i) {
        path += rng.chance(0.8) ? u"\\"_s : u"/"_s;
        const int pieces = rng.between(3, 10);
        for (int j = 0; j < pieces; ++j) path += pick(rng, kPathPieces).toString();
    }
    return path;
}

QString longLogText(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.4) {
        // unit 3999: a surrogate pair (mostly), the low half of a pair (3998..3999), a lone high or a lone low surrogate;
        // half of them inside a path that is still running at the cut
        const QString tail = rng.chance(0.5) ? driveLetter(rng) + u":\\노래\\" + pathPart(rng) : QString();
        const double at = rng.next();
        QString mark;
        qsizetype before = 3999;
        if (at < 0.6) mark = pick(rng, kPairs).toString();
        else if (at < 0.75) { mark = pick(rng, kPairs).toString(); before = 3998; }
        else if (at < 0.9) mark = u"\xD83D"_s;
        else mark = u"\xDE00"_s;
        QString prefix = filler(rng, 4000).left(std::max<qsizetype>(0, before - tail.size())) + tail;
        while (prefix.size() < before) prefix += u'x';
        return prefix.left(before) + mark + logText(rng);
    }
    if (roll < 0.75) {
        // a path that starts shortly before the cut and runs over it
        return filler(rng, 4000).left(4000 - rng.between(1, 60)) + pick(rng, kSafeJoiners).toString() + longPartsPath(rng) +
               pick(rng, kJoiners).toString() + logText(rng);
    }
    return filler(rng, rng.between(4001, 9000));
}

QString redactText(Rng& rng) {
    return rng.chance(0.04) ? longLogText(rng) : logText(rng);
}

QString generateR(Rng& rng) {
    return hex(redactText(rng));
}

// ---- kind N: known names planted in the log text ----
constexpr QStringView kNameWords[] = {u"가수", u"Artist", u"A (feat. B) [x]$1", u"아이유", u"🎵 Song", u"İstanbul", u"Straße",
                                      u"제목", u"Title", u"A.*", u"$0$1", u"Ünïcode", u"\xD83D", u"KELVIN", u"σοφία"};
constexpr QStringView kExtensions[] = {u".m4a", u".mp3", u".webm", u".f251.webm", u".m4a", u".m4a"};
// Names that overlap the placeholders: without distinct() or with another order, the placeholders themselves change.
constexpr QStringView kPlaceholderNames[] = {u"파일명", u"<파일명>", u"경로>", u"<경로", u"파일명.m4a", u"<경로>", u"일명>",
                                             u"<파일"};
constexpr QStringView kOddNames[] = {u"a.b.c.m4a", u".m4a", u"abc.", u"...", u"ab.c", u"x.y", u"a b.c d"};
constexpr QStringView kShortNames[] = {u"ab", u"가나", u"😀", u"a", u"", u"A.", u".a"};
constexpr QStringView kBlankNames[] = {u" ", u"  ", u"\t", u"\u3000", u" \u00a0 "};

QString nameWord(Rng& rng) {
    return rng.chance(0.6) ? pick(rng, kNameWords).toString() : text(rng, 4);
}

QString nameOf(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.45) {
        return QString::number(rng.between(1, 999)).rightJustified(3, u'0') + u' ' + nameWord(rng) + u" - " + nameWord(rng) +
               (rng.chance(0.85) ? pick(rng, kExtensions).toString() : QString());
    }
    if (roll < 0.55) return pick(rng, kShortNames).toString();
    if (roll < 0.61) return pick(rng, kBlankNames).toString();
    if (roll < 0.71) return nameWord(rng) + u' ' + nameWord(rng);
    if (roll < 0.79) return pick(rng, kPlaceholderNames).toString();
    if (roll < 0.86) return pick(rng, kOddNames).toString();
    return text(rng, 6) + (rng.chance(0.5) ? u".m4a"_s : QString());
}

QString stemOf(const QString& name) {
    const qsizetype dot = name.lastIndexOf(u'.');
    return dot < 0 ? name : name.left(dot);
}

// How a name appears in the text: as given, without its extension, in another case, inside a path.
QString plantingOf(Rng& rng, const QString& name) {
    const double roll = rng.next();
    QString value = rng.chance(0.3) ? stemOf(name) : name;
    if (roll < 0.3) value = flipCase(rng, value);
    if (roll >= 0.3 && roll < 0.5) {
        static constexpr QStringView folders[] = {u"C:\\Music\\", u"D:/Music/My Playlist/", u"\\\\NAS\\share\\", u"C:\\\\Users\\\\me\\\\",
                                                  u"file:///C:/x/"};
        value = pick(rng, folders).toString() + value;
    }
    return value;
}

QString generateN(Rng& rng) {
    QStringList names;
    const int count = rng.between(1, 3);
    for (int i = 0; i < count; ++i) names.append(nameOf(rng));
    if (rng.chance(0.2)) {
        // one name a prefix of another
        const QString& first = names.first();
        if (first.size() > 4 && rng.chance(0.5)) names.append(first.left(rng.between(3, int(first.size()) - 1)));
        else names.append(stemOf(first) + u" (Remix)" + pick(rng, kExtensions).toString());
    }
    if (rng.chance(0.05)) names.append(names.at(rng.between(0, int(names.size()) - 1)));   // a name given twice
    QString line = rng.chance(0.02) ? longLogText(rng) : logText(rng);
    for (const QString& name : names) {
        if (!rng.chance(0.8)) continue;
        const int times = rng.chance(0.2) ? 2 : 1;
        for (int t = 0; t < times; ++t) {
            const QString planted = plantingOf(rng, name);
            if (rng.chance(0.5)) {
                const int at = rng.between(0, int(line.size()));
                line = line.left(at) + planted + line.mid(at);
            } else if (rng.chance(0.5)) {
                line += pick(rng, kJoiners).toString() + planted;
            } else {
                line = planted + pick(rng, kJoiners).toString() + line;
            }
        }
    }
    QString out = hex(line);
    for (const QString& name : names) out += u'\t' + hex(name);
    return out;
}

// ---- kind H: NFKC (java.text.Normalizer) of text with Hangul jamo, width variants, combining marks and compatibility forms ----
QString codePointText(char32_t codePoint) {
    QString out;
    if (codePoint > 0xFFFF) out.append(QChar(QChar::highSurrogate(codePoint))).append(QChar(QChar::lowSurrogate(codePoint)));
    else out.append(QChar(char16_t(codePoint)));
    return out;
}

QString codePointIn(Rng& rng, char32_t first, char32_t last) {
    return codePointText(char32_t(rng.between(int(first), int(last))));
}

// Combining marks of several combining classes (230 above, 220 below, 216 attached, 240 iota subscript, 202 cedilla, 8 kana
// voicing, Hebrew points 10..20, 1 overlay), so that a shuffled run needs canonical reordering.
constexpr char16_t kMarks[] = {0x0301, 0x0308, 0x3099, 0x309A, 0x0300, 0x0302, 0x0303, 0x030A, 0x030C, 0x0323, 0x0327,
                               0x031B, 0x0345, 0x0328, 0x05B4, 0x05B7, 0x05BC, 0x05C1, 0x0334, 0x0335, 0x0951, 0x093C};
// Starters that combine with some of the marks, or with each other (Hangul LV + T).
constexpr QStringView kBases[] = {u"a", u"e", u"o", u"u", u"A", u"O", u"i", u"I", u"c", u"s", u"か", u"カ", u"ｶ", u"は",
                                  u"ウ", u"가", u"א", u"ש", u"α", u"ω", u"и", u"न", u"ᄀ"};
// Canonical singletons, composition exclusions and other compatibility forms.
constexpr QStringView kCompatibility[] = {u"Å", u"Ω", u"K", u"क़", u"གྷ", u"ײַ", u"⫝̸", u"\U0001D15E",
                                          u"\U0001D160", u"̈́", u"̀", u"ẛ", u"ϓ", u"¨", u"¯", u"µ",
                                          u"ſ", u"Ǆ", u"Ĳ", u"‥", u"…", u"　", u" ", u" ",
                                          u"ẛ̣", u"ཱི", u"ཱུ", u"ཱྀ", u"\U0001D7CF", u"\U0001F100", u"\U0001F190"};

constexpr char32_t kFractionsAndScripts[] = {0x00BC, 0x00BD, 0x00BE, 0x00B2, 0x00B3, 0x00B9, 0x2070, 0x2075, 0x2089, 0x209C,
                                             0x1D2C, 0x1D62, 0x2071, 0x207F, 0x2189, 0x00AA, 0x00BA};

// The code points that Qt's Unicode (newer than JVM 21's 15.0) added with a decomposition or a non-zero combining class, and
// the parts of those decompositions: the material that would show a kt::nfkc that forgot its barriers.
const QList<char32_t>& newerUnicodeMaterial() {
    static const QList<char32_t> material = [] {
        QList<char32_t> out;
        for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;
            const QChar::UnicodeVersion version = QChar::unicodeVersion(cp);
            if (version == QChar::Unicode_Unassigned || version <= QChar::Unicode_15_0) continue;
            if (QChar::combiningClass(cp) == 0 && QChar::decompositionTag(cp) == QChar::NoDecomposition) continue;
            out.append(cp);
            const QString parts = QChar::decomposition(cp);
            for (qsizetype i = 0; i < parts.size(); ++i) {
                if (parts[i].isHighSurrogate() && i + 1 < parts.size()) {
                    out.append(QChar::surrogateToUcs4(parts[i], parts[i + 1]));
                    ++i;
                } else {
                    out.append(parts[i].unicode());
                }
            }
        }
        return out;
    }();
    return material;
}

QString nfkcPiece(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.08) return codePointIn(rng, 0x1100, 0x1112) + codePointIn(rng, 0x1161, 0x1175) +
                            (rng.chance(0.6) ? codePointIn(rng, 0x11A8, 0x11C2) : QString());          // L V (T)
    if (roll < 0.12) return codePointIn(rng, 0xAC00, 0xD7A3) + codePointIn(rng, 0x11A8, 0x11C2);       // LV(T) + T
    if (roll < 0.17) {                                                                                 // a lone jamo
        const double which = rng.next();
        if (which < 0.3) return codePointIn(rng, 0x1100, 0x11FF);
        if (which < 0.8) return codePointIn(rng, 0x3131, 0x318E);                                      // compatibility jamo
        if (which < 0.9) return codePointIn(rng, 0xA960, 0xA97C);
        return codePointIn(rng, 0xD7B0, 0xD7FB);
    }
    if (roll < 0.27) {                                                                                 // full and half width
        const double which = rng.next();
        if (which < 0.5) return codePointIn(rng, 0xFF01, 0xFF5E);
        if (which < 0.8) return codePointIn(rng, 0xFF61, 0xFF9F);
        if (which < 0.95) return codePointIn(rng, 0xFFA0, 0xFFDC);
        return codePointIn(rng, 0xFFE0, 0xFFEE);
    }
    if (roll < 0.42) {                                                                                 // a base and shuffled marks
        QString out = rng.chance(0.85) ? pick(rng, kBases).toString() : QString();
        const int marks = rng.between(1, 4);
        for (int i = 0; i < marks; ++i) out += QChar(pick(rng, kMarks));
        return out;
    }
    if (roll < 0.47) return QString(QChar(pick(rng, kMarks)));                                         // a mark on its own
    if (roll < 0.55) {                                                                                 // ligatures, circled, fractions
        const double which = rng.next();
        if (which < 0.2) return codePointIn(rng, 0xFB00, 0xFB06);
        if (which < 0.45) return codePointIn(rng, 0x2460, 0x24FF);
        if (which < 0.7) return codePointIn(rng, 0x3200, 0x33FF);
        if (which < 0.85) return codePointIn(rng, 0x2150, 0x218B);
        return codePointText(pick(rng, kFractionsAndScripts));
    }
    if (roll < 0.62) {                                                                                 // Arabic and Hebrew forms
        const double which = rng.next();
        if (which < 0.45) return codePointIn(rng, 0xFB50, 0xFDFF);
        if (which < 0.75) return codePointIn(rng, 0xFE70, 0xFEFC);
        return codePointIn(rng, 0xFB1D, 0xFB4F);
    }
    if (roll < 0.68) return pick(rng, kCompatibility).toString();
    if (roll < 0.71) return rng.chance(0.7) ? codePointIn(rng, 0xF900, 0xFAD9) : codePointIn(rng, 0x2F800, 0x2FA1D);   // CJK compatibility
    if (roll < 0.76) {                                                                                 // newer than JVM 21
        // after a base that composes with kMarks, and now and then a code point twice or next to its neighbour in the list
        // (a decomposition is listed right before its parts): shapes that would compose or reorder if Unicode 15.0 were
        // not obeyed
        const QList<char32_t>& material = newerUnicodeMaterial();
        QString out = rng.chance(0.5) ? pick(rng, kBases).toString() : QString();
        const int count = rng.between(1, 3);
        for (int i = 0; i < count; ++i) {
            const int at = rng.between(0, int(material.size()) - 1);
            out += codePointText(material.at(at));
            if (rng.chance(0.25)) out += codePointText(material.at(rng.chance(0.5) || at + 1 >= material.size() ? at : at + 1));
        }
        if (rng.chance(0.4)) out += QChar(pick(rng, kMarks));
        return out;
    }
    return token(rng);
}

QString generateH(Rng& rng) {
    if (rng.chance(0.03)) return hex(QString());
    QString value;
    const int pieces = rng.between(1, 10);
    for (int i = 0; i < pieces; ++i) value += nfkcPiece(rng);
    return hex(value);
}

// ---- kind O: Kotlin's String.toDoubleOrNull() ----
QString digitsOf(Rng& rng, int low, int high, QStringView set = u"0123456789") {
    QString out;
    const int count = rng.between(low, high);
    for (int i = 0; i < count; ++i) out += set[rng.between(0, int(set.size()) - 1)];
    return out;
}

QString signOf(Rng& rng, double p) {
    if (!rng.chance(p)) return QString();
    return rng.chance(0.5) ? QStringLiteral("-") : QStringLiteral("+");
}

constexpr char16_t kSuffixes[] = {u'f', u'F', u'd', u'D'};

QString suffixOf(Rng& rng, double p) {
    if (!rng.chance(p)) return QString();
    return QString(QChar(pick(rng, kSuffixes)));
}

QString exponentDigits(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.75) return digitsOf(rng, 1, 3);
    if (roll < 0.9) return QStringLiteral("0").repeated(rng.between(1, 25)) + digitsOf(rng, 1, 3);   // leading zeros
    return digitsOf(rng, 4, 25);                                                                     // far out of range
}

QString decimalLiteral(Rng& rng) {
    QString body;
    switch (rng.between(0, 3)) {
    case 0: body = digitsOf(rng, 1, 18); break;
    case 1: body = digitsOf(rng, 1, 10) + u'.'; break;
    case 2: body = digitsOf(rng, 1, 10) + u'.' + digitsOf(rng, 1, 20); break;
    default: body = u'.' + digitsOf(rng, 1, 20); break;
    }
    if (rng.chance(0.4)) body += QString(QChar(rng.chance(0.5) ? u'e' : u'E')) + signOf(rng, 0.5) + exponentDigits(rng);
    return signOf(rng, 0.3) + body + suffixOf(rng, 0.12);
}

QString hexLiteral(Rng& rng) {
    constexpr QStringView hexDigits = u"0123456789abcdefABCDEF";
    QString body;
    switch (rng.between(0, 2)) {
    case 0: body = digitsOf(rng, 1, 15, hexDigits); break;
    case 1: body = digitsOf(rng, 1, 8, hexDigits) + u'.' + digitsOf(rng, 0, 15, hexDigits); break;
    default: body = u'.' + digitsOf(rng, 1, 15, hexDigits); break;
    }
    const QString exponent = QString(QChar(rng.chance(0.5) ? u'p' : u'P')) + signOf(rng, 0.6) + exponentDigits(rng);
    return signOf(rng, 0.3) + QString(rng.chance(0.7) ? u"0x" : u"0X") + body + exponent + suffixOf(rng, 0.12);
}

QString specialLiteral(Rng& rng) {
    return signOf(rng, 0.5) + (rng.chance(0.5) ? QStringLiteral("NaN") : QStringLiteral("Infinity"));
}

// Many digits: correct rounding needs all of them; some land near the largest double or in the subnormal range.
QString longLiteral(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.3) return digitsOf(rng, 300, 1200);
    if (roll < 0.55) return QStringLiteral("0.") + QStringLiteral("0").repeated(rng.between(300, 330)) + digitsOf(rng, 1, 800);
    if (roll < 0.75) return digitsOf(rng, 1, 3) + u'.' + digitsOf(rng, 300, 900) + u'e' + QString::number(rng.between(-345, -300));
    return QStringLiteral("17976931348623157") + digitsOf(rng, 0, 400) + u'e' + QString::number(rng.between(290, 292));
}

// Boundaries of the double format and of the grammar, as they are.
constexpr QStringView kEdgeLiterals[] = {
    u"4.9e-324", u"2.4703282292062327e-324", u"2.4703282292062328e-324", u"2e-324", u"1e-400", u"2.2250738585072011e-308",
    u"2.2250738585072014e-308", u"1.7976931348623157e308", u"1.7976931348623158e308", u"1.7976931348623159e308", u"1e309",
    u"0x1p-1074", u"0x1p-1075", u"0x1.8p-1075", u"0x1p-1076", u"0x1.fffffffffffffp1023", u"0x1.fffffffffffff8p1023",
    u"0x1.fffffffffffff7ffp1023", u"0x0.0000000000001p-1022", u"0x1p1024", u"0x.8p1", u"0X1P3", u"0x1.p0", u"0x.p1",
    u"0x1p", u"0x", u"0xp1", u"0x1pf", u"0x1p1f", u"0x1p1d", u"0x10", u"0x1e3", u"1.", u".5", u".", u"-", u"+", u"+.", u"-.e1",
    u"1e", u"1e+", u"1e-", u"1e+5", u"1E-3", u"1.e5", u".e5", u"e5", u"00012", u"-0", u"+0", u"-0.0", u"0e99999", u"1f", u"1d",
    u"1F", u"1D", u"1ff", u"1fd", u"NaN", u"-NaN", u"+NaN", u"Infinity", u"-Infinity", u"+Infinity", u"NaNd", u"Infinityf",
    u"infinity", u"nan", u"NAN", u"Inf", u"inf", u"INFINITY", u"Infinit", u"NA", u"N/A", u"1,5", u"1 000", u"1_000", u"12.5%",
    u"3.2MiB", u"--1", u"+-1", u"1..2", u"1.2.3", u"1e1.5", u"0b101", u"1L", u"1.5f5"};

// Ten digit sets that are not ASCII: Arabic-Indic, extended Arabic-Indic, Devanagari, Thai, full width, NKo, Bengali,
// mathematical bold (supplementary), Myanmar, Khmer.
constexpr char32_t kOtherZeros[] = {0x0660, 0x06F0, 0x0966, 0x0E50, 0xFF10, 0x07C0, 0x09E6, 0x1D7CE, 0x1040, 0x17E0};

QString otherScriptDigits(Rng& rng, const QString& literal) {
    const char32_t zero = pick(rng, kOtherZeros);
    const bool all = rng.chance(0.6);
    QString out;
    for (const QChar c : literal) {
        if (c >= u'0' && c <= u'9' && (all || rng.chance(0.5))) out += codePointText(zero + char32_t(c.unicode() - u'0'));
        else out += c;
    }
    return out;
}

// One character replaced, inserted, removed or doubled.
QString corrupt(Rng& rng, const QString& literal) {
    constexpr QStringView noise = u"0123456789.eExXpP+- ,_aAfFdDnNIi١ ";
    const int at = rng.between(0, int(literal.size()));
    const QChar other = noise[rng.between(0, int(noise.size()) - 1)];
    switch (rng.between(0, 3)) {
    case 0: return at < literal.size() ? literal.left(at) + other + literal.mid(at + 1) : literal + other;
    case 1: return literal.left(at) + other + literal.mid(at);
    case 2: return at < literal.size() ? literal.left(at) + literal.mid(at + 1) : literal.chopped(literal.isEmpty() ? 0 : 1);
    default: return at < literal.size() ? literal.left(at + 1) + literal.mid(at) : literal + literal.right(1);
    }
}

// Characters up to U+0020 (which Double.valueOf drops) and other white space (which it keeps).
constexpr QStringView kAsciiPads[] = {u" ", u"\t", u"\n", u"\r", u"\u000b", u"\u000c", QStringView(u"\0", 1), u"\u001f", u"  "};
constexpr QStringView kOtherPads[] = {u" ", u"　", u" ", u"\u0085", u"﻿", u"​", u" ", u"\u007f"};

QString padded(Rng& rng, const QString& literal) {
    const auto pad = [&rng] { return rng.chance(0.7) ? pick(rng, kAsciiPads).toString() : pick(rng, kOtherPads).toString(); };
    const int where = rng.between(0, 2);
    return (where != 1 ? pad() : QString()) + literal + (where != 0 ? pad() : QString());
}

QString generateO(Rng& rng) {
    const double roll = rng.next();
    QString literal;
    if (roll < 0.03) return hex(QString());
    if (roll < 0.33) literal = decimalLiteral(rng);
    else if (roll < 0.43) literal = hexLiteral(rng);
    else if (roll < 0.49) literal = specialLiteral(rng);
    else if (roll < 0.53) literal = longLiteral(rng);
    else if (roll < 0.63) literal = pick(rng, kEdgeLiterals).toString();
    else if (roll < 0.85) {
        const double which = rng.next();
        literal = corrupt(rng, which < 0.5 ? decimalLiteral(rng) : which < 0.75 ? hexLiteral(rng) : specialLiteral(rng));
    } else if (roll < 0.91) {
        literal = otherScriptDigits(rng, decimalLiteral(rng));
    } else {
        literal = text(rng, 6);
    }
    if (rng.chance(0.2)) literal = padded(rng, literal);
    return hex(literal);
}

// ---- kind E: ErrorClassifier.classify (the stderr lines of yt-dlp) ----
// Lines of 0..6: noise, lines carrying a phrase of one of the lists (the phrases of several lists in one case make the order
// of the checks show), ERROR: lines (leading white space, several, empty after the prefix), white-space-only lines and long
// lines whose cut at 300 units lands in an emoji. Phrases are written in mixed case and with look-alike letters.
constexpr QStringView kFatalPhrases[] = {u"no space left on device", u"not enough space on the disk", u"disk quota exceeded",
                                         u"permission denied"};
constexpr QStringView kRatePhrases[] = {u"try again later", u"rate-limited", u"rate limited"};
constexpr QStringView kUnavailablePhrases[] = {
    u"private video", u"sign in to confirm your age", u"age-restricted", u"available in your country",
    u"blocked it in your country", u"who has blocked it", u"members-only", u"join this channel", u"copyright",
    u"has been removed", u"account associated with this video has been terminated", u"video unavailable",
    u"this video is not available"};
constexpr QStringView kTransientPhrases[] = {
    u"http error 429", u"http error 403", u"http error 500", u"http error 502", u"http error 503", u"http error 504",
    u"timed out", u"connection reset", u"connection aborted", u"remote end closed", u"incompleteread",
    u"temporary failure in name resolution", u"getaddrinfo failed", u"network is unreachable", u"unable to download"};
constexpr QStringView kFormatPhrases[] = {u"requested format is not available"};
// Near misses: they must not match (a letter changed, a word dropped, another spelling)
constexpr QStringView kErrorNearMisses[] = {
    u"no space left", u"permision denied", u"private  video", u"video  unavailable", u"rate-limit", u"http error 404",
    u"http error 42", u"timeout", u"timed-out", u"age restricted", u"members only", u"copy right",
    u"sign in to confirm you're not a bot", u"unable to extract", u"ERROR", u"requested format", u"format is not available",
    u"try again", u"connection refused"};
constexpr QStringView kLinePrefixes[] = {u"ERROR: [youtube] abc: ", u"WARNING: ", u"", u"OSError: [Errno 28] ", u"[download] ",
                                         u"ERROR: ", u"  ERROR: ", u"\tERROR:", u"ERROR:", u"yt_dlp.utils.DownloadError: ",
                                         u"error: ", u"Error: ", u" ERROR : "};
constexpr QStringView kLineSuffixes[] = {u"", u"", u".", u". Use --list-formats", u" (caused by ...)", u"!", u"\r", u"  ",
                                         u"　", u" \t"};

// ASCII letters swapped for the look-alikes that Kotlin's lowercase() treats differently: U+0130 (lowercases to i + U+0307,
// so "private" stops matching), U+017F (stays), U+212A KELVIN SIGN (lowercases to k, so a phrase with k still matches).
QString decoratedPhrase(Rng& rng, QStringView phrase) {
    QString out = phrase.toString();
    const double roll = rng.next();
    if (roll < 0.35) return out;
    if (roll < 0.5) return out.toUpper();
    if (roll < 0.6) return out.at(0).toUpper() + out.mid(1);
    if (roll < 0.8) {
        for (QChar& c : out)
            if (rng.chance(0.5)) c = c.toUpper();
        return out;
    }
    if (roll < 0.9) {
        for (QChar& c : out) {
            if (!rng.chance(0.3)) continue;
            if (c == u'i' || c == u'I') c = QChar(0x0130);
            else if (c == u's') c = QChar(0x017F);
            else if (c == u'k') c = QChar(0x212A);
        }
        return out;
    }
    // exactly one look-alike (when the phrase has one of the letters)
    QList<int> candidates;
    for (int i = 0; i < out.size(); ++i)
        if (out.at(i) == u'i' || out.at(i) == u's' || out.at(i) == u'k') candidates.append(i);
    if (!candidates.isEmpty()) {
        const int at = candidates.at(rng.between(0, int(candidates.size()) - 1));
        const QChar c = out.at(at);
        out[at] = QChar(c == u'i' ? 0x0130 : c == u's' ? 0x017F : 0x212A);
    }
    return out;
}

QString phraseOfList(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.14) return decoratedPhrase(rng, pick(rng, kFatalPhrases));
    if (roll < 0.24) return decoratedPhrase(rng, pick(rng, kRatePhrases));
    if (roll < 0.52) return decoratedPhrase(rng, pick(rng, kUnavailablePhrases));
    if (roll < 0.80) return decoratedPhrase(rng, pick(rng, kTransientPhrases));
    if (roll < 0.90) return decoratedPhrase(rng, pick(rng, kFormatPhrases));
    return decoratedPhrase(rng, pick(rng, kErrorNearMisses));
}

// A line of more than 300 units after the prefix, an emoji (a surrogate pair) where the cut falls (299|300 or a bit away).
QString longErrorLine(Rng& rng) {
    const QString prefix = rng.chance(0.7) ? QStringLiteral("ERROR: ") : (rng.chance(0.5) ? QStringLiteral("  ERROR:   ") : QString());
    const int cutAt = rng.between(296, 302);
    QString body = rng.chance(0.3) ? QStringLiteral("가").repeated(cutAt) : QStringLiteral("x").repeated(cutAt);
    body += rng.chance(0.85) ? QStringLiteral("😀") : QString(QChar(0xD83D));   // a pair, or an unpaired high surrogate
    body += QStringLiteral("tail").repeated(rng.between(0, 10));
    if (rng.chance(0.2)) body += pick(rng, kSpaces).toString();
    return prefix + body;
}

QString errorLineOf(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.1) return QStringLiteral("ERROR:");
    if (roll < 0.2) return pick(rng, kSpaces).toString() + u"ERROR:" + pick(rng, kSpaces).toString();   // empty after the prefix
    if (roll < 0.3) return u"ERROR:" + QString(u' ').repeated(rng.between(0, 3)) + u"ERROR:" + text(rng, 3);   // the prefix twice
    return pick(rng, kLinePrefixes).toString() + text(rng, 8);
}

QString lineOfE(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.28) return pick(rng, kLinePrefixes).toString() + phraseOfList(rng) + pick(rng, kLineSuffixes).toString();
    if (roll < 0.40) return pick(rng, kLinePrefixes).toString() + text(rng, 6) + u' ' + phraseOfList(rng) + u' ' + text(rng, 4);
    if (roll < 0.52) return errorLineOf(rng);
    if (roll < 0.58) return QString();
    if (roll < 0.66) {
        QString blank;
        const int count = rng.between(1, 4);
        for (int i = 0; i < count; ++i) blank += pick(rng, kSpaces).toString();
        return blank;
    }
    if (roll < 0.70) return longErrorLine(rng);
    if (roll < 0.73) return text(rng, 120);   // long and without a prefix
    if (roll < 0.75) return phraseOfList(rng) + u"\n" + phraseOfList(rng);   // a line with a line break inside
    return text(rng, 10);
}

QString generateE(Rng& rng) {
    if (rng.chance(0.06)) return QString();   // no lines at all
    int count = rng.between(1, 6);
    if (rng.chance(0.2)) count = 1;
    const bool noiseOnly = rng.chance(0.2);   // few phrases, so the classification often falls to OTHER
    QStringList fields;
    for (int i = 0; i < count; ++i) {
        QString line = lineOfE(rng);
        if (noiseOnly && rng.chance(0.7)) line = pick(rng, kLinePrefixes).toString() + text(rng, 8);
        fields.append(hex(line));
    }
    return fields.join(u'\t');
}

// ---- kind Q: ProgressParser.parse ----
// `XGSP|status|downloaded|total|estimated total` in a few spellings. Field values come from the generator of kind O (every
// grammar of toDoubleOrNull) or are built to give a percent: a total, a downloaded amount as a share of it, "NA".
constexpr QStringView kStatusOther[] = {u"started", u"Downloading", u"FINISHED", u"Finished", u"downloading ", u" finished", u"",
                                        u"error", u"fınished", u"downloadıng", u"paused", u"finished|", u"NA"};
constexpr QStringView kBadHeads[] = {u"XGSPP|downloading|1|2|3", u"xgsp|downloading|1|2|3", u"XGSP downloading|1|2|3", u"XGSP",
                                     u"XGSP|", u"XGS|downloading|1|2|3", u"XGSPX|finished", u"[youtube] Extracting URL", u"",
                                     u"|XGSP|finished", u"XGSP:finished", u"XGSP||downloading|1|2", u"ZGSP|finished",
                                     u"XGSPP|started|ExtractAudio", u"\u0001XGSP|finished", u"XGSP|downloading", u"XGSP|finished"};

QString numberField(Rng& rng) {
    // a field of O's generator: any literal of toDoubleOrNull, many of them not numbers
    return unhex(generateO(rng)).value_or(QString());
}

QString structuredNumber(Rng& rng, qint64 value) {
    const double roll = rng.next();
    if (roll < 0.7) return QString::number(value);
    if (roll < 0.8) return QString::number(value) + u".0";
    if (roll < 0.85) return QString::number(value) + u".5";
    if (roll < 0.9) return QString::number(value) + u"e0";
    if (roll < 0.95) return u" " + QString::number(value) + u" ";
    return QStringLiteral("+") + QString::number(value);
}

// The fields downloaded, total, estimated total of a downloading line that mostly gives a percent.
void percentFields(Rng& rng, QStringList& parts) {
    constexpr qint64 totals[] = {100, 100, 100, 1000, 1, 2, 3, 7, 29, 4096, 1048576, 123456789, 2000000000, 0, 0, -5};
    const qint64 total = pick(rng, totals);
    const double roll = rng.next();
    qint64 downloaded;
    if (roll < 0.08) downloaded = 0;
    else if (roll < 0.2) downloaded = total;
    else if (roll < 0.3) downloaded = total + rng.between(1, 1000);
    else if (roll < 0.35) downloaded = -rng.between(1, 1000);
    else if (total == 100) downloaded = rng.between(1, 99);
    else downloaded = total > 1 ? rng.between(1, int(std::min<qint64>(total, 2000000000)) - 1) : 0;
    const bool known = rng.chance(0.55);   // the total is the third field; else "NA" there and the estimate in the fourth
    parts.append(structuredNumber(rng, downloaded));
    parts.append(known ? structuredNumber(rng, total) : (rng.chance(0.8) ? QStringLiteral("NA") : numberField(rng)));
    parts.append(!known || rng.chance(0.25) ? (rng.chance(0.85) ? structuredNumber(rng, total) : QStringLiteral("NA"))
                                           : QStringLiteral("NA"));
}

QString generateQ(Rng& rng) {
    QString line;
    if (rng.chance(0.08)) {
        line = pick(rng, kBadHeads).toString();
    } else {
        QStringList parts;
        parts.append(QStringLiteral("XGSP"));
        const double statusRoll = rng.next();
        QString status;
        if (statusRoll < 0.2) status = QStringLiteral("finished");
        else if (statusRoll < 0.8) status = QStringLiteral("downloading");
        else status = pick(rng, kStatusOther).toString();
        parts.append(status);
        if (status == u"downloading" && rng.chance(0.75)) {
            percentFields(rng, parts);
        } else {
            const int count = rng.between(0, 5);
            for (int i = 0; i < count; ++i)
                parts.append(rng.chance(0.4) ? numberField(rng) : (rng.chance(0.4) ? QStringLiteral("NA") : text(rng, 2)));
        }
        if (rng.chance(0.1) && parts.size() > 2) parts[rng.between(2, int(parts.size()) - 1)] = numberField(rng);   // one field from O
        line = parts.join(u'|');
    }
    if (rng.chance(0.25)) {
        const QString pad = rng.chance(0.7) ? pick(rng, kSpaces).toString() : (rng.chance(0.5) ? QStringLiteral("\r\n") : QStringLiteral("\t \r"));
        const int where = rng.between(0, 2);
        line = (where != 1 ? pad : QString()) + line + (where != 0 ? pad : QString());
    }
    return hex(line);
}

// ---- kind J: kotlinx.serialization's Json.parseToJsonElement (xgs::json) ----
// Documents from a grammar (objects, arrays, strings, literals; depth 0..6, sometimes a chain of 60, rarely the deep
// families below), every category of the Step 2 observations, and in 30% one place broken.

// Unquoted values: what Python writes, what JSON calls numbers and words that are neither (all literals for Kotlin).
constexpr QStringView kJsonLiterals[] = {
    u"NaN", u"Infinity", u"-Infinity", u"true", u"false", u"null", u"TRUE", u"NULL", u"True", u"Null", u"nul", u"tru",
    u"1e", u"01", u"+1", u".5", u"1.", u"0x10", u"1e999", u"-1e999", u"123456789012345678901234567890", u"-0", u"1.5e-400",
    u"abc", u"-", u"'a'", u"1/*c*/", u"nullx", u"null1", u"가나", u"😀", u"1\x00a0", u"\xFEFF", u"\xD83D", u"a-b_c$d",
    u"1.2.3", u"--1", u"#", u"//", u"NA", u"None", u"undefined", u"0", u"42", u"-2.5E-3", u"258.5"};
// Keys written as they stand between the quotes: few, so that keys repeat; the escape of U+0061 is another spelling of `a`.
constexpr QStringView kJsonKeys[] = {u"a", u"b", u"id", u"album", u"description", u"duration", u"", u"한", u"\\u0061",
                                     u"A", u"a ", u"😀", u"\xDC00", u"\\n", u"title"};
constexpr QStringView kJsonWhite[] = {u" ", u"\t", u"\n", u"\r", u"\r\n", u"  \n  "};
// White space Kotlin does not skip (form feed, vertical tab, no-break space, line separator, ideographic space, next
// line, BOM, U+001F, NUL)
constexpr QStringView kJsonOtherWhite[] = {u"\u000c", u"\u000b", u"\x00A0", u"\x2028", u"\x3000", u"\u0085", u"\xFEFF",
                                           u"\u001f", QStringView(u"\0", 1)};
constexpr QStringView kJsonShortEscapes[] = {u"\\\"", u"\\\\", u"\\/", u"\\b", u"\\f", u"\\n", u"\\r", u"\\t"};
constexpr QStringView kJsonBadEscapes[] = {u"\\x41", u"\\'", u"\\U0041", u"\\a", u"\\0", u"\\u12G4", u"\\u004", u"\\\n",
                                           u"\\u", u"\\u00", u"\\uＡ000"};
constexpr char16_t kJsonEscapedUnits[] = {0x0000, 0x001f, 0x0022, 0x005c, 0x0041, 0x00e9, 0xd83d, 0xde00, 0xdbff, 0xdfff,
                                          0xac00, 0xfeff, 0x2028, 0x007f};

QString jsonSpace(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.55) return QString();
    if (roll < 0.8) return u" "_s;
    if (roll < 0.99) return pick(rng, kJsonWhite).toString();
    return pick(rng, kJsonOtherWhite).toString();
}

QString hex4Of(Rng& rng, char16_t unit) {
    QString out = u"%1"_s.arg(int(unit), 4, 16, QChar(u'0'));
    if (rng.chance(0.4)) {
        for (QChar& c : out) {
            if (c >= u'a' && c <= u'f') c = QChar(char16_t(c.unicode() - u'a' + u'A'));
        }
    }
    return out;
}

// The text between the quotes of a string: token() material (its `"` and `\` escaped, now and then left raw), escapes
// valid and not, raw control characters.
QString jsonStringBody(Rng& rng) {
    QString out;
    const int pieces = rng.chance(0.15) ? 0 : rng.between(1, 6);
    for (int i = 0; i < pieces; ++i) {
        const double roll = rng.next();
        if (roll < 0.45) {
            QString piece = token(rng);
            if (!rng.chance(0.03)) piece.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s);
            out += piece;
        } else if (roll < 0.62) {
            out += pick(rng, kJsonShortEscapes);
        } else if (roll < 0.76) {
            const char16_t unit = rng.chance(0.8) ? pick(rng, kJsonEscapedUnits) : char16_t(rng.between(0, 0xFFFF));
            out += u"\\u"_s + hex4Of(rng, unit);
        } else if (roll < 0.8) {
            out += u"\\u"_s + hex4Of(rng, 0xd83d) + u"\\u"_s + hex4Of(rng, 0xde00);
        } else if (roll < 0.815) {
            out += pick(rng, kJsonBadEscapes);
        } else if (roll < 0.87) {
            out += pick(rng, kControl);
        } else {
            out += charOf(rng, kAscii).repeated(rng.between(1, 5));
        }
    }
    return out;
}

QString jsonLiteral(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.35) return decimalLiteral(rng);
    if (roll < 0.9) return pick(rng, kJsonLiterals).toString();
    // token() material as it comes (it may hold a character that ends a literal, or white space)
    QString out;
    const int count = rng.between(1, 3);
    for (int i = 0; i < count; ++i) out += token(rng);
    return out;
}

QString jsonKey(Rng& rng, QStringList& used) {
    QString key;
    if (!used.isEmpty() && rng.chance(0.15)) key = used[rng.between(0, int(used.size()) - 1)];   // a repeated key
    else if (rng.chance(0.85)) key = pick(rng, kJsonKeys).toString();
    else key = jsonStringBody(rng);
    used.append(key);
    return u"\""_s + key + u"\"";
}

QString jsonValue(Rng& rng, int depth, int maxDepth);

QString jsonObject(Rng& rng, int depth, int maxDepth, const QString& forcedChild = QString()) {
    QString out = u"{"_s + jsonSpace(rng);
    QStringList used;
    const int count = rng.chance(0.1) ? rng.between(5, 9) : rng.between(0, 4);
    const int forcedAt = forcedChild.isNull() ? -1 : rng.between(0, count);
    const int total = forcedChild.isNull() ? count : count + 1;
    for (int i = 0; i < total; ++i) {
        if (i > 0) out += jsonSpace(rng) + u"," + jsonSpace(rng);
        const QString value = i == forcedAt ? forcedChild : jsonValue(rng, depth + 1, maxDepth);
        out += jsonKey(rng, used) + jsonSpace(rng) + u":" + jsonSpace(rng) + value;
    }
    return out + jsonSpace(rng) + u"}";
}

QString jsonArray(Rng& rng, int depth, int maxDepth, const QString& forcedChild = QString()) {
    QString out = u"["_s + jsonSpace(rng);
    const int count = rng.chance(0.1) ? rng.between(5, 9) : rng.between(0, 4);
    const int forcedAt = forcedChild.isNull() ? -1 : rng.between(0, count);
    const int total = forcedChild.isNull() ? count : count + 1;
    for (int i = 0; i < total; ++i) {
        if (i > 0) out += jsonSpace(rng) + u"," + jsonSpace(rng);
        out += i == forcedAt ? forcedChild : jsonValue(rng, depth + 1, maxDepth);
    }
    return out + jsonSpace(rng) + u"]";
}

QString jsonValue(Rng& rng, int depth, int maxDepth) {
    const double roll = depth >= maxDepth ? 0.4 + 0.6 * rng.next() : rng.next();
    if (roll < 0.22) return jsonObject(rng, depth, maxDepth);
    if (roll < 0.4) return jsonArray(rng, depth, maxDepth);
    if (roll < 0.68) return u"\""_s + jsonStringBody(rng) + u"\"";
    return jsonLiteral(rng);
}

// A chain of [levels] containers (each an object or an array at random, with siblings), a value at the bottom.
QString jsonChain(Rng& rng, int levels) {
    QString inner = jsonValue(rng, 0, 1);
    for (int i = 0; i < levels; ++i) inner = rng.chance(0.5) ? jsonObject(rng, 5, 6, inner) : jsonArray(rng, 5, 6, inner);
    return inner;
}

// The deep families. Kotlin's answer is certain only up to 1000 open arrays (with at most one object between two of
// them) and from 60000; between those it depends on the JVM's state, so nothing here nests arrays deeper than 1000 and
// shallower than 60000. Object chains of any depth parse in Kotlin.
QString jsonDeep(Rng& rng) {
    const auto nest = [](QStringView open, const QString& inner, QStringView close, int n) {
        return open.toString().repeated(n) + inner + close.toString().repeated(n);
    };
    switch (rng.between(0, 10)) {
    case 9:
    case 10: return nest(u"[", rng.chance(0.5) ? QString() : jsonLiteral(rng), u"]", 60000);   // Kotlin: always a stack overflow
    case 0: return nest(u"[", rng.chance(0.5) ? QString() : jsonLiteral(rng), u"]", 1000);
    case 1: return nest(u"[{\"a\":", u"1"_s, u"}]", rng.chance(0.5) ? 1000 : rng.between(990, 999));
    case 2: return u"["_s + nest(u"[", QString(), u"]", 999) + u"," + nest(u"[", u"\"x\""_s, u"]", 999) + u"]";
    case 3: return nest(u"{\"a\":", jsonValue(rng, 0, 2), u"}", rng.between(1001, 3000));
    case 4: return nest(u"{\"k\":1,\"a\":", nest(u"[", u"0"_s, u"]", 1000), u"}", rng.between(1, 2000));
    case 5: return nest(u"[", nest(u"{\"a\":", u"null"_s, u"}", rng.between(1001, 3000)), u"]", 1000);
    case 6: return rng.chance(0.5) ? u"["_s.repeated(rng.between(500, 1000)) : u"{\"a\":"_s.repeated(rng.between(1000, 3000));   // unclosed
    case 7: return nest(u"[", QString(), u"]", 1000) + u"]";   // one bracket too many
    default: return nest(u"[", QString(), u"]", rng.between(1, 1000));
    }
}

// One place broken: a character deleted, inserted or replaced, a key's quotes removed, a comma added, the text cut,
// garbage after it, a BOM or a comment in front, single quotes, other white space, a comma turned into `]`.
QString jsonBreak(Rng& rng, const QString& doc) {
    constexpr QStringView noise = u"{}[],:\"\\' /#*\n\tax0-\x00A0";
    const int at = rng.between(0, int(doc.size()));
    switch (rng.between(0, 10)) {
    case 10: {
        // a comma turned into `]` (which, after an array item and before a value, Kotlin takes for a comma)
        QList<qsizetype> commas;
        for (qsizetype i = 0; i < doc.size(); ++i) {
            if (doc[i] == u',') commas.append(i);
        }
        if (commas.isEmpty()) return doc + u"]x]"_s;
        QString out = doc;
        out[commas[rng.between(0, int(commas.size()) - 1)]] = u']';
        return out;
    }
    case 0: return at < doc.size() ? doc.left(at) + doc.mid(at + 1) : doc.chopped(doc.isEmpty() ? 0 : 1);
    case 1: return doc.left(at) + noise[rng.between(0, int(noise.size()) - 1)] + doc.mid(at);
    case 2: return at < doc.size() ? doc.left(at) + noise[rng.between(0, int(noise.size()) - 1)] + doc.mid(at + 1) : doc + u"x";
    case 3: {
        // the quotes around the first key (or around the first string)
        const qsizetype open = doc.indexOf(u'"');
        const qsizetype close = open < 0 ? -1 : doc.indexOf(u'"', open + 1);
        if (close < 0) return doc + u",";
        return doc.left(open) + doc.mid(open + 1, close - open - 1) + doc.mid(close + 1);
    }
    case 4: {
        // a trailing or doubled comma before a closing bracket or after a comma
        QList<qsizetype> places;
        for (qsizetype i = 0; i < doc.size(); ++i) {
            if (doc[i] == u']' || doc[i] == u'}' || doc[i] == u',') places.append(i);
        }
        if (places.isEmpty()) return u","_s + doc;
        const qsizetype place = places[rng.between(0, int(places.size()) - 1)];
        return doc.left(place) + u"," + doc.mid(place);
    }
    case 5: return doc.left(at);
    case 6: {
        constexpr QStringView garbage[] = {u" x", u"]", u"}", u",", u"//c", u"/*c*/", QStringView(u"\0", 1), u" 1", u" \"y\"", u"\xFEFF"};
        return doc + pick(rng, garbage);
    }
    case 7: return (rng.chance(0.5) ? u"\xFEFF"_s : u"/*c*/"_s) + doc;
    case 8: {
        QString out = doc;
        const qsizetype first = out.indexOf(u'"');
        const qsizetype second = first < 0 ? -1 : out.indexOf(u'"', first + 1);
        if (second < 0) return out + u"'";
        out[first] = u'\'';
        out[second] = u'\'';
        return out;
    }
    default: return doc.left(at) + jsonSpace(rng) + pick(rng, kJsonOtherWhite) + doc.mid(at);
    }
}

QString generateJ(Rng& rng) {
    const double roll = rng.next();
    QString doc;
    if (roll < 0.008) return hex(jsonDeep(rng));
    if (roll < 0.018) {
        // nothing, white space only, or white space Kotlin does not skip
        const int count = rng.between(0, 3);
        for (int i = 0; i < count; ++i) doc += rng.chance(0.7) ? pick(rng, kJsonWhite) : pick(rng, kJsonOtherWhite);
        return hex(doc);
    }
    if (roll < 0.03) doc = jsonChain(rng, 60);
    else if (roll < 0.75) doc = jsonObject(rng, 0, rng.chance(0.75) ? rng.between(1, 3) : rng.between(4, 6));
    else if (roll < 0.85) doc = jsonArray(rng, 0, rng.between(1, 6));
    else doc = jsonValue(rng, 0, 0);   // a string or a literal at the top
    if (rng.chance(0.15)) doc = jsonSpace(rng) + doc + jsonSpace(rng);
    if (rng.chance(0.3)) doc = jsonBreak(rng, doc);
    return hex(doc);
}

// ---- kind V: VideoInfoFile.read (the info.json that yt-dlp writes) ----
// The bytes of a file: a root object from kind J's document generator with the keys album, description and duration put
// in, in many shapes (strings that trim to nothing or to NA, descriptions with blank lines in front and white space behind,
// numbers at the rounding and Int boundaries, strings of digits, null, booleans, containers, bare literals, repeated keys,
// the same keys in nested objects), written as UTF-8; some files get bytes that are not UTF-8, valid but rare sequences,
// a BOM, the UTF-16 encoding, or nothing at all. Nothing nests more than a few arrays (kind J's 1001+ zone stays out).

// The JSON escape of one UTF-16 unit
QString jsonEscapeOf(char16_t unit) {
    return QString(QChar(u'\\')) + u"u"_s + u"%1"_s.arg(int(unit), 4, 16, QChar(u'0'));
}

// White space and look-alikes at the ends of a string, as they stand between the quotes: raw, as escapes, Kotlin white
// space (U+3000, U+00A0, U+2028, U+001C..U+001F) and what Kotlin does not count (U+0085, U+FEFF, U+200B).
QString infoPad(Rng& rng) {
    constexpr QStringView pieces[] = {u" ", u"  ", u"\t", u"\\t", u"\\n", u"\\r", u"\\r\\n", u"\n", u"\r", u"\x3000", u"\x00A0",
                                      u"\x2028", u"\x001C", u"\x001F", u"\x0085", u"\xFEFF", u"\x200B", u"\x000B", u"\\f"};
    QString out;
    const int count = rng.between(1, 3);
    for (int i = 0; i < count; ++i) out += pick(rng, pieces);
    return out;
}

// Kind J's white space between tokens without what Kotlin does not skip: a document broken that way is rarer here (jsonBreak,
// the members from kind J's generator and the white space around the whole document still bring it).
QString infoSpace(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.55) return QString();
    if (roll < 0.8) return u" "_s;
    return pick(rng, kJsonWhite).toString();
}

QString quoted(const QString& body) {
    return u"\""_s + body + u"\"";
}

// A value that is not a string (for any of the three keys)
QString infoNonString(Rng& rng) {
    constexpr QStringView values[] = {u"null", u"true", u"false", u"2024", u"NA", u"Palette", u"[\"Palette\"]", u"[258]",
                                      u"{\"name\":\"Palette\"}", u"{\"seconds\":258}", u"[]", u"{}", u"0", u"258"};
    return rng.chance(0.7) ? pick(rng, values).toString() : jsonValue(rng, 1, rng.between(1, 3));
}

QString albumValue(Rng& rng) {
    if (rng.chance(0.2)) return infoNonString(rng);
    constexpr QStringView albums[] = {u"Palette", u"Love poem", u"NA", u"NA", u"na", u"Na", u"N A", u"NA Nights", u"NAN",
                                      u"사랑의 시", u"", u"Love \\\"poem\\\"", u"N\\u0041", u"\x00A0NA\x00A0", u"A"};
    QString body;
    const double roll = rng.next();
    if (roll < 0.75) {
        body = pick(rng, albums).toString();
    } else if (roll < 0.85) {
        body = jsonEscapeOf(u'N') + jsonEscapeOf(u'A');
    } else {
        body = jsonStringBody(rng);
    }
    if (rng.chance(0.4)) body = infoPad(rng) + body;
    if (rng.chance(0.4)) body += infoPad(rng);
    return quoted(body);
}

QString lineBreakOf(Rng& rng) {
    constexpr QStringView breaks[] = {u"\\n", u"\\n", u"\\r\\n", u"\\r", u"\n", u"\r\n", u"\r"};
    return pick(rng, breaks).toString();
}

QString descriptionValue(Rng& rng) {
    if (rng.chance(0.2)) return infoNonString(rng);
    const double roll = rng.next();
    if (roll < 0.1) return quoted(jsonStringBody(rng));
    if (roll < 0.18) return quoted(rng.chance(0.3) ? QString() : infoPad(rng) + lineBreakOf(rng) + infoPad(rng));   // blank
    constexpr QStringView lines[] = {u"[Lyrics]", u"Line one", u"Line two", u"첫 줄", u"\\\"둘째\\\" 줄", u"é 🎵", u"a  b",
                                     u"Some text", u"가사", u"x"};
    QString body;
    const int blankLines = rng.chance(0.5) ? 0 : rng.between(1, 3);
    for (int i = 0; i < blankLines; ++i) body += (rng.chance(0.5) ? infoPad(rng) : QString()) + lineBreakOf(rng);
    if (rng.chance(0.5)) body += infoPad(rng);   // the first text's own indentation (or white space before it on its line)
    const int count = rng.between(1, 5);
    for (int i = 0; i < count; ++i) {
        if (i > 0) body += lineBreakOf(rng) + (rng.chance(0.2) ? lineBreakOf(rng) : QString()) + (rng.chance(0.2) ? infoPad(rng) : QString());
        body += rng.chance(0.75) ? pick(rng, lines).toString() : text(rng, 6).replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s);
    }
    if (rng.chance(0.6)) {
        const int tail = rng.between(1, 3);
        for (int i = 0; i < tail; ++i) body += rng.chance(0.5) ? infoPad(rng) : lineBreakOf(rng);
    }
    return quoted(body);
}

// Durations as written: the rounding ties, the ends of the Int range, what the Long range clamps, and what is no number.
constexpr QStringView kDurationLiterals[] = {
    u"258", u"1", u"258.4", u"258.5", u"258.6", u"19.0", u"2.584E2", u"0.5", u"1.5", u"2.5", u"-0.5", u"-1.5", u"0", u"0.0",
    u"-0", u"-0.0", u"-5", u"-0.4", u"0.4", u"0.49999999999999994", u"0.5000000000000001", u"1.4999999999999998", u"1e3",
    u"1E3", u"1e0", u"2147483647", u"2147483647.4", u"2147483647.5", u"2147483646.5", u"2147483646.4999", u"2147483648",
    u"-2147483648", u"4294967296", u"4294967297.5", u"1e30", u"-1e30", u"1e308", u"1e309", u"-1e309", u"9223372036854775807",
    u"9223372036854775808", u"9.223372036854775807E18", u"-9223372036854775809", u"1e19", u"4503599627370495.5",
    u"4503599627370496.5", u"9007199254740993", u"NaN", u"-NaN", u"Infinity", u"-Infinity", u"258f", u"258d", u"258F",
    u"0x1p8", u"0x10", u"+5", u".5", u"5.", u"01", u"1_0", u"true", u"false", u"null", u"NA", u"1.5e-400", u"4.9e-324",
    u"2147483647.49999997", u"2147483646.50000001", u"3.5", u"0.5f", u"0x1.8p0"};

QString durationValue(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.25) return pick(rng, kDurationLiterals).toString();
    if (roll < 0.7) {
        constexpr QStringView fractions[] = {u"", u"", u".0", u".5", u".4", u".6", u".49999", u".50001", u".25", u"e0", u"E1"};
        const qint64 whole = rng.chance(0.85) ? rng.between(0, 20000) : 2147483647LL - rng.between(0, 3);
        return (rng.chance(0.08) ? u"-"_s : QString()) + QString::number(whole) + pick(rng, fractions);
    }
    if (roll < 0.77) return decimalLiteral(rng);
    if (roll < 0.79) return hexLiteral(rng);
    if (roll < 0.86) {
        constexpr QStringView strings[] = {u"\"258\"", u"\"NA\"", u"\"\"", u"\"258.5\"", u"\" 258 \"", u"\"1e3\""};
        return pick(rng, strings).toString();
    }
    return infoNonString(rng);
}

QString keySpelling(Rng& rng, QStringView name) {
    const double roll = rng.next();
    if (roll < 0.9) return name.toString();
    if (roll < 0.95) {
        // the same key with one letter escaped
        const int at = rng.between(0, int(name.size()) - 1);
        return name.left(at).toString() + jsonEscapeOf(name[at].unicode()) + name.mid(at + 1).toString();
    }
    constexpr QStringView nearMisses[] = {u"Album", u"album ", u"albums", u"ALBUM", u"descriptio", u"Description", u"durations",
                                          u"Duration", u" duration"};
    return pick(rng, nearMisses).toString();
}

QString infoMember(Rng& rng, QStringView name, const QString& value) {
    return quoted(keySpelling(rng, name)) + infoSpace(rng) + u":" + infoSpace(rng) + value;
}

QString valueFor(Rng& rng, int which) {
    return which == 0 ? albumValue(rng) : which == 1 ? descriptionValue(rng) : durationValue(rng);
}

constexpr QStringView kInfoKeys[] = {u"album", u"description", u"duration"};

// An object (a format or a meta entry) holding the same keys, which mean something else there
QString nestedInfo(Rng& rng) {
    QStringList members;
    for (int which = 0; which < 3; ++which)
        if (rng.chance(0.6)) members.append(infoMember(rng, kInfoKeys[which], valueFor(rng, which)));
    return u"{"_s + members.join(u",") + u"}";
}

QString infoDocument(Rng& rng) {
    QStringList members;
    QStringList used;
    const int others = rng.chance(0.6) ? 0 : rng.between(1, 3);
    for (int i = 0; i < others; ++i) members.append(jsonKey(rng, used) + infoSpace(rng) + u":" + infoSpace(rng) + jsonValue(rng, 1, rng.between(1, 2)));
    for (int which = 0; which < 3; ++which) {
        if (!rng.chance(0.88)) continue;
        members.append(infoMember(rng, kInfoKeys[which], valueFor(rng, which)));
        if (rng.chance(0.12)) members.append(infoMember(rng, kInfoKeys[which], valueFor(rng, which)));   // a repeated key
    }
    if (rng.chance(0.25)) {
        QStringList formats;
        const int count = rng.between(1, 3);
        for (int i = 0; i < count; ++i) formats.append(nestedInfo(rng));
        members.append(u"\"formats\":["_s + formats.join(u",") + u"]");
    }
    if (rng.chance(0.15)) members.append(u"\"meta\":"_s + nestedInfo(rng));
    for (qsizetype i = members.size() - 1; i > 0; --i) members.swapItemsAt(i, rng.between(0, int(i)));
    QString out = u"{"_s + infoSpace(rng);
    for (qsizetype i = 0; i < members.size(); ++i) {
        if (i > 0) out += infoSpace(rng) + u"," + infoSpace(rng);
        out += members[i];
    }
    return out + infoSpace(rng) + u"}";
}

// UTF-8; an unpaired surrogate becomes the three bytes of its code unit (which is not UTF-8) or U+FFFD.
QByteArray utf8Of(const QString& text, bool keepLoneSurrogates) {
    QByteArray out;
    out.reserve(text.size() * 3);
    for (qsizetype i = 0; i < text.size(); ++i) {
        char32_t cp = text[i].unicode();
        if (QChar::isHighSurrogate(cp) && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(char16_t(cp), text[i + 1].unicode());
            ++i;
        } else if (QChar::isSurrogate(cp) && !keepLoneSurrogates) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

QByteArray utf16Of(const QString& text, bool littleEndian, bool bom) {
    QByteArray out;
    const auto put = [&out, littleEndian](char16_t unit) {
        const char low = char(unit & 0xFF);
        const char high = char(unit >> 8);
        out += littleEndian ? low : high;
        out += littleEndian ? high : low;
    };
    if (bom) put(0xFEFF);
    for (const QChar c : text) put(c.unicode());
    return out;
}

// Byte sequences that are not UTF-8: stray continuation bytes, overlong forms, encoded surrogates (alone and as a CESU-8
// pair), code points above U+10FFFF, bytes that never occur, the old 5- and 6-byte forms and truncated sequences.
const QByteArray& notUtf8(Rng& rng) {
    static const QByteArray sequences[] = {
        QByteArray("\x80", 1), QByteArray("\xBF", 1), QByteArray("\xC0\x80", 2), QByteArray("\xC1\xBF", 2),
        QByteArray("\xC0\xAF", 2), QByteArray("\xC2", 1), QByteArray("\xE0\x80\x80", 3), QByteArray("\xE0\x9F\xBF", 3),
        QByteArray("\xED\xA0\x80", 3), QByteArray("\xED\xBF\xBF", 3), QByteArray("\xED\xA0\xBD\xED\xB8\x80", 6),
        QByteArray("\xF0\x80\x80\x80", 4), QByteArray("\xF0\x8F\xBF\xBF", 4), QByteArray("\xF4\x90\x80\x80", 4),
        QByteArray("\xF5\x80\x80\x80", 4), QByteArray("\xF8\x88\x80\x80\x80", 5), QByteArray("\xFC\x84\x80\x80\x80\x80", 6),
        QByteArray("\xFE", 1), QByteArray("\xFF", 1), QByteArray("\xE2\x82", 2), QByteArray("\xF0\x9F\x98", 3),
        QByteArray("\xC3\x28", 2), QByteArray("\xE2\x28\xA1", 3), QByteArray("\xFF\xFE", 2), QByteArray("\xE0\xA0", 2),
        QByteArray("\xC2\xC2\x80", 3), QByteArray("\xF4\x8F\xBF", 3)};
    return pick(rng, sequences);
}

// Valid sequences that a decoder might get wrong: noncharacters, the ends of the planes and of the surrogate gap, the
// smallest forms of each length, a BOM inside the text, no-break and ideographic space.
const QByteArray& rareUtf8(Rng& rng) {
    static const QByteArray sequences[] = {
        QByteArray("\xEF\xBF\xBE", 3), QByteArray("\xEF\xBF\xBF", 3), QByteArray("\xEF\xB7\x90", 3),
        QByteArray("\xF4\x8F\xBF\xBF", 4), QByteArray("\xF0\x9F\xBF\xBE", 4), QByteArray("\xED\x9F\xBF", 3),
        QByteArray("\xEE\x80\x80", 3), QByteArray("\xC2\x80", 2), QByteArray("\xDF\xBF", 2), QByteArray("\xE0\xA0\x80", 3),
        QByteArray("\xF0\x90\x80\x80", 4), QByteArray("\xEF\xBB\xBF", 3), QByteArray("\xC2\xA0", 2),
        QByteArray("\xE3\x80\x80", 3), QByteArray("\x7F", 1), QByteArray("\xEF\xBF\xBD", 3)};
    return pick(rng, sequences);
}

// A place to put bytes: mostly just inside a string value (after `:"`), so that only the decoding decides the answer.
qsizetype insertionPoint(Rng& rng, const QByteArray& bytes) {
    QList<qsizetype> places;
    for (qsizetype i = 0; i + 1 < bytes.size(); ++i) {
        if (bytes[i] == ':' && bytes[i + 1] == '"') places.append(i + 2);
    }
    if (!places.isEmpty() && rng.chance(0.85)) return places[rng.between(0, int(places.size()) - 1)];
    return rng.between(0, int(bytes.size()));
}

QString bytesField(const QByteArray& bytes) {
    return bytes.isEmpty() ? u"."_s : QString::fromLatin1(bytes.toHex());
}

QString generateV(Rng& rng) {
    const double roll = rng.next();
    QString doc;
    if (roll < 0.02) return bytesField(QByteArray());   // an empty file
    if (roll < 0.03) {
        const int count = rng.between(1, 3);
        for (int i = 0; i < count; ++i) doc += rng.chance(0.7) ? pick(rng, kJsonWhite) : pick(rng, kJsonOtherWhite);
    } else if (roll < 0.06) {
        // a root that is not an object
        doc = rng.chance(0.5) ? u"["_s + infoDocument(rng) + u"]" : jsonValue(rng, 0, rng.chance(0.5) ? 0 : 2);
    } else {
        doc = infoDocument(rng);
        if (rng.chance(0.05)) doc = jsonBreak(rng, doc);
    }
    if (rng.chance(0.1)) doc = jsonSpace(rng) + doc + jsonSpace(rng);

    const double encoding = rng.next();
    QByteArray bytes;
    if (encoding < 0.08) {
        bytes = utf8Of(doc, false);
        const QByteArray& bad = notUtf8(rng);
        if (rng.chance(0.1)) bytes += bad;   // at the very end (often a truncated sequence)
        else bytes.insert(insertionPoint(rng, bytes), bad);
    } else if (encoding < 0.11) {
        bytes = QByteArray("\xEF\xBB\xBF", 3) + utf8Of(doc, false);   // a BOM
    } else if (encoding < 0.14) {
        bytes = utf16Of(doc, rng.chance(0.5), rng.chance(0.5));
    } else if (encoding < 0.22) {
        bytes = utf8Of(doc, false);
        const int count = rng.between(1, 2);
        for (int i = 0; i < count; ++i) bytes.insert(insertionPoint(rng, bytes), rareUtf8(rng));
    } else {
        bytes = utf8Of(doc, rng.chance(0.1));
    }
    return bytesField(bytes);
}

// ---- kind K: YtDlpCommands (rp, rv, dl) and FfmpegCommands.tag (tag) ----
// Paths are normalised Windows absolute paths only (a drive letter, `\`, one to four parts of ASCII letters and digits, Korean,
// space, `.`, `(`, `)`, `&` and `%`; never a trailing space or dot, never an empty part), which java.nio.file.Path gives back
// unchanged. The tool paths are absent now and then (an absent yt-dlp makes the answer ERR); the js runtime's file name is one of
// the spellings that do and do not count as Deno or Node. The video id of a `dl` case is described at videoIdText.
constexpr QStringView kPathPartChars = u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789가나다라마바사아자차카타파하 .()&%";
constexpr QStringView kDriveLetters = u"CCCDEc";
constexpr QStringView kJsRuntimeNames[] = {u"node.exe", u"NODE.EXE", u"deno", u"Deno.exe", u"deno.EXE", u"node", u"Node.Exe.exe",
                                           u"bun.exe", u"node.cmd", u"nodejs.exe", u"node.exe.exe", u"denox.exe"};
constexpr QStringView kJsRuntimeValid[] = {u"node.exe", u"NODE.EXE", u"deno", u"Deno.exe", u"deno.EXE", u"node"};

QString winPathPart(Rng& rng) {
    QString part;
    const int length = rng.between(1, 12);
    for (int i = 0; i < length; ++i) part += charOf(rng, kPathPartChars);
    while (!part.isEmpty() && (part.back() == u' ' || part.back() == u'.')) part.chop(1);
    if (part.isEmpty()) part = u"x"_s;
    return part;
}

QString winPath(Rng& rng) {
    QString path = QString(QChar(kDriveLetters[rng.between(0, int(kDriveLetters.size()) - 1)])) + u":";
    const int parts = rng.between(1, 4);
    for (int i = 0; i < parts; ++i) path += u'\\' + winPathPart(rng);
    return path;
}

// The video id of a `dl` case: text(), so `* ? " < > |` and control characters occur and the port's refusal is checked against
// Kotlin's ERR. Left out are the inputs on which the port throws and Kotlin does not (downloadCommand's header): an empty id
// (replaced by "id"), `\`, `/` and `:` (removed), and control characters when the id has no ASCII letter or digit (Kotlin accepts
// an id of control characters only; with a letter beside them it throws; removed otherwise).
QString videoIdText(Rng& rng) {
    QString id = text(rng, 12);
    id.remove(u'\\').remove(u'/').remove(u':');
    bool hasAlnum = false;
    for (const QChar c : id) {
        const char16_t unit = c.unicode();
        if ((unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9')) hasAlnum = true;
    }
    if (!hasAlnum) {
        QString kept;
        for (const QChar c : id)
            if (c.unicode() >= 0x20) kept += c;
        id = kept;
    }
    return id.isEmpty() ? QStringLiteral("id") : id;
}

QString optionalPathField(Rng& rng, double absent) {
    return rng.chance(absent) ? QStringLiteral("-") : hex(winPath(rng));
}

QString jsRuntimeField(Rng& rng) {
    if (rng.chance(0.1)) return QStringLiteral("-");
    const QStringView name = rng.chance(0.7) ? pick(rng, kJsRuntimeValid) : pick(rng, kJsRuntimeNames);
    return hex(winPath(rng) + u'\\' + name);
}

QString generateK(Rng& rng) {
    static constexpr QStringView ops[] = {u"rp", u"rv", u"dl", u"tag"};
    const QString op = pick(rng, ops).toString();
    if (op == u"tag") {
        static constexpr QStringView extensions[] = {u".m4a", u".m4a", u".m4a", u".M4A", u".M4a", u".mp3", u".mp4", u"", u".m4a.tmp", u".m4"};
        QString output = winPath(rng) + pick(rng, extensions).toString();
        // the whole file name is the extension
        if (rng.chance(0.05)) output = winPath(rng) + (rng.chance(0.5) ? u"\\.m4a"_s : u"\\.M4A"_s);
        return u"tag\t" + hex(winPath(rng)) + u'\t' + hex(winPath(rng)) + u'\t' + (rng.chance(0.55) ? hex(winPath(rng)) : QStringLiteral("-")) +
               u'\t' + hex(winPath(rng)) + u'\t' + hex(output);
    }
    QString line = op + u'\t' + optionalPathField(rng, 0.12) + u'\t' + optionalPathField(rng, 0.3) + u'\t' + jsRuntimeField(rng) + u'\t' + hex(text(rng, 20));
    if (op == u"dl") line += u'\t' + hex(winPath(rng)) + u'\t' + hex(videoIdText(rng));
    return line;
}

// ---- kind A: Ffmetadata.render ----
// Every value is text(), and 60% get one to three of the characters that matter (`=` `;` `#` `\`, LF, CR, tab, NUL, U+001B,
// U+007F) at random places; 25% end in one to three backslashes.
constexpr QStringView kTagSpecials[] = {u"=", u";", u"#", u"\\", u"\n", u"\r", u"\t", QStringView(u"\0", 1), u"\u001b", u"\u007f",
                                        u"\\\n", u"\r\n", u"=\\", u"\u0001", u"\u001f"};

QString tagValueFrom(Rng& rng, QString value) {
    if (rng.chance(0.6)) {
        const int count = rng.between(1, 3);
        for (int i = 0; i < count; ++i)
            value.insert(rng.between(0, int(value.size())), pick(rng, kTagSpecials).toString());
    }
    if (rng.chance(0.25)) value += QString(u'\\').repeated(rng.between(1, 3));
    return value;
}

QString tagValue(Rng& rng) {
    return tagValueFrom(rng, text(rng, 14));
}

QString optionalTagField(Rng& rng) {
    std::optional<QString> value = nullable(rng);
    if (value) value = tagValueFrom(rng, *value);
    return hex(value);
}

QString generateA(Rng& rng) {
    const QString title = tagValue(rng);
    const QString artist = tagValue(rng);
    const QString albumArtist = rng.chance(0.2) ? artist : tagValue(rng);
    return hex(title) + u'\t' + hex(artist) + u'\t' + optionalTagField(rng) + u'\t' + hex(albumArtist) + u'\t' +
           QString::number(rng.between(-5, 1500)) + u'\t' + optionalTagField(rng) + u'\t' + optionalTagField(rng);
}

// ---- kind I: LyricsMatcher (titleVariants, artistVariants, normalize, isMatch, pick) ----
// Titles are built from pieces: a body (words, full-width words, Hangul as separate jamo, letters with combining marks, NFKC
// material of kind H, tokens of text()), zero to three bracketed parts of the four kinds (version words, Korean version words,
// plain words or credits inside; nested, unclosed or stray), a credit (` feat. X`, ` FT X`, ` Prod.X`, `\tNarr.\nX`, ` with X`,
// one after U+00A0...) and one of ten quote kinds. A candidate's title is made from the query's in 60% of the cases (as it is,
// without its brackets, without its credit, its body only, with other case, width or spaces, with a version part added); the
// artist likewise (an alias in parentheses, several joined by `&`, one letter, word boundaries such as `IU` and `Liu`). Album,
// length, instrumental and lyrics are optional and take their edge values (length differences 7.9, 8.0 and 8.1, NaN, the
// infinities, negative values; lyrics of white space only). A pick case has 0 to 6 candidates, often copies of one another so
// that equal ranks occur.
//
// No case contains Σ, σ or ς: in matching the C++ port takes ς and σ for the same letter (a user decision of 2026-11-15, a
// documented divergence), so every string is cleared of the code points whose NFKC, lowercase or NFKC of that holds one of the
// three (Σ σ ς themselves, and forms such as U+03F2 and the mathematical sigmas, which NFKC turns into them).
bool holdsSigma(const QString& value) {
    for (const QChar c : value) {
        const char16_t unit = c.unicode();
        if (unit == 0x03A3 || unit == 0x03C2 || unit == 0x03C3) return true;
    }
    return false;
}

const QList<bool>& sigmaSources() {
    static const QList<bool> table = [] {
        QList<bool> out(0x110000, false);
        for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;
            const QString compatible = kt::nfkc(codePointText(cp));
            const QString lowered = kt::lowercase(compatible);
            out[cp] = holdsSigma(compatible) || holdsSigma(lowered) || holdsSigma(kt::nfkc(lowered));
        }
        return out;
    }();
    return table;
}

QString sigmaFree(const QString& value) {
    const QList<bool>& sources = sigmaSources();
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size();) {
        char32_t cp = value.at(i).unicode();
        qsizetype units = 1;
        if (value.at(i).isHighSurrogate() && i + 1 < value.size() && value.at(i + 1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(value.at(i), value.at(i + 1));
            units = 2;
        }
        if (!sources.at(cp)) out += QStringView(value).sliced(i, units);
        i += units;
    }
    return out;
}

QString lyricsField(const QString& value) {
    return hex(sigmaFree(value));
}

QString lyricsField(const std::optional<QString>& value) {
    return value ? lyricsField(*value) : QStringLiteral("-");
}

constexpr QStringView kSongWords[] = {u"Love", u"poem", u"Hello", u"Stay", u"With", u"Me", u"Dance", u"Golden", u"Night", u"Rain",
                                      u"Without", u"You", u"Featuring", u"Prodigal", u"Son", u"Verse", u"Lively", u"Discover",
                                      u"Mixture", u"Democracy", u"Editor's", u"Korea", u"Don't", u"Stop", u"2", u"I", u"ATTACK",
                                      u"A-B", u"사랑", u"시", u"밤", u"노래", u"사랑의", u"한글", u"日本語", u"ÜNÏ", u"Café", u"Ｌｏｖｅ",
                                      u"feat", u"ft", u"prod", u"narr", u"with", u"Live", u"Remix", u"Ver", u"Ft.", u"FEAT."};
constexpr QStringView kVersionPhrases[] = {u"Japanese Ver.", u"Remixes", u"Remastered 2011", u"Live", u"Lived", u"Acoustic",
                                           u"Verse", u"Discover", u"Ver2", u"inst", u"Ｌｉｖｅ", u"Live Ver.", u"Radio Edit",
                                           u"Club Mix", u"Instrumental", u"English Version", u"Cover", u"Demo", u"Lives",
                                           u"Acoustics", u"Acousticd", u"Liveds", u"Remix2", u"Live2023", u"Mixture", u"Lively",
                                           u"Remix٣", u"LIVE", u"acousticed", u"remixes", u"versions", u"Ver.", u"live at Somewhere",
                                           u"2011 Remaster", u"Korean ver", u"Live١", u"Ver१", u"Remix²", u"Mix２", u"ıns", u"Remıx", u"ＲＥＭＩＸ",
                                           // bare `s` forms of the stems
                                           u"Covers", u"Demos", u"Edits", u"Mixs", u"Vers", u"Insts", u"Remasters", u"Koreans",
                                           u"Englishs", u"Versions", u"Instrumentals", u"Covers2"};
constexpr QStringView kKoreanVersionPhrases[] = {u"일본어 버전", u"라이브", u"반주", u"어쿠스틱", u"밴드 버전", u"리믹스", u"한국어Ver",
                                                 u"AR 반주", u"라이브 버전", u"어쿠스틱 라이브", u"인스트", u"커버", u"영어"};
constexpr QStringView kPlainPhrases[] = {u"Official Audio", u"MV", u"LOVE ATTACK", u"B", u"feat. X", u"with X", u"Prod. Y", u"Love poem",
                                         u"사랑의 시", u"Official", u"", u" ", u"Editor's Note"};
constexpr QStringView kOpenBrackets = u"([{<";
constexpr QStringView kCloseBrackets = u")]}>";
constexpr QStringView kCredits[] = {u" feat. X", u" FT X", u" Prod.X", u"\tNarr.\nX", u" with X", u"\u00A0feat. X", u" ft. X Remix",
                                    u" feat X Y", u" Feat.  Somebody", u" ft.Y", u" prod. 라이브", u" narr. X", u" FEAT. X\nY",
                                    u"\u3000feat. X", u" \u001cfeat. X", u" featuring X", u" feat.. X", u" ft. "};
// Ten quote kinds: the six pairs of Kotlin's QUOTES, the apostrophe inside a word, and three that are no quote to Kotlin.
constexpr QStringView kQuoteOpen[] = {u"'", u"\"", u"\u2018", u"\u201C", u"\u201A", u"\u201E", u"", u"`", u"\u00AB", u"\uFF02"};
constexpr QStringView kQuoteClose[] = {u"'", u"\"", u"\u2019", u"\u201D", u"\u201B", u"\u201F", u"'s", u"`", u"\u00BB", u"\uFF02"};
constexpr QStringView kWordSeparators[] = {u" ", u" ", u" ", u" ", u"  ", u"\t", u"\u3000", u"\u00A0", u"-", u"_", u"", u"\n", u" \u001c "};
constexpr QStringView kArtistNames[] = {u"IU", u"iu", u"I.U.", u"Liu", u"Liu Yifei", u"Rain", u"Rainbow", u"Jin", u"Jinx", u"BTS",
                                        u"Abtsa", u"소연", u"SOYEON", u"아이유", u"I.O.I", u"IOI", u"아이오아이", u"Taylor Swift",
                                        u"Swift Taylor", u"AKMU", u"악뮤", u"ABBA", u"Band", u"Friends", u"Someone",
                                        u"Somebody Else", u"(G)I-DLE", u"I-DLE", u"LOVE ATTACK", u"Ｉ Ｕ"};
// Names that are too short to match on once normalised (one letter, or no letter at all).
constexpr QStringView kShortArtistNames[] = {u"A", u"B", u"G", u"X", u"!", u"?", u"아", u"Ａ", u"A.", u"-"};
constexpr QStringView kArtistJoiners[] = {u" & ", u", ", u" feat. ", u" x ", u" X ", u"&", u" and "};
constexpr QStringView kAlbums[] = {u"Love poem", u"Another album", u"Golden", u"사랑", u"LOVE POEM!", u"love-poem", u"Ｌｏｖｅ ｐｏｅｍ"};
constexpr double kDurationDeltas[] = {0.0, 0.0, 7.9, -7.9, 8.0, -8.0, 8.1, -8.1, 0.5, 3.0, -2.0, 20.0, -20.0, 0.1, 1e-9, -7.999999};

QString asciiToFullWidth(const QString& value) {
    QString out = value;
    for (QChar& c : out) {
        const char16_t unit = c.unicode();
        if (unit >= 0x21 && unit <= 0x7E) c = QChar(char16_t(unit + 0xFEE0));
        else if (unit == u' ') c = QChar(char16_t(0x3000));
    }
    return out;
}

// ASCII letters in the other case (all upper, all lower, or each at random); everything else stays.
QString asciiCaseShuffle(Rng& rng, const QString& value) {
    const int mode = rng.between(0, 2);
    QString out = value;
    for (QChar& c : out) {
        const char16_t unit = c.unicode();
        const bool upper = unit >= u'A' && unit <= u'Z';
        const bool lower = unit >= u'a' && unit <= u'z';
        if (!upper && !lower) continue;
        const bool toUpper = mode == 0 || (mode == 2 && rng.chance(0.5));
        if (toUpper && lower) c = QChar(char16_t(unit - 32));
        else if (!toUpper && upper) c = QChar(char16_t(unit + 32));
    }
    return out;
}

// A Hangul syllable written as its conjoining jamo (L V, or L V T), which NFKC composes.
QString decomposedSyllable(Rng& rng) {
    const int index = rng.between(0, 11171);
    QString out;
    out += QChar(char16_t(0x1100 + index / 588));
    out += QChar(char16_t(0x1161 + (index % 588) / 28));
    if (index % 28 != 0) out += QChar(char16_t(0x11A7 + index % 28));
    return out;
}

QString markedLetter(Rng& rng) {
    QString out = pick(rng, kBases).toString();
    const int marks = rng.between(1, 2);
    for (int i = 0; i < marks; ++i) out += QChar(pick(rng, kMarks));
    return out;
}

QString songWord(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.45) return pick(rng, kSongWords).toString();
    if (roll < 0.53) return asciiToFullWidth(pick(rng, kSongWords).toString());
    if (roll < 0.61) {
        QString word;
        const int syllables = rng.between(1, 3);
        for (int i = 0; i < syllables; ++i) word += decomposedSyllable(rng);
        return word;
    }
    if (roll < 0.66) return QString(QChar(char16_t(rng.between(0xAC00, 0xD7A3))));
    if (roll < 0.73) return markedLetter(rng) + (rng.chance(0.5) ? pick(rng, kSongWords).toString() : QString());
    if (roll < 0.80) return nfkcPiece(rng);
    if (roll < 0.88) return token(rng);
    if (roll < 0.94) return pick(rng, kVersionPhrases).toString();
    return pick(rng, kKoreanVersionPhrases).toString();
}

QString songWords(Rng& rng, int low, int high) {
    QString out;
    const int count = rng.between(low, high);
    for (int i = 0; i < count; ++i) {
        if (i > 0) out += pick(rng, kWordSeparators);
        out += songWord(rng);
    }
    return out;
}

QString bracketContent(Rng& rng, int depth);

QString bracketedPart(Rng& rng, int depth) {
    const int kind = rng.between(0, 3);
    const QString content = bracketContent(rng, depth);
    const double shape = rng.next();
    if (shape < 0.06) return QString(kOpenBrackets[kind]) + content;                     // never closed
    if (shape < 0.10) return content + QString(kCloseBrackets[kind]);                    // a stray closing bracket
    if (shape < 0.14) return QString(kOpenBrackets[kind]) + content + QString(kCloseBrackets[rng.between(0, 3)]);   // crossed kinds
    return QString(kOpenBrackets[kind]) + content + QString(kCloseBrackets[kind]);
}

QString bracketContent(Rng& rng, int depth) {
    const double roll = rng.next();
    QString content;
    if (roll < 0.40) content = pick(rng, kVersionPhrases).toString();
    else if (roll < 0.55) content = pick(rng, kKoreanVersionPhrases).toString();
    else if (roll < 0.80) content = rng.chance(0.5) ? pick(rng, kPlainPhrases).toString() : songWords(rng, 1, 3);
    else content = text(rng, 6);
    if (depth < 2 && rng.chance(0.15)) {
        const QString inner = bracketedPart(rng, depth + 1);
        content = rng.chance(0.5) ? content + u' ' + inner : inner + u' ' + content;
    }
    return content;
}

// The pieces of a title, so that a candidate can leave some out.
struct TitlePieces {
    QString body;
    QString quotedBody;
    QStringList partsBefore;   // bracketed parts between the body and the credit
    QString credit;
    QStringList partsAfter;    // bracketed parts after the credit
    QString lead;
    QString trail;
};

QString joinParts(Rng& rng, const QStringList& parts) {
    QString out;
    for (const QString& part : parts) out += (rng.chance(0.85) ? u" "_s : QString()) + part;
    return out;
}

TitlePieces titlePieces(Rng& rng) {
    TitlePieces t;
    t.body = songWords(rng, 1, 4);
    t.quotedBody = t.body;
    if (rng.chance(0.5)) {
        const int kind = rng.between(0, int(std::size(kQuoteOpen)) - 1);
        if (kind == 6) t.quotedBody = u"Don"_s + kQuoteClose[kind].left(1) + u"t "_s + t.body;   // an apostrophe in a word
        else t.quotedBody = kQuoteOpen[kind].toString() + t.body + kQuoteClose[kind];
    }
    const int parts = rng.chance(0.4) ? 0 : rng.between(1, 3);
    for (int i = 0; i < parts; ++i) (rng.chance(0.7) ? t.partsBefore : t.partsAfter).append(bracketedPart(rng, 0));
    if (rng.chance(0.5)) t.credit = pick(rng, kCredits).toString();
    if (rng.chance(0.08)) t.lead = pick(rng, kWordSeparators).toString();
    if (rng.chance(0.08)) t.trail = pick(rng, kWordSeparators).toString();
    return t;
}

QString composeTitle(Rng& rng, const TitlePieces& t) {
    return t.lead + t.quotedBody + joinParts(rng, t.partsBefore) + t.credit + joinParts(rng, t.partsAfter) + t.trail;
}

// The title of a candidate: made from the query's pieces with the probability `fromQuery` (60% unless the case wants a candidate
// that is likely to match), otherwise another title.
QString candidateTitle(Rng& rng, const TitlePieces& t, double fromQuery = 0.6) {
    if (!rng.chance(fromQuery)) {
        const double other = rng.next();
        if (other < 0.15) return t.quotedBody + u" 2";
        if (other < 0.25) return songWord(rng);
        if (other < 0.30) return text(rng, 10);
        return composeTitle(rng, titlePieces(rng));
    }
    const double roll = rng.next();
    QString title;
    if (roll < 0.30) title = composeTitle(rng, t);
    else if (roll < 0.40) title = t.quotedBody + t.credit;
    else if (roll < 0.52) title = t.quotedBody + joinParts(rng, t.partsBefore + t.partsAfter);
    else if (roll < 0.66) title = t.quotedBody;
    else if (roll < 0.74) title = t.body;
    else if (roll < 0.82) title = asciiCaseShuffle(rng, composeTitle(rng, t));
    else if (roll < 0.86) title = asciiToFullWidth(composeTitle(rng, t));
    else if (roll < 0.90) title = composeTitle(rng, t).replace(u' ', rng.chance(0.5) ? u"  "_s : u"\t"_s);
    else {
        // an added part: a version part half the time, otherwise a plain part or a credit (a shortening only the candidate
        // needs: title rank 2)
        const QString base = rng.chance(0.5) ? t.quotedBody : composeTitle(rng, t);
        const double what = rng.next();
        const int kind = rng.between(0, 3);
        if (what < 0.5) {
            const QString added = rng.chance(0.7) ? pick(rng, kVersionPhrases).toString() : pick(rng, kKoreanVersionPhrases).toString();
            title = base + u' ' + kOpenBrackets[kind] + added + kCloseBrackets[kind];
        } else if (what < 0.8) {
            title = base + u' ' + kOpenBrackets[kind] + pick(rng, kPlainPhrases) + kCloseBrackets[kind];
        } else {
            title = base + pick(rng, kCredits);
        }
    }
    return title;
}

struct ArtistPieces {
    QString text;
    QStringList names;   // the names it is made of (for a candidate that has only one of them)
};

ArtistPieces artistPieces(Rng& rng) {
    const double roll = rng.next();
    ArtistPieces a;
    const QString first = rng.chance(0.08) ? pick(rng, kShortArtistNames).toString() : pick(rng, kArtistNames).toString();
    const QString second = rng.chance(0.15) ? pick(rng, kShortArtistNames).toString() : pick(rng, kArtistNames).toString();
    a.names = {first, second};
    if (roll < 0.40) {
        a.text = first;
        a.names = {first};
    } else if (roll < 0.60) {
        a.text = first + (rng.chance(0.8) ? u" ("_s : u"("_s) + second + u")";
    } else if (roll < 0.75) {
        a.text = first + pick(rng, kArtistJoiners) + second;
    } else if (roll < 0.82) {
        const QString third = pick(rng, kArtistNames).toString();
        a.names.append(third);
        static constexpr QStringView shapes[] = {u"%1 (%2) (%3) (%1)", u"%1 (%2 (%3)) (%1)", u"(%1)%2", u"%1 (%2) (%3)", u"%1 & %2 (%3)",
                                                 u"%1 ( %2 )", u"%1 (\t%2 \t) (%3 )", u"( %1 )%2"};
        a.text = pick(rng, shapes).toString().arg(first, second, third);
    } else if (roll < 0.89) {
        a.text = asciiToFullWidth(first);
        a.names = {first};
    } else if (roll < 0.94) {
        a.text = rng.chance(0.5) ? QString(QChar(char16_t(rng.between(u'A', u'Z')))) : QString(QChar(char16_t(rng.between(0xAC00, 0xD7A3))));
        a.names = {a.text};
    } else if (roll < 0.98) {
        a.text = text(rng, 8);
        a.names = {a.text};
    } else {
        a.text = rng.chance(0.5) ? QString() : u"  "_s;
        a.names = {a.text};
    }
    return a;
}

QString candidateArtist(Rng& rng, const ArtistPieces& a, double fromQuery = 0.6) {
    if (!rng.chance(fromQuery)) return artistPieces(rng).text;
    const double roll = rng.next();
    if (roll < 0.45) return a.text;
    if (roll < 0.55) return a.names.at(rng.between(0, int(a.names.size()) - 1));
    if (roll < 0.70) return a.text + pick(rng, kArtistJoiners) + pick(rng, kArtistNames);
    if (roll < 0.78) return pick(rng, kArtistNames).toString() + pick(rng, kArtistJoiners) + a.text;
    if (roll < 0.88) return asciiCaseShuffle(rng, a.text);
    if (roll < 0.94) return asciiToFullWidth(a.text);
    return a.text + u" (" + pick(rng, kArtistNames) + u")";
}

struct QueryPieces {
    ArtistPieces artist;
    TitlePieces title;
    QString titleText;
    std::optional<QString> album;
    std::optional<int> duration;
};

QueryPieces queryPieces(Rng& rng) {
    QueryPieces q;
    q.artist = artistPieces(rng);
    q.title = titlePieces(rng);
    q.titleText = rng.chance(0.05) ? text(rng, 12) : composeTitle(rng, q.title);
    const double album = rng.next();
    if (album < 0.35) q.album = std::nullopt;
    else if (album < 0.40) q.album = u"  "_s;
    else if (album < 0.43) q.album = QString();
    else if (album < 0.85) q.album = pick(rng, kAlbums).toString();
    else q.album = q.title.body;
    const double duration = rng.next();
    if (duration < 0.15) q.duration = std::nullopt;
    else if (duration < 0.75) q.duration = rng.between(150, 300);
    else if (duration < 0.80) q.duration = 0;
    else if (duration < 0.82) {
        static constexpr int edges[] = {-5, 2147483647, -2147483647 - 1, 1, -1};
        q.duration = pick(rng, edges);
    } else {
        q.duration = rng.between(0, 1000);
    }
    return q;
}

QString queryFields(const QueryPieces& q) {
    return lyricsField(q.artist.text) + u'\t' + lyricsField(q.titleText) + u'\t' + lyricsField(q.album) + u'\t' +
           (q.duration ? QString::number(*q.duration) : QStringLiteral("-"));
}

struct CandidatePieces {
    std::optional<QString> trackName;
    std::optional<QString> artistName;
    std::optional<QString> albumName;
    std::optional<double> duration;
    std::optional<bool> instrumental;
    std::optional<QString> plainLyrics;
};

std::optional<double> candidateDuration(Rng& rng, const std::optional<int>& wanted) {
    const double roll = rng.next();
    if (roll < 0.13) return std::nullopt;
    if (roll < 0.17) return std::numeric_limits<double>::quiet_NaN();
    if (roll < 0.19) return std::numeric_limits<double>::infinity();
    if (roll < 0.21) return -std::numeric_limits<double>::infinity();
    if (roll < 0.23) return -0.0;
    if (roll < 0.25) return -double(rng.between(1, 300)) - rng.next();
    if (!wanted) return double(rng.between(0, 600)) + (rng.chance(0.3) ? rng.next() : 0.0);
    if (roll < 0.85) return double(*wanted) + pick(rng, kDurationDeltas);
    return double(*wanted) + (rng.next() - 0.5) * 40.0;
}

// A candidate for the query; a `likely` one has its title and artist from the query's and lyrics that count.
CandidatePieces candidatePieces(Rng& rng, const QueryPieces& q, bool likely = false) {
    CandidatePieces c;
    const double fromQuery = likely ? 1.0 : 0.6;
    c.trackName = rng.chance(0.05) ? std::nullopt : std::optional<QString>(rng.chance(0.03) ? QString() : candidateTitle(rng, q.title, fromQuery));
    c.artistName = rng.chance(0.05) ? std::nullopt : std::optional<QString>(candidateArtist(rng, q.artist, fromQuery));
    const double album = rng.next();
    if (album < 0.20) c.albumName = std::nullopt;
    else if (album < 0.23) c.albumName = QString();
    else if (album < 0.26) c.albumName = u" "_s;
    else if (album < 0.56) c.albumName = q.album.value_or(u"Love poem"_s);
    else if (album < 0.66) c.albumName = asciiCaseShuffle(rng, q.album.value_or(u"Love poem"_s)) + u"!";
    else c.albumName = pick(rng, kAlbums).toString();
    c.duration = candidateDuration(rng, q.duration);
    const double instrumental = rng.next();
    c.instrumental = instrumental < 0.18 ? std::nullopt : std::optional<bool>(instrumental > (likely ? 0.98 : 0.92));
    const double lyrics = likely ? 0.5 : rng.next();   // (0.5: the plain lyrics below)
    if (lyrics < 0.04) c.plainLyrics = std::nullopt;
    else if (lyrics < 0.07) c.plainLyrics = QString();
    else if (lyrics < 0.12) {
        QString blank;
        const int length = rng.between(1, 4);
        for (int i = 0; i < length; ++i) blank += pick(rng, kWhitespace);
        c.plainLyrics = blank;
    } else if (lyrics < 0.85) {
        c.plainLyrics = u"La la la\nLa la\nLa"_s;
    } else {
        c.plainLyrics = text(rng, 12);
    }
    return c;
}

QString candidateFields(const CandidatePieces& c) {
    QString bits = QStringLiteral("-");
    if (c.duration) bits = QStringLiteral("%1").arg(std::bit_cast<quint64>(*c.duration), 16, 16, QChar(u'0'));
    const QString instrumental = c.instrumental ? (*c.instrumental ? u"1"_s : u"0"_s) : u"-"_s;
    return lyricsField(c.trackName) + u'\t' + lyricsField(c.artistName) + u'\t' + lyricsField(c.albumName) + u'\t' + bits + u'\t' +
           instrumental + u'\t' + lyricsField(c.plainLyrics);
}

// ---- kind I: inputs that reach the decisions most responsible for wrong lyrics ----
// Pairs of artists where one is inside the other as letters but not as a word (`IU` in `Liu`): never the same artist.
constexpr QStringView kSubstringArtists[][2] = {{u"IU", u"Liu"}, {u"Rain", u"Rainbow"}, {u"Jin", u"Jinx"}, {u"BTS", u"Abtsa"},
                                                {u"AKMU", u"AKMUX"}, {u"Band", u"Bandit"}, {u"소연", u"소연희"}, {u"IOI", u"IOIS"},
                                                {u"Taylor", u"Taylors"}, {u"아이유", u"아이유나"}, {u"Ｉ Ｕ", u"LIU"}};
// Plain titles, parts a query may lose (no version marker for the query), parts only a candidate may lose (title rank 2), and
// parts with a bare-`s` version word that a candidate may not lose.
constexpr QStringView kPlainTitles[] = {u"Love poem", u"Hello", u"Golden", u"Stay", u"사랑의 시", u"Night Rain", u"Ｌｏｖｅ ｐｏｅｍ",
                                        u"Don't Stop", u"\u201CGolden\u201D", u"\u1112\u1161\u11AB\u1100\u1173\u11AF"};
constexpr QStringView kQueryRemovable[] = {u" (feat. X)", u" (Official)", u" [MV]", u" {Editor's Note}", u" <Verse 2>", u" (with X)",
                                           u" (Lived)", u" feat. X", u" ft. Y", u" (Live)", u" (라이브)"};
constexpr QStringView kCandidateRemovable[] = {u" (Official Audio)", u" (MV)", u" feat. X", u" [Lyric Video]", u" (Discover)",
                                               u" {Mixture}", u" <with Y>", u" Prod. Z"};
constexpr QStringView kBareSParts[] = {u" (Covers)", u" (Demos)", u" (Edits)", u" (Vers)", u" [Insts]", u" (Mixs)", u" (Remasters)",
                                       u" {Versions}"};

// A query artist and a name that holds it as letters but not as a word, either way round, sometimes with more words.
std::pair<QString, QString> substringArtists(Rng& rng) {
    const int at = rng.between(0, int(std::size(kSubstringArtists)) - 1);
    const bool swap = rng.chance(0.4);
    const QString inner = kSubstringArtists[at][0].toString();
    QString outer = kSubstringArtists[at][1].toString();
    const double shape = rng.next();
    if (shape < 0.2) outer += u" Yifei";
    else if (shape < 0.3) outer = u"The "_s + outer;
    else if (shape < 0.4) outer = asciiCaseShuffle(rng, outer);
    return swap ? std::pair{outer, inner} : std::pair{inner, outer};
}

CandidatePieces acceptableCandidate(const QString& title, const QString& artist) {
    return CandidatePieces{title, artist, std::nullopt, std::nullopt, false, u"La la la\nLa la\nLa"_s};
}

// A match case whose title and lyrics agree and whose artists are a substring-not-word pair (Kotlin answers 0).
QString substringArtistMatch(Rng& rng) {
    QueryPieces query = queryPieces(rng);
    const auto [wanted, found] = substringArtists(rng);
    query.artist = {wanted, {wanted}};
    CandidatePieces c = acceptableCandidate(rng.chance(0.7) ? query.titleText : asciiCaseShuffle(rng, query.titleText), found);
    if (rng.chance(0.3)) c.instrumental = std::nullopt;
    if (query.duration && rng.chance(0.5)) c.duration = double(*query.duration) + 1.0;
    c.albumName = query.album;
    return u"match\t"_s + queryFields(query) + u'\t' + candidateFields(c);
}

// A pick case whose candidates mostly are acceptable and tie: the whole title, the query's title without its removable part
// (title rank 1), the title with a part only the candidate may lose (rank 2), a part with a bare-`s` version word, another
// song, and now and then a substring-not-word artist; lengths null, NaN, equal or close; albums equal or not.
QString orderedPick(Rng& rng) {
    QueryPieces query = queryPieces(rng);
    const QString body = pick(rng, kPlainTitles).toString();
    const QString removable = rng.chance(0.75) ? pick(rng, kQueryRemovable).toString() : QString();
    query.titleText = body + removable;
    if (!query.duration || rng.chance(0.4)) query.duration = rng.between(150, 300);
    QString partner;
    if (rng.chance(0.5)) {
        const auto [wanted, found] = substringArtists(rng);
        query.artist = {wanted, {wanted}};
        partner = found;
    } else {
        query.artist = {pick(rng, kArtistNames).toString(), {}};
        partner = pick(rng, kArtistNames).toString();
    }
    if (!query.album || rng.chance(0.3)) query.album = pick(rng, kAlbums).toString();
    const int count = rng.between(2, 6);
    QString line = u"pick\t"_s + queryFields(query) + u'\t' + QString::number(count);
    for (int i = 0; i < count; ++i) {
        const double kind = rng.next();
        QString title;
        if (kind < 0.25) title = query.titleText;
        else if (kind < 0.45) title = body;
        else if (kind < 0.70) title = (rng.chance(0.5) ? body : query.titleText) + pick(rng, kCandidateRemovable);
        else if (kind < 0.85) title = body + pick(rng, kBareSParts);
        else if (kind < 0.92) title = asciiCaseShuffle(rng, query.titleText);
        else title = pick(rng, kPlainTitles).toString();
        CandidatePieces c = acceptableCandidate(title, rng.chance(0.2) ? partner : query.artist.text);
        const double album = rng.next();
        if (album < 0.45) c.albumName = query.album;
        else if (album < 0.75) c.albumName = pick(rng, kAlbums).toString();
        const double length = rng.next();
        if (length < 0.2) c.duration = std::nullopt;
        else if (length < 0.45) c.duration = std::numeric_limits<double>::quiet_NaN();
        else if (length < 0.65) c.duration = double(*query.duration);
        else if (length < 0.90) c.duration = double(*query.duration) + (rng.chance(0.5) ? 3.0 : -3.0);
        else c.duration = double(*query.duration) + 20.0;
        const double flags = rng.next();
        if (flags < 0.04) c.instrumental = true;
        else if (flags < 0.25) c.instrumental = std::nullopt;
        if (rng.chance(0.06)) c.plainLyrics = u" \n "_s;
        line += u'\t' + candidateFields(c);
    }
    return line;
}

QString generateI(Rng& rng) {
    const double op = rng.next();
    if (op < 0.19) {
        const double source = rng.next();
        QString title;
        if (source < 0.70) title = composeTitle(rng, titlePieces(rng));
        else if (source < 0.90) title = candidateTitle(rng, titlePieces(rng));
        else title = text(rng, 20);
        return u"tv\t"_s + lyricsField(title);
    }
    if (op < 0.38) return u"av\t"_s + lyricsField(rng.chance(0.9) ? artistPieces(rng).text : text(rng, 20));
    if (op < 0.55) {
        const double source = rng.next();
        QString value;
        if (source < 0.35) value = songWords(rng, 1, 6);
        else if (source < 0.55) value = composeTitle(rng, titlePieces(rng));
        else if (source < 0.70) value = artistPieces(rng).text;
        else if (source < 0.85) {
            for (int i = rng.between(1, 6); i > 0; --i) value += nfkcPiece(rng);
        } else {
            value = text(rng, 30);
        }
        return u"norm\t"_s + lyricsField(value);
    }
    const QueryPieces query = queryPieces(rng);
    if (op < 0.79) {
        if (rng.chance(0.1)) return substringArtistMatch(rng);
        return u"match\t"_s + queryFields(query) + u'\t' + candidateFields(candidatePieces(rng, query, rng.chance(0.65)));
    }
    if (rng.chance(0.5)) return orderedPick(rng);
    // pick: 0 to 6 candidates, half of them copies of one candidate with another length, album or title, so ranks tie
    const int count = rng.between(0, 6);
    const CandidatePieces base = candidatePieces(rng, query, rng.chance(0.75));
    QString line = u"pick\t"_s + queryFields(query) + u'\t' + QString::number(count);
    for (int i = 0; i < count; ++i) {
        CandidatePieces c = base;
        const double roll = rng.next();
        if (roll < 0.25) {
            // an exact copy
        } else if (roll < 0.50) {
            const double tweak = rng.next();
            if (tweak < 0.4) c.duration = candidateDuration(rng, query.duration);
            else if (tweak < 0.7) c.albumName = rng.chance(0.5) ? query.album : std::optional<QString>(pick(rng, kAlbums).toString());
            else c.trackName = candidateTitle(rng, query.title);
        } else if (roll < 0.85) {
            c = candidatePieces(rng, query, rng.chance(0.4));
        } else {
            c = candidatePieces(rng, queryPieces(rng));
        }
        line += u'\t' + candidateFields(c);
    }
    return line;
}

}  // namespace

void registerKind(char kind, Generator gen) {
    generators().insert(kind, gen);
}

Generator generatorFor(char kind) {
    return generators().value(kind, nullptr);
}

void registerGenerators() {
    registerKind('S', generateS);
    registerKind('C', generateC);
    registerKind('F', generateF);
    registerKind('D', generateD);
    registerKind('G', generateG);
    registerKind('T', generateT);
    registerKind('L', generateL);
    registerKind('Y', generateY);
    registerKind('R', generateR);
    registerKind('N', generateN);
    registerKind('H', generateH);
    registerKind('O', generateO);
    registerKind('J', generateJ);
    registerKind('E', generateE);
    registerKind('Q', generateQ);
    registerKind('V', generateV);
    registerKind('K', generateK);
    registerKind('A', generateA);
    registerKind('I', generateI);
}

// Weights of fuzz.mts's token(); the tricky case letters take 4% from the Korean share (16% -> 12%).
QString token(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.3) return charOf(rng, kAscii);
    if (roll < 0.42) return charOf(rng, kPunct);
    if (roll < 0.52) return charOf(rng, kForbidden);
    if (roll < 0.58) return pick(rng, kControl).toString();
    if (roll < 0.66) return pick(rng, kSpaces).toString();
    if (roll < 0.78) return charOf(rng, kKorean);
    if (roll < 0.82) return pick(rng, kTricky).toString();
    if (roll < 0.9) return pick(rng, kEmoji).toString();
    if (roll < 0.93) return pick(rng, kLone).toString();
    QString device = pick(rng, kDevices).toString();
    return device + pick(rng, kDeviceTails);
}

QString text(Rng& rng, int maxTokens) {
    const double shape = rng.next();
    if (shape < 0.08) return QString();
    if (shape < 0.18) return pick(rng, kDevices).toString() + pick(rng, kDeviceExtensions);
    if (shape < 0.3) {
        // long runs near the truncation limits of the file names (80 for an artist or a folder, 180 for a whole name)
        const QString unit = pick(rng, kRunUnits).toString();
        QString result = unit.repeated(rng.between(60, 220));
        if (rng.chance(0.5)) result += token(rng);
        return result;
    }
    const int few = std::min(12, maxTokens);
    const int length = shape < 0.7 ? rng.between(1, few) : rng.between(few, maxTokens);
    QString result;
    for (int i = 0; i < length; ++i) result += token(rng);
    return result;
}

std::optional<QString> nullable(Rng& rng) {
    const double roll = rng.next();
    if (roll < 0.08) return std::nullopt;
    if (roll < 0.16) return QString();
    if (roll < 0.24) {
        QString blank;
        const int length = rng.between(1, 4);
        for (int i = 0; i < length; ++i) blank += pick(rng, kWhitespace);
        return blank;
    }
    return text(rng);
}

}  // namespace xgs::diff
