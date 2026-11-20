#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>

#include "ffprobe.h"
#include "integration_support.h"
#include "paths/paths.h"
#include "process/system_process_runner.h"
#include "tags/m4a_tagger.h"
#include "tags/track_tags.h"
#include "tools/tool_locator.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::integration::Deadline;
using xgs::integration::Ffprobe;
using xgs::integration::Probed;
using xgs::process::RunResult;
using xgs::process::RunStatus;
using xgs::tags::M4aTagger;
using xgs::tags::TagResult;
using xgs::tags::TagStatus;
using xgs::tags::TrackTags;

namespace {

/// An optional text for QCOMPARE: `null` or the text in quotes, so an absent tag and a tag with the text "null" differ.
QString shown(const std::optional<QString>& value) { return value ? u'"' + *value + u'"' : u"null"_s; }

QString describe(const TagResult& result) {
    switch (result.status) {
    case TagStatus::Tagged: return u"Tagged"_s;
    case TagStatus::Cancelled: return u"Cancelled"_s;
    case TagStatus::Failed: break;
    }
    return u"Failed: "_s + result.failure.message;
}

}  // namespace

// Port of RealFfmpegTaggingIntegrationTest.kt. Runs the real M4aTagger with the real ffmpeg and reads the result back with the
// real ffprobe. It needs no network. Skipped unless XGS_INTEGRATION=1 (the Kotlin class is tagged "integration" and excluded
// from `test`), and, like Kotlin's assumeTrue, when ffmpeg or the ffprobe next to it is missing. Each Kotlin `withTimeout` is a
// Deadline whose token goes to every run.
class RealFfmpegTaggingIntegrationTest : public QObject {
    Q_OBJECT

    xgs::process::SystemProcessRunner m_runner;
    std::unique_ptr<xgs::tools::ToolLocator> m_locator;
    std::unique_ptr<QTemporaryDir> m_temp;
    QString m_root;
    QString m_ffmpeg;
    std::unique_ptr<Ffprobe> m_ffprobe;
    TrackTags m_tags;

    M4aTagger tagger() { return M4aTagger(m_runner, *m_locator); }

    QString inRoot(const QString& name) const { return *xgs::paths::child(m_root, name); }

    void generate(const QStringList& args, std::stop_token stop) {
        QStringList stderrLines;
        std::mutex mutex;
        const RunResult result = m_runner.run(
            QStringList{m_ffmpeg, u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s} + args,
            [](const QString&) {}, [&](const QString& line) { std::lock_guard lock(mutex); stderrLines << line; }, stop);
        QVERIFY2(result.status == RunStatus::Exited, qPrintable(u"ffmpeg did not run to its end: "_s + result.error));
        QVERIFY2(result.exitCode == 0, qPrintable(u"exit code "_s + QString::number(result.exitCode) + u": "_s +
                                                  stderrLines.join(u", "_s)));
    }

    /// A one-second silent AAC file in a regular MP4 container.
    void silentM4a(QString& out, std::stop_token stop) {
        out = inRoot(u"track.m4a"_s);
        generate({u"-f"_s, u"lavfi"_s, u"-i"_s, u"anullsrc=r=44100:cl=mono"_s, u"-t"_s, u"1"_s, u"-c:a"_s, u"aac"_s, out}, stop);
    }

    /// Like silentM4a, but fragmented with the `dash` brand, which is how yt-dlp leaves YouTube's itag 140 audio.
    void fragmentedM4a(QString& out, std::stop_token stop) {
        out = inRoot(u"track.m4a"_s);
        generate({u"-f"_s, u"lavfi"_s, u"-i"_s, u"anullsrc=r=44100:cl=mono"_s, u"-t"_s, u"1"_s, u"-c:a"_s, u"aac"_s,
                  u"-movflags"_s, u"frag_keyframe+empty_moov+default_base_moof"_s, u"-brand"_s, u"dash"_s, u"-f"_s, u"mp4"_s, out},
                 stop);
    }

    /// A 640x360 red picture, like a YouTube thumbnail (16:9).
    void redJpg(QString& out, std::stop_token stop) {
        out = inRoot(u"track.jpg"_s);
        generate({u"-f"_s, u"lavfi"_s, u"-i"_s, u"color=c=red:s=640x360"_s, u"-frames:v"_s, u"1"_s, out}, stop);
    }

