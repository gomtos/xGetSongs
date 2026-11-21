// xgs-diff: the C++ side of the differential test of the Qt port (qt/tools/differential/run.ps1 drives it with Oracle.java,
// the Kotlin side). Input and output are ASCII, one line per case or answer.
//
//   xgs-diff gen <kind> <count> <seed>    write <count> cases of a kind ("K\tfields...")
//   xgs-diff answer                       answer the cases on stdin, one line each
//   xgs-diff arglists <count> <seed>      the random argument lists of tst_system_process_runner, one per line (every
//                                         argument as hex, tab-separated), for `Oracle.java argv`
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
#include <QDir>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <io.h>

#include "argument_lists.h"
#include "codec.h"
#include "filename/filename_formatter.h"
#include "gen.h"
#include "input/input_classifier.h"
#include "job/default_download_service.h"
#include "job/item_downloader.h"
#include "json/json.h"
#include "log/log_redaction.h"
#include "lyrics/lrclib_lyrics_provider.h"
#include "lyrics/lyrics_extractor.h"
#include "lyrics/lyrics_matcher.h"
#include "model/models.h"
#include "output/local_folder_sink.h"
#include "paths/paths.h"
#include "process/line_splitter.h"
#include "process/process_runner.h"
#include "support/fake_http_client.h"
#include "support/fake_lyrics_provider.h"
#include "support/fake_output_sink.h"
#include "support/fake_process_runner.h"
#include "support/job_fakes.h"
#include "support/test_tools.h"
#include "tags/ffmetadata.h"
#include "text/kt_text.h"
#include "title/title_parser.h"
#include "tools/tool_manager.h"
#include "ytdlp/error_classifier.h"
#include "ytdlp/ffmpeg_commands.h"
#include "ytdlp/progress_parser.h"
#include "ytdlp/video_info_file.h"
#include "ytdlp/ytdlp_commands.h"
#include "ytdlp/ytdlp_resolver.h"

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

// ---- kind J ----
// J text -> ERR | the canonical dump (json::parse, json::dump)
QString answerJ(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    const std::optional<json::Value> value = json::parse(textOf(fields.at(0)));
    return value ? json::dump(*value) : QStringLiteral("ERR");
}

// ---- kinds E and Q ----
// E line1 [line2 ...] -> FATAL|UNAVAILABLE|TRANSIENT|OTHER <message hex> (ErrorClassifier.classify; a case without lines has
// one empty field, which is no line)
QString answerE(const QStringList& fields) {
    QStringList lines;
    for (const QString& field : fields)
        if (!field.isEmpty()) lines.append(textOf(field));
    const ytdlp::Failure failure = ytdlp::classifyError(lines);
    QString kind;
    switch (failure.kind) {
    case ytdlp::FailureKind::Fatal: kind = QStringLiteral("FATAL"); break;
    case ytdlp::FailureKind::Unavailable: kind = QStringLiteral("UNAVAILABLE"); break;
    case ytdlp::FailureKind::Transient: kind = QStringLiteral("TRANSIENT"); break;
    case ytdlp::FailureKind::Other: kind = QStringLiteral("OTHER"); break;
    }
    return kind + u' ' + hexText(failure.message);
}

// Q line -> NULL | P - | P <16 hex digits of the percent's bits> (ProgressParser.parse)
QString answerQ(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    const std::optional<ytdlp::DownloadProgress> update = ytdlp::parseProgress(textOf(fields.at(0)));
    if (!update) return QStringLiteral("NULL");
    return update->percent ? QStringLiteral("P ") + kt::hexBits(*update->percent) : QStringLiteral("P -");
}

// ---- kind V ----
// V bytes -> album description duration (VideoInfoFile.read of a file holding the bytes; the bytes as 2 hex digits each,
// "." for none; the strings as hex, "-" when empty; the duration in decimal or "-")
QString answerV(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    const QByteArray bytes = fields.at(0) == u"." ? QByteArray() : QByteArray::fromHex(fields.at(0).toLatin1());
    const ytdlp::VideoInfo info = ytdlp::parseVideoInfo(bytes);
    return hex(info.album) + u' ' + hex(info.description) + u' ' + (info.duration ? QString::number(*info.duration) : QStringLiteral("-"));
}

// ---- kind K ----
// K rp|rv ytDlp ffmpeg jsRuntime url | K dl ytDlp ffmpeg jsRuntime url outputDir videoId | K tag ffmpeg input cover metadataFile output
// -> OK <each argument as hex> | ERR (the exception of std::invalid_argument). A path field is hex, "-" when absent.
QString answerK(const QStringList& fields) {
    if (fields.size() < 1) return QStringLiteral("?");
    const QString& op = fields.at(0);
    const auto path = [&](qsizetype index) { return unhex(fields.at(index)); };
    QStringList command;
    try {
        if (op == u"tag") {
            if (fields.size() < 6) return QStringLiteral("?");
            command = ytdlp::tagCommand(textOf(fields.at(1)), textOf(fields.at(2)), path(3), textOf(fields.at(4)), textOf(fields.at(5)));
        } else if (op == u"rp" || op == u"rv" || op == u"dl") {
            if (fields.size() < (op == u"dl" ? 7 : 5)) return QStringLiteral("?");
            const ToolPaths tools{path(1), path(2), path(3)};
            const QString url = textOf(fields.at(4));
            if (op == u"rp") command = ytdlp::resolvePlaylistCommand(tools, url);
            else if (op == u"rv") command = ytdlp::resolveVideoCommand(tools, url);
            else command = ytdlp::downloadCommand(tools, url, textOf(fields.at(5)), textOf(fields.at(6)));
        } else {
            return QStringLiteral("?");
        }
    } catch (const std::invalid_argument&) {
        return QStringLiteral("ERR");
    }
    QString answer = QStringLiteral("OK");
    for (const QString& argument : command) answer += u' ' + hexText(argument);
    return answer;
}

