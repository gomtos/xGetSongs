// xgs-diff: the C++ side of the differential test of the Qt port (qt/tools/differential/run.ps1 drives it with Oracle.java,
// the Kotlin side). Input and output are ASCII, one line per case or answer.
//
//   xgs-diff gen <kind> <count> <seed>    write <count> cases of a kind ("K\tfields...")
//   xgs-diff answer                       answer the cases on stdin, one line each
//   xgs-diff chartable                    for each UTF-16 unit 0000..FFFF: "w l d u c" from xgs::kt (kind X)
//   xgs-diff constants                    the user-facing constants, sorted by name: "name hex" (kind M)
//   xgs-diff unicodecase                  for each code point 0..10FFFF but the surrogates: "cp lower upper" from
//                                         kt::lowercase and kt::uppercase of the one-code-point string (kind U)
//   xgs-diff lettertable                  for each code point 0..10FFFF but the surrogates: "cp d" = kt::isLetterOrDigit(char32_t)
//                                         as 0/1 (kind B)
//   xgs-diff nfkctable                    for each code point 0..10FFFF but the surrogates: "cp hex" = kt::nfkc of the
//                                         one-code-point string (kind Z)
//   xgs-diff qt-chartable                 as chartable, from Qt's own tables only (gen-tables.ps1)
//   xgs-diff qt-casetable               for each unit: QString::toUpper and QString::toLower of it, as hex (gen-tables.ps1)
//   xgs-diff qt-lettertable               as lettertable, from Qt's categories only (gen-tables.ps1)
//   xgs-diff qt-version                  the Qt version the tables were compared with
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <io.h>

#include "codec.h"
#include "filename/filename_formatter.h"
#include "gen.h"
#include "input/input_classifier.h"
#include "log/log_redaction.h"
#include "lyrics/lyrics_extractor.h"
#include "text/kt_text.h"
#include "title/title_parser.h"