    /// The MD5 of the audio stream as it is stored: a stream copy must not change it.
    void audioMd5(const QString& file, QString& out, std::stop_token stop) {
        QStringList stdoutLines;
        std::mutex mutex;
        const RunResult result = m_runner.run(
            {m_ffmpeg, u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-i"_s, file, u"-map"_s, u"0:a"_s,
             u"-c"_s, u"copy"_s, u"-f"_s, u"md5"_s, u"-"_s},
            [&](const QString& line) { std::lock_guard lock(mutex); stdoutLines << line; }, [](const QString&) {}, stop);
        QVERIFY2(result.status == RunStatus::Exited, qPrintable(u"ffmpeg did not run to its end: "_s + result.error));
        QVERIFY2(result.exitCode == 0, qPrintable(stdoutLines.join(u", "_s)));
        QStringList md5;
        for (const QString& line : stdoutLines) {
            if (line.startsWith(u"MD5="_s)) md5 << line;
        }
        QVERIFY2(md5.size() == 1, qPrintable(u"expected exactly one MD5= line: "_s + stdoutLines.join(u", "_s)));
        out = md5.first();
    }

    void assertTags(const TrackTags& expected, const Probed& actual) {
        QCOMPARE(shown(actual.tag(u"title"_s)), shown(expected.title));
        QCOMPARE(shown(actual.tag(u"artist"_s)), shown(expected.artist));
        QCOMPARE(shown(actual.tag(u"album_artist"_s)), shown(expected.albumArtist));
        QCOMPARE(shown(actual.tag(u"album"_s)), shown(expected.album));
        QCOMPARE(shown(actual.tag(u"track"_s)), shown(QString::number(expected.trackNumber)));
        QCOMPARE(shown(actual.tag(u"comment"_s)), shown(expected.comment));
        QCOMPARE(shown(actual.tag(u"lyrics"_s)), shown(expected.lyrics));
    }

    void tagged(const QString& file, const std::optional<QString>& cover, const TrackTags& tags, std::stop_token stop) {
        const TagResult result = tagger().tag(file, cover, tags, stop);
        QVERIFY2(result.status == TagStatus::Tagged, qPrintable(describe(result)));   // Kotlin: assertNull(failure)
    }

    // Ffprobe::probe throws when ffprobe fails or prints something unexpected: that fails this test only (QFAIL), so the
    // other tests still run and cleanup() still removes the temporary folder.
    void probe(const QString& file, std::stop_token stop, Probed& out) {
        try {
            out = m_ffprobe->probe(file, stop);
        } catch (const std::exception& e) {
            QFAIL(qPrintable(u"ffprobe: "_s + QString::fromUtf8(e.what())));
        }
    }

private slots:
    void initTestCase() {
        m_locator = std::make_unique<xgs::tools::ToolLocator>(xgs::integration::appBinDir());
    }

    // Kotlin's @BeforeTest requireTools: a QSKIP here skips the test function.
    void init() {
        if (!xgs::integration::enabled()) QSKIP(xgs::integration::disabledText());
        const std::optional<QString> found = m_locator->current().ffmpeg;
        const std::optional<QString> probe = Ffprobe::besides(found);
        if (!(found && probe)) QSKIP("ffmpeg and ffprobe must be installed");
        m_ffmpeg = *found;
        m_ffprobe = std::make_unique<Ffprobe>(*probe, m_runner);
        // Files.createTempDirectory("xgs-tagging"), one per test (JUnit makes a new instance per test).
        m_temp = std::make_unique<QTemporaryDir>(QDir::tempPath() + u"/xgs-tagging-XXXXXX"_s);
        QVERIFY(m_temp->isValid());
        m_root = xgs::integration::windowsPath(m_temp->path());
        m_tags = TrackTags{
            u"\"Golden\" '골든' (feat. 지민)"_s,
            u"방탄소년단"_s,
            u"My \"Best\" List"_s,
            u"방탄소년단"_s,
            7,
            u"https://www.youtube.com/watch?v=jNQXAC9IVRw"_s,
            u"첫 번째 줄\nLa la la 🎵\n\nSecond line\n마지막 줄"_s,
        };
    }

    // Kotlin's @AfterTest cleanUp.
    void cleanup() {
        m_temp.reset();
        m_ffprobe.reset();
    }

