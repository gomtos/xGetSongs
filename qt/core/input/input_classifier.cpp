#include "input/input_classifier.h"

#include <QHash>
#include <QStringList>

#include "text/kt_text.h"

namespace xgs {

namespace {

namespace kt = xgs::kt;

constexpr QStringView kPlaylistPrefixes[] = {u"PL", u"UU", u"LL", u"FL", u"OL"};
constexpr QStringView kMixPrefix = u"RD";
constexpr qsizetype kMinPlaylistIdLength = 12;
constexpr qsizetype kVideoIdLength = 11;
constexpr QStringView kAllowedHosts[] = {u"youtube.com", u"www.youtube.com", u"m.youtube.com", u"music.youtube.com", u"youtu.be"};
constexpr QStringView kVideoPathPrefixes[] = {u"shorts", u"embed", u"live"};

ClassifyResult ok(ParsedInput input) {
    ClassifyResult result;
    result.input = std::move(input);
    return result;
}

ClassifyResult rejected(RejectReason reason) {
    ClassifyResult result;
    result.reason = reason;
    return result;
}

ParsedInput playlist(const QString& id, std::optional<QString> alsoVideoId = std::nullopt) {
    return ParsedInput{ParsedInput::Kind::Playlist, id, std::move(alsoVideoId)};
}

ParsedInput video(const QString& id) {
    return ParsedInput{ParsedInput::Kind::Video, id, std::nullopt};
}

// ASCII letters and digits (code below 128), '_' and '-'; not empty.
bool isIdChars(const QString& s) {
    if (s.isEmpty()) return false;
    for (const QChar c : s) {
        const bool asciiLetterOrDigit = kt::isLetterOrDigit(c) && c.unicode() < 128;
        if (!(asciiLetterOrDigit || c == u'_' || c == u'-')) return false;
    }
    return true;
}

bool isPlaylistId(const QString& s) {
    if (s.size() < kMinPlaylistIdLength || !isIdChars(s)) return false;
    for (const QStringView prefix : kPlaylistPrefixes) {
        if (kt::startsWith(s, prefix)) return true;
    }
    return false;
}

bool isMixId(const QString& s) {
    return s.size() >= kMinPlaylistIdLength && isIdChars(s) && kt::startsWith(s, kMixPrefix);
}

bool isVideoId(const QString& s) {
    return s.size() == kVideoIdLength && isIdChars(s);
}

ClassifyResult classifyBareId(const QString& text) {
    if (isPlaylistId(text)) return ok(playlist(text));
    if (isMixId(text)) return rejected(RejectReason::MixPlaylist);
    if (isVideoId(text)) return ok(video(text));
    return rejected(RejectReason::Unrecognized);
}

// Kotlin's substringBefore/substringAfter with a one-unit delimiter: the first occurrence. Without the delimiter Kotlin
// returns the whole string; every caller below either wants that or has checked for the delimiter first.
QString substringBefore(const QString& s, QChar delimiter) {
    const qsizetype at = s.indexOf(delimiter);
    return at < 0 ? s : s.left(at);
}

QString substringAfter(const QString& s, QChar delimiter) {
    const qsizetype at = s.indexOf(delimiter);
    return at < 0 ? s : s.mid(at + 1);
}

ClassifyResult classifyUrl(const QString& text) {
    // substringAfter("://", missingDelimiterValue = text)
    const qsizetype schemeAt = text.indexOf(u"://");
    const QString afterScheme = schemeAt < 0 ? text : text.mid(schemeAt + 3);
    qsizetype hostEnd = -1;
    for (qsizetype i = 0; i < afterScheme.size(); ++i) {
        const QChar c = afterScheme.at(i);
        if (c == u'/' || c == u'?' || c == u'#') {
            hostEnd = i;
            break;
        }
    }
    const QString host = kt::lowercase(hostEnd < 0 ? afterScheme : afterScheme.left(hostEnd));
    bool allowed = false;
    for (const QStringView candidate : kAllowedHosts) allowed = allowed || host == candidate;
    if (!allowed) return rejected(RejectReason::UnsupportedHost);

    const QString rest = hostEnd < 0 ? QString() : afterScheme.mid(hostEnd);
    const QString beforeFragment = substringBefore(rest, u'#');
    const QString path = substringBefore(beforeFragment, u'?');
    const QString query = beforeFragment.contains(u'?') ? substringAfter(beforeFragment, u'?') : QString();
    // Only the pieces with '=' count; a repeated key keeps its last value (Kotlin associate).
    QHash<QString, QString> params;
    for (const QString& piece : query.split(u'&')) {
        if (!piece.contains(u'=')) continue;
        params.insert(substringBefore(piece, u'='), substringAfter(piece, u'='));
    }

    QStringList segments;
    for (const QString& segment : path.split(u'/')) {
        if (!segment.isEmpty()) segments.append(segment);
    }
    bool videoPrefixFirst = false;
    if (!segments.isEmpty()) {
        for (const QStringView prefix : kVideoPathPrefixes) videoPrefixFirst = videoPrefixFirst || segments.first() == prefix;
    }
    std::optional<QString> videoCandidate;
    if (host == u"youtu.be") {
        if (!segments.isEmpty()) videoCandidate = segments.first();
    } else if (videoPrefixFirst) {
        if (segments.size() > 1) videoCandidate = segments.at(1);
    } else if (params.contains(QStringLiteral("v"))) {
        videoCandidate = params.value(QStringLiteral("v"));
    }
    std::optional<QString> videoId;
    if (videoCandidate && isVideoId(*videoCandidate)) videoId = *videoCandidate;
    std::optional<QString> listId;
    if (params.contains(QStringLiteral("list"))) listId = params.value(QStringLiteral("list"));

    if (listId && isPlaylistId(*listId)) return ok(playlist(*listId, videoId));
    if (listId && isMixId(*listId)) {
        if (videoId) return ok(video(*videoId));
        return rejected(RejectReason::MixPlaylist);
    }
    if (videoId) return ok(video(*videoId));
    return rejected(RejectReason::Unrecognized);
}

}  // namespace

QString rejectMessage(RejectReason reason) {
    switch (reason) {
        case RejectReason::Empty: return QStringLiteral(u"재생목록 ID 또는 영상 주소를 입력하세요.");
        case RejectReason::UnsupportedHost: return QStringLiteral(u"YouTube 주소만 사용할 수 있습니다.");
        case RejectReason::MixPlaylist: return QStringLiteral(u"자동 생성 믹스 목록은 지원하지 않습니다.");
        case RejectReason::Unrecognized: return QStringLiteral(u"재생목록 ID 또는 영상 주소로 인식할 수 없습니다.");
    }
    return QString();
}

QString rejectName(RejectReason reason) {
    switch (reason) {
        case RejectReason::Empty: return QStringLiteral("EMPTY");
        case RejectReason::UnsupportedHost: return QStringLiteral("UNSUPPORTED_HOST");
        case RejectReason::MixPlaylist: return QStringLiteral("MIX_PLAYLIST");
        case RejectReason::Unrecognized: return QStringLiteral("UNRECOGNIZED");
    }
    return QString();
}

QString ParsedInput::canonicalUrl() const {
    return (kind == Kind::Playlist ? QStringLiteral("https://www.youtube.com/playlist?list=")
                                   : QStringLiteral("https://www.youtube.com/watch?v=")) + id;
}

ClassifyResult classifyInput(const QString& raw) {
    const QString text = kt::trim(raw);
    if (text.isEmpty()) return rejected(RejectReason::Empty);
    if (isIdChars(text)) return classifyBareId(text);
    if (!text.contains(u'.') && !text.contains(u'/')) return rejected(RejectReason::Unrecognized);
    return classifyUrl(text);
}

}  // namespace xgs