// ---- kind A ----
// A title artist album albumArtist track comment lyrics -> OK <hex of the ffmetadata text> (Ffmetadata.render; album, comment and
// lyrics as hex, "-" when absent; track in decimal)
QString answerA(const QStringList& fields) {
    if (fields.size() < 7) return QStringLiteral("?");
    bool ok = false;
    const int track = fields.at(4).toInt(&ok);
    if (!ok) return QStringLiteral("?");
    const tags::TrackTags trackTags{textOf(fields.at(0)), textOf(fields.at(1)), unhex(fields.at(2)), textOf(fields.at(3)), track,
                                    unhex(fields.at(5)), unhex(fields.at(6))};
    return okHex(tags::renderFfmetadata(trackTags));
}

// ---- kind I ----
// I tv title | I av artist -> OK <each variant as hex> (LyricsMatcher.titleVariants / artistVariants)
// I norm text -> OK hex (LyricsMatcher.normalize)
// I match <query> <candidate> -> 0 | 1 (LyricsMatcher.isMatch)
// I pick <query> n <candidate>... -> the index of the candidate picked, -1 for none (LyricsMatcher.pick)
// A query is 4 fields: artist title album durationSeconds (strings as hex, album "-" when absent, the duration in decimal or
// "-"). A candidate is 6 fields: trackName artistName albumName duration instrumental plainLyrics (strings as hex or "-", the
// duration as the 16 hex digits of the double's bits or "-", instrumental 0 | 1 | -).
LyricsQuery lyricsQueryOf(const QStringList& fields, qsizetype at, bool& ok) {
    LyricsQuery query{textOf(fields.at(at)), textOf(fields.at(at + 1)), unhex(fields.at(at + 2)), std::nullopt};
    if (fields.at(at + 3) != u"-") {
        bool parsed = false;
        query.durationSeconds = fields.at(at + 3).toInt(&parsed);
        ok = ok && parsed;
    }
    return query;
}

lyrics::LyricsCandidate lyricsCandidateOf(const QStringList& fields, qsizetype at, bool& ok) {
    lyrics::LyricsCandidate candidate{unhex(fields.at(at)), unhex(fields.at(at + 1)), unhex(fields.at(at + 2)), std::nullopt,
                                      std::nullopt, unhex(fields.at(at + 5))};
    if (fields.at(at + 3) != u"-") {
        bool parsed = false;
        candidate.duration = std::bit_cast<double>(quint64(fields.at(at + 3).toULongLong(&parsed, 16)));
        ok = ok && parsed && fields.at(at + 3).size() == 16;
    }
    const QString& instrumental = fields.at(at + 4);
    if (instrumental == u"0" || instrumental == u"1") candidate.instrumental = instrumental == u"1";
    else ok = ok && instrumental == u"-";
    return candidate;
}

QString answerI(const QStringList& fields) {
    if (fields.size() < 2) return QStringLiteral("?");
    const QString& op = fields.at(0);
    if (op == u"tv" || op == u"av") {
        const QString value = textOf(fields.at(1));
        QString answer = QStringLiteral("OK");
        for (const QString& variant : op == u"tv" ? lyrics::titleVariants(value) : lyrics::artistVariants(value))
            answer += u' ' + hexText(variant);
        return answer;
    }
    if (op == u"norm") return okHex(lyrics::normalizeForMatching(textOf(fields.at(1))));
    bool ok = true;
    if (op == u"match") {
        if (fields.size() != 11) return QStringLiteral("?");
        const LyricsQuery query = lyricsQueryOf(fields, 1, ok);
        const lyrics::LyricsCandidate candidate = lyricsCandidateOf(fields, 5, ok);
        return ok ? flag(lyrics::isMatch(query, candidate)) : QStringLiteral("?");
    }
    if (op == u"pick") {
        if (fields.size() < 6) return QStringLiteral("?");
        const LyricsQuery query = lyricsQueryOf(fields, 1, ok);
        qsizetype count = 0;
        if (!numberOf(fields.at(5), count) || fields.size() != 6 + 6 * count) return QStringLiteral("?");
        QList<lyrics::LyricsCandidate> candidates;
        for (qsizetype i = 0; i < count; ++i) candidates.append(lyricsCandidateOf(fields, 6 + 6 * i, ok));
        return ok ? QString::number(lyrics::pick(query, candidates)) : QStringLiteral("?");
    }
    return QStringLiteral("?");
}

