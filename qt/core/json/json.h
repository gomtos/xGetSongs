#pragma once

#include <QString>
#include <QStringView>

#include <optional>
#include <utility>
#include <variant>
#include <vector>

// xgs::json: a read-only JSON reader that accepts exactly the texts that kotlinx.serialization's default
// `Json.parseToJsonElement(text)` accepts, and gives the same tree. yt-dlp's info.json may hold what Python writes
// (bare NaN, Infinity), which a strict parser would reject together with the rest of the file.
//
// What it accepts was observed on the real Kotlin/JVM (qt/tools/differential, kind J), not taken from kotlinx.serialization:
//   - white space between tokens is U+0020, U+0009, U+000A and U+000D only;
//   - a string is "..." with any UTF-16 unit inside (control characters, line breaks and unpaired surrogates included)
//     except an unescaped `"` or `\`; the escapes are \" \\ \/ \b \f \n \r \t and \uXXXX with four ASCII hex digits of
//     either case (an escaped unpaired surrogate stays as it is); any other escape is an error;
//   - an unquoted value (a literal) is a run of units other than U+0000..U+0020, `"`, `,`, `:`, `[`, `]`, `{`, `}` and `\`,
//     kept as written: `null` is Null, anything else (`true`, `NaN`, `-Infinity`, `01`, `0x10`, `abc`) is a Primitive that
//     is not a string. Object keys must be strings;
//   - no trailing or doubled commas, no comments, nothing but white space after the value, and not an empty text;
//   - in an array, after an item, a `]` followed (past white space) by the start of a value (`"`, `{`, `[` or a literal
//     unit) separates two items like a comma: `[1]x]` is [1, x], `[[a]]b]` is [[a], b]. Not after no item (`[]b]` is an
//     error) and not `}` in an object;
//   - a key that occurs again replaces the earlier value and keeps the earlier position;
//   - nesting: Kotlin overflows its stack on deep arrays (a StackOverflowError, at a depth that depends on the JVM's
//     state: 1000 nested arrays always parsed, 60000 never did); deep objects alone parsed at any depth tried (10^6).
//     Here more than 1000 arrays open at once is an error and objects do not count. The reader and the destructor, copy
//     and dump of a Value use no recursion, so no depth overflows the C++ stack.
//
// Resources: objects are not depth-counted and each open object costs a frame of the reader, so a hostile multi-megabyte
// chain of `{"a":` can take a great deal of memory; std::bad_alloc is not caught. `find` is a linear scan of the object.
namespace xgs::json {

struct Value;
using Object = std::vector<std::pair<QString, Value>>;   // in Kotlin's (JsonObject's) order; keys are unique
using Array = std::vector<Value>;
struct Primitive {  // kotlinx JsonPrimitive: isString, content
    bool isString = false;
    QString content;
};
struct Null {};  // JsonNull

struct Value {
    std::variant<Null, Primitive, Object, Array> data;

    Value() = default;
    Value(Null) {}
    Value(Primitive primitive) : data(std::move(primitive)) {}
    Value(Object object) : data(std::move(object)) {}
    Value(Array array) : data(std::move(array)) {}
    Value(const Value& other);              // without recursion
    Value(Value&& other) noexcept = default;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;
    ~Value();                               // without recursion
};

/// The most open arrays a document may nest (more is an error; see above).
inline constexpr int maxArrayDepth = 1000;

/// Nothing when kotlinx.serialization's default `Json.parseToJsonElement(text)` throws for the text (or, for nesting, as
/// described above).
std::optional<Value> parse(const QString& text);

/// The value of [key] in [object]; nullptr when it has none.
const Value* find(const Object& object, QStringView key);

/// The canonical form of the differential test: N (null), S<hex> (a string primitive), L<hex> (the content of any other
/// primitive), O{<key hex>:<value>,...}, A[<value>,...]; hex = 4 lowercase digits per UTF-16 unit, "." for the empty string.
QString dump(const Value& value);

}  // namespace xgs::json
