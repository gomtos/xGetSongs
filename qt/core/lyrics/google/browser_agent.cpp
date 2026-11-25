#include "lyrics/google/browser_agent.h"

#include <utility>
#include <vector>

#include "json/json.h"

using namespace Qt::StringLiterals;

namespace xgs::lyrics::google {

namespace {

// Rust's allowed_url refuses an address holding a char for which `char::is_control()` or `char::is_whitespace()` is true.
// is_control is the general category Cc: U+0000..U+001F and U+007F..U+009F. is_whitespace is the Unicode property White_Space:
// U+0009..U+000D, U+0020, U+0085, U+00A0, U+1680, U+2000..U+200A, U+2028, U+2029, U+202F, U+205F, U+3000. The test below is
// those two lists written out (the White_Space list has not changed since Unicode 4.1; Rust's documentation names it), so it
// does not depend on Qt's or xgs::kt's Unicode tables. Every one of them is in the BMP, so looking at UTF-16 units is the same as
// looking at chars. tst_browser_agent's theControlAndWhitespaceSetIsRusts checks it against kt::isWhitespace (Kotlin's
// Char.isWhitespace: Zs, Zl, Zp, U+0009..U+000D, U+001C..U+001F) together with Cc: Zs = U+0020, U+00A0, U+1680,
// U+2000..U+200A, U+202F, U+205F, U+3000; Zl = U+2028; Zp = U+2029; so White_Space and Kotlin's set differ only in U+0085 and
// U+001C..U+001F, which are all Cc, and the two unions with Cc are the same set.
bool isRustControlOrWhitespace(char16_t unit) {
    if (unit <= 0x20 || (unit >= 0x7F && unit <= 0x9F)) return true;   // Cc, and U+0020 (U+0009..U+000D are Cc too)
    switch (unit) {
    case 0x00A0: case 0x1680: case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
        return true;
    default:
        return unit >= 0x2000 && unit <= 0x200A;
    }
}

bool isHighSurrogate(char16_t unit) {
    return unit >= 0xD800 && unit <= 0xDBFF;
}

bool isLowSurrogate(char16_t unit) {
    return unit >= 0xDC00 && unit <= 0xDFFF;
}

/// Whether text holds a surrogate that is not half of a pair (which a Rust string cannot hold).
bool hasUnpairedSurrogate(QStringView text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t unit = text[i].unicode();
        if (isHighSurrogate(unit) && i + 1 < text.size() && isLowSurrogate(text[i + 1].unicode())) {
            ++i;
        } else if (isHighSurrogate(unit) || isLowSurrogate(unit)) {
            return true;
        }
    }
    return false;
}

bool isJsonSpace(char16_t unit) {
    return unit == u' ' || unit == u'\t' || unit == u'\n' || unit == u'\r';
}

bool isAsciiDigit(char16_t unit) {
    return unit >= u'0' && unit <= u'9';
}

/// A literal of RFC 8259 JSON: true, false or a number (`-? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?`). `null` is
/// read as Null and never comes here.
bool isStrictLiteral(QStringView literal) {
    if (literal == u"true" || literal == u"false") return true;
    qsizetype i = 0;
    const qsizetype n = literal.size();
    auto digits = [&] {
        const qsizetype from = i;
        while (i < n && isAsciiDigit(literal[i].unicode())) ++i;
        return i - from;
    };
    if (i < n && literal[i] == u'-') ++i;
    if (i < n && literal[i] == u'0') ++i;
    else if (digits() == 0) return false;
    if (i < n && literal[i] == u'.') {
        ++i;
        if (digits() == 0) return false;
    }
    if (i < n && (literal[i] == u'e' || literal[i] == u'E')) {
        ++i;
        if (i < n && (literal[i] == u'+' || literal[i] == u'-')) ++i;
        if (digits() == 0) return false;
    }
    return i == n;
}

/// What xgs::json (Kotlin's grammar) accepts beyond RFC 8259 and serde_json, looked for in the text: a control character
/// (below U+0020) written as it is inside a string, and anything but `,`, `]`, `}` or the end after a closing `]` or `}`
/// (xgs::json takes `[1]x]` as [1, x]).
bool hasLooseText(QStringView text) {
    bool inString = false;
    bool escaped = false;
    bool afterClose = false;
    for (const QChar character : text) {
        const char16_t unit = character.unicode();
        if (inString) {
            if (unit < 0x20) return true;
            if (escaped) escaped = false;
            else if (unit == u'\\') escaped = true;
            else if (unit == u'"') inString = false;
            continue;
        }
        if (isJsonSpace(unit)) continue;
        if (afterClose && unit != u',' && unit != u']' && unit != u'}') return true;
        afterClose = unit == u']' || unit == u'}';
        if (unit == u'"') inString = true;
    }
    return false;
}

/// Whether the tree holds only what serde_json reads: strings (and keys) without unpaired surrogates and literals of RFC 8259.
/// Without recursion, like xgs::json itself.
bool isStrictTree(const json::Value& root) {
    std::vector<const json::Value*> pending{&root};
    while (!pending.empty()) {
        const json::Value* value = pending.back();
        pending.pop_back();
        if (const auto* primitive = std::get_if<json::Primitive>(&value->data)) {
            if (primitive->isString ? hasUnpairedSurrogate(primitive->content) : !isStrictLiteral(primitive->content)) return false;
        } else if (const auto* object = std::get_if<json::Object>(&value->data)) {
            for (const auto& [key, member] : *object) {
                if (hasUnpairedSurrogate(key)) return false;
                pending.push_back(&member);
            }
        } else if (const auto* array = std::get_if<json::Array>(&value->data)) {
            for (const json::Value& item : *array) pending.push_back(&item);
        }
    }
    return true;
}

BrowserAnswer failedAnswer(quint64 id, const QString& error) {
    BrowserAnswer answer;
    answer.id = id;
    answer.ok = false;
    answer.error = error;
    return answer;
}

}  // namespace