// ---- kind W ----
// W pn text -> hex of paths::fromText | ERR
// W pc dir name -> hex of paths::child(fromText(dir), name) | ERR
// W pf path -> hex of paths::fileName(fromText(path)), "-" for none | ERR
// W px path -> hex of paths::nameWithoutExtension(fromText(path)) | ERR
// W ln bytes chunkSize... -> OK <each line as hex> (answerLn)
// W st yt-dlp ffmpeg js / W up yt-dlp -> tools::ToolManager on a runner that plays the case (answerSt, answerUp)
// W fn name -> 0 when output::LocalFolderSink::exists(name) throws std::invalid_argument, else 1 (answerFn)
// W lf query maxRequests timeout step... -> lyrics::LrclibLyricsProvider on a scripted HTTP client (answerLf)
// W jb request items scripts... -> job::DefaultDownloadService on scripted collaborators (answerJb)
// One answerer per op (kWAnswers); the op's fields follow the op.
QString pathOrErr(const std::optional<QString>& path) {
    return path ? hexText(*path) : QStringLiteral("ERR");
}

QString answerPn(const QStringList& fields) {
    return pathOrErr(paths::fromText(textOf(fields.at(0))));
}

QString answerPc(const QStringList& fields) {
    if (fields.size() < 2) return QStringLiteral("?");
    const std::optional<QString> dir = paths::fromText(textOf(fields.at(0)));
    return dir ? pathOrErr(paths::child(*dir, textOf(fields.at(1)))) : QStringLiteral("ERR");
}

QString answerPf(const QStringList& fields) {
    const std::optional<QString> path = paths::fromText(textOf(fields.at(0)));
    return path ? hex(paths::fileName(*path)) : QStringLiteral("ERR");
}

QString answerPx(const QStringList& fields) {
    const std::optional<QString> path = paths::fromText(textOf(fields.at(0)));
    return path ? hexText(paths::nameWithoutExtension(*path)) : QStringLiteral("ERR");
}

// W ln bytes chunkSize... -> OK <each line as hex>: the bytes fed to process::LineSplitter in chunks of the sizes (repeated
// until the bytes end), then finish().
QString answerLn(const QStringList& fields) {
    if (fields.size() < 2) return QStringLiteral("?");
    const QByteArray bytes = fields.at(0) == u"." ? QByteArray() : QByteArray::fromHex(fields.at(0).toLatin1());
    QList<int> sizes;
    for (qsizetype i = 1; i < fields.size(); ++i) {
        bool ok = false;
        const int size = fields.at(i).toInt(&ok);
        if (!ok || size < 1) return QStringLiteral("?");
        sizes.append(size);
    }
    QString answer = QStringLiteral("OK");
    process::LineSplitter splitter([&answer](const QString& line) { answer += u' ' + hexText(line); });
    qsizetype at = 0;
    for (qsizetype chunk = 0; at < bytes.size(); ++chunk) {
        const qsizetype size = std::min<qsizetype>(sizes.at(chunk % sizes.size()), bytes.size() - at);
        splitter.feed(QByteArrayView(bytes).sliced(at, size));
        at += size;
    }
    splitter.finish();
    return answer;
}

// W st / W up: a tool is three fields: its path (hex, "-" when missing), its run (the exit code in decimal, or S: a start
// failure with startFailureText) and its output lines ("~" for none, else comma-separated items: o or e for stdout or stderr,
// then the line as hex), handed to the handlers in that order on the caller's thread. The answer ends in " R" and every
// command the runner was given (its arguments as hex, comma-separated), as Oracle.java's answerTools.
const QString startFailureText = QStringLiteral("Cannot run program (start failure of the differential test)");

// What the runner does for one program: its run (the exit code in decimal, or S) and its output lines in order (each a stdout
// flag and the line).
struct Play {
    QString run;
    QList<std::pair<bool, QString>> lines;

    // kind W's notation of the lines: "~" for none, else comma-separated o<hex> / e<hex>
    static Play of(const QString& run, const QString& items) {
        Play play{run, {}};
        if (items != u"~") {
            for (const QString& item : items.split(u',')) play.lines.append({item.startsWith(u'o'), textOf(item.sliced(1))});
        }
        return play;
    }
};

// A ProcessRunner that plays the case (kinds W and P), as Oracle.java's CaseRunner: the program at command[0] runs as its Play
// says (a program without one exits with 1), its lines going to the handlers in order on the caller's thread. Records every
// command.
class CaseRunner final : public process::ProcessRunner {
public:
    QMap<QString, Play> runs;   // path -> what it does
    QString commands = QStringLiteral(" R");
    bool bad = false;           // a field that is not what the generator writes

    process::RunResult run(const QStringList& command, const process::LineHandler& onStdout, const process::LineHandler& onStderr,
                           std::stop_token) override {
        commands += u' ';
        for (qsizetype k = 0; k < command.size(); ++k) commands += (k > 0 ? QStringLiteral(",") : QString()) + hexText(command.at(k));
        const auto found = runs.constFind(command.first());
        if (found == runs.constEnd()) return {process::RunStatus::Exited, 1};
        if (found->run == u"S") return {process::RunStatus::StartFailed, -1, startFailureText};
        for (const auto& [toStdout, line] : found->lines) (toStdout ? onStdout : onStderr)(line);
        bool ok = false;
        const int exitCode = found->run.toInt(&ok);
        if (!ok) bad = true;
        return {process::RunStatus::Exited, exitCode};
    }
};

