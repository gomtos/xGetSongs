#pragma once

#include <QString>

#include <optional>

// Port of shared/.../input/InputClassifier.kt: what a user typed (a playlist ID, a video ID or a YouTube address) reduced to a
// YouTube ID, or the reason it is rejected. The Kotlin code is the ground truth; qt/tools/differential (kind C) compares them.
namespace xgs {

enum class RejectReason { Empty, UnsupportedHost, MixPlaylist, Unrecognized };

QString rejectMessage(RejectReason reason);          // Kotlin RejectReason.message, letter for letter
QString rejectName(RejectReason reason);             // "EMPTY" "UNSUPPORTED_HOST" "MIX_PLAYLIST" "UNRECOGNIZED"

struct ParsedInput {
    enum class Kind { Playlist, Video };
    Kind kind;
    QString id;
    std::optional<QString> alsoVideoId;              // only a playlist can have one
    QString canonicalUrl() const;                    // https://www.youtube.com/playlist?list=<id> or /watch?v=<id>
    bool operator==(const ParsedInput&) const = default;
};

struct ClassifyResult {
    std::optional<ParsedInput> input;                // present on success
    RejectReason reason = RejectReason::Empty;       // meaningful only when there is no input
    bool ok() const { return input.has_value(); }
};

ClassifyResult classifyInput(const QString& raw);

}  // namespace xgs
