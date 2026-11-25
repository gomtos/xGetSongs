#include "lyrics/google/page_classifier.h"

#include "lyrics/google/selectors.h"
#include "text/kt_text.h"

namespace xgs::lyrics::google {

namespace {

constexpr int HttpTooManyRequests = 429;

template <std::size_t N>
bool containsAny(const QString& text, const std::array<QStringView, N>& needles) {
    for (const QStringView needle : needles)
        if (kt::contains(text, needle)) return true;
    return false;
}

}  // namespace

std::optional<GoogleLyricsResult::Kind> blockOf(const QString& url, std::optional<int> statusCode, const QString& bodyText) {
    using Kind = GoogleLyricsResult::Kind;
    const QString address = kt::lowercase(url);
    if (kt::contains(address, ConsentHost)) return Kind::Consent;
    if (kt::contains(address, SorryPath)) return Kind::Captcha;
    const QString text = bodyText.size() <= BlockPageMaxText ? kt::lowercase(bodyText) : QString();
    if (containsAny(text, UnusualTrafficTexts)) return Kind::Captcha;
    if (statusCode == HttpTooManyRequests || containsAny(text, TooManyRequestsTexts)) return Kind::RateLimited;
    return std::nullopt;
}

}  // namespace xgs::lyrics::google