bool allowedUrl(QStringView url) {
    for (const QChar character : url) {
        if (isRustControlOrWhitespace(character.unicode())) return false;
    }
    if (hasUnpairedSurrogate(url)) return false;
    bool secure = false;
    QStringView rest;
    if (url.startsWith(u"https://")) {
        secure = true;
        rest = url.sliced(8);
    } else if (url.startsWith(u"http://")) {
        rest = url.sliced(7);
    } else {
        return false;
    }
    qsizetype end = 0;
    while (end < rest.size() && rest[end] != u'/' && rest[end] != u'?' && rest[end] != u'#') ++end;
    const QStringView authority = rest.first(end);
    if (authority.isEmpty() || authority.contains(u'@')) return false;
    if (secure) return true;
    QStringView host = authority;
    const qsizetype colon = authority.lastIndexOf(u':');
    if (colon >= 0) {
        const QStringView port = authority.sliced(colon + 1);
        bool digits = !port.isEmpty();
        for (const QChar character : port) digits = digits && isAsciiDigit(character.unicode());
        if (digits) host = authority.first(colon);
    }
    return host == u"127.0.0.1" || host == u"localhost";
}

bool allowedNavigation(QStringView url) {
    return url == u"about:blank" || allowedUrl(url);
}

// Rust read the text with serde_json::from_str (RFC 8259). Here xgs::json (Kotlin's grammar, which accepts more) reads it, and
// what it accepts beyond RFC 8259 is refused before (hasLooseText) and after (isStrictTree). Not followed: serde_json refuses a
// number too large for an f64 (`1e400`), and writes "any other value" anew (keys in sorted order, numbers in its own form) where
// this keeps the text as it came, white space inside it included (serde_json writes it without). serde_json also refuses
// nesting deeper than 128 levels, which xgs::json reads (up to its own limit of 1000 open arrays, objects without limit).
// WebView2's answer is JSON.stringify's output, which never holds such a number, and the scripts of this lookup always answer
// a string, so none of these shows.
std::optional<QString> decodeScriptResult(const QString& jsonText) {
    if (hasLooseText(jsonText)) return std::nullopt;
    const std::optional<json::Value> parsed = json::parse(jsonText);
    if (!parsed || !isStrictTree(*parsed)) return std::nullopt;
    if (std::holds_alternative<json::Null>(parsed->data)) return std::nullopt;
    if (const auto* primitive = std::get_if<json::Primitive>(&parsed->data); primitive && primitive->isString) {
        return primitive->content;
    }
    qsizetype from = 0;
    qsizetype to = jsonText.size();
    while (from < to && isJsonSpace(jsonText.at(from).unicode())) ++from;
    while (to > from && isJsonSpace(jsonText.at(to - 1).unicode())) --to;
    return jsonText.sliced(from, to - from);
}

BrowserAgent::BrowserAgent(Page& page, std::chrono::nanoseconds idleClose, Clock clock)
    : m_state(std::make_shared<State>()), m_idleClose(idleClose), m_clock(std::move(clock)), m_lastCommand(now()) {
    m_state->page = &page;
}

std::chrono::nanoseconds BrowserAgent::now() const {
    if (m_clock) return m_clock();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

void BrowserAgent::handle(const BrowserCommand& command, std::function<void(BrowserAnswer)> done) {
    using Type = BrowserCommand::Type;
    m_lastCommand = now();
    const quint64 id = command.id;
    const Type type = command.type;
    switch (type) {
    case Type::Load:
        if (!allowedUrl(command.text)) {
            done(failedAnswer(id, u"url not allowed"_s));
            return;
        }
        break;
    case Type::Evaluate:
    case Type::Reset:
        break;
    default:
        done(failedAnswer(id, u"unknown command"_s));
        return;
    }

    // The page's done, called inside the page call or later, once: a second call of it (a page that breaks its contract) is
    // ignored. It holds the state, not the agent, so that a late one finds what it needs even when the agent is gone.
    ++m_state->underway;
    auto answered = std::make_shared<bool>(false);
    std::function<void(PageOutcome)> finish = [state = m_state, answered, id, type, done = std::move(done)](PageOutcome outcome) {
        if (*answered) return;
        *answered = true;
        --state->underway;
        BrowserAnswer answer;
        answer.id = id;
        if (outcome.ok) {
            answer.ok = true;
            if (type == Type::Load) state->opened = true;
            if (type == Type::Evaluate) answer.value = decodeScriptResult(outcome.text);
        } else {
            state->page->close();
            state->opened = false;
            answer.error = outcome.text;
        }
        done(answer);
    };

    // A page must not throw; whatever does (std::bad_alloc) fails the command, unless the page has answered already.
    try {
        switch (type) {
        case Type::Load: m_state->page->load(command.text, finish); break;
        case Type::Evaluate: m_state->page->evaluate(command.text, finish); break;
        default: m_state->page->reset(finish); break;
        }
    } catch (...) {
        finish(PageOutcome{false, u"exception"_s});
    }
}

void BrowserAgent::idleTick() {
    if (m_state->opened && m_state->underway == 0 && now() - m_lastCommand >= m_idleClose) {
        m_state->page->close();
        m_state->opened = false;
    }
}

}  // namespace xgs::lyrics::google
