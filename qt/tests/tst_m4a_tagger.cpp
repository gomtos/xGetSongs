#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>

#include "fs/file_ops.h"
#include "paths/paths.h"
#include "support/fake_process_runner.h"
#include "support/ffmetadata_reader.h"
#include "support/test_tools.h"
#include "tags/ffmetadata.h"
#include "tags/m4a_tagger.h"
#include "ytdlp/ffmpeg_commands.h"

using namespace Qt::StringLiterals;
using xgs::ToolPaths;
using xgs::process::LineHandler;
using xgs::tags::M4aTagger;
using xgs::tags::TagResult;
using xgs::tags::TagStatus;
using xgs::tags::TrackTags;
using xgs::testsupport::FakeProcessRunner;
using xgs::testsupport::FixedTools;
using xgs::testsupport::StartFailure;
using xgs::testsupport::testTools;
using xgs::ytdlp::FailureKind;

namespace {

// The parts of engine/src/test/.../testutil/Fakes.kt that the tagger tests use.

/// The bytes of what the fake ffmpeg writes. Nothing after ffmpeg may change them.
const QByteArray kFakeAudio = "tagged-m4a-data";

QString windowsPath(const QString& path) {
    QString out = path;
    return out.replace(u'/', u'\\');
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray("<unreadable>");
    return file.readAll();
}

/// Pretends ffmpeg finished tagging: creates the output file, which is the last argument.
void writeFakeTagged(const QStringList& command) {
    if (!xgs::fs::writeBytes(command.last(), kFakeAudio).isOk()) throw std::runtime_error("the fake output was not written");
}

/// The text of the ffmetadata file an ffmpeg tagging command reads (the input after `-f ffmetadata`).
QString ffmetadataTextOf(const QStringList& command) {
    return QString::fromUtf8(readBytes(command.at(command.indexOf(u"ffmetadata"_s) + 2)));
}

/// True when the file still holds the output of the fake ffmpeg.
bool endsWithFakeAudio(const QString& file) {
    return readBytes(file).endsWith(kFakeAudio);
}

}  // namespace

