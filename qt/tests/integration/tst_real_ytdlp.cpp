#include <QtTest>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <windows.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>

#include "ffprobe.h"
#include "fs/file_ops.h"
#include "input/input_classifier.h"
#include "integration_support.h"
#include "json/json.h"
#include "lyrics/lyrics_extractor.h"
#include "paths/paths.h"
#include "process/system_process_runner.h"
#include "text/kt_text.h"
#include "tools/tool_locator.h"
#include "ytdlp/video_info_file.h"
#include "ytdlp/ytdlp_commands.h"
#include "ytdlp/ytdlp_resolver.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::integration::Deadline;
using xgs::integration::Ffprobe;
using xgs::process::RunResult;
using xgs::process::RunStatus;

namespace {

QString shown(const std::optional<QString>& value) { return value ? u'"' + *value + u'"' : u"null"_s; }

/// The names in a directory, sorted (Kotlin's Files.list(dir) for a message).
QStringList namesIn(const QString& dir) {
    QStringList names = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Unsorted);
    std::sort(names.begin(), names.end());
    return names;
}

/// The root object of a JSON file, or nothing.
std::optional<xgs::json::Object> jsonObjectOf(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const std::optional<xgs::json::Value> root = xgs::json::parse(QString::fromUtf8(file.readAll()));
    if (!root) return std::nullopt;
    const auto* object = std::get_if<xgs::json::Object>(&root->data);
    if (!object) return std::nullopt;
    return *object;
}

/// Kotlin's `root.getValue(key).jsonPrimitive.content` (nothing when the key is absent or not a primitive).
std::optional<QString> contentOf(const xgs::json::Object& object, QStringView key) {
    const xgs::json::Value* value = xgs::json::find(object, key);
    if (!value) return std::nullopt;
    const auto* primitive = std::get_if<xgs::json::Primitive>(&value->data);
    if (!primitive) return std::nullopt;
    return primitive->content;
}

/// Kotlin's `root[key]` in a message: the JSON element's text, or null.
QString elementText(const xgs::json::Object& object, QStringView key) {
    const xgs::json::Value* value = xgs::json::find(object, key);
    if (!value) return u"null"_s;
    if (const auto* primitive = std::get_if<xgs::json::Primitive>(&value->data)) {
        return primitive->isString ? u'"' + primitive->content + u'"' : primitive->content;
    }
    if (std::holds_alternative<xgs::json::Null>(value->data)) return u"null"_s;
    return u"<container>"_s;
}

/// Counts the kinds of lines in description: enough to describe its structure without quoting any of it.
QString structureOf(const QString& description) {
    const QStringList lines = QString(description).replace(u"\r\n"_s, u"\n"_s).split(u'\n');
    auto count = [&](auto predicate) { return QString::number(std::count_if(lines.cbegin(), lines.cend(), predicate)); };
    const QString separators = count([](const QString& line) {
        const QString t = xgs::kt::trim(line);
        return t.size() >= 3 && std::all_of(t.cbegin(), t.cend(), [&](QChar c) { return c == t[0]; }) &&
               u"=-_*~#.+"_s.contains(t[0]);
    });
    const QString links = count([](const QString& line) {
        return line.contains(u"http://"_s) || line.contains(u"https://"_s) || line.contains(u"www."_s);
    });
    const QString copyright = count([](const QString& line) {
        const QString t = xgs::kt::trim(line);
        return t.startsWith(u"©"_s) || xgs::kt::startsWith(t, u"copyright", xgs::kt::CaseMode::Ignore);
    });
    const QString hashtags = count([](const QString& line) {
        const QString t = xgs::kt::trim(line);
        return t.startsWith(u'#') && t.size() > 1 && !xgs::kt::isWhitespace(t[1]);
    });
    const QString bracketed = count([](const QString& line) {
        const QString t = xgs::kt::trim(line);
        return t.size() >= 2 && ((t.front() == u'[' && t.back() == u']') || (t.front() == u'【' && t.back() == u'】'));
    });
    return u"lines=%1, blank=%2, separator lines=%3, link lines=%4, copyright lines=%5, hashtag lines=%6, bracketed headings=%7"_s
        .arg(QString::number(lines.size()), count([](const QString& line) { return xgs::kt::isBlank(line); }), separators,
             links, copyright, hashtags, bracketed);
}

}  // namespace

