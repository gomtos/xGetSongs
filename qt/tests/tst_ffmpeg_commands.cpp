#include <QtTest>

#include <optional>
#include <stdexcept>

#include "ytdlp/ffmpeg_commands.h"

using namespace Qt::StringLiterals;

namespace {

// Kotlin's Path.of("C:/work/1/vid.m4a").toString() is C:\work\1\vid.m4a.
const QString ffmpeg = u"C:\\tools\\ffmpeg.exe"_s;
const QString input = u"C:\\work\\1\\vid.m4a"_s;
const QString cover = u"C:\\work\\1\\vid.jpg"_s;
const QString metadata = u"C:\\work\\1\\vid.ffmeta"_s;
const QString output = u"C:\\work\\1\\vid.tagged.m4a"_s;

}  // namespace

class FfmpegCommandsTest : public QObject {
    Q_OBJECT

private slots:
    void withoutACoverCopiesTheAudioAndTagsItFromTheMetadataFile() {
        const QStringList command = xgs::ytdlp::tagCommand(ffmpeg, input, std::nullopt, metadata, output);

        QCOMPARE(command, (QStringList{
                              ffmpeg, u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s,
                              u"-i"_s, input, u"-f"_s, u"ffmetadata"_s, u"-i"_s, metadata,
                              u"-map"_s, u"0:a"_s, u"-map_chapters"_s, u"-1"_s, u"-map_metadata"_s, u"1"_s,
                              u"-c:a"_s, u"copy"_s,
                              output,
                          }));
    }

    void withACoverAddsItAsAnAttachedSquareMjpegPicture() {
        const QStringList command = xgs::ytdlp::tagCommand(ffmpeg, input, cover, metadata, output);

        QCOMPARE(command, (QStringList{
                              ffmpeg, u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s,
                              u"-i"_s, input, u"-i"_s, cover, u"-f"_s, u"ffmetadata"_s, u"-i"_s, metadata,
                              u"-map"_s, u"0:a"_s, u"-map"_s, u"1:v"_s, u"-map_chapters"_s, u"-1"_s, u"-map_metadata"_s, u"2"_s,
                              u"-c:a"_s, u"copy"_s,
                              u"-c:v"_s, u"mjpeg"_s, u"-q:v"_s, u"2"_s, u"-vf"_s, u"crop=min(iw\\,ih):min(iw\\,ih)"_s,
                              u"-disposition:v"_s, u"attached_pic"_s,
                              u"-metadata:s:v"_s, u"title=Album cover"_s, u"-metadata:s:v"_s, u"comment=Cover (front)"_s,
                              output,
                          }));
    }

    void theCropFilterIsOneArgumentWithAnEscapedComma() {
        const QStringList command = xgs::ytdlp::tagCommand(ffmpeg, input, cover, metadata, output);

        QCOMPARE(command.at(command.indexOf(u"-vf"_s) + 1), uR"(crop=min(iw\,ih):min(iw\,ih))"_s);
    }

    void theOutputMustBeAnM4aSoFfmpegPicksTheMp4Muxer() {
        for (const QString& wrong : {u"vid.tmp"_s, u"vid.mp3"_s}) {
            const QString wrongOutput = u"C:\\work\\1\\"_s + wrong;
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                     xgs::ytdlp::tagCommand(ffmpeg, input, std::nullopt, metadata, wrongOutput));
        }
    }
};

QTEST_APPLESS_MAIN(FfmpegCommandsTest)
#include "tst_ffmpeg_commands.moc"
