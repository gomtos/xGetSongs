#include "paths/paths.h"

#include <QStringList>
#include <QStringView>

// The rules are in paths.h; each was observed through the differential oracle (kind W) and the observation table of the Q2b
// Task 1 report.
namespace xgs::paths {

namespace {

using namespace Qt::StringLiterals;

enum class RootKind {
    None,            // a relative path: `a\b`
    DriveRelative,   // `C:` and maybe parts: `C:a\b`
    DriveAbsolute,   // `C:\`
    Separator,       // `\`: the root of the current drive
    Unc,             // `\\host\share\`
};

struct Parsed {
    RootKind kind = RootKind::None;
    QString root;        // as written in the string: `C:`, `C:\`, `\`, `\\host\share\`, or empty
    QStringList parts;   // never empty strings

    QString text() const { return root + parts.join(u'\\'); }
};

bool isSeparator(QChar c) {
    return c == u'\\' || c == u'/';
}

bool isAsciiAlpha(QChar c) {
    const char16_t unit = c.unicode();
    return (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z');
}

// A unit that Path.of refuses in a part, a host or a share.
bool isForbidden(QChar c) {
    const char16_t unit = c.unicode();
    return unit < 0x20 || unit == u'<' || unit == u'>' || unit == u':' || unit == u'"' || unit == u'|' || unit == u'?' || unit == u'*';
}

bool isValidName(QStringView name) {
    for (const QChar c : name)
        if (isForbidden(c)) return false;
    return true;
}

// The parts of `rest` between runs of separators, each checked. False when one holds a forbidden unit.
bool splitParts(QStringView rest, QStringList& parts) {
    qsizetype at = 0;
    while (at < rest.size()) {
        while (at < rest.size() && isSeparator(rest[at])) ++at;
        const qsizetype start = at;
        while (at < rest.size() && !isSeparator(rest[at])) ++at;
        if (at == start) break;
        const QStringView part = rest.sliced(start, at - start);
        if (!isValidName(part)) return false;
        parts.append(part.toString());
    }
    return true;
}

// `rest` is what follows the separators that start a UNC path: a host, separators, a share, then parts.
std::optional<Parsed> parseUnc(QStringView rest) {
    qsizetype at = 0;
    while (at < rest.size() && isSeparator(rest[at])) ++at;
    const qsizetype hostStart = at;
    while (at < rest.size() && !isSeparator(rest[at])) ++at;
    const QStringView host = rest.sliced(hostStart, at - hostStart);
    if (host.isEmpty() || at == rest.size()) return std::nullopt;
    while (at < rest.size() && isSeparator(rest[at])) ++at;
    const qsizetype shareStart = at;
    while (at < rest.size() && !isSeparator(rest[at])) ++at;
    const QStringView share = rest.sliced(shareStart, at - shareStart);
    if (share.isEmpty() || !isValidName(host) || !isValidName(share)) return std::nullopt;
    Parsed parsed;
    parsed.kind = RootKind::Unc;
    parsed.root = u"\\\\"_s + host + u'\\' + share + u'\\';
    if (!splitParts(rest.sliced(at), parsed.parts)) return std::nullopt;
    return parsed;
}

bool startsWithDrive(QStringView text) {
    return text.size() >= 2 && isAsciiAlpha(text[0]) && text[1] == u':';
}

std::optional<Parsed> parse(QStringView text) {
    if (text.startsWith(u"\\\\?\\")) {
        // the verbatim prefix, exactly: a drive and a separator, or `UNC\` and a UNC root, must follow
        const QStringView rest = text.sliced(4);
        if (rest.startsWith(u"UNC\\")) return parseUnc(rest.sliced(4));
        if (rest.size() >= 3 && startsWithDrive(rest) && isSeparator(rest[2])) return parse(rest);
        return std::nullopt;
    }
    if (text.size() >= 2 && isSeparator(text[0]) && isSeparator(text[1])) return parseUnc(text.sliced(2));
    Parsed parsed;
    qsizetype restStart = 0;
    if (startsWithDrive(text)) {
        if (text.size() > 2 && isSeparator(text[2])) {
            parsed.kind = RootKind::DriveAbsolute;
            parsed.root = text.first(2).toString() + u'\\';
            restStart = 3;
        } else {
            parsed.kind = RootKind::DriveRelative;
            parsed.root = text.first(2).toString();
            restStart = 2;
        }
    } else if (!text.isEmpty() && isSeparator(text[0])) {
        parsed.kind = RootKind::Separator;
        parsed.root = u"\\"_s;
        restStart = 1;
    }
    if (!splitParts(text.sliced(restStart), parsed.parts)) return std::nullopt;
    return parsed;
}

bool sameDrive(QChar a, QChar b) {
    const char16_t x = a.unicode() | 0x20;   // both are ASCII letters
    const char16_t y = b.unicode() | 0x20;
    return x == y;
}

// dir and then name, with a `\` between unless dir ends with its root's `\` or is a drive-relative root alone (`C:`).
QString joined(const QString& dir, const QString& name) {
    if (dir.endsWith(u'\\') || dir.endsWith(u':')) return dir + name;
    return dir + u'\\' + name;
}

QString resolved(const Parsed& dir, const Parsed& name) {
    const QString dirText = dir.text();
    const QString nameText = name.text();
    if (nameText.isEmpty()) return dirText;
    if (dirText.isEmpty()) return nameText;
    switch (name.kind) {
    case RootKind::DriveAbsolute:
    case RootKind::Unc:
        return nameText;
    case RootKind::Separator:
        // the directory's drive or UNC root, then the name from its `\`
        if (dir.kind == RootKind::DriveAbsolute || dir.kind == RootKind::DriveRelative) return dir.root.first(2) + nameText;
        if (dir.kind == RootKind::Unc) return dir.root.chopped(1) + nameText;
        return nameText;
    case RootKind::DriveRelative:
        // continues an absolute directory on the same drive (the rest of the name may be empty: `C:\a` + `C:` is `C:\a\`)
        if (dir.kind == RootKind::DriveAbsolute && sameDrive(dir.root[0], name.root[0])) return joined(dirText, name.parts.join(u'\\'));
        return nameText;
    case RootKind::None:
        break;
    }
    return joined(dirText, nameText);
}

}  // namespace

std::optional<QString> fromText(const QString& text) {
    const std::optional<Parsed> parsed = parse(text);
    if (!parsed) return std::nullopt;
    return parsed->text();
}

std::optional<QString> child(const QString& dir, const QString& name) {
    const std::optional<Parsed> parent = parse(dir);
    const std::optional<Parsed> other = parse(name);
    if (!parent || !other) return std::nullopt;
    return resolved(*parent, *other);
}

std::optional<QString> fileName(const QString& path) {
    const std::optional<Parsed> parsed = parse(path);
    if (!parsed) return std::nullopt;
    if (parsed->parts.isEmpty()) {
        if (parsed->kind == RootKind::None) return QString();   // the empty path is its own file name
        return std::nullopt;
    }
    return parsed->parts.last();
}

QString nameWithoutExtension(const QString& path) {
    const QString name = fileName(path).value_or(QString());
    const qsizetype dot = name.lastIndexOf(u'.');
    return dot < 0 ? name : name.first(dot);
}

std::optional<QString> sibling(const QString& path, const QString& name) {
    const std::optional<Parsed> parsed = parse(path);
    const std::optional<Parsed> other = parse(name);
    if (!parsed || !other) return std::nullopt;
    // no parent: a root alone, or a single part without a root
    if (parsed->parts.isEmpty() || (parsed->parts.size() == 1 && parsed->kind == RootKind::None)) return other->text();
    Parsed parent = *parsed;
    parent.parts.removeLast();
    return resolved(parent, *other);
}

}  // namespace xgs::paths
