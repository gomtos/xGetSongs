#include "support/job_fakes.h"

#include <QFile>

#include <chrono>
#include <condition_variable>
#include <stdexcept>
#include <utility>

#include "fs/file_ops.h"
#include "paths/paths.h"
#include "support/test_tools.h"

namespace xgs::testsupport {

namespace {

using namespace Qt::StringLiterals;

const QString kExtensionTemplate = u".%(ext)s"_s;

QString templateOf(const QStringList& command) {
    const qsizetype at = command.indexOf(u"-o"_s);
    if (at < 0 || at + 1 >= command.size()) throw std::runtime_error("the command has no -o argument");
    return command.at(at + 1);
}

QString fileInOutputDir(const QStringList& command, const QString& extension) {
    const std::optional<QString> file = paths::child(outputDirOf(command), videoIdOf(command) + extension);
    if (!file) throw std::runtime_error("the output directory of the command is not a path");
    return *file;
}

void writeOrThrow(const QString& path, const QByteArray& bytes) {
    if (!fs::writeBytes(path, bytes).isOk()) throw std::runtime_error("a fake file was not written");
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray("<unreadable>");
    return file.readAll();
}

QList<QStringList> commandsWhere(const FakeProcessRunner& runner, bool ffmpeg) {
    QList<QStringList> picked;
    for (const QStringList& command : runner.commands()) {
        if (isFfmpegCommand(command) == ffmpeg) picked.append(command);
    }
    return picked;
}

}  // namespace

QString outputDirOf(const QStringList& command) {
    const QString outputTemplate = templateOf(command);
    QString dir = outputTemplate.left(outputTemplate.lastIndexOf(u'\\'));
    return dir.replace(u"%%"_s, u"%"_s);
}

QString videoIdOf(const QStringList& command) {
    const QString outputTemplate = templateOf(command);
    QString name = outputTemplate.mid(outputTemplate.lastIndexOf(u'\\') + 1);
    if (name.endsWith(kExtensionTemplate)) name.chop(kExtensionTemplate.size());
    return name;
}

void writeFakeM4a(const QStringList& command) {
    writeOrThrow(fileInOutputDir(command, u".m4a"_s), "m4a-data");
}

void writeFakeCover(const QStringList& command) {
    writeOrThrow(fileInOutputDir(command, u".jpg"_s), "jpg-data");
}

void writeFakeInfo(const QStringList& command, const QByteArray& json) {
    writeOrThrow(fileInOutputDir(command, u".info.json"_s), json);
}

QByteArray fakeAudio() {
    return QByteArray("tagged-m4a-data");
}

bool endsWithFakeAudio(const QString& file) {
    return readBytes(file).endsWith(fakeAudio());
}

bool isFfmpegCommand(const QStringList& command) {
    return !command.isEmpty() && command.first() == testTools().ffmpeg.value_or(QString());
}

void writeFakeTagged(const QStringList& command) {
    writeOrThrow(command.last(), fakeAudio());
}

QString ffmetadataTextOf(const QStringList& command) {
    // `-f ffmetadata -i <file>`: the file is two places after the format name
    const qsizetype at = command.indexOf(u"ffmetadata"_s);
    if (at < 0 || at + 2 >= command.size() || command.at(at + 1) != u"-i"_s)
        throw std::runtime_error("the command has no ffmetadata input");
    return QString::fromUtf8(readBytes(command.at(at + 2)));
}

FakeProcessRunner downloadRunner(ProcessHandler ytDlp) {
    return downloadRunner(StoppableProcessHandler(
        [ytDlp = std::move(ytDlp)](const QStringList& command, const process::LineHandler& out,
                                   const process::LineHandler& err, std::stop_token) { return ytDlp(command, out, err); }));
}

FakeProcessRunner downloadRunner(StoppableProcessHandler ytDlp) {
    return FakeProcessRunner(StoppableProcessHandler(
        [ytDlp = std::move(ytDlp)](const QStringList& command, const process::LineHandler& out,
                                   const process::LineHandler& err, std::stop_token stop) {
            if (isFfmpegCommand(command)) {
                writeFakeTagged(command);
                return 0;
            }
            return ytDlp(command, out, err, stop);
        }));
}

QList<QStringList> ytDlpCommands(const FakeProcessRunner& runner) {
    return commandsWhere(runner, false);
}

QList<QStringList> ffmpegCommands(const FakeProcessRunner& runner) {
    return commandsWhere(runner, true);
}

void awaitStop(std::stop_token stop) {
    // A token without a stop state can never be stopped: waiting on it would hang the test instead of failing it.
    if (!stop.stop_possible()) throw std::logic_error("awaitStop was given a token that can never be stopped");
    // The limit is only a safety net: a broken service that never requests the stop fails the test instead of hanging the
    // whole run (in a process handler the exception becomes a failed item; in a lyrics provider it is swallowed as "no
    // lyrics" and the item goes on; either way the test's assertions see a result a working service never gives). 30 s is
    // far beyond any wait a working service causes.
    constexpr std::chrono::seconds kSafetyLimit{30};
    std::mutex mutex;
    std::condition_variable_any changed;
    std::unique_lock lock(mutex);
    changed.wait_for(lock, stop, kSafetyLimit, [] { return false; });
    if (!stop.stop_requested()) throw std::runtime_error("awaitStop: no stop was requested within 30 s");
}

FakeMetadata::FakeMetadata(Answer answer) : m_answer(std::move(answer)) {}

std::optional<ytdlp::VideoMeta> FakeMetadata::fetch(const QString& videoId, std::stop_token stop) {
    {
        const std::lock_guard lock(m_mutex);
        m_videoIds.append(videoId);
    }
    return m_answer ? m_answer(videoId, std::move(stop)) : std::nullopt;
}

QStringList FakeMetadata::videoIds() const {
    const std::lock_guard lock(m_mutex);
    return m_videoIds;
}

}  // namespace xgs::testsupport
