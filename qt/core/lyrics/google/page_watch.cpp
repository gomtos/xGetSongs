#include "lyrics/google/page_watch.h"

#include <utility>
#include <variant>

#include "json/json.h"
#include "lyrics/google/page_classifier.h"

using namespace Qt::StringLiterals;

namespace xgs::lyrics::google {

namespace {

// Kotlin's `Json { ignoreUnknownKeys = true }.decodeFromString<PageProbe>(text)`, as the Q3 Task 5 differential (kind P op pj)
// observed it. xgs::json::parse with a ClassShape of no properties checks the text as decodeFromString reads a class
// (json.h, ParseOptions::typed: white space, the root object, keys, `,` or the next key's `"` after a value, the unchecked
// skipping of other keys' values, nothing after the object). ProbeReader then goes over the checked text once more for the
// four properties, which ClassShape cannot describe (non-null strings and a Boolean):
//   - url, readyState, bodyText take a string only (`null`, any other literal, a container: no probe);
//   - hasCard takes `true` or `false` in any ASCII case, as a literal or as a string (escapes decoded): `TRUE`, `"True"` and
//     `"true"` with its t written as a \u escape are true; `1`, `null`, `"true "`, `falſe` (U+017F) are no probe;
//   - every occurrence of a repeated property must be of the right type, and the last one counts;
//   - a missing property is no probe.
class ProbeReader {
public:
    explicit ProbeReader(const QString& text) : m_text(text) {}

    // The probe of a text that xgs::json::parse has accepted as a class.
    std::optional<PageProbe> read() {
        std::optional<QString> url, readyState, bodyText;
        std::optional<bool> hasCard;
        skipWhite();
        ++m_at;   // {
        skipWhite();
        if (peek() == u'}') return std::nullopt;   // no properties
        while (true) {
            const QString key = readString();
            skipWhite();
            ++m_at;   // :
            skipWhite();
            if (key == u"url"_s || key == u"readyState"_s || key == u"bodyText"_s) {
                if (peek() != u'"') return std::nullopt;
                const QString value = readString();
                (key == u"url"_s ? url : key == u"readyState"_s ? readyState : bodyText) = value;
            } else if (key == u"hasCard"_s) {
                const QChar first = peek();
                if (first == u'{' || first == u'[') return std::nullopt;
                const QString value = first == u'"' ? readString() : readLiteral();
                if (asciiEqualsIgnoreCase(value, u"true")) hasCard = true;
                else if (asciiEqualsIgnoreCase(value, u"false")) hasCard = false;
                else return std::nullopt;
            } else {
                skipValue();
            }
            skipWhite();
            if (peek() == u',') {
                ++m_at;
                skipWhite();
            } else if (peek() == u'}') {
                break;
            }   // else the `"` of the next key
        }
        if (!url || !readyState || !hasCard || !bodyText) return std::nullopt;
        return PageProbe{std::move(*url), std::move(*readyState), *hasCard, std::move(*bodyText)};
    }

private:
    static bool asciiEqualsIgnoreCase(const QString& value, QStringView word) {
        if (value.size() != word.size()) return false;
        for (qsizetype k = 0; k < value.size(); ++k) {
            char16_t unit = value.at(k).unicode();
            if (unit >= u'A' && unit <= u'Z') unit = char16_t(unit - u'A' + u'a');
            if (unit != word.at(k).unicode()) return false;
        }
        return true;
    }

    static bool isWhite(QChar c) { return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r'; }

    // json.h: a unit that cannot be part of an unquoted literal
    static bool endsLiteral(QChar c) {
        return c.unicode() <= 0x20 || c == u'"' || c == u',' || c == u':' || c == u'[' || c == u']' || c == u'{' || c == u'}' ||
               c == u'\\';
    }

    QChar peek() const { return m_at < m_text.size() ? m_text.at(m_at) : QChar(); }

    void skipWhite() {
        while (m_at < m_text.size() && isWhite(m_text.at(m_at))) ++m_at;
    }

    QString readLiteral() {
        const qsizetype start = m_at;
        while (m_at < m_text.size() && !endsLiteral(m_text.at(m_at))) ++m_at;
        return m_text.sliced(start, m_at - start);
    }

    // The string at the `"` at m_at (checked by json::parse: closed, with valid escapes), its escapes decoded.
    QString readString() {
        QString out;
        ++m_at;
        while (m_at < m_text.size() && m_text.at(m_at) != u'"') {
            const QChar c = m_text.at(m_at++);
            if (c != u'\\') {
                out += c;
                continue;
            }
            const QChar e = m_text.at(m_at++);
            switch (e.unicode()) {
            case u'b': out += u'\b'; break;
            case u'f': out += u'\f'; break;
            case u'n': out += u'\n'; break;
            case u'r': out += u'\r'; break;
            case u't': out += u'\t'; break;
            case u'u': {
                char16_t unit = 0;
                for (int k = 0; k < 4; ++k) {
                    const char16_t digit = m_text.at(m_at++).unicode();
                    unit = char16_t(unit * 16 + (digit <= u'9' ? digit - u'0' : (digit | 0x20) - u'a' + 10));
                }
                out += QChar(unit);
                break;
            }
            default: out += e; break;   // \" \\ \/
            }
        }
        ++m_at;   // "
        return out;
    }

    // The value of another key (checked by json::parse): a string, a literal, or a container up to its closing bracket.
    void skipValue() {
        if (peek() == u'"') {
            readString();
            return;
        }
        if (peek() != u'{' && peek() != u'[') {
            readLiteral();
            return;
        }
        int depth = 0;
        while (m_at < m_text.size()) {
            const QChar c = m_text.at(m_at);
            if (c == u'"') {
                readString();
                continue;
            }
            ++m_at;
            if (c == u'{' || c == u'[') ++depth;
            else if ((c == u'}' || c == u']') && --depth == 0) return;
        }
    }

    const QString& m_text;
    qsizetype m_at = 0;
};

}  // namespace

std::optional<PageProbe> probeFromJson(const QString& text) {
    static const json::ClassShape noProperties;
    json::ParseOptions options;
    options.typed = &noProperties;
    if (!json::parse(text, options)) return std::nullopt;
    return ProbeReader(text).read();
}

PageWatch::PageWatch(std::chrono::nanoseconds settleTime, Clock clock) : m_settleTime(settleTime), m_clock(std::move(clock)) {}

std::chrono::nanoseconds PageWatch::now() const {
    if (m_clock) return m_clock();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

bool PageWatch::isDone(const PageProbe& probe) {
    if (probe.hasCard) return true;
    if (blockOf(probe.url, std::nullopt, probe.bodyText)) return true;
    if (probe.readyState != u"complete"_s) {
        m_completeSince.reset();
        return false;
    }
    if (probe.url != m_watchedUrl) {
        m_watchedUrl = probe.url;
        m_completeSince.reset();
    }
    if (!m_completeSince) m_completeSince = now();
    return now() - *m_completeSince >= m_settleTime;
}

}  // namespace xgs::lyrics::google
