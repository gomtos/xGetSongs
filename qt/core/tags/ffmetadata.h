#pragma once

#include <QString>

#include "tags/track_tags.h"

// Port of engine/.../tags/Ffmetadata.kt: renders TrackTags as an ffmetadata file (`-f ffmetadata`), so tag text reaches ffmpeg
// through a file and never through a command line.
namespace xgs::tags {

// The text to write as UTF-8. A value loses every control character (U+0000..U+001F) but tab, LF and CR; its trailing
// backslashes become U+FF3C (ffmpeg joins a line that ends in a backslash with the next one, even an escaped one); then `=`, `;`,
// `#`, `\`, LF and CR get a backslash in front. An absent or blank album, comment or lyrics writes no entry.
QString renderFfmetadata(const TrackTags& tags);

}  // namespace xgs::tags
