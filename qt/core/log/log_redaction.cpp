#include "log/log_redaction.h"

#include <QSet>
#include <QStringView>

#include <algorithm>

#include "text/kt_text.h"

namespace xgs::redaction {

namespace {

namespace kt = xgs::kt;

// Names shorter than this are not replaced: they would hit unrelated text.
constexpr qsizetype kMinNameLength = 3;

// The characters Windows does not allow in a file name, and the line breaks, end a part of a path.
constexpr QStringView kForbiddenInPart = u"\\/:*?\"<>|\r\n";

// What a path leaves at its end when it is followed by a list: `C:\a\b.mp3, D:\c\d.mp3`. File names cannot end in a space.
constexpr QStringView kListPunctuation = u" ,;";

QString bound(const QString& text) {
    if (text.size() <= MaxTextLength) return text;
    // Never end on the first half of a surrogate pair.
    const qsizetype end = kt::isHighSurrogate(text[MaxTextLength - 1]) ? MaxTextLength - 1 : MaxTextLength;
    return text.left(end) + u'…';
}

bool isAsciiLetter(QChar c) {
    const char16_t u = c.unicode();
    return (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z');
}

bool isAsciiDigit(QChar c) {
    return c.unicode() >= u'0' && c.unicode() <= u'9';
}

bool isSeparator(QChar c) {
    return c == u'\\' || c == u'/';
}

// `C:` followed by a separator, starting at p.
bool isDriveStart(const QString& t, qsizetype p) {
    return p + 2 < t.size() && isAsciiLetter(t[p]) && t[p + 1] == u':' && isSeparator(t[p + 2]);
}

// The end of the part that starts at p (p itself when it is empty).
qsizetype partEnd(const QString& t, qsizetype p) {
    qsizetype q = p;
    while (q < t.size()) {
        const QChar c = t[q];
        if (kForbiddenInPart.contains(c)) break;
        if (c == u' ' && kt::startsWith(t, u" ->", q)) break;
        if (isAsciiLetter(c) && isDriveStart(t, q)) break;
        ++q;
    }
    return q;
}

qsizetype hostEnd(const QString& t, qsizetype p) {
    const qsizetype end = partEnd(t, p);
    return end > p ? end : -1;
}

// After `\\`: `?\` or `.\` and then a drive or `UNC\host`, or a host. The index where the parts start, or -1.
qsizetype uncPrefixEnd(const QString& t, qsizetype p) {
    if (p + 1 < t.size() && (t[p] == u'?' || t[p] == u'.') && t[p + 1] == u'\\') {
        const qsizetype q = p + 2;
        if (kt::regionMatches(t, q, u"UNC\\", kt::CaseMode::Ignore)) return hostEnd(t, q + 4);
        if (isDriveStart(t, q)) return q + 2;
        return -1;
    }
    return hostEnd(t, p);
}

// The end of the last of the `\part` pieces that follow start; -1 when there is none.
qsizetype partsEnd(const QString& t, qsizetype start) {
    qsizetype p = start;
    qsizetype end = -1;
    while (p < t.size() && isSeparator(t[p])) {
        qsizetype q = p + 1;
        if (q < t.size() && isSeparator(t[q])) ++q;
        const qsizetype e = partEnd(t, q);
        if (e == q) break;
        end = e;
        p = e;
    }
    return end;
}

// The index after the path that starts at i, or -1 when no path starts there.
qsizetype pathEnd(const QString& t, qsizetype i, bool mayFollowLetter) {
    const QChar first = t[i];
    qsizetype afterPrefix = -1;
    if ((first == u'f' || first == u'F') && kt::regionMatches(t, i, u"file:///", kt::CaseMode::Ignore)) {
        if (!isDriveStart(t, i + 8)) return -1;
        afterPrefix = i + 10;
    } else if (isAsciiLetter(first)) {
        if (!isDriveStart(t, i)) return -1;
        // `https://...` is not a drive: a letter that belongs to a word is no drive (unless a path just ended in it).
        if (i > 0 && !mayFollowLetter && (isAsciiLetter(t[i - 1]) || isAsciiDigit(t[i - 1]))) return -1;
        afterPrefix = i + 2;
    } else if (first == u'\\' && kt::startsWith(t, u"\\\\", i)) {
        afterPrefix = uncPrefixEnd(t, i + 2);
        if (afterPrefix < 0) return -1;
    } else {
        return -1;
    }
    return partsEnd(t, afterPrefix);
}

}  // namespace

QString pathPlaceholder() {
    return QStringLiteral(u"<경로>");
}

QString fileNamePlaceholder() {
    return QStringLiteral(u"<파일명>");
}

QString redactPaths(const QString& text) {
    const QString bounded = bound(text);
    const QString placeholder = pathPlaceholder();
    QString out;
    out.reserve(bounded.size());
    qsizetype i = 0;
    qsizetype lastEnd = -1;
    while (i < bounded.size()) {
        const qsizetype end = pathEnd(bounded, i, i == lastEnd);
        if (end < 0) {
            out += bounded[i];
            ++i;
            continue;
        }
        qsizetype keep = end;
        while (keep > i && kListPunctuation.contains(bounded[keep - 1])) --keep;
        out += placeholder;
        out += QStringView(bounded).sliced(keep, end - keep);
        i = end;
        lastEnd = end;
    }
    return out;
}

QString redact(const QString& text, const QStringList& knownNames) {
    QString result = redactPaths(text);
    // flatMap { listOf(it, it.substringBeforeLast('.')) }
    QStringList candidates;
    candidates.reserve(knownNames.size() * 2);
    for (const QString& name : knownNames) {
        candidates.append(name);
        const qsizetype dot = name.lastIndexOf(u'.');
        candidates.append(dot < 0 ? name : name.left(dot));
    }
    // filter { isNotBlank() && length >= 3 }, then distinct() (first occurrence order)
    QStringList names;
    QSet<QString> seen;
    for (const QString& name : candidates) {
        if (kt::isBlank(name) || name.size() < kMinNameLength) continue;
        if (seen.contains(name)) continue;
        seen.insert(name);
        names.append(name);
    }
    // sortedByDescending { length } (stable)
    std::stable_sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    const QString placeholder = fileNamePlaceholder();
    for (const QString& name : names) result = kt::replace(result, name, placeholder, kt::CaseMode::Ignore);
    return result;
}

}  // namespace xgs::redaction