namespace xgs::diff {

using Answerer = QString (*)(const QStringList& fields);   // fields = the case line's fields after the kind letter

namespace {

QMap<char, Answerer>& answerers() {
    static QMap<char, Answerer> table;
    return table;
}

QMap<QString, QString>& constantTable() {
    static QMap<QString, QString> table;
    return table;
}

std::string toAscii(const QString& line) {
    return line.toLatin1().toStdString();
}

QString hexText(const QString& value) {
    return hex(std::optional<QString>(value));
}

QString textOf(const QString& field) {
    return unhex(field).value_or(QString());
}

QString flag(bool value) {
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

QString okHex(const QString& value) {
    return QStringLiteral("OK ") + hexText(value);
}

// The decimal field of a case (an index), or false when it is not one.
bool numberOf(const QString& field, qsizetype& out) {
    bool ok = false;
    out = field.toLongLong(&ok);
    return ok;
}

// ---- kind S ----
// A case with too few fields answers "?" (run.ps1 fails the run on any "?").
QString answerS(const QStringList& fields) {
    if (fields.size() < 2) return QStringLiteral("?");
    const QString& op = fields.at(0);
    const QString value = textOf(fields.at(1));
    if (op == u"lower") return okHex(kt::lowercase(value));
    if (op == u"upper") return okHex(kt::uppercase(value));
    if (op == u"trim") return okHex(kt::trim(value));
    if (op == u"trimStart") return okHex(kt::trimStart(value));
    if (op == u"trimEnd") return okHex(kt::trimEnd(value));
    if (op == u"blank") return flag(kt::isBlank(value));
    // ignoreCase family: S op text other [from|replacement]
    if (op == u"ci-starts" || op == u"ci-ends" || op == u"ci-contains") {
        if (fields.size() < 3) return QStringLiteral("?");
        const QString other = textOf(fields.at(2));
        if (op == u"ci-starts") return flag(kt::startsWith(value, other, kt::CaseMode::Ignore));
        if (op == u"ci-ends") return flag(kt::endsWith(value, other, kt::CaseMode::Ignore));
        return flag(kt::contains(value, other, kt::CaseMode::Ignore));
    }
    if (op == u"ci-starts-from") {
        if (fields.size() < 4) return QStringLiteral("?");
        qsizetype from = 0;
        if (!numberOf(fields.at(3), from)) return QStringLiteral("?");
        return flag(kt::startsWith(value, textOf(fields.at(2)), from, kt::CaseMode::Ignore));
    }
    if (op == u"ci-index" || op == u"index") {
        if (fields.size() < 4) return QStringLiteral("?");
        qsizetype from = 0;
        if (!numberOf(fields.at(3), from)) return QStringLiteral("?");
        return QString::number(kt::indexOf(value, textOf(fields.at(2)), from, op == u"ci-index" ? kt::CaseMode::Ignore : kt::CaseMode::Sensitive));
    }
    if (op == u"ci-replace" || op == u"replace") {
        if (fields.size() < 4) return QStringLiteral("?");
        return okHex(kt::replace(value, textOf(fields.at(2)), textOf(fields.at(3)), op == u"ci-replace" ? kt::CaseMode::Ignore : kt::CaseMode::Sensitive));
    }
    if (op == u"eqic") {
        const QString other = fields.size() > 2 ? textOf(fields.at(2)) : QString();
        if (fields.size() < 3 || value.size() != 1 || other.size() != 1) return QStringLiteral("?");
        return flag(kt::equalsIgnoreCase(value.at(0), other.at(0)));
    }
    return QStringLiteral("?");
}

// ---- kind C ----
QString answerC(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    const ClassifyResult result = classifyInput(textOf(fields.at(0)));
    if (!result.ok()) return QStringLiteral("REJECT ") + rejectName(result.reason);
    const ParsedInput& input = *result.input;
    if (input.kind == ParsedInput::Kind::Video) return QStringLiteral("V ") + hexText(input.id);
    return QStringLiteral("P ") + hexText(input.id) + u' ' + hex(input.alsoVideoId);
}

// ---- kinds F, D, G ----
// F rank includeRank artist title -> OK hex | ERR (rank outside 1..999)
QString answerF(const QStringList& fields) {
    if (fields.size() < 4) return QStringLiteral("?");
    bool ok = false;
    const int rank = fields.at(0).toInt(&ok);
    if (!ok) return QStringLiteral("?");
    try {
        return okHex(filename::format(rank, textOf(fields.at(2)), textOf(fields.at(3)), fields.at(1) == u"1"));
    } catch (const std::invalid_argument&) {
        return QStringLiteral("ERR");
    }
}

// D title -> OK hex
QString answerD(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    return okHex(filename::folderName(unhex(fields.at(0))));
}

// G isPlaylist playlistTitle typed -> OK hex | NULL
QString answerG(const QStringList& fields) {
    if (fields.size() < 3) return QStringLiteral("?");
    const std::optional<QString> folder = filename::destinationFolder(fields.at(0) == u"1", unhex(fields.at(1)), unhex(fields.at(2)));
    return folder ? okHex(*folder) : QStringLiteral("NULL");
}

// ---- kind T ----
// T rawTitle channel metaArtist metaTrack -> OK artist title HIGH|MEDIUM|LOW
QString answerT(const QStringList& fields) {
    if (fields.size() < 4) return QStringLiteral("?");
    const ParsedTrack parsed = parseTitle(textOf(fields.at(0)), unhex(fields.at(1)), unhex(fields.at(2)), unhex(fields.at(3)));
    const QString confidence = parsed.confidence == Confidence::High     ? QStringLiteral("HIGH")
                               : parsed.confidence == Confidence::Medium ? QStringLiteral("MEDIUM")
                                                                          : QStringLiteral("LOW");
    return QStringLiteral("OK ") + hexText(parsed.artist) + u' ' + hexText(parsed.title) + u' ' + confidence;
}

// ---- kinds L and Y ----
// L description -> OK hex | NULL (LyricsExtractor.extract); Y text -> OK hex | NULL (LyricsExtractor.tidy)
QString okOrNull(const std::optional<QString>& value) {
    return value ? okHex(*value) : QStringLiteral("NULL");
}

QString answerL(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    return okOrNull(lyrics::extract(unhex(fields.at(0))));
}

QString answerY(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    return okOrNull(lyrics::tidy(unhex(fields.at(0))));
}

// ---- kinds R and N ----
// R text -> OK hex (LogRedaction.redactPaths); N text name1 [name2 ...] -> OK hex (LogRedaction.redact)
QString answerR(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    return okHex(redaction::redactPaths(textOf(fields.at(0))));
}

QString answerN(const QStringList& fields) {
    if (fields.size() < 2) return QStringLiteral("?");
    QStringList names;
    for (qsizetype i = 1; i < fields.size(); ++i) names.append(textOf(fields.at(i)));
    return okHex(redaction::redact(textOf(fields.at(0)), names));
}

// ---- kinds H and O ----
// H text -> OK hex (NFKC); O text -> NULL | the 16 hex digits of the double's bits (String.toDoubleOrNull)
QString answerH(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    return okHex(kt::nfkc(textOf(fields.at(0))));
}

QString answerO(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    const std::optional<double> value = kt::toDoubleOrNull(textOf(fields.at(0)));
    return value ? kt::hexBits(*value) : QStringLiteral("NULL");
}

}  // namespace

void registerAnswer(char kind, Answerer answer) {
    answerers().insert(kind, answer);
}

void registerConstant(const QString& name, const QString& value) {
    constantTable().insert(name, value);
}

// Each kind's answerer is registered here.
void registerAnswers() {
    registerAnswer('S', answerS);
    registerAnswer('C', answerC);
    registerAnswer('F', answerF);
    registerAnswer('D', answerD);
    registerAnswer('G', answerG);
    registerAnswer('T', answerT);
    registerAnswer('L', answerL);
    registerAnswer('Y', answerY);
    registerAnswer('R', answerR);
    registerAnswer('N', answerN);
    registerAnswer('H', answerH);
    registerAnswer('O', answerO);
}

// Each user-facing constant is registered here, under the names Oracle.java prints (all 11 of the Kotlin side).
void registerConstants() {
    for (const RejectReason reason : {RejectReason::Empty, RejectReason::UnsupportedHost, RejectReason::MixPlaylist, RejectReason::Unrecognized})
        registerConstant(QStringLiteral("RejectReason.") + rejectName(reason), rejectMessage(reason));
    registerConstant(QStringLiteral("FilenameFormatter.UNTITLED_PLAYLIST"), filename::untitledPlaylist());
    registerConstant(QStringLiteral("FilenameFormatter.format(1,a,)"), filename::format(1, QStringLiteral("a"), QString(), true));
    registerConstant(QStringLiteral("FilenameFormatter.folderName(null)"), filename::folderName(std::nullopt));
    registerConstant(QStringLiteral("FilenameFormatter.format(1,a,b)"), filename::format(1, QStringLiteral("a"), QStringLiteral("b"), true));
    registerConstant(QStringLiteral("TitleParser.UNKNOWN_ARTIST"), unknownArtist());
    registerConstant(QStringLiteral("LogRedaction.PATH_PLACEHOLDER"), redaction::pathPlaceholder());
    registerConstant(QStringLiteral("LogRedaction.FILE_NAME_PLACEHOLDER"), redaction::fileNamePlaceholder());
}

namespace {

void writeLines(const std::string& out) {
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
}

std::string hex4(char16_t unit) {
    static constexpr char digits[] = "0123456789abcdef";
    return {digits[(unit >> 12) & 15], digits[(unit >> 8) & 15], digits[(unit >> 4) & 15], digits[unit & 15]};
}

std::string charRow(bool whitespace, bool letter, bool letterOrDigit, char16_t upper, char16_t lower) {
    std::string row;
    row += whitespace ? '1' : '0';
    row += ' ';
    row += letter ? '1' : '0';
    row += ' ';
    row += letterOrDigit ? '1' : '0';
    row += ' ' + hex4(upper) + ' ' + hex4(lower) + '\n';
    return row;
}

int chartable() {
    std::string out;
    for (int unit = 0; unit <= 0xFFFF; ++unit) {
        const QChar c{char16_t(unit)};
        out += charRow(kt::isWhitespace(c), kt::isLetter(c), kt::isLetterOrDigit(c), kt::uppercaseChar(c).unicode(),
                       kt::lowercaseChar(c).unicode());
    }
    writeLines(out);
    return 0;
}

int qtChartable() {
    std::string out;
    for (int unit = 0; unit <= 0xFFFF; ++unit) {
        const kt::detail::CharFacts facts = kt::detail::qtCharFacts(char16_t(unit));
        out += charRow(facts.whitespace, facts.letter, facts.letterOrDigit, facts.upper, facts.lower);
    }
    writeLines(out);
    return 0;
}

int qtCasetable() {
    std::string out;
    for (int unit = 0; unit <= 0xFFFF; ++unit) {
        const QString one{QChar{char16_t(unit)}};
        out += toAscii(hexText(one.toUpper())) + ' ' + toAscii(hexText(one.toLower())) + '\n';
    }
    writeLines(out);
    return 0;
}

int unicodecase() {
    std::string out;
    out.reserve(std::size_t(0x110000) * 24);
    for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        // (not QString::fromUcs4, which drops U+FEFF at the start as a byte order mark)
        QString one;
        if (cp > 0xFFFF) one.append(QChar(QChar::highSurrogate(cp))).append(QChar(QChar::lowSurrogate(cp)));
        else one.append(QChar(char16_t(cp)));
        char head[16];
        std::snprintf(head, sizeof head, "%x ", unsigned(cp));
        out += head + toAscii(hexText(kt::lowercase(one))) + ' ' + toAscii(hexText(kt::uppercase(one))) + '\n';
    }
    writeLines(out);
    return 0;
}

// The one-code-point string of `cp` (not QString::fromUcs4, which drops U+FEFF at the start as a byte order mark).
QString codePointString(char32_t cp) {
    QString one;
    if (cp > 0xFFFF) one.append(QChar(QChar::highSurrogate(cp))).append(QChar(QChar::lowSurrogate(cp)));
    else one.append(QChar(char16_t(cp)));
    return one;
}

// One line per code point 0..10FFFF but the surrogates: the code point in hex, a space and what `row` writes.
template <typename Row>
int codePointTable(Row row) {
    std::string out;
    out.reserve(std::size_t(0x110000) * 10);
    for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        char head[16];
        std::snprintf(head, sizeof head, "%x ", unsigned(cp));
        out += head;
        row(cp, out);
        out += '\n';
    }
    writeLines(out);
    return 0;
}

int lettertable() {
    return codePointTable([](char32_t cp, std::string& out) { out += kt::isLetterOrDigit(cp) ? '1' : '0'; });
}

int nfkctable() {
    return codePointTable([](char32_t cp, std::string& out) { out += toAscii(hexText(kt::nfkc(codePointString(cp)))); });
}

int qtLettertable() {
    return codePointTable([](char32_t cp, std::string& out) { out += kt::detail::qtLetterOrDigit(cp) ? '1' : '0'; });
}

int constants() {
    std::string out;
    const QMap<QString, QString>& table = constantTable();
    for (auto it = table.begin(); it != table.end(); ++it) out += toAscii(it.key()) + ' ' + toAscii(hexText(it.value())) + '\n';
    writeLines(out);
    return 0;
}

int generate(char kind, long long count, quint32 seed) {
    const Generator gen = generatorFor(kind);
    if (!gen) {
        std::fprintf(stderr, "xgs-diff: no generator for kind %c\n", kind);
        return 2;
    }
    Rng rng{seed};
    std::string out;
    for (long long i = 0; i < count; ++i) out += kind + std::string("\t") + toAscii(gen(rng)) + '\n';
    writeLines(out);
    return 0;
}

int answer() {
    std::string out;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const QStringList fields = QString::fromLatin1(line).split(u'\t');
        const Answerer answerer = fields.at(0).size() == 1 ? answerers().value(char(fields.at(0).at(0).unicode()), nullptr) : nullptr;
        out += (answerer ? toAscii(answerer(fields.mid(1))) : std::string("?")) + '\n';
    }
    writeLines(out);
    return 0;
}

}  // namespace

}  // namespace xgs::diff

