#include "lyrics/google/lyrics_card.h"

#include <QList>
#include <QStringList>

#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "json/json.h"
#include "lyrics/lyrics_extractor.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

// The text of a line. Kotlin took `element.text().trim()` of the line element with Jsoup 1.23.2 (gradle/libs.versions.toml).
// What Element.text() does was read from the bytecode of that Jsoup version (MIT licence; `javap -c` on the classes
// Element, Element$TextAccumulator, TextNode and internal.StringUtil of jsoup-1.23.2.jar):
//   - it walks the element's descendants in document order with an accumulator, and returns accumulator.trim() (Java's
//     String.trim: every unit up to U+0020 off both ends);
//   - a text node outside <pre>-like elements is appended by StringUtil.appendNormalisedWhitespace(accum, text,
//     stripLeading = accum ends with ' '): per code point, a "whitespace" (isActuallyWhitespace: U+0020, U+0009, U+000A,
//     U+000C, U+000D, U+00A0) is dropped when stripLeading holds and nothing visible has been appended from this node yet,
//     or when the last unit this node appended was a space, and appended as one ' ' otherwise; an "invisible" character
//     (isInvisibleChar: U+200B, U+00AD) is dropped and changes nothing; anything else is appended as it is;
//   - a <br> (and a block or text-boundary element) appends ' ' when the accumulator is not empty and does not end with ' ';
//   - comments and the data of <script> and <style> are not text nodes and add nothing.
// Taken together, for text nodes and <br>s only: a whitespace unit is appended as ' ' unless the accumulator already ends with
// ' ' (it can only end with one that a whitespace or a <br> put there), an invisible unit is dropped, any other unit is kept,
// and a <br> is the same as a whitespace unit (they differ only on an empty accumulator, where the space that the whitespace
// unit leaves is trimmed). lyrics_card.js therefore sends the text nodes with "\n" for every <br>, and normalisedLine below
// does the rest. Whitespace and invisible characters are BMP units, so the walk goes unit by unit (a surrogate pair passes
// through as appendCodePoint would append it, an unpaired surrogate unit as well).
// Not followed (the script does not report them, and Google's line spans hold only text, <b>/<i> and <br>): the separators of
// block and text-boundary elements nested inside a line, and the kept whitespace inside <pre> or <textarea>.
namespace xgs::lyrics::google {

namespace {

bool isJsoupWhitespace(char16_t unit) {
    return unit == u' ' || unit == u'\t' || unit == u'\n' || unit == u'\f' || unit == u'\r' || unit == 0x00A0;
}

bool isJsoupInvisible(char16_t unit) {
    return unit == 0x200B || unit == 0x00AD;
}

// Java's String.trim(): units up to U+0020 off both ends.
QString javaTrim(const QString& text) {
    qsizetype start = 0;
    qsizetype end = text.size();
    while (start < end && text.at(start).unicode() <= 0x20) ++start;
    while (end > start && text.at(end - 1).unicode() <= 0x20) --end;
    return text.sliced(start, end - start);
}

// `element.text().trim()` of LyricsCardParser.kt for the raw text of a line (see the comment at the top).
QString normalisedLine(const QString& raw) {
    QString accum;
    accum.reserve(raw.size());
    for (const QChar c : raw) {
        const char16_t unit = c.unicode();
        if (isJsoupWhitespace(unit)) {
            if (accum.isEmpty() || accum.back() != u' ') accum += u' ';
        } else if (!isJsoupInvisible(unit)) {
            accum += c;
        }
    }
    return kt::trim(javaTrim(accum));
}

// A whole number as the script writes one: ASCII digits only.
bool isParentNumber(const QString& content) {
    if (content.isEmpty()) return false;
    for (const QChar c : content)
        if (c < u'0' || c > u'9') return false;
    return true;
}

struct CardLine {
    QString parent;
    QString text;
};

struct Card {
    bool scope = false;
    std::vector<CardLine> lines;
};

// The answer as a Card; nothing when it is not of the script's shape or goes over a limit.
std::optional<Card> readCard(const QString& scriptResult) {
    // Bounds what is parsed at all: a text within the limits is at most every unit written as a six-unit \uXXXX escape plus
    // an item's syntax per line. Anything longer cannot be a card answer within the limits.
    constexpr qsizetype maxAnswerUnits = 6 * CardMaxUnits + 64 * qsizetype(CardMaxLines + 1) + 64;
    if (scriptResult.size() > maxAnswerUnits) return std::nullopt;

    const std::optional<json::Value> root = json::parse(scriptResult);
    if (!root) return std::nullopt;
    const json::Object* object = std::get_if<json::Object>(&root->data);
    if (!object) return std::nullopt;

    const json::Value* scopeValue = json::find(*object, u"scope");
    const json::Primitive* scope = scopeValue ? std::get_if<json::Primitive>(&scopeValue->data) : nullptr;
    if (!scope || scope->isString || (scope->content != u"true" && scope->content != u"false")) return std::nullopt;

    const json::Value* linesValue = json::find(*object, u"lines");
    const json::Array* lines = linesValue ? std::get_if<json::Array>(&linesValue->data) : nullptr;
    if (!lines || lines->size() > std::size_t(CardMaxLines)) return std::nullopt;

    Card card;
    card.scope = scope->content == u"true";
    card.lines.reserve(lines->size());
    qsizetype units = 0;
    for (const json::Value& item : *lines) {
        const json::Object* line = std::get_if<json::Object>(&item.data);
        if (!line) return std::nullopt;
        const json::Value* parentValue = json::find(*line, u"p");
        const json::Primitive* parent = parentValue ? std::get_if<json::Primitive>(&parentValue->data) : nullptr;
        if (!parent || parent->isString || !isParentNumber(parent->content)) return std::nullopt;
        const json::Value* textValue = json::find(*line, u"t");
        const json::Primitive* text = textValue ? std::get_if<json::Primitive>(&textValue->data) : nullptr;
        if (!text || !text->isString) return std::nullopt;
        units += text->content.size();
        if (units > CardMaxUnits) return std::nullopt;
        card.lines.push_back(CardLine{parent->content, text->content});
    }
    return card;
}

}  // namespace

GoogleLyricsResult parseCard(const QString& scriptResult) {
    using Kind = GoogleLyricsResult::Kind;
    const std::optional<Card> card = readCard(scriptResult);
    if (!card) return GoogleLyricsResult{Kind::ExtractionFailed};
    if (card->lines.empty()) return GoogleLyricsResult{card->scope ? Kind::ExtractionFailed : Kind::NoCard};

    QList<QStringList> paragraphs;
    std::optional<QString> parent;
    for (const CardLine& element : card->lines) {
        const QString line = normalisedLine(element.text);
        if (line.isEmpty()) continue;
        if (paragraphs.isEmpty() || element.parent != parent) {
            paragraphs.append(QStringList());
            parent = element.parent;
        }
        paragraphs.last().append(line);
    }

    QStringList joined;
    for (const QStringList& paragraph : std::as_const(paragraphs)) joined.append(paragraph.join(u'\n'));
    const std::optional<QString> lyrics = tidy(joined.join(u"\n\n"_s));
    if (!lyrics) return GoogleLyricsResult{Kind::ExtractionFailed};

    GoogleLyricsResult result{Kind::Found};
    result.lyrics = *lyrics;
    for (const QString& line : lyrics->split(u'\n'))
        if (!line.isEmpty()) ++result.lineCount;
    for (const QString& paragraph : lyrics->split(u"\n\n"_s))
        if (!kt::isBlank(paragraph)) ++result.paragraphCount;
    return result;
}

}  // namespace xgs::lyrics::google