    void writesEveryTagAndASquareCoverIntoTheM4aWithoutTouchingTheAudio() {
        const Deadline deadline(60s);
        QString m4a, audioBefore, jpg;
        XGS_CHECKED(silentM4a(m4a, deadline.token()));
        XGS_CHECKED(audioMd5(m4a, audioBefore, deadline.token()));
        XGS_CHECKED(redJpg(jpg, deadline.token()));

        XGS_CHECKED(tagged(m4a, jpg, m_tags, deadline.token()));

        Probed probed;
        XGS_CHECKED(probe(m4a, deadline.token(), probed));
        XGS_CHECKED(assertTags(m_tags, probed));
        QCOMPARE(shown(probed.tag(u"major_brand"_s)), shown(u"M4A "_s));
        QVERIFY2(probed.streams.size() == 2, qPrintable(probed.describe()));
        const auto audio = std::find_if(probed.streams.cbegin(), probed.streams.cend(),
                                        [](const auto& s) { return s.codecType == u"audio"_s; });
        QVERIFY2(audio != probed.streams.cend() && std::count_if(probed.streams.cbegin(), probed.streams.cend(),
                                                                 [](const auto& s) { return s.codecType == u"audio"_s; }) == 1,
                 qPrintable(probed.describe()));
        QCOMPARE(audio->codecName, u"aac"_s);
        QVERIFY(!audio->attachedPic);
        const auto picture = std::find_if(probed.streams.cbegin(), probed.streams.cend(),
                                          [](const auto& s) { return s.codecType == u"video"_s; });
        QVERIFY2(picture != probed.streams.cend() && std::count_if(probed.streams.cbegin(), probed.streams.cend(),
                                                                   [](const auto& s) { return s.codecType == u"video"_s; }) == 1,
                 qPrintable(probed.describe()));
        QCOMPARE(picture->codecName, u"mjpeg"_s);
        QCOMPARE(picture->width.value_or(-1), 360);
        QCOMPARE(picture->height.value_or(-1), 360);
        QVERIFY(picture->attachedPic);
        QString audioAfter;
        XGS_CHECKED(audioMd5(m4a, audioAfter, deadline.token()));
        QVERIFY2(audioBefore == audioAfter, "the audio must be copied, not re-encoded");
        QStringList left = QDir(m_temp->path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                          QDir::Unsorted);
        std::sort(left.begin(), left.end());
        QVERIFY2(left == (QStringList{u"track.jpg"_s, u"track.m4a"_s}),
                 qPrintable(u"no temporary file may stay behind: "_s + left.join(u", "_s)));
    }

    void aFragmentedDashFileBecomesARegularM4aWithTheSameAudio() {
        const Deadline deadline(60s);
        QString m4a, audioBefore, jpg;
        XGS_CHECKED(fragmentedM4a(m4a, deadline.token()));
        Probed before;
        XGS_CHECKED(probe(m4a, deadline.token(), before));
        QVERIFY2(before.tag(u"major_brand"_s) == u"dash"_s, "precondition: the input is a fragmented dash file");
        XGS_CHECKED(audioMd5(m4a, audioBefore, deadline.token()));
        XGS_CHECKED(redJpg(jpg, deadline.token()));

        XGS_CHECKED(tagged(m4a, jpg, m_tags, deadline.token()));

        Probed probed;
        XGS_CHECKED(probe(m4a, deadline.token(), probed));
        QCOMPARE(shown(probed.tag(u"major_brand"_s)), shown(u"M4A "_s));
        XGS_CHECKED(assertTags(m_tags, probed));
        QString audioAfter;
        XGS_CHECKED(audioMd5(m4a, audioAfter, deadline.token()));
        QVERIFY2(audioBefore == audioAfter, "the audio must be copied, not re-encoded");
    }

    void withoutACoverOnlyTheAudioStreamIsWritten() {
        const Deadline deadline(60s);
        QString m4a;
        XGS_CHECKED(silentM4a(m4a, deadline.token()));

        XGS_CHECKED(tagged(m4a, std::nullopt, m_tags, deadline.token()));

        Probed probed;
        XGS_CHECKED(probe(m4a, deadline.token(), probed));
        XGS_CHECKED(assertTags(m_tags, probed));
        QStringList types;
        for (const auto& stream : probed.streams) types << stream.codecType;
        QVERIFY2(types == QStringList{u"audio"_s}, qPrintable(probed.describe()));
    }

    void aSingleVideoGetsNoAlbumTag() {
        const Deadline deadline(60s);
        QString m4a;
        XGS_CHECKED(silentM4a(m4a, deadline.token()));
        TrackTags noAlbum = m_tags;
        noAlbum.album = std::nullopt;

        XGS_CHECKED(tagged(m4a, std::nullopt, noAlbum, deadline.token()));

        Probed probed;
        XGS_CHECKED(probe(m4a, deadline.token(), probed));
        QCOMPARE(shown(probed.tag(u"album"_s)), shown(std::nullopt));
        QCOMPARE(shown(probed.tag(u"title"_s)), shown(m_tags.title));
    }