// The tool paths of a case (count tools from fields[0]) into runner; false when a path does not come back unchanged from
// paths::fromText (Oracle.java answers BADPATH then).
bool casePaths(const QStringList& fields, int count, CaseRunner& runner, ToolPaths& paths) {
    std::optional<QString>* targets[] = {&paths.ytDlp, &paths.ffmpeg, &paths.jsRuntime};
    for (int t = 0; t < count; ++t) {
        if (fields.at(3 * t) == u"-") continue;
        const QString text = textOf(fields.at(3 * t));
        const std::optional<QString> path = paths::fromText(text);
        if (path != text) return false;
        *targets[t] = path;
        runner.runs.insert(text, Play::of(fields.at(3 * t + 1), fields.at(3 * t + 2)));
    }
    return true;
}

// A ToolPathProvider of fixed paths.
class CaseTools final : public tools::ToolPathProvider {
public:
    explicit CaseTools(ToolPaths paths) : m_paths(std::move(paths)) {}
    ToolPaths current() const override { return m_paths; }

private:
    ToolPaths m_paths;
};

Outcome<QByteArray> unusedFetch(const QString&, std::stop_token) {
    return Outcome<QByteArray>::failed(QStringLiteral("fetch is not used"));
}

QString toolInfoText(const ToolInfo& info) {
    return flag(info.found) + u' ' + hex(info.version) + u' ' + hex(info.path);
}

// W st yt-dlp ffmpeg js (3 fields each) -> found version path of each tool (ToolManager::status)
QString answerSt(const QStringList& fields) {
    if (fields.size() != 9) return QStringLiteral("?");
    CaseRunner runner;
    ToolPaths paths;
    if (!casePaths(fields, 3, runner, paths)) return QStringLiteral("BADPATH");
    const CaseTools caseTools(paths);
    const ToolsStatus status = tools::ToolManager(caseTools, runner, QStringLiteral("C:\\xgs-diff-unused"), unusedFetch).status({});
    if (runner.bad) return QStringLiteral("?");
    return toolInfoText(status.ytDlp) + u' ' + toolInfoText(status.ffmpeg) + u' ' + toolInfoText(status.jsRuntime) + runner.commands;
}

// W up yt-dlp (3 fields) -> OK hex | ERR hex (ToolManager::updateYtDlp)
QString answerUp(const QStringList& fields) {
    if (fields.size() != 3) return QStringLiteral("?");
    CaseRunner runner;
    ToolPaths paths;
    if (!casePaths(fields, 1, runner, paths)) return QStringLiteral("BADPATH");
    const CaseTools caseTools(paths);
    const Outcome<QString> result = tools::ToolManager(caseTools, runner, QStringLiteral("C:\\xgs-diff-unused"), unusedFetch).updateYtDlp({});
    if (runner.bad || result.status == OutcomeStatus::Cancelled) return QStringLiteral("?");
    return (result.isOk() ? QStringLiteral("OK ") + hexText(*result.value) : QStringLiteral("ERR ") + hexText(result.error)) + runner.commands;
}

// W fn name -> 0 | 1: does the output folder accept the file name? As Oracle.java, exists() on a sink two levels inside a
// temporary folder (Windows resolves some accepted names, such as `...` or `.. `, to the folder or to its parent; exists only
// reads attributes). The folder is created at the first case and removed when the program ends.
QString answerFn(const QStringList& fields) {
    static QTemporaryDir temp;
    static std::optional<output::LocalFolderSink> sink;
    if (!sink) {
        if (!temp.isValid()) return QStringLiteral("?");
        const std::optional<QString> root = paths::fromText(temp.path().replace(u'/', u'\\'));
        if (!root) return QStringLiteral("?");
        sink.emplace(*paths::child(*paths::child(*root, QStringLiteral("x")), QStringLiteral("sink")));
    }
    try {
        sink->exists(textOf(fields.at(0)));
        return QStringLiteral("1");
    } catch (const std::invalid_argument&) {
        return QStringLiteral("0");
    }
}