// Port of RealYtDlpIntegrationTest.kt. Talks to the real YouTube through the real yt-dlp and ffmpeg. Skipped unless
// XGS_INTEGRATION=1 (the Kotlin class is tagged "integration" and runs only through integrationTest), and, like Kotlin's
// assumeTrue, when a tool is missing. Each Kotlin `withTimeout` is a Deadline whose token goes to every run.
//
// Not in Kotlin, to keep everything yt-dlp writes inside one QTemporaryDir: for the whole class the environment variables
// XDG_CACHE_HOME (yt-dlp's cache, by default %USERPROFILE%\.cache\yt-dlp), TEMP and TMP (where the one-file yt-dlp.exe unpacks
// itself) point into that directory, and every test's own temporary directory is made under it as well. The values the test
// process had are put back at the end.
class RealYtDlpIntegrationTest : public QObject {
    Q_OBJECT

    xgs::process::SystemProcessRunner m_runner;
    std::unique_ptr<xgs::tools::ToolLocator> m_locator;
    std::unique_ptr<QTemporaryDir> m_sandbox;
    struct SavedVariable {
        const wchar_t* name;
        std::optional<std::wstring> value;
    };
    std::vector<SavedVariable> m_saved;

    void setVariable(const wchar_t* name, const QString& value) {
        const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
        std::optional<std::wstring> old;
        if (length > 0) {
            std::wstring buffer(length, L'\0');
            buffer.resize(GetEnvironmentVariableW(name, buffer.data(), length));
            old = buffer;
        }
        m_saved.push_back({name, old});
        QVERIFY(SetEnvironmentVariableW(name, reinterpret_cast<const wchar_t*>(value.utf16())));
    }

    /// A new temporary directory inside the sandbox (Kotlin's Files.createTempDirectory(prefix)).
    std::unique_ptr<QTemporaryDir> tempDir(const QString& prefix) {
        auto dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + u'/' + prefix + u"-XXXXXX"_s);
        return dir->isValid() ? std::move(dir) : nullptr;
    }

    /// Runs the real downloadCommand for videoId into dir, with extraOptions put in front of the `--` separator, and prints
    /// what yt-dlp left in dir. commandDir is what downloadCommand gets as its output directory (dir unless a test escapes it).
    void runDownload(const QString& videoId, const QString& dir, const QStringList& extraOptions, std::stop_token stop,
                     const std::optional<QString>& commandDir = std::nullopt) {
        const xgs::ParsedInput input{xgs::ParsedInput::Kind::Video, videoId, std::nullopt};
        const QStringList base =
            xgs::ytdlp::downloadCommand(m_locator->current(), input.canonicalUrl(), commandDir.value_or(dir), videoId);
        const qsizetype separator = base.lastIndexOf(u"--"_s);
        const QStringList command = base.first(separator) + extraOptions + base.sliced(separator);
        QStringList output;
        std::mutex mutex;
        auto collect = [&](const QString& line) { std::lock_guard lock(mutex); output << line; };
        const RunResult result = m_runner.run(command, collect, collect, stop);
        QVERIFY2(result.status == RunStatus::Exited && result.exitCode == 0,
                 qPrintable(u"yt-dlp failed (%1, exit %2 %3): [%4]"_s.arg(QString::number(int(result.status)),
                                                                         QString::number(result.exitCode), result.error,
                                                                         output.join(u", "_s))));
        QStringList left;
        for (const QString& name : namesIn(dir)) {
            left << u"%1 (%2 bytes)"_s.arg(name, QString::number(QFileInfo(*xgs::paths::child(dir, name)).size()));
        }
        qInfo().noquote() << u"yt-dlp left in the work folder: ["_s + left.join(u", "_s) + u']';
    }

