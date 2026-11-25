#include "lyrics/google/page_scripts.h"

#include "lyrics/google/selectors.h"

// kLyricsCardJs: the bytes of lyrics_card.js, written into the build folder by qt/core/CMakeLists.txt when CMake runs (and
// again whenever the file changes).
#include "lyrics_card_js.inc"

using namespace Qt::StringLiterals;

namespace xgs::lyrics::google {

namespace {

// JsonPrimitive(text).toString() for a text without characters that need an escape other than `"` and `\`.
QString quoted(QStringView text) {
    QString out = u"\""_s;
    for (const QChar c : text) {
        if (c == u'"' || c == u'\\') out += u'\\';
        out += c;
    }
    return out + u'"';
}

}  // namespace

QString cardScript() {
    static const QString script = QString::fromUtf8(reinterpret_cast<const char*>(kLyricsCardJs), qsizetype(sizeof(kLyricsCardJs)));
    return script;
}

QString probeScript() {
    // The text of Kotlin's PROBE_SCRIPT after trimIndent().
    static const QString script =
        u"(function () {\n"
        u"  var text = document.body ? document.body.innerText : '';\n"
        u"  return JSON.stringify({\n"
        u"    url: location.href,\n"
        u"    readyState: document.readyState,\n"
        u"    hasCard: !!document.querySelector("_s
        + quoted(CardPresence)
        + u"),\n"
          u"    bodyText: text.length > 4000 ? text.substring(0, 4000) : text\n"
          u"  });\n"
          u"})()"_s;
    return script;
}

}  // namespace xgs::lyrics::google