// Port of M4aTaggerTest.kt. A cancelled coroutine is a stop requested on the token; tag returns Cancelled.
class M4aTaggerTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_temp;
    QString m_dir, m_file, m_cover;
    TrackTags m_tags;
    const QString m_lyrics = u"첫 번째 줄\nLa la la\n세 번째 줄"_s;

    M4aTagger tagger(FakeProcessRunner& runner, const FixedTools& tools) { return M4aTagger(runner, tools); }

    QStringList filesInDir() const {
        QStringList names = QDir(m_dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        std::sort(names.begin(), names.end());
        return names;
    }

    static void compareFailure(const TagResult& result, FailureKind kind, const QString& message) {
        QCOMPARE(result.status, TagStatus::Failed);
        QCOMPARE(result.failure.kind, kind);
        QCOMPARE(result.failure.message, message);
    }

private slots:
    void init() {
        // a fresh directory per test, as Kotlin's per-instance Files.createTempDirectory
        m_temp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp->isValid());
        m_dir = *xgs::paths::fromText(windowsPath(m_temp->path()));
        m_file = *xgs::paths::child(m_dir, u"vid00000001.m4a"_s);
        m_cover = *xgs::paths::child(m_dir, u"vid00000001.jpg"_s);
        m_tags = TrackTags{u"\"Golden\" 'x'"_s, u"아티스트"_s, u"My List"_s, u"아티스트"_s, 3,
                           u"https://www.youtube.com/watch?v=vid00000001"_s, std::nullopt};
        QVERIFY(xgs::fs::writeUtf8(m_file, u"original-m4a"_s).isOk());
    }
    void cleanup() { m_temp.reset(); }

    void replacesTheOriginalWithTheTaggedFileAndRemovesTheTemporaryFiles() {
        std::optional<QString> metadataText;
        FakeProcessRunner runner([&](const QStringList& command, const LineHandler&, const LineHandler&) {
            metadataText = ffmetadataTextOf(command);
            writeFakeTagged(command);
            return 0;
        });
        const FixedTools tools;

        const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, m_tags, {});

        QCOMPARE(result.status, TagStatus::Tagged);
        QVERIFY2(endsWithFakeAudio(m_file), "the output of ffmpeg must reach the final file");
        QCOMPARE(filesInDir(), QStringList{u"vid00000001.m4a"_s});
        QCOMPARE(metadataText, std::optional<QString>(xgs::tags::renderFfmetadata(m_tags)));
    }

    void theCommentAndTheLyricsReachFfmpegThroughTheMetadataFile() {
        TrackTags withLyrics = m_tags;
        withLyrics.lyrics = m_lyrics;
        std::optional<QString> metadataText;
        FakeProcessRunner runner([&](const QStringList& command, const LineHandler&, const LineHandler&) {
            metadataText = ffmetadataTextOf(command);
            writeFakeTagged(command);
            return 0;
        });
        const FixedTools tools;

        const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, withLyrics, {});

        QCOMPARE(result.status, TagStatus::Tagged);
        QVERIFY(metadataText.has_value());
        const QMap<QString, QString> values = xgs::testsupport::readFfmetadata(*metadataText);
        QCOMPARE(values.contains(u"comment"_s) ? std::optional(values.value(u"comment"_s)) : std::nullopt, m_tags.comment);
        QCOMPARE(values.contains(u"lyrics"_s) ? std::optional(values.value(u"lyrics"_s)) : std::nullopt,
                 std::optional<QString>(m_lyrics));
    }

    void aMissingOrBlankCommentAndLyricsWriteNoEntry() {
        for (const std::optional<QString>& blank : {std::optional<QString>(), std::optional<QString>(u""_s),
                                                    std::optional<QString>(u" \n\t"_s)}) {
            std::optional<QString> metadataText;
            FakeProcessRunner runner([&](const QStringList& command, const LineHandler&, const LineHandler&) {
                metadataText = ffmetadataTextOf(command);
                writeFakeTagged(command);
                return 0;
            });
            const FixedTools tools;
            TrackTags blankTags = m_tags;
            blankTags.comment = blank;
            blankTags.lyrics = blank;

            const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, blankTags, {});

            QCOMPARE(result.status, TagStatus::Tagged);
            QVERIFY(metadataText.has_value());
            const QMap<QString, QString> values = xgs::testsupport::readFfmetadata(*metadataText);
            QVERIFY2(!values.contains(u"comment"_s) && !values.contains(u"lyrics"_s),
                     qPrintable(u"comment and lyrics = [%1]: %2"_s.arg(blank.value_or(u"null"_s), QStringList(values.keys()).join(u','))));
        }
    }

    void runsTheFfmpegCommandWithTemporaryFilesNextToTheOriginal() {
        FakeProcessRunner runner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeTagged(command);
            return 0;
        });
        const FixedTools tools;

        tagger(runner, tools).tag(m_file, std::nullopt, m_tags, {});

        const QStringList expected = xgs::ytdlp::tagCommand(*testTools().ffmpeg, m_file, std::nullopt,
                                                            *xgs::paths::child(m_dir, u"vid00000001.ffmeta"_s),
                                                            *xgs::paths::child(m_dir, u"vid00000001.tagged.m4a"_s));
        QCOMPARE(runner.commands(), QList<QStringList>{expected});
    }

    void passesTheCoverAsASecondInput() {
        QVERIFY(xgs::fs::writeUtf8(m_cover, u"jpg-data"_s).isOk());
        FakeProcessRunner runner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeTagged(command);
            return 0;
        });
        const FixedTools tools;

        tagger(runner, tools).tag(m_file, m_cover, m_tags, {});

        QCOMPARE(runner.commands().size(), 1);
        const QStringList command = runner.commands().first();
        QStringList inputs, maps;
        for (qsizetype i = 0; i + 1 < command.size(); ++i) {
            if (command.at(i) == u"-i"_s && inputs.size() < 2) inputs.append(command.at(i + 1));
            if (command.at(i) == u"-map"_s) maps.append(command.at(i + 1));
        }
        QCOMPARE(inputs, (QStringList{m_file, m_cover}));
        QCOMPARE(maps, (QStringList{u"0:a"_s, u"1:v"_s}));
        QCOMPARE(command.at(command.indexOf(u"-map_metadata"_s) + 1), u"2"_s);
    }

    void noTagTextAppearsOnTheCommandLine() {
        FakeProcessRunner runner([](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeTagged(command);
            return 0;
        });
        const FixedTools tools;
        TrackTags withLyrics = m_tags;
        withLyrics.lyrics = m_lyrics;

        tagger(runner, tools).tag(m_file, std::nullopt, withLyrics, {});

        QCOMPARE(runner.commands().size(), 1);
        const QString commandLine = runner.commands().first().join(u' ');
        const QStringList texts{u"Golden"_s, u"아티스트"_s, u"My List"_s, u"youtube"_s, u"첫 번째"_s, u"La la la"_s};
        QVERIFY2(std::none_of(texts.begin(), texts.end(), [&](const QString& text) { return commandLine.contains(text); }),
                 qPrintable(commandLine));
    }

    void aNonZeroExitCodeFailsWithTheLastStderrLineAndKeepsTheOriginal() {
        FakeProcessRunner runner([](const QStringList& command, const LineHandler&, const LineHandler& err) {
            writeFakeTagged(command);   // a half-written output must not survive
            err(u"first problem"_s);
            err(u"Invalid argument"_s);
            err(u"   "_s);
            return 1;
        });
        const FixedTools tools;

        const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, m_tags, {});

        compareFailure(result, FailureKind::Other, u"태그를 쓰지 못했습니다: Invalid argument"_s);
        QCOMPARE(readBytes(m_file), QByteArray("original-m4a"));
        QCOMPARE(filesInDir(), QStringList{u"vid00000001.m4a"_s});
    }

    void exitCodeZeroWithoutAnOutputFileIsAFailure() {
        FakeProcessRunner runner([](const QStringList&, const LineHandler&, const LineHandler&) { return 0; });
        const FixedTools tools;

        const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, m_tags, {});

        compareFailure(result, FailureKind::Other, u"태그를 쓰지 못했습니다: 알 수 없는 오류"_s);
        QCOMPARE(readBytes(m_file), QByteArray("original-m4a"));
        QCOMPARE(filesInDir(), QStringList{u"vid00000001.m4a"_s});
    }

    void theStderrSummaryIsCutToTwoHundredCharacters() {
        FakeProcessRunner runner([](const QStringList&, const LineHandler&, const LineHandler& err) {
            err(QString(500, u'x'));
            return 1;
        });
        const FixedTools tools;

        const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, m_tags, {});

        compareFailure(result, FailureKind::Other, u"태그를 쓰지 못했습니다: "_s + QString(200, u'x'));
    }

    void aMissingFfmpegIsFatalAndRunsNothing() {
        FakeProcessRunner runner([](const QStringList&, const LineHandler&, const LineHandler&) { return 0; });
        ToolPaths noFfmpeg = testTools();
        noFfmpeg.ffmpeg = std::nullopt;
        const FixedTools tools(noFfmpeg);

        const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, m_tags, {});

        compareFailure(result, FailureKind::Fatal, u"ffmpeg를 찾을 수 없습니다."_s);
        QVERIFY(runner.commands().isEmpty());
        QCOMPARE(filesInDir(), QStringList{u"vid00000001.m4a"_s});
    }

    void anIoErrorBecomesAFailureInsteadOfAnException() {
        FakeProcessRunner runner([](const QStringList&, const LineHandler&, const LineHandler&) { return 0; });
        const FixedTools tools;
        const QString missingDir = *xgs::paths::child(*xgs::paths::child(m_dir, u"no-such-dir"_s), u"a.m4a"_s);

        const TagResult result = tagger(runner, tools).tag(missingDir, std::nullopt, m_tags, {});

        QCOMPARE(result.status, TagStatus::Failed);
        QCOMPARE(result.failure.kind, FailureKind::Other);
        QVERIFY2(result.failure.message.startsWith(u"태그를 쓰지 못했습니다: "_s), qPrintable(result.failure.message));
        QVERIFY(runner.commands().isEmpty());
    }

    void cancellingWhileFfmpegRunsPropagatesAndStillRemovesTheTemporaryFiles() {
        std::stop_source stop;
        bool started = false;
        FakeProcessRunner runner([&](const QStringList& command, const LineHandler&, const LineHandler&) {
            writeFakeTagged(command);
            started = true;
            stop.request_stop();   // the job is cancelled while ffmpeg runs
            return 0;
        });
        const FixedTools tools;

        const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, m_tags, stop.get_token());

        QVERIFY(started);
        QCOMPARE(result.status, TagStatus::Cancelled);
        QCOMPARE(filesInDir(), QStringList{u"vid00000001.m4a"_s});
        QCOMPARE(readBytes(m_file), QByteArray("original-m4a"));

        // Not in Kotlin (a cancelled coroutine never starts): a stop requested before tag writes and runs nothing.
        FakeProcessRunner unused([](const QStringList&, const LineHandler&, const LineHandler&) { return 0; });
        const TagResult early = tagger(unused, tools).tag(m_file, std::nullopt, m_tags, stop.get_token());
        QCOMPARE(early.status, TagStatus::Cancelled);
        QVERIFY(unused.commands().isEmpty());
        QCOMPARE(filesInDir(), QStringList{u"vid00000001.m4a"_s});
    }

    // Added: ffmpeg that cannot be started (Kotlin: the runner throws IOException, which tag reports with its message).
    void aStartFailureOfFfmpegIsAFailureAndKeepsTheOriginal() {
        FakeProcessRunner runner([](const QStringList& command, const LineHandler&, const LineHandler&) -> int {
            writeFakeTagged(command);   // even a file left by the attempt must not survive
            throw StartFailure{u"cannot start"_s};
        });
        const FixedTools tools;

        const TagResult result = tagger(runner, tools).tag(m_file, std::nullopt, m_tags, {});

        compareFailure(result, FailureKind::Other, u"태그를 쓰지 못했습니다: cannot start"_s);
        QCOMPARE(readBytes(m_file), QByteArray("original-m4a"));
        QCOMPARE(filesInDir(), QStringList{u"vid00000001.m4a"_s});
    }
};

QTEST_GUILESS_MAIN(M4aTaggerTest)
#include "tst_m4a_tagger.moc"