// W lf artist title album durationSeconds maxRequests timeout step... -> OK <lyrics hex> | NULL, N <requests of the second
// call>, C <requests of one call with maxRequests + 1 on a fresh client and provider>, U <every URL of the first call as hex>:
// lyrics::LrclibLyricsProvider on xgs::testing::FakeHttpClient, as Oracle.java's
// answerLookUp (which documents the notation). The base URL is http://h/api, the clock never moves, the cool-down is the
// default. A step: <code>:<bytes> (an answer), <code>:L (an answer whose body is over the limit: bodyTooLarge, no bytes),
// C / T / U (Unreachable: connection refused, timeout, unknown host), I (Failed: another IO error). Once the steps run out,
// every request is answered 404 with no body. Nothing is sent anywhere.
QString answerLf(const QStringList& fields) {
    if (fields.size() < 6) return QStringLiteral("?");
    bool ok = true;
    const LyricsQuery query = lyricsQueryOf(fields, 0, ok);
    bool parsed = false;
    lyrics::LrclibOptions options;
    options.baseUrl = QStringLiteral("http://h/api");
    options.maxRequests = fields.at(4).toInt(&parsed);
    ok = ok && parsed;
    const QString& timeout = fields.at(5);
    if (timeout == u"D") options.requestTimeout = std::chrono::milliseconds(15000);
    else if (timeout == u"0") options.requestTimeout = std::chrono::milliseconds(0);
    else if (timeout == u"N") options.requestTimeout = std::chrono::milliseconds(-1000);
    else ok = false;
    options.clock = [] { return std::chrono::nanoseconds(0); };
    if (!ok) return QStringLiteral("?");

    const QStringList steps = fields.mid(6);
    std::mutex mutex;
    bool bad = false;
    // what the client answers for one step
    const auto answerStep = [&](const QString& step) {
        net::HttpResponse answer;
        if (step == u"C" || step == u"T" || step == u"U") {
            answer.status = net::HttpStatus::Unreachable;
            return answer;
        }
        if (step == u"I") {
            answer.status = net::HttpStatus::Failed;
            return answer;
        }
        const qsizetype colon = step.indexOf(u':');
        bool codeOk = false;
        const int code = colon < 0 ? 0 : step.left(colon).toInt(&codeOk);
        if (!codeOk) {
            std::lock_guard lock(mutex);
            bad = true;
            return answer;
        }
        const QString body = step.mid(colon + 1);
        if (body == u"L") {
            answer = testing::FakeHttpClient::response(code);
            answer.bodyTooLarge = true;
            return answer;
        }
        return testing::FakeHttpClient::response(code, body == u"." ? QByteArray() : QByteArray::fromHex(body.toLatin1()));
    };
    // a script that plays the steps from the start, with its own position
    const auto script = [&](qsizetype& next) {
        return [&](const net::HttpRequest&, std::stop_token) {
            QString step = QStringLiteral("404:.");
            {
                std::lock_guard lock(mutex);
                if (next < steps.size()) step = steps.at(next++);
            }
            return answerStep(step);
        };
    };
    qsizetype next = 0;
    testing::FakeHttpClient client(script(next));
    lyrics::LrclibLyricsProvider provider(client, options);
    const std::optional<QString> found = provider.find(query, {});
    const QStringList firstUrls = client.urls();
    provider.find(query, {});
    const qsizetype second = client.urls().size() - firstUrls.size();
    // the same lookup on a fresh client and provider whose cap is one higher (C)
    qsizetype widerNext = 0;
    testing::FakeHttpClient wider(script(widerNext));
    lyrics::LrclibOptions widerOptions = options;
    // (the generator never writes INT_MAX; clamped there, as Oracle.java does, so a hand-written case cannot overflow)
    widerOptions.maxRequests = options.maxRequests == std::numeric_limits<int>::max() ? options.maxRequests
                                                                                     : options.maxRequests + 1;
    lyrics::LrclibLyricsProvider(wider, widerOptions).find(query, {});
    if (bad) return QStringLiteral("?");
    QString out = found ? QStringLiteral("OK ") + hexText(*found) : QStringLiteral("NULL");
    out += QStringLiteral(" N ") + QString::number(second) + QStringLiteral(" C ") + QString::number(wider.urls().size()) +
           QStringLiteral(" U");
    for (const QString& url : firstUrls) out += u' ' + hexText(url);
    return out;
}

// W jb overwrite includeRank album albumOverride searchLyrics existing n item... meta ytdlp ffmpeg lyrics put -> E <event>...
// M <ffmetadata hex>... Q <query>... Y <rank>... G <rank>... P <rank/fileName>... R <count>: job::DefaultDownloadService
// (job::ItemDownloader on a testsupport::FakeProcessRunner, testsupport::FixedTools, testsupport::FakeMetadata and
// testing::FakeLyricsProvider; retry delays 0 and 0; the temporary folder a QTemporaryDir) runs one request (concurrency 1) on
// a testing::FakeOutputSink, and every event is read with nextEvent. Oracle.java's answerJob documents the notation; the
// fakes do what its fakes do: the same script rule (call order, then the last entry again), the same files, the same lines in
// the same order. The put results stand for Kotlin's exceptions by decision 5 of the Q2d plan: S Stored, A<text>
// AlreadyExists with that error text, F<code>/<text> Failed with that code and text.
class JbScript {
public:
    explicit JbScript(const QString& field) : m_entries(field.split(u',')) {}
    QString take() {
        const std::lock_guard lock(m_mutex);
        const QString entry = m_entries.at(std::min<qsizetype>(m_next, m_entries.size() - 1));
        ++m_next;
        return entry;
    }

private:
    std::mutex m_mutex;
    QStringList m_entries;
    qsizetype m_next = 0;
};

QStringList jbLines(const QString& field) {
    QStringList lines;
    if (field != u"~") {
        for (const QString& line : field.split(u'+')) lines.append(textOf(line));
    }
    return lines;
}