    void withoutCommentAndLyricsNeitherTagIsWritten() {
        const Deadline deadline(60s);
        QString m4a;
        XGS_CHECKED(silentM4a(m4a, deadline.token()));
        TrackTags bare = m_tags;
        bare.comment = std::nullopt;
        bare.lyrics = std::nullopt;

        XGS_CHECKED(tagged(m4a, std::nullopt, bare, deadline.token()));

        Probed probed;
        XGS_CHECKED(probe(m4a, deadline.token(), probed));
        QCOMPARE(shown(probed.tag(u"comment"_s)), shown(std::nullopt));
        const QStringList keys = probed.tags.keys();
        QVERIFY2(std::none_of(keys.cbegin(), keys.cend(), [](const QString& k) { return k.startsWith(u"lyrics"_s); }),
                 qPrintable(keys.join(u", "_s)));
        QCOMPARE(shown(probed.tag(u"title"_s)), shown(m_tags.title));
    }

    void syntaxCharactersAndLineBreaksSurviveTheRoundTrip() {
        const Deadline deadline(60s);
        QString m4a;
        XGS_CHECKED(silentM4a(m4a, deadline.token()));
        TrackTags nasty = m_tags;
        nasty.title = u"a=b;c#d\\e"_s;
        nasty.album = u"100% [x] {y} #1; k=v"_s;
        nasty.comment = u"line one\nline two\r\nline three"_s;
        nasty.lyrics = u"x=y;z\r\nsecond #line\n\nlast \\ line"_s;

        XGS_CHECKED(tagged(m4a, std::nullopt, nasty, deadline.token()));

        Probed probed;
        XGS_CHECKED(probe(m4a, deadline.token(), probed));
        XGS_CHECKED(assertTags(nasty, probed));
    }

    // A value ending in a backslash made ffmpeg swallow the next line of the ffmetadata file (the artist went missing).
    void aTitleEndingInABackslashDoesNotSwallowTheNextTag() {
        const Deadline deadline(60s);
        QString m4a;
        XGS_CHECKED(silentM4a(m4a, deadline.token()));
        TrackTags trailing = m_tags;
        trailing.title = u"Path to C:\\"_s;
        trailing.artist = u"Artist\\\\"_s;
        trailing.albumArtist = u"Artist\\\\"_s;
        trailing.album = u"Album\\"_s;

        XGS_CHECKED(tagged(m4a, std::nullopt, trailing, deadline.token()));

        Probed probed;
        XGS_CHECKED(probe(m4a, deadline.token(), probed));
        QCOMPARE(shown(probed.tag(u"title"_s)), shown(u"Path to C:\uFF3C"_s));
        QCOMPARE(shown(probed.tag(u"artist"_s)), shown(u"Artist\uFF3C\uFF3C"_s));
        QCOMPARE(shown(probed.tag(u"album_artist"_s)), shown(u"Artist\uFF3C\uFF3C"_s));
        QCOMPARE(shown(probed.tag(u"album"_s)), shown(u"Album\uFF3C"_s));
        QCOMPARE(shown(probed.tag(u"track"_s)), shown(u"7"_s));
        QCOMPARE(shown(probed.tag(u"comment"_s)), shown(trailing.comment));
        QCOMPARE(shown(probed.tag(u"lyrics"_s)), shown(trailing.lyrics));
    }

    void aNulInATitleCannotInjectAnotherTag() {
        const Deadline deadline(60s);
        QString m4a;
        XGS_CHECKED(silentM4a(m4a, deadline.token()));
        TrackTags injected = m_tags;
        injected.title = u"abc"_s + QChar(u'\0') + u"album=Injected"_s;
        injected.album = u"Real Album"_s;

        XGS_CHECKED(tagged(m4a, std::nullopt, injected, deadline.token()));

        Probed probed;
        XGS_CHECKED(probe(m4a, deadline.token(), probed));
        QCOMPARE(shown(probed.tag(u"title"_s)), shown(u"abcalbum=Injected"_s));
        QCOMPARE(shown(probed.tag(u"album"_s)), shown(u"Real Album"_s));
    }
};

QTEST_APPLESS_MAIN(RealFfmpegTaggingIntegrationTest)
#include "tst_real_ffmpeg_tagging.moc"
