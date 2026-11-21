#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>

#include "support/fake_process_runner.h"
#include "ytdlp/ytdlp_resolver.h"

// The download part of engine/src/test/.../testutil/Fakes.kt: what the fake yt-dlp and the fake ffmpeg leave behind, and the
// runner of a whole download. Commands are the ones xgs::ytdlp builds with the paths of testTools().
namespace xgs::testsupport {

/// The directory a download command writes to, read from its `-o` argument (`<dir>\<id>.%(ext)s`) the way yt-dlp reads the
/// template: the directory part with every `%%` turned back into `%` (the caller escapes it, see
/// ytdlp::escapeOutputTemplateDirectory), so a fake writes where the real tool would.
QString outputDirOf(const QStringList& command);

/// The video ID a download command is for, read from the `-o` argument (`<dir>\<id>.%(ext)s`).
QString videoIdOf(const QStringList& command);

/// Pretends yt-dlp finished: creates `<dir>\<id>.m4a` the way the real tool would.
void writeFakeM4a(const QStringList& command);

/// Pretends yt-dlp also saved the converted thumbnail: creates `<dir>\<id>.jpg`.
void writeFakeCover(const QStringList& command);

/// Pretends yt-dlp also wrote the video's info file: creates `<dir>\<id>.info.json` holding json.
void writeFakeInfo(const QStringList& command, const QByteArray& json);

/// The bytes of what the fake ffmpeg writes. Nothing after ffmpeg may change them (Kotlin's FAKE_AUDIO).
QByteArray fakeAudio();

/// True when the file still holds the output of the fake ffmpeg.
bool endsWithFakeAudio(const QString& file);

/// True for the tagging pass (the command starts with the ffmpeg path of testTools()); false for yt-dlp commands.
bool isFfmpegCommand(const QStringList& command);

/// Pretends ffmpeg finished tagging: creates the output file, which is the last argument.
void writeFakeTagged(const QStringList& command);

/// The text of the ffmetadata file an ffmpeg tagging command reads (the input after `-f ffmetadata`).
QString ffmetadataTextOf(const QStringList& command);

/// A runner for a whole download: the ffmpeg tagging pass succeeds by writing the tagged file, every other command (yt-dlp)
/// is handled by ytDlp. The second form hands ytDlp the stop token of the run.
FakeProcessRunner downloadRunner(ProcessHandler ytDlp);
FakeProcessRunner downloadRunner(StoppableProcessHandler ytDlp);

/// The yt-dlp commands and the ffmpeg commands a runner was given, in order.
QList<QStringList> ytDlpCommands(const FakeProcessRunner& runner);
QList<QStringList> ffmpegCommands(const FakeProcessRunner& runner);

/// Kotlin's `awaitCancellation()` inside a handler: blocks until stop is requested.
void awaitStop(std::stop_token stop);

/// A VideoMetadataSource whose answer is a function; records every video ID it is asked for. Safe to call from several
/// threads. An empty answer function answers nothing.
class FakeMetadata final : public ytdlp::VideoMetadataSource {
public:
    using Answer = std::function<std::optional<ytdlp::VideoMeta>(const QString& videoId, std::stop_token stop)>;

    explicit FakeMetadata(Answer answer = {});
    std::optional<ytdlp::VideoMeta> fetch(const QString& videoId, std::stop_token stop) override;
    QStringList videoIds() const;

private:
    Answer m_answer;
    mutable std::mutex m_mutex;
    QStringList m_videoIds;
};

}  // namespace xgs::testsupport
