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

/// The properties of a @Serializable Kotlin class, for ParseOptions::typed: its `String?` properties (default null) and its
/// `List<T?>` properties of objects of another class (default emptyList()).
struct ClassShape {
    std::vector<QString> strings;
    std::vector<std::pair<QString, const ClassShape*>> objectLists;
};

struct ParseOptions {
    /// nullptr: `Json.parseToJsonElement(text)` (above). Otherwise the text is read as kotlinx.serialization's
    /// `Json { ignoreUnknownKeys = true }.decodeFromString(serializer, text)` reads a class of this shape, which accepts
    /// other texts (stricter in some places, looser in others). Observed on the real Kotlin/JVM through YtDlpResolver
    /// (qt/tools/differential kind P, the observation table of the Q2b Task 5 report), not taken from kotlinx.serialization:
    ///   - white space is U+0020, U+0009, U+000A and U+000D only; the root must be an object, with nothing but white space
    ///     around it (no BOM, nothing after it);
    ///   - an object is `{`, then `}` or members; a member is a string key (escapes decoded, so `"title"` is `title`),
    ///     `:` and the value. After a value comes `,` and the next key (`,}` and `,,` are errors), `}`, or, without a comma,
    ///     the `"` of the next key (`{"a":"x" "b":"y"}` is accepted); anything else is an error. Strings are read as above;
    ///   - a key that occurs again is read again (and must have the right type each time); the last value wins;
    ///   - a `String?` property takes a string or the literal `null` (exactly; a literal is a run of units as above);
    ///     a number, `true`, any other literal, an object or an array is an error;
    ///   - a list property takes `[`, items separated by single commas (no leading, doubled or trailing comma, and not the
    ///     `]` separator of above), `]`; an item is an object of the class or `null`. `null` for the list itself is an error;
    ///   - any other key's value is skipped: a string (whose escapes must be valid), a literal, or a container. Inside a
    ///     container only strings (escapes checked) and brackets count: every `[` or `{` must be closed by its own kind
    ///     before an outer one, and everything else (commas, colons, literals, missing or doubled separators, control
    ///     characters, `\`) is passed over unchecked. A value that starts with anything else (`}`, `]`, `,`, `:`, `\`, a
    ///     control character) is an error. A skipped container may nest to any depth (no recursion here).
    /// The result is an object holding the class's properties that occur (an absent one is absent): a `String?` property as
    /// a string Primitive or Null, a list property as an Array of Objects (of the item class) and Nulls. Other keys are not
    /// kept.
    const ClassShape* typed = nullptr;
};

/// Nothing when kotlinx.serialization's default `Json.parseToJsonElement(text)` throws for the text (or, for nesting, as
/// described above); with options.typed, nothing when decodeFromString throws (see ParseOptions).
std::optional<Value> parse(const QString& text, const ParseOptions& options = {});

/// The value of [key] in [object]; nullptr when it has none.
const Value* find(const Object& object, QStringView key);

/// The canonical form of the differential test: N (null), S<hex> (a string primitive), L<hex> (the content of any other
/// primitive), O{<key hex>:<value>,...}, A[<value>,...]; hex = 4 lowercase digits per UTF-16 unit, "." for the empty string.
QString dump(const Value& value);

}  // namespace xgs::json
