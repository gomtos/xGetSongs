#pragma once

#include <QString>

#include <optional>

// Port of engine/.../tags/TrackTags.kt: the tag values of one track, as they are written into the m4a. They are the original
// text: unlike the file name nothing is sanitized or shortened.
namespace xgs::tags {

struct TrackTags {
    QString title;                        // the title (©nam)
    QString artist;                       // the artist (©ART)
    std::optional<QString> album;         // the album (©alb); absent (no tag) if none
    QString albumArtist;                  // the album artist (aART)
    int trackNumber = 0;                  // the track number (trkn): the playlist position, written without leading zeros
    std::optional<QString> comment;       // the comment (©cmt): the video URL. An absent or blank comment writes no tag
    std::optional<QString> lyrics;        // the lyrics (©lyr), lines separated by \n. An absent or blank text writes no tag
};

}  // namespace xgs::tags