// the rank folder of a file in the job folder (<job>\<rank>\<file>)
QString jbRankOf(const QString& file) {
    const QString dir = file.left(file.lastIndexOf(u'\\'));
    return dir.mid(dir.lastIndexOf(u'\\') + 1);
}

QString jbPercent(const std::optional<double>& percent) {
    return percent ? kt::hexBits(*percent) : QStringLiteral("-");
}

QString jbEvent(const JobEvent& event) {
    static const QString outcomes[] = {QStringLiteral("DESCRIPTION"), QStringLiteral("ONLINE"), QStringLiteral("NOT_FOUND"),
                                       QStringLiteral("SEARCH_OFF")};
    static const QString statuses[] = {QStringLiteral("COMPLETED"), QStringLiteral("CANCELLED"), QStringLiteral("FAILED")};
    const QString comma = QStringLiteral(",");
    if (const auto* e = std::get_if<ItemStarted>(&event))
        return QStringLiteral("S,") + QString::number(e->rank) + comma + hexText(e->videoId) + comma + hexText(e->fileName);
    if (const auto* e = std::get_if<Progress>(&event))
        return QStringLiteral("P,") + QString::number(e->rank) + comma + (e->stage == Stage::Downloading ? QStringLiteral("D") : QStringLiteral("F")) + comma + jbPercent(e->percent);
    if (const auto* e = std::get_if<ItemDone>(&event))
        return QStringLiteral("D,") + QString::number(e->rank) + comma + hexText(e->fileName) + comma +
               (e->lyrics ? outcomes[static_cast<int>(*e->lyrics)] : QStringLiteral("-"));
    if (const auto* e = std::get_if<ItemSkipped>(&event)) return QStringLiteral("K,") + QString::number(e->rank) + comma + hexText(e->reason);
    if (const auto* e = std::get_if<ItemFailed>(&event)) return QStringLiteral("F,") + QString::number(e->rank) + comma + hexText(e->message);
    const JobDone& done = std::get<JobDone>(event);
    return QStringLiteral("J,") + statuses[static_cast<int>(done.status)] + comma + QString::number(done.summary.succeeded) + comma +
           QString::number(done.summary.skipped) + comma + QString::number(done.summary.failed);
}

