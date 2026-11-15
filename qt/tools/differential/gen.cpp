#include "gen.h"

#include <QHash>
#include <QStringView>

#include <algorithm>
#include <iterator>

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