private slots:
    void initTestCase() {
        m_locator = std::make_unique<xgs::tools::ToolLocator>(xgs::integration::appBinDir());
        if (!xgs::integration::enabled()) return;   // every test skips in init(); nothing is set up
        m_sandbox = std::make_unique<QTemporaryDir>(QDir::tempPath() + u"/xgs-ytdlp-integration-XXXXXX"_s);
        QVERIFY(m_sandbox->isValid());
        const QString root = xgs::integration::windowsPath(m_sandbox->path());
        const QString cache = *xgs::paths::child(root, u"cache"_s);
        const QString temp = *xgs::paths::child(root, u"tmp"_s);
        QVERIFY(xgs::fs::makeDirectories(cache) && xgs::fs::makeDirectories(temp));
        XGS_CHECKED(setVariable(L"XDG_CACHE_HOME", cache));
        XGS_CHECKED(setVariable(L"TEMP", temp));
        XGS_CHECKED(setVariable(L"TMP", temp));
    }

    void cleanupTestCase() {
        if (m_sandbox) {
            const QString root = xgs::integration::windowsPath(m_sandbox->path());
            qInfo().noquote() << u"the sandbox held: cache ["_s + namesIn(*xgs::paths::child(root, u"cache"_s)).join(u", "_s) +
                                     u"], tmp ["_s + namesIn(*xgs::paths::child(root, u"tmp"_s)).join(u", "_s) + u']';
        }
        for (auto it = m_saved.rbegin(); it != m_saved.rend(); ++it) {
            SetEnvironmentVariableW(it->name, it->value ? it->value->c_str() : nullptr);
        }
        m_saved.clear();
        m_sandbox.reset();
    }

    // Kotlin's @BeforeTest requireTools: a QSKIP here skips the test function.
    void init() {
        if (!xgs::integration::enabled()) QSKIP(xgs::integration::disabledText());
        const xgs::ToolPaths tools = m_locator->current();
        if (!(tools.ytDlp && tools.ffmpeg && tools.jsRuntime)) QSKIP("yt-dlp, ffmpeg and Node 22+/Deno 2.3+ must be installed");
    }

    void resolvesTheReferencePlaylistWithoutDownloadingAnything() {
        const Deadline deadline(120s);
        xgs::ytdlp::YtDlpResolver resolver(m_runner, *m_locator);
        const xgs::Outcome<xgs::ResolveResult> outcome =
            resolver.resolve(u"https://www.youtube.com/playlist?list=PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"_s, deadline.token());
        QVERIFY2(outcome.isOk(), qPrintable(outcome.status == xgs::OutcomeStatus::Cancelled ? u"cancelled (timeout)"_s
                                                                                           : u"failed: "_s + outcome.error));
        const xgs::ResolveResult& response = *outcome.value;

        qInfo().noquote() << u"playlist '%1' has %2 items"_s.arg(response.playlistTitle.value_or(u"null"_s),
                                                                 QString::number(response.items.size()));
        for (const xgs::ResolvedItem& item : response.items.first(std::min<qsizetype>(25, response.items.size()))) {
            qInfo().noquote() << u"%1 %2 %3"_s.arg(item.rank, 3, 10, QChar(u'0'))
                                     .arg(item.lowConfidence ? u"WARN"_s : u"ok"_s, -5)
                                     .arg(item.expectedFileName.value_or(u'(' + item.unavailableReason.value_or(u"null"_s) +
                                                                         u')'));
        }

        QVERIFY2(response.items.size() > 10, "expected a long chart playlist");
        QList<int> expectedRanks, ranks;
        for (qsizetype i = 0; i < response.items.size(); ++i) {
            expectedRanks << int(i + 1);
            ranks << response.items[i].rank;
        }
        QCOMPARE(ranks, expectedRanks);
        QList<xgs::ResolvedItem> available;
        for (const xgs::ResolvedItem& item : response.items) {
            if (item.available) available << item;
        }
        QVERIFY(!available.isEmpty());
        const QRegularExpression name(QRegularExpression::anchoredPattern(uR"(\d{3} .+ - .+\.m4a)"_s));
        QVERIFY(std::all_of(available.cbegin(), available.cend(), [&](const xgs::ResolvedItem& item) {
            return item.expectedFileName && name.match(*item.expectedFileName).hasMatch();
        }));
    }

    // Downloads the audio (about 300 KB) of a 19-second video, so it also needs XGS_INTEGRATION_AUDIO=1 (not in Kotlin).
    void writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum() {
        if (qEnvironmentVariable("XGS_INTEGRATION_AUDIO") != u"1"_s) {
            QSKIP("downloads the audio of a 19-second video: set XGS_INTEGRATION_AUDIO=1 as well to run it");
        }
        const Deadline deadline(180s);
        // "Me at the zoo" again: 19 seconds, uploaded by a person, so YouTube knows no album for it.
        const QString videoId = u"jNQXAC9IVRw"_s;
        const auto temp = tempDir(u"xgs-info-integration"_s);
        QVERIFY(temp);
        const QString dir = xgs::integration::windowsPath(temp->path());
        XGS_CHECKED(runDownload(videoId, dir, {}, deadline.token()));

        const QString info = *xgs::paths::child(dir, videoId + u".info.json"_s);
        QVERIFY2(xgs::fs::isRegularFile(*xgs::paths::child(dir, videoId + u".m4a"_s)),
                 qPrintable(u"the m4a must be there: "_s + namesIn(dir).join(u", "_s)));
        QVERIFY2(xgs::fs::isRegularFile(info),
                 qPrintable(u"yt-dlp must write <videoId>.info.json next to the m4a: "_s + namesIn(dir).join(u", "_s)));
        const std::optional<xgs::json::Object> root = jsonObjectOf(info);
        QVERIFY2(root, "the info file must be a JSON object");
        QCOMPARE(shown(contentOf(*root, u"id")), shown(videoId));
        QVERIFY2(contentOf(*root, u"format_id") == u"140"_s, "the AAC stream (itag 140) is what is downloaded");
        qInfo().noquote() << u"info file: %1 bytes; album=%2, artist=%3, track=%4"_s.arg(
            QString::number(QFileInfo(info).size()), elementText(*root, u"album"), elementText(*root, u"artist"),
            elementText(*root, u"track"));
        QVERIFY2(!xgs::ytdlp::readVideoAlbum(info).has_value(), "the test video has no album");
    }

    void readsTheAlbumOfAMusicVideoFromItsInfoFileWithoutDownloadingAudio() {
        const Deadline deadline(120s);
        // Metadata only: --skip-download keeps yt-dlp from fetching any audio.
        // This depends on the music metadata YouTube currently shows for this video; if it changes, pick another track
        // that YouTube lists with an album.
        const QString videoId = u"kcx0a2OAhN0"_s;
        const auto temp = tempDir(u"xgs-info-integration"_s);
        QVERIFY(temp);
        const QString dir = xgs::integration::windowsPath(temp->path());
        XGS_CHECKED(runDownload(videoId, dir, {u"--skip-download"_s}, deadline.token()));

        const QString info = *xgs::paths::child(dir, videoId + u".info.json"_s);
        QVERIFY2(xgs::fs::isRegularFile(info),
                 qPrintable(u"yt-dlp must write <videoId>.info.json: "_s + namesIn(dir).join(u", "_s)));
        QVERIFY2(!xgs::fs::exists(*xgs::paths::child(dir, videoId + u".m4a"_s)), "no audio may be downloaded");
        const std::optional<xgs::json::Object> root = jsonObjectOf(info);
        QVERIFY2(root, "the info file must be a JSON object");
        qInfo().noquote() << u"info file: %1 bytes; album=%2, artist=%3, track=%4"_s.arg(
            QString::number(QFileInfo(info).size()), elementText(*root, u"album"), elementText(*root, u"artist"),
            elementText(*root, u"track"));
        QCOMPARE(shown(xgs::ytdlp::readVideoAlbum(info)), shown(u"Love poem"_s));
    }

    void findsTheLyricsInTheDescriptionOfAVideoThatHasThemWithoutDownloadingAudio() {
        const Deadline deadline(120s);
        // Metadata only: --skip-download keeps yt-dlp from fetching any audio. The test asserts structure only, never lyric text.
        // This depends on the CURRENT description of this video: a "[Lyrics]" heading with 20 or more non-blank lyric
        // lines below it, "=======" separator lines around the block, and channel link lines. If the uploader edits
        // the description, pick another video whose description has a lyrics section.
        const QString videoId = u"7mDDM0eBWR0"_s;
        const auto temp = tempDir(u"xgs-info-integration"_s);
        QVERIFY(temp);
        const QString dir = xgs::integration::windowsPath(temp->path());
        XGS_CHECKED(runDownload(videoId, dir, {u"--skip-download"_s}, deadline.token()));

        const QString info = *xgs::paths::child(dir, videoId + u".info.json"_s);
        QVERIFY2(xgs::fs::isRegularFile(info),
                 qPrintable(u"yt-dlp must write <videoId>.info.json: "_s + namesIn(dir).join(u", "_s)));
        QVERIFY2(!xgs::fs::exists(*xgs::paths::child(dir, videoId + u".m4a"_s)), "no audio may be downloaded");
        const std::optional<QString> description = xgs::ytdlp::readVideoInfo(info).description;
        QVERIFY2(description.has_value(), "the video must have a description");
        qInfo().noquote() << u"description structure: "_s + structureOf(*description);

        const std::optional<QString> lyrics = xgs::lyrics::extract(description);
        QVERIFY2(lyrics.has_value(), "a lyrics block must be found in the description");
        const QStringList lines = lyrics->split(u'\n');
        const auto nonBlank = std::count_if(lines.cbegin(), lines.cend(), [](const QString& l) { return !xgs::kt::isBlank(l); });
        qInfo().noquote() << u"lyrics block: %1 lines, %2 non-blank, %3 characters"_s.arg(
            QString::number(lines.size()), QString::number(nonBlank), QString::number(lyrics->size()));
        QVERIFY2(nonBlank >= 20, qPrintable(u"expected at least 20 non-blank lines, got "_s + QString::number(nonBlank)));
        QVERIFY2(!lyrics->contains(u"http"_s), "no link may be part of the lyrics");
        QVERIFY2(std::none_of(lines.cbegin(), lines.cend(), [](const QString& l) { return l.startsWith(u"i-dle Official"_s); }),
                 "the channel's link block must not be part of the lyrics");
        QVERIFY2(std::none_of(lines.cbegin(), lines.cend(), [](const QString& l) { return l == u"======="_s; }),
                 "no separator line may be part of the lyrics");
        QVERIFY(!lyrics->contains(u'\r'));
    }

    // The Kotlin test runs the whole download service (DefaultDownloadService, ItemDownloader, LrclibLyricsProvider) on a
    // 19-second video. Those are ported in later stages (Q2c: the lyrics HTTP lookup; Q2d: the download job), so the body cannot
    // be ported yet and the test skips with that reason after Kotlin's own ffprobe assumption.
    void downloadsAShortVideoAsM4aWithoutReencoding() {
        const std::optional<QString> ffprobePath = Ffprobe::besides(m_locator->current().ffmpeg);
        if (!ffprobePath) QSKIP("ffprobe must be installed next to ffmpeg");
        QSKIP("needs the download job (DefaultDownloadService, ItemDownloader: Q2d) and the LRCLIB lookup (Q2c), "
              "which are not ported yet");
    }

    // Not in Kotlin. The info-only command (--skip-download) into a folder whose name has spaces, Korean and `&`: the files
    // land in that folder (no shell reads the `&`, and the name reaches yt-dlp as one argument).
    void anOutputFolderWithSpacesKoreanAndAnAmpersandWorks() {
        const Deadline deadline(120s);
        const QString videoId = u"jNQXAC9IVRw"_s;
        const auto temp = tempDir(u"xgs-info-integration"_s);
        QVERIFY(temp);
        const QString parent = xgs::integration::windowsPath(temp->path());
        const QString folderName = u"내 노래 & Songs (1)"_s;
        const QString dir = *xgs::paths::child(parent, folderName);
        QVERIFY(xgs::fs::makeDirectories(dir));
        XGS_CHECKED(runDownload(videoId, dir, {u"--skip-download"_s}, deadline.token()));

        const QString info = *xgs::paths::child(dir, videoId + u".info.json"_s);
        QVERIFY2(xgs::fs::isRegularFile(info), qPrintable(u"the info file must be in the folder: "_s + namesIn(dir).join(u", "_s)));
        QVERIFY2(!xgs::fs::exists(*xgs::paths::child(dir, videoId + u".m4a"_s)), "no audio may be downloaded");
        QCOMPARE(namesIn(parent), QStringList{folderName});
        const std::optional<xgs::json::Object> root = jsonObjectOf(info);
        QVERIFY2(root, "the info file must be a JSON object");
        QCOMPARE(shown(contentOf(*root, u"id")), shown(videoId));
    }

    // Not in Kotlin. yt-dlp reads its `-o` value, the output folder included, as a template. Observed with yt-dlp 2026.08.19
    // (offline, with --load-info-json and --print filename on a hand-written info file): WITHOUT escaping, a folder `100% %(id)s 50%%`
    // becomes `100% jNQXAC9IVRw 50%` (the `%(id)s` is expanded, `%%` turns into `%`; a lone `%` survives) and yt-dlp creates
    // that other folder and exits with 0, so the files are silently misplaced. Neither Kotlin's YtDlpCommands.download nor
    // downloadCommand escapes; ytdlp::escapeOutputTemplateDirectory (doubling every `%`) is what the caller passes (Q2d), and
    // with it the files land in the folder itself.
    void aPercentSignInTheOutputFolderIsReportedNotSilentlyMisplaced() {
        const Deadline deadline(120s);
        const QString videoId = u"jNQXAC9IVRw"_s;
        const auto temp = tempDir(u"xgs-info-integration"_s);
        QVERIFY(temp);
        const QString parent = xgs::integration::windowsPath(temp->path());
        const QString folderName = u"100% %(id)s 50%%"_s;
        const QString dir = *xgs::paths::child(parent, folderName);
        QVERIFY(xgs::fs::makeDirectories(dir));
        XGS_CHECKED(runDownload(videoId, dir, {u"--skip-download"_s}, deadline.token(),
                                xgs::ytdlp::escapeOutputTemplateDirectory(dir)));

        const QString info = *xgs::paths::child(dir, videoId + u".info.json"_s);
        QVERIFY2(xgs::fs::isRegularFile(info),
                 qPrintable(u"the info file must be in the folder itself; the parent holds: "_s + namesIn(parent).join(u", "_s)));
        QVERIFY2(!xgs::fs::exists(*xgs::paths::child(dir, videoId + u".m4a"_s)), "no audio may be downloaded");
        QCOMPARE(namesIn(parent), QStringList{folderName});   // no misplaced sibling folder such as `100% jNQXAC9IVRw 50%`
    }
};

QTEST_APPLESS_MAIN(RealYtDlpIntegrationTest)
#include "tst_real_ytdlp.moc"