QString answerJb(const QStringList& fields) {
    if (fields.size() < 7) return QStringLiteral("?");
    bool ok = false;
    const int n = fields.at(6).toInt(&ok);
    if (!ok || n < 1 || fields.size() != 7 + 7 * n + 5) return QStringLiteral("?");
    job::DownloadRequest request;
    request.overwrite = fields.at(0) == u"1";
    request.includeRank = fields.at(1) == u"1";
    request.album = unhex(fields.at(2));
    request.albumOverride = unhex(fields.at(3));
    request.searchLyricsOnline = fields.at(4) == u"1";
    request.concurrency = 1;
    for (int i = 0; i < n; ++i) {
        const qsizetype at = 7 + 7 * i;
        ResolvedItem item;
        item.rank = fields.at(at).toInt();
        item.videoId = textOf(fields.at(at + 1));
        item.title = textOf(fields.at(at + 2));
        item.channel = unhex(fields.at(at + 3));
        item.artist = textOf(fields.at(at + 4));
        item.track = textOf(fields.at(at + 5));
        item.lowConfidence = fields.at(at + 6) == u"1";
        request.items.append(item);
    }
    const qsizetype scripts = 7 + 7 * n;
    JbScript meta(fields.at(scripts));
    JbScript ytDlp(fields.at(scripts + 1));
    JbScript ffmpeg(fields.at(scripts + 2));
    JbScript lyricsScript(fields.at(scripts + 3));
    JbScript putScript(fields.at(scripts + 4));

    std::mutex mutex;   // guards what the fakes record and the sink's names
    QStringList ytDlpRanks, ffmpegRanks, ffmetadata, queries, puts;
    QSet<QString> names;
    if (fields.at(5) != u"~") {
        for (const QString& name : fields.at(5).split(u',')) names.insert(textOf(name));
    }

    testsupport::FakeProcessRunner runner([&](const QStringList& command, const process::LineHandler& out,
                                              const process::LineHandler& err) {
        if (testsupport::isFfmpegCommand(command)) {
            const QString text = testsupport::ffmetadataTextOf(command);
            {
                const std::lock_guard lock(mutex);
                ffmpegRanks.append(jbRankOf(command.last()));
                ffmetadata.append(hexText(text));
            }
            const QStringList entry = ffmpeg.take().split(u'/');
            if (entry.at(1) == u"1") testsupport::writeFakeTagged(command);
            for (const QString& line : jbLines(entry.at(2))) err(line);
            return entry.at(0).toInt();
        }
        const QString dir = testsupport::outputDirOf(command);
        {
            const std::lock_guard lock(mutex);
            ytDlpRanks.append(dir.mid(dir.lastIndexOf(u'\\') + 1));
        }
        const QStringList entry = ytDlp.take().split(u'/');
        if (entry.at(1) == u"1") testsupport::writeFakeM4a(command);
        if (entry.at(2) == u"1") testsupport::writeFakeCover(command);
        if (entry.at(3) != u"-")
            testsupport::writeFakeInfo(command, entry.at(3) == u"." ? QByteArray() : QByteArray::fromHex(entry.at(3).toLatin1()));
        for (const QString& line : jbLines(entry.at(4))) out(line);
        for (const QString& line : jbLines(entry.at(5))) err(line);
        return entry.at(0).toInt();
    });
    const testsupport::FixedTools tools;
    testsupport::FakeMetadata metadata([&](const QString&, std::stop_token) -> std::optional<ytdlp::VideoMeta> {
        const QString entry = meta.take();
        if (entry == u"-") return std::nullopt;
        const QStringList f = entry.split(u'/');
        return ytdlp::VideoMeta{unhex(f.at(0)), unhex(f.at(1)), unhex(f.at(2)), unhex(f.at(3))};
    });
    testing::FakeLyricsProvider lyrics([&](const LyricsQuery& query) -> std::optional<QString> {
        {
            const std::lock_guard lock(mutex);
            queries.append(hex(query.artist) + u'/' + hex(query.title) + u'/' + hex(query.album) + u'/' +
                           (query.durationSeconds ? QString::number(*query.durationSeconds) : QStringLiteral("-")));
        }
        const QString entry = lyricsScript.take();
        if (entry == u"X") throw std::runtime_error("the lookup failed (differential test)");
        return unhex(entry);
    });
    testing::FakeOutputSink sink(
        [&](const QString& fileName) {
            const std::lock_guard lock(mutex);
            return names.contains(fileName);
        },
        [&](const QString& fileName, const QString& source, bool) {
            {
                const std::lock_guard lock(mutex);
                puts.append(jbRankOf(source) + u'/' + hexText(fileName));
            }
            const QString entry = putScript.take();
            if (entry == u"S") {
                const std::lock_guard lock(mutex);
                names.insert(fileName);
                return output::PutResult{output::PutStatus::Stored, {}, 0};
            }
            if (entry.startsWith(u'A')) return output::PutResult{output::PutStatus::AlreadyExists, textOf(entry.mid(1)), 0};
            const qsizetype slash = entry.indexOf(u'/');
            return output::PutResult{output::PutStatus::Failed, textOf(entry.mid(slash + 1)),
                                     static_cast<quint32>(entry.mid(1, slash - 1).toUInt())};
        });
    request.sink = &sink;

    QTemporaryDir temp;
    if (!temp.isValid()) return QStringLiteral("?");
    const std::optional<QString> tempRoot = paths::fromText(QDir::toNativeSeparators(temp.path()));
    if (!tempRoot) return QStringLiteral("?");
    job::ItemDownloader downloader(runner, tools, metadata, lyrics);
    job::DefaultDownloadService service(downloader, *tempRoot, {std::chrono::milliseconds(0), std::chrono::milliseconds(0)});
    QString out = QStringLiteral("E");
    {
        const std::unique_ptr<job::JobHandle> handle = service.start(std::move(request));
        while (const std::optional<JobEvent> event = handle->nextEvent()) out += u' ' + jbEvent(*event);
        handle->join();
    }
    const qsizetype remaining =
        QDir(temp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).size();
    out += QStringLiteral(" M");
    for (const QString& text : ffmetadata) out += u' ' + text;
    out += QStringLiteral(" Q");
    for (const QString& query : queries) out += u' ' + query;
    out += QStringLiteral(" Y");
    for (const QString& rank : ytDlpRanks) out += u' ' + rank;
    out += QStringLiteral(" G");
    for (const QString& rank : ffmpegRanks) out += u' ' + rank;
    out += QStringLiteral(" P");
    for (const QString& put : puts) out += u' ' + put;
    return out + QStringLiteral(" R ") + QString::number(remaining);
}

struct WAnswer {
    QStringView op;
    Answerer answer;   // gets the fields after the op (at least one)
};
constexpr WAnswer kWAnswers[] = {{u"pn", answerPn}, {u"pc", answerPc}, {u"pf", answerPf}, {u"px", answerPx}, {u"ln", answerLn},
                                {u"st", answerSt}, {u"up", answerUp}, {u"fn", answerFn}, {u"lf", answerLf}, {u"jb", answerJb}};

QString answerW(const QStringList& fields) {
    if (fields.size() < 2) return QStringLiteral("?");
    for (const WAnswer& entry : kWAnswers)
        if (fields.at(0) == entry.op) return entry.answer(fields.mid(1));
    return QStringLiteral("?");
}

// ---- kind P: ytdlp::YtDlpResolver on a CaseRunner, as Oracle.java's answerP ----
// The fields after the op: the input (rs) or the video id (fx) as hex; the run (the exit code, S: a start failure with
// startFailureText, -: no yt-dlp path); the stdout bytes (2 hex digits each, "." for none), split into lines by
// process::LineSplitter as SystemProcessRunner does; the stderr lines, one per field (an empty field is no line). Stdout goes
// to the handler before stderr. The tools are C:\tools\yt-dlp.exe, C:\tools\ffmpeg.exe and C:\tools\node.exe. Every answer
// ends in " R" and the commands the runner was given.
const QString pYtDlp = QStringLiteral("C:\\tools\\yt-dlp.exe");

struct PCase {
    CaseRunner runner;
    std::optional<CaseTools> tools;
    QString argument;
};

