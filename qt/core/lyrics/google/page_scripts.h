#pragma once

#include <QString>

// The scripts DrivenBrowser runs in the page.
namespace xgs::lyrics::google {

/// The text of qt/core/lyrics/google/lyrics_card.js exactly as it is in the repository (embedded at build time). Its answer is
/// what parseCard (lyrics_card.h) reads.
QString cardScript();

/// One look at the page, as DrivenBrowser.kt's PROBE_SCRIPT (the same text): where it is (`url`), how far it is
/// (`readyState`), whether a lyrics card shows (`hasCard`, CardPresence) and the first 4000 units of its visible text
/// (`bodyText`). The answer is what probeFromJson (page_watch.h) reads.
QString probeScript();

}  // namespace xgs::lyrics::google
