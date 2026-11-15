#pragma once

#include <QString>

#include <optional>

// Port of shared/.../filename/FilenameFormatter.kt: `{rank 3 digits} {artist} - {title}.m4a` file names (the rank is
// optional) and playlist folder names that are safe on Windows. Lengths are UTF-16 units, as in Kotlin.
namespace xgs::filename {

inline constexpr int MinRank = 1;
inline constexpr int MaxRank = 999;
inline constexpr int MaxBaseLength = 180;     // the file name without extension
inline constexpr int MaxArtistLength = 80;
inline constexpr int MaxFolderLength = 80;    // before a reserved-name `_` is added

// Folder name used when a playlist has no usable title ("재생목록").
QString untitledPlaylist();

// `{rank} {artist} - {title}.m4a`, or `{artist} - {title}.m4a` when includeRank is false. rank outside MinRank..MaxRank
// throws std::invalid_argument, even when it is not printed.
QString format(int rank, const QString& artist, const QString& title, bool includeRank = true);

// The folder name of a playlist titled `title`; untitledPlaylist() for null, blank or an empty result.
QString folderName(const std::optional<QString>& title);

// The folder a job saves into, or nullopt for the output folder itself. A typed name that is not blank wins; otherwise a
// playlist gets a folder named after playlistTitle and a single video gets none.
std::optional<QString> destinationFolder(bool isPlaylist, const std::optional<QString>& playlistTitle,
                                         const std::optional<QString>& typed);

// Replaces the nine forbidden characters with full-width look-alikes, drops U+0000..001F and U+007F, then trims.
QString sanitize(const QString& text);

}  // namespace xgs::filename
