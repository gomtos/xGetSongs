#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include <optional>
#include <stop_token>

#include "process/process_runner.h"

// Port of engine/src/test/.../integration/Ffprobe.kt: reads a file with the real ffprobe, which is installed next to the real
// ffmpeg.
namespace xgs::integration {

/// One stream of a probed file. attachedPic is ffprobe's `disposition.attached_pic` (an embedded cover).
struct ProbedStream {
    QString codecType;
    QString codecName;
    std::optional<int> width;
    std::optional<int> height;
    bool attachedPic = false;
};

/// What ffprobe reports about a file: the container's tags and its streams.
struct Probed {
    QMap<QString, QString> tags;
    QList<ProbedStream> streams;

    std::optional<QString> tag(const QString& key) const;
    QString describe() const;   // Kotlin's toString() of the data class, for failure messages
};

QString describe(const QList<ProbedStream>& streams);

class Ffprobe {
public:
    Ffprobe(QString ffprobe, process::ProcessRunner& runner) : m_ffprobe(std::move(ffprobe)), m_runner(runner) {}

    /// Throws std::runtime_error when ffprobe does not exit with 0 (Kotlin's check) or its output is not what is expected.
    Probed probe(const QString& file, std::stop_token stop) const;

    /// ffprobe sits next to ffmpeg (`ffmpeg.exe` -> `ffprobe.exe`); nothing when it is not there.
    static std::optional<QString> besides(const std::optional<QString>& ffmpeg);

private:
    QString m_ffprobe;
    process::ProcessRunner& m_runner;
};

}  // namespace xgs::integration