int main(int argc, char** argv) {
    using namespace xgs::diff;
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    registerGenerators();
    registerAnswers();
    registerConstants();
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "gen" && argc == 5) {
        const std::string kind = argv[2];
        const long long count = std::stoll(argv[3]);
        const unsigned long long seed = std::stoull(argv[4]);
        if (kind.size() != 1 || count <= 0) {
            std::fprintf(stderr, "xgs-diff: gen <kind letter> <positive count> <seed>\n");
            return 2;
        }
        return generate(kind[0], count, quint32(seed));
    }
    if (mode == "answer") return answer();
    if (mode == "chartable") return chartable();
    if (mode == "constants") return constants();
    if (mode == "unicodecase") return unicodecase();
    if (mode == "qt-chartable") return qtChartable();
    if (mode == "qt-casetable") return qtCasetable();
    if (mode == "lettertable") return lettertable();
    if (mode == "nfkctable") return nfkctable();
    if (mode == "qt-lettertable") return qtLettertable();
    if (mode == "qt-version") {
        std::printf("%s\n", QT_VERSION_STR);
        return 0;
    }
    std::fprintf(stderr, "usage: xgs-diff gen <kind> <count> <seed> | answer | chartable | constants | unicodecase | lettertable | nfkctable | qt-chartable | qt-casetable | qt-lettertable | qt-version\n");
    return 2;
}
