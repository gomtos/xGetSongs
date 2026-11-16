#pragma once

#include <QString>
#include <QStringList>

// Port of engine/.../ytdlp/ErrorClassifier.kt: maps yt-dlp's stderr to a Failure. The order of the checks and the user-facing
// Korean messages are Kotlin's. Lengths are UTF-16 units, as in Kotlin.
namespace xgs::ytdlp {

enum class FailureKind {
    Unavailable,   // the video cannot be downloaded at all (private, removed, geo-blocked...): skip it
    Transient,     // worth retrying (network hiccup, rate limit)
    Fatal,         // retrying or continuing is pointless (disk full, no permission): abort the job
    Other,
};

struct Failure {
    FailureKind kind;
    QString message;
    bool operator==(const Failure&) const = default;
};

Failure classifyError(const QStringList& stderrLines);

}  // namespace xgs::ytdlp
