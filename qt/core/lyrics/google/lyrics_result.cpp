#include "lyrics/google/lyrics_result.h"

using namespace Qt::StringLiterals;

namespace xgs::lyrics::google {

QString GoogleLyricsResult::toString() const {
    switch (kind) {
        case Kind::Found:
            return u"Found(lines=%1, paragraphs=%2)"_s.arg(lineCount).arg(paragraphCount);
        case Kind::NoCard: return u"NoCard"_s;
        case Kind::ExtractionFailed: return u"ExtractionFailed"_s;
        case Kind::Captcha: return u"Captcha"_s;
        case Kind::Consent: return u"Consent"_s;
        case Kind::RateLimited: return u"RateLimited"_s;
        case Kind::Timeout: return u"Timeout"_s;
        case Kind::BrowserUnavailable: return u"BrowserUnavailable("_s + reason + u')';
        case Kind::CoolingDown: return u"CoolingDown"_s;
    }
    return QString();
}

}  // namespace xgs::lyrics::google