// Sets up the runner and the tools of a P case; false when the fields are not what the generator writes.
bool pCase(const QStringList& fields, PCase& out) {
    if (fields.size() < 4) return false;
    const QString& run = fields.at(1);
    out.argument = textOf(fields.at(0));
    out.tools.emplace(ToolPaths{run == u"-" ? std::nullopt : std::optional<QString>(pYtDlp), QStringLiteral("C:\\tools\\ffmpeg.exe"),
                                QStringLiteral("C:\\tools\\node.exe")});
    if (run == u"-") return true;
    Play play{run, {}};
    const QByteArray bytes = fields.at(2) == u"." ? QByteArray() : QByteArray::fromHex(fields.at(2).toLatin1());
    process::LineSplitter splitter([&play](const QString& line) { play.lines.append({true, line}); });
    splitter.feed(QByteArrayView(bytes));
    splitter.finish();
    for (qsizetype k = 3; k < fields.size(); ++k)
        if (!fields.at(k).isEmpty()) play.lines.append({false, textOf(fields.at(k))});
    out.runner.runs.insert(pYtDlp, std::move(play));
    return true;
}

// P rs input run stdout stderr... -> OK resolveId kind playlistTitle truncated alsoVideoId count, then per item rank videoId
// title channel artist track lowConfidence available unavailableReason expectedFileName | ERR hex
QString answerRs(const QStringList& fields) {
    PCase c;
    if (!pCase(fields, c)) return QStringLiteral("?");
    const Outcome<ResolveResult> outcome = ytdlp::YtDlpResolver(c.runner, *c.tools).resolve(c.argument, {});
    if (c.runner.bad || outcome.status == OutcomeStatus::Cancelled) return QStringLiteral("?");
    if (!outcome.isOk()) return QStringLiteral("ERR ") + hexText(outcome.error) + c.runner.commands;
    const ResolveResult& result = *outcome.value;
    QString out = QStringLiteral("OK ") + hexText(result.resolveId) + u' ' +
                  (result.kind == InputKind::Playlist ? QStringLiteral("PLAYLIST") : QStringLiteral("VIDEO")) + u' ' +
                  hex(result.playlistTitle) + u' ' + flag(result.truncated) + u' ' + hex(result.alsoVideoId) + u' ' +
                  QString::number(result.items.size());
    for (const ResolvedItem& item : result.items) {
        out += u' ' + QString::number(item.rank) + u' ' + hexText(item.videoId) + u' ' + hexText(item.title) + u' ' + hex(item.channel) +
               u' ' + hexText(item.artist) + u' ' + hexText(item.track) + u' ' + flag(item.lowConfidence) + u' ' + flag(item.available) +
               u' ' + hex(item.unavailableReason) + u' ' + hex(item.expectedFileName);
    }
    return out + c.runner.commands;
}

// P fx videoId run stdout stderr... -> M title channel artist track | NULL
QString answerFx(const QStringList& fields) {
    PCase c;
    if (!pCase(fields, c)) return QStringLiteral("?");
    const std::optional<ytdlp::VideoMeta> meta = ytdlp::YtDlpResolver(c.runner, *c.tools).fetch(c.argument, {});
    if (c.runner.bad) return QStringLiteral("?");
    const QString answer = meta ? QStringLiteral("M ") + hex(meta->title) + u' ' + hex(meta->channel) + u' ' + hex(meta->artist) + u' ' +
                                      hex(meta->track)
                                : QStringLiteral("NULL");
    return answer + c.runner.commands;
}

constexpr WAnswer kPAnswers[] = {{u"rs", answerRs}, {u"fx", answerFx}};

QString answerP(const QStringList& fields) {
    if (fields.size() < 2) return QStringLiteral("?");
    for (const WAnswer& entry : kPAnswers)
        if (fields.at(0) == entry.op) return entry.answer(fields.mid(1));
    return QStringLiteral("?");
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
    registerAnswer('J', answerJ);
    registerAnswer('E', answerE);
    registerAnswer('Q', answerQ);
    registerAnswer('V', answerV);
    registerAnswer('K', answerK);
    registerAnswer('A', answerA);
    registerAnswer('I', answerI);
    registerAnswer('W', answerW);
    registerAnswer('P', answerP);
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

// The argument lists of tst_system_process_runner's argumentsReachTheChildVerbatim (the same count and seed give the same
// lists), one per line: every argument as hex, tab-separated. For the oracle's look at ProcessBuilder (Oracle.java argv).
int argumentLists(int count, unsigned seed) {
    std::string out;
    for (const QStringList& list : xgs::testing::argumentLists(count, seed)) {
        QStringList fields;
        for (const QString& argument : list) fields.append(hexText(argument));
        out += toAscii(fields.join(u'\t')) + '\n';
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
    if (mode == "arglists" && argc == 4) return argumentLists(std::stoi(argv[2]), unsigned(std::stoul(argv[3])));
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
    std::fprintf(stderr, "usage: xgs-diff gen <kind> <count> <seed> | answer | arglists <count> <seed> | chartable | constants | unicodecase | lettertable | nfkctable | qt-chartable | qt-casetable | qt-lettertable | qt-version\n");
    return 2;
}
