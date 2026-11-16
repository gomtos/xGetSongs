#include "ytdlp/error_classifier.h"

#include <QStringView>

#include <algorithm>
#include <iterator>
#include <optional>

#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::ytdlp {

namespace {

struct Pattern {
    QStringView needle;
    QStringView message;
};

const Pattern kFatal[] = {
    {u"no space left on device", u"디스크 공간이 부족합니다."},
    {u"not enough space on the disk", u"디스크 공간이 부족합니다."},
    {u"disk quota exceeded", u"디스크 공간이 부족합니다."},
    {u"permission denied", u"출력 폴더에 쓸 권한이 없습니다."},
};

// A rate-limited session also says "Video unavailable", so this must be checked before kUnavailable.
const QStringView kRateLimited[] = {u"try again later", u"rate-limited", u"rate limited"};

const Pattern kUnavailable[] = {
    {u"private video", u"비공개 영상"},
    {u"sign in to confirm your age", u"연령 제한 영상"},
    {u"age-restricted", u"연령 제한 영상"},
    {u"available in your country", u"지역 제한 영상"},
    {u"blocked it in your country", u"지역 제한 영상"},
    {u"who has blocked it", u"지역 제한 영상"},
    {u"members-only", u"멤버 전용 영상"},
    {u"join this channel", u"멤버 전용 영상"},
    {u"copyright", u"저작권으로 차단된 영상"},
    {u"has been removed", u"삭제된 영상"},
    {u"account associated with this video has been terminated", u"삭제된 영상"},
    {u"video unavailable", u"사용할 수 없는 영상"},
    {u"this video is not available", u"사용할 수 없는 영상"},
};

const QStringView kTransient[] = {
    u"http error 429", u"http error 403", u"http error 500", u"http error 502", u"http error 503",
    u"http error 504", u"timed out", u"connection reset", u"connection aborted", u"remote end closed",
    u"incompleteread", u"temporary failure in name resolution", u"getaddrinfo failed",
    u"network is unreachable", u"unable to download",
};

const Pattern* firstMatch(const QString& text, const Pattern* begin, const Pattern* end) {
    const Pattern* found = std::find_if(begin, end, [&text](const Pattern& p) { return kt::contains(text, p.needle); });
    return found == end ? nullptr : found;
}

bool anyContained(const QString& text, const QStringView* begin, const QStringView* end) {
    return std::any_of(begin, end, [&text](QStringView needle) { return kt::contains(text, needle); });
}

QString summarize(const QStringList& stderrLines) {
    // lastOrNull { it.trimStart().startsWith("ERROR:") }?.trim()?.removePrefix("ERROR:")?.trim()
    std::optional<QString> error;
    for (auto it = stderrLines.crbegin(); it != stderrLines.crend(); ++it) {
        if (!kt::startsWith(kt::trimStart(*it), u"ERROR:")) continue;
        QString line = kt::trim(*it);
        if (kt::startsWith(line, u"ERROR:")) line = line.mid(6);
        error = kt::trim(line);
        break;
    }
    QString summary;
    if (error) {
        summary = *error;
    } else {
        // lastOrNull { it.isNotBlank() }?.trim() ?: "yt-dlp가 비정상 종료했습니다."
        bool found = false;
        for (auto it = stderrLines.crbegin(); it != stderrLines.crend(); ++it) {
            if (kt::isBlank(*it)) continue;
            summary = kt::trim(*it);
            found = true;
            break;
        }
        if (!found) summary = u"yt-dlp가 비정상 종료했습니다."_s;
    }
    return summary.left(300);   // take(300): by UTF-16 units, so a surrogate pair may be cut, as in Kotlin
}

}  // namespace

Failure classifyError(const QStringList& stderrLines) {
    const QString text = kt::lowercase(stderrLines.join(u'\n'));
    if (const Pattern* hit = firstMatch(text, std::begin(kFatal), std::end(kFatal)))
        return {FailureKind::Fatal, hit->message.toString()};
    if (anyContained(text, std::begin(kRateLimited), std::end(kRateLimited)))
        return {FailureKind::Transient, summarize(stderrLines)};
    if (const Pattern* hit = firstMatch(text, std::begin(kUnavailable), std::end(kUnavailable)))
        return {FailureKind::Unavailable, hit->message.toString()};
    const QString message = summarize(stderrLines);
    if (anyContained(text, std::begin(kTransient), std::end(kTransient))) return {FailureKind::Transient, message};
    // Not "unavailable": it can be a hiccup on YouTube's side, so the item fails and can be retried.
    if (kt::contains(text, u"requested format is not available")) return {FailureKind::Other, u"m4a 오디오 형식이 없습니다."_s};
    return {FailureKind::Other, message};
}

}  // namespace xgs::ytdlp
