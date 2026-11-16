#include <QtTest>

#include <optional>
#include <stdexcept>

#include "model/models.h"
#include "ytdlp/ytdlp_commands.h"

using xgs::ToolPaths;
using namespace Qt::StringLiterals;

namespace {

// engine/.../testutil/Fakes.kt TEST_TOOLS. Kotlin's Path.of("C:/tools/x.exe").toString() is C:\tools\x.exe.
ToolPaths testTools() {
    return ToolPaths{u"C:\\tools\\yt-dlp.exe"_s, u"C:\\tools\\ffmpeg.exe"_s, u"C:\\tools\\node.exe"_s};
}

const QString url = u"https://www.youtube.com/watch?v=dQw4w9WgXcQ"_s;

}  // namespace

class YtDlpCommandsTest : public QObject {
    Q_OBJECT

private slots:
    void playlistListingIsFlatAndPutsTheUrlAfterDoubleDash() {
        const QStringList command =
            xgs::ytdlp::resolvePlaylistCommand(testTools(), u"https://www.youtube.com/playlist?list=PLabcdefghijkl"_s);
        QCOMPARE(command.first(), *testTools().ytDlp);
        QVERIFY(command.contains(u"--flat-playlist"_s));
        QVERIFY(command.contains(u"--ignore-config"_s));
        QCOMPARE(command.last(2), (QStringList{u"--"_s, u"https://www.youtube.com/playlist?list=PLabcdefghijkl"_s}));
    }

    void videoMetadataUsesTheJsRuntime() {
        const QStringList command = xgs::ytdlp::resolveVideoCommand(testTools(), url);
        const qsizetype i = command.indexOf(u"--js-runtimes"_s);
        QCOMPARE(command.at(i + 1), u"node:"_s + *testTools().jsRuntime);
        QCOMPARE(command.last(2), (QStringList{u"--"_s, url}));
    }

    void downloadSavesTheAacStreamAsItIsIntoTheOutputDirectory() {
        const QStringList command =
            xgs::ytdlp::downloadCommand(testTools(), url, u"C:\\work\\job-1"_s, u"dQw4w9WgXcQ"_s);
        QCOMPARE(command.at(command.indexOf(u"-f"_s) + 1), u"bestaudio[ext=m4a]"_s);
        QCOMPARE(command.at(command.indexOf(u"--fixup"_s) + 1), u"never"_s);
        for (const QString& reencoding : {u"-x"_s, u"--extract-audio"_s, u"--audio-format"_s, u"--audio-quality"_s}) {
            QVERIFY2(!command.contains(reencoding), qPrintable(u"the audio must not be re-encoded: "_s + reencoding));
        }
        QVERIFY(command.at(command.indexOf(u"-o"_s) + 1).endsWith(u"dQw4w9WgXcQ.%(ext)s"_s));
        QCOMPARE(command.at(command.indexOf(u"--ffmpeg-location"_s) + 1), *testTools().ffmpeg);
        QCOMPARE(command.count(u"--progress-template"_s), 1);
        QCOMPARE(command.last(2), (QStringList{u"--"_s, url}));
    }

    void downloadSavesTheThumbnailAsJpgNextToTheAudio() {
        const QStringList command =
            xgs::ytdlp::downloadCommand(testTools(), url, u"C:\\work\\job-1"_s, u"dQw4w9WgXcQ"_s);
        QVERIFY(command.contains(u"--write-thumbnail"_s));
        QCOMPARE(command.at(command.indexOf(u"--convert-thumbnails"_s) + 1), u"jpg"_s);
        QCOMPARE(command.count(u"--write-thumbnail"_s), 1);
        QCOMPARE(command.count(u"--convert-thumbnails"_s), 1);
    }

    void thumbnailOptionsComeBeforeTheOutputTemplate() {
        const QStringList command =
            xgs::ytdlp::downloadCommand(testTools(), url, u"C:\\work\\job-1"_s, u"dQw4w9WgXcQ"_s);
        QVERIFY(command.indexOf(u"--write-thumbnail"_s) < command.indexOf(u"-o"_s));
        QVERIFY(command.indexOf(u"--convert-thumbnails"_s) < command.indexOf(u"-o"_s));
    }

    void downloadWritesTheInfoJsonBeforeTheOutputTemplate() {
        const QStringList command =
            xgs::ytdlp::downloadCommand(testTools(), url, u"C:\\work\\job-1"_s, u"dQw4w9WgXcQ"_s);
        QCOMPARE(command.count(u"--write-info-json"_s), 1);
        QVERIFY(command.indexOf(u"--write-info-json"_s) < command.indexOf(u"-o"_s));
        QVERIFY(command.contains(u"--no-playlist"_s));   // keeps it to one video, so one info file
    }

    void onlyTheDownloadCommandWritesAnInfoJson() {
        QVERIFY(!xgs::ytdlp::resolveVideoCommand(testTools(), url).contains(u"--write-info-json"_s));
        QVERIFY(!xgs::ytdlp::resolvePlaylistCommand(testTools(), url).contains(u"--write-info-json"_s));
    }

    void denoIsPassedWithItsPath() {
        ToolPaths tools = testTools();
        tools.jsRuntime = u"C:\\tools\\deno.exe"_s;
        const QStringList command = xgs::ytdlp::resolveVideoCommand(tools, url);
        QCOMPARE(command.at(command.indexOf(u"--js-runtimes"_s) + 1), u"deno:"_s + *tools.jsRuntime);
    }

    void noJsRuntimeFlagWhenNoneIsInstalled() {
        ToolPaths tools = testTools();
        tools.jsRuntime = std::nullopt;
        tools.ffmpeg = std::nullopt;
        const QStringList command = xgs::ytdlp::downloadCommand(tools, url, u"C:\\w"_s, u"id"_s);
        QVERIFY(!command.contains(u"--js-runtimes"_s));
        QVERIFY(!command.contains(u"--ffmpeg-location"_s));
    }

    void ytDlpPathIsRequired() {
        ToolPaths tools = testTools();
        tools.ytDlp = std::nullopt;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, xgs::ytdlp::resolveVideoCommand(tools, url));
    }
};

QTEST_APPLESS_MAIN(YtDlpCommandsTest)
#include "tst_ytdlp_commands.moc"
