#include <QtTest>

#include <QElapsedTimer>

#include <optional>
#include <variant>

#include "json/json.h"

using namespace Qt::StringLiterals;
using xgs::json::Array;
using xgs::json::dump;
using xgs::json::find;
using xgs::json::Null;
using xgs::json::Object;
using xgs::json::parse;
using xgs::json::Primitive;
using xgs::json::Value;

// Every expected value in here is the answer of the real Kotlin/JVM (kotlinx.serialization's Json.parseToJsonElement, via
// qt/tools/differential/Oracle.java kind J) to the same input, observed before xgs::json was written (the observation
// table of .superpowers/sdd/2026-11-15-qt-engine-logic/task-3-report.md). ERR there is nullopt here.
namespace {

// The test notation of a text: «hhhh» stands for the UTF-16 unit hhhh (so that control characters, unpaired surrogates and
// the BOM can be written), everything else is itself.
QString marked(QStringView notation) {
    QString out;
    for (qsizetype i = 0; i < notation.size(); ++i) {
        if (notation[i] == u'«' && i + 5 < notation.size() && notation[i + 5] == u'»') {
            char16_t unit = 0;
            for (qsizetype k = 1; k <= 4; ++k) {
                const char16_t c = notation[i + k].unicode();
                unit = char16_t(unit * 16 + (c <= u'9' ? c - u'0' : c - u'a' + 10));
            }
            out.append(QChar(unit));
            i += 5;
        } else {
            out.append(notation[i]);
        }
    }
    return out;
}

// A dump (json::dump, the differential's canonical form) with each hex string shown as "text" in the notation of marked()
// (`"` and `\` as «0022» and «005c»), so that the expected trees below can be read: O{"a":L"1"} for O{0061:L0031}.
QString readable(const QString& canonical) {
    QString out;
    std::vector<char16_t> open;   // '{' or '['
    bool keyNext = false;
    auto hexString = [&](qsizetype& i) {
        out.append(u'"');
        if (i < canonical.size() && canonical[i] == u'.') {
            ++i;
        } else {
            while (i + 4 <= canonical.size()) {
                char16_t unit = 0;
                bool hex = true;
                for (qsizetype k = 0; k < 4; ++k) {
                    const char16_t c = canonical[i + k].unicode();
                    if (c >= u'0' && c <= u'9') unit = char16_t(unit * 16 + (c - u'0'));
                    else if (c >= u'a' && c <= u'f') unit = char16_t(unit * 16 + (c - u'a' + 10));
                    else hex = false;
                }
                if (!hex) break;
                if (unit >= 0x20 && unit < 0x7F && unit != u'"' && unit != u'\\') out.append(QChar(unit));
                else out.append(u"«%1»"_s.arg(int(unit), 4, 16, QChar(u'0')));
                i += 4;
            }
        }
        out.append(u'"');
    };
    for (qsizetype i = 0; i < canonical.size();) {
        if (keyNext && canonical[i] != u'}') {
            hexString(i);
            keyNext = false;
            continue;
        }
        keyNext = false;
        const QChar c = canonical[i++];
        out.append(c);
        if (c == u'S' || c == u'L') hexString(i);
        else if (c == u'{') { open.push_back(u'{'); keyNext = true; }
        else if (c == u'[') open.push_back(u'[');
        else if (c == u'}' || c == u']') open.pop_back();
        else if (c == u',' && !open.empty() && open.back() == u'{') keyNext = true;
    }
    return out;
}

// The readable dump of parse(marked(notation)), or "ERR" when it gives nothing.
QString answer(QStringView notation) {
    const std::optional<Value> value = parse(marked(notation));
    return value ? readable(dump(*value)) : u"ERR"_s;
}

QString nested(const QString& open, const QString& inner, const QString& close, int depth) {
    return open.repeated(depth) + inner + close.repeated(depth);
}

}  // namespace

class JsonTest : public QObject {
    Q_OBJECT
private slots:
    void parsesAnObjectWithEveryKindOfValue();
    void keepsTheContentOfUnquotedLiteralsAsWritten();
    void duplicateKeysBehaveLikeKotlin();
    void stringEscapesBehaveLikeKotlin();
    void rejectsWhatKotlinRejects_data();
    void rejectsWhatKotlinRejects();
    void acceptsWhatKotlinAccepts_data();
    void acceptsWhatKotlinAccepts();
    void deepNestingBehavesLikeKotlinAndDoesNotOverflowTheStack();
    void parsesATenMegabyteDocumentQuickly();
};

void JsonTest::parsesAnObjectWithEveryKindOfValue() {
    const std::optional<Value> value = parse(u"{\"a\":1,\"b\":\"x\",\"c\":null,\"d\":true,\"e\":[1,2],\"f\":{}}"_s);
    QVERIFY(value.has_value());
    QCOMPARE(readable(dump(*value)), u"O{\"a\":L\"1\",\"b\":S\"x\",\"c\":N,\"d\":L\"true\",\"e\":A[L\"1\",L\"2\"],\"f\":O{}}"_s);
    QCOMPARE(dump(*value), u"O{0061:L0031,0062:S0078,0063:N,0064:L0074007200750065,0065:A[L0031,L0032],0066:O{}}"_s);

    const Object* root = std::get_if<Object>(&value->data);
    QVERIFY(root);
    QCOMPARE(root->size(), std::size_t(6));
    const QStringList keys = {u"a"_s, u"b"_s, u"c"_s, u"d"_s, u"e"_s, u"f"_s};
    for (std::size_t i = 0; i < root->size(); ++i) QCOMPARE((*root)[i].first, keys[qsizetype(i)]);

    const Primitive* a = std::get_if<Primitive>(&find(*root, u"a")->data);
    QVERIFY(a);
    QVERIFY(!a->isString);
    QCOMPARE(a->content, u"1"_s);
    const Primitive* b = std::get_if<Primitive>(&find(*root, u"b")->data);
    QVERIFY(b);
    QVERIFY(b->isString);
    QCOMPARE(b->content, u"x"_s);
    QVERIFY(std::holds_alternative<Null>(find(*root, u"c")->data));
    const Array* e = std::get_if<Array>(&find(*root, u"e")->data);
    QVERIFY(e);
    QCOMPARE(e->size(), std::size_t(2));
    QCOMPARE(std::get<Primitive>((*e)[1].data).content, u"2"_s);
    const Object* f = std::get_if<Object>(&find(*root, u"f")->data);
    QVERIFY(f);
    QVERIFY(f->empty());
    QVERIFY(!find(*root, u"g"));
    QVERIFY(!find(*root, u"A"));
}

void JsonTest::keepsTheContentOfUnquotedLiteralsAsWritten() {
    const std::optional<Value> value = parse(u"[NaN, Infinity,-Infinity, 1e3, true, false, null, 01, 0x10, abc, \"NaN\"]"_s);
    QVERIFY(value.has_value());
    const Array& items = std::get<Array>(value->data);
    QCOMPARE(items.size(), std::size_t(11));
    const QStringList literals = {u"NaN"_s, u"Infinity"_s, u"-Infinity"_s, u"1e3"_s, u"true"_s, u"false"_s};
    for (qsizetype i = 0; i < literals.size(); ++i) {
        const Primitive& p = std::get<Primitive>(items[std::size_t(i)].data);
        QVERIFY(!p.isString);
        QCOMPARE(p.content, literals[i]);
    }
    QVERIFY(std::holds_alternative<Null>(items[6].data));
    QCOMPARE(std::get<Primitive>(items[7].data).content, u"01"_s);
    QCOMPARE(std::get<Primitive>(items[8].data).content, u"0x10"_s);
    QCOMPARE(std::get<Primitive>(items[9].data).content, u"abc"_s);
    QVERIFY(std::get<Primitive>(items[10].data).isString);
    QCOMPARE(std::get<Primitive>(items[10].data).content, u"NaN"_s);

    // only the exact lowercase `null` is JsonNull
    QCOMPARE(answer(u"null"), u"N"_s);
    QCOMPARE(answer(u"NULL"), u"L\"NULL\""_s);
    QCOMPARE(answer(u"[Null,nullx,null1]"), u"A[L\"Null\",L\"nullx\",L\"null1\"]"_s);
    QCOMPARE(answer(u"\"null\""), u"S\"null\""_s);
    // a literal runs up to white space or one of " , : [ ] { } \ and keeps everything else
    QCOMPARE(answer(u"[1/*c*/]"), u"A[L\"1/*c*/\"]"_s);
    QCOMPARE(answer(u"{\"a\":a-b_c$d}"), u"O{\"a\":L\"a-b_c$d\"}"_s);
    QCOMPARE(answer(u"['a']"), u"A[L\"'a'\"]"_s);
    QCOMPARE(answer(u"[1«00a0»]"), u"A[L\"1«00a0»\"]"_s);
    QCOMPARE(answer(u"«feff»"), u"L\"«feff»\""_s);
}

void JsonTest::duplicateKeysBehaveLikeKotlin() {
    // the later value replaces the earlier one, which keeps its position
    QCOMPARE(answer(u"{\"a\":1,\"a\":2}"), u"O{\"a\":L\"2\"}"_s);
    QCOMPARE(answer(u"{\"a\":1,\"b\":2,\"a\":3}"), u"O{\"a\":L\"3\",\"b\":L\"2\"}"_s);
    QCOMPARE(answer(u"{\"a\":1,\"b\":2,\"a\":3,\"c\":4}"), u"O{\"a\":L\"3\",\"b\":L\"2\",\"c\":L\"4\"}"_s);
    QCOMPARE(answer(u"{\"a\":{\"x\":1},\"a\":[2]}"), u"O{\"a\":A[L\"2\"]}"_s);
    QCOMPARE(answer(u"{\"a\":null,\"a\":\"x\"}"), u"O{\"a\":S\"x\"}"_s);
    QCOMPARE(answer(u"{\"a\":[1],\"b\":{\"a\":1,\"a\":2}}"), u"O{\"a\":A[L\"1\"],\"b\":O{\"a\":L\"2\"}}"_s);
    // keys are compared after their escapes are decoded
    QCOMPARE(answer(u"{\"«005c»u0061\":1,\"a\":2}"), u"O{\"a\":L\"2\"}"_s);

    // many keys, each written twice: still one entry per key, in first-seen order, with the last value
    QString text = u"{"_s;
    for (int i = 0; i < 500; ++i) text += u"\"k%1\":%1,"_s.arg(i);
    for (int i = 499; i >= 0; --i) text += u"\"k%1\":\"v%1\"%2"_s.arg(i).arg(i > 0 ? u","_s : u""_s);
    text += u"}"_s;
    const std::optional<Value> value = parse(text);
    QVERIFY(value.has_value());
    const Object& object = std::get<Object>(value->data);
    QCOMPARE(object.size(), std::size_t(500));
    for (int i = 0; i < 500; ++i) {
        QCOMPARE(object[std::size_t(i)].first, u"k%1"_s.arg(i));
        const Primitive& p = std::get<Primitive>(object[std::size_t(i)].second.data);
        QVERIFY(p.isString);
        QCOMPARE(p.content, u"v%1"_s.arg(i));
    }
    QCOMPARE(std::get<Primitive>(find(object, u"k7")->data).content, u"v7"_s);
}

void JsonTest::stringEscapesBehaveLikeKotlin() {
    QCOMPARE(answer(u"\"«005c»/«005c»b«005c»f«005c»n«005c»r«005c»t«005c»\"«005c»«005c»\""), u"S\"/«0008»«000c»«000a»«000d»«0009»«0022»«005c»\""_s);
    QCOMPARE(answer(u"\"«005c»u0000\""), u"S\"«0000»\""_s);
    QCOMPARE(answer(u"\"«005c»u0022\""), u"S\"«0022»\""_s);
    // escaped surrogates are kept as they are, paired or not, in either order
    QCOMPARE(answer(u"\"«005c»uD83D\""), u"S\"«d83d»\""_s);
    QCOMPARE(answer(u"\"«005c»uD83D«005c»uDE00\""), u"S\"«d83d»«de00»\""_s);
    QCOMPARE(answer(u"\"«005c»uDE00«005c»uD83D\""), u"S\"«de00»«d83d»\""_s);
    // hex digits of either case, ASCII only
    QCOMPARE(answer(u"\"«005c»uabcd«005c»uABCD\""), u"S\"«abcd»«abcd»\""_s);
    QCOMPARE(answer(u"\"«005c»u00e9«005c»u00E9\""), u"S\"«00e9»«00e9»\""_s);
    QCOMPARE(answer(u"\"«005c»u004«ff10»\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»u004G\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»u004\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»u\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»u12"), u"ERR"_s);
    // no other escape
    QCOMPARE(answer(u"\"«005c»x\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»'\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»U0041\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»a\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»«000a»\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»\""), u"ERR"_s);
    QCOMPARE(answer(u"\"«005c»"), u"ERR"_s);
    // raw control characters, line breaks, unpaired surrogates and the BOM inside a string are kept
    QCOMPARE(answer(u"\"a«0000»b«0001»c«000a»d«0009»e«000d»f«001f»g«007f»\""), u"S\"a«0000»b«0001»c«000a»d«0009»e«000d»f«001f»g«007f»\""_s);
    QCOMPARE(answer(u"\"«d83d»«de00»«d83d»x«de00»\""), u"S\"«d83d»«de00»«d83d»x«de00»\""_s);
    QCOMPARE(answer(u"\"«feff»a\""), u"S\"«feff»a\""_s);
    QCOMPARE(answer(u"{\"«d800»\":\"«dc00»\"}"), u"O{\"«d800»\":S\"«dc00»\"}"_s);
    // escapes in a key too
    QCOMPARE(answer(u"{\"«005c»n«005c»u0041\":1}"), u"O{\"«000a»A\":L\"1\"}"_s);
}

void JsonTest::rejectsWhatKotlinRejects_data() {
    QTest::addColumn<QString>("notation");
    const char* const rows[][2] = {
        {"unquoted key", "{a:1}"},
        {"unquoted key with a space", "{a b:1}"},
        {"single-quoted key", "{'a':1}"},
        {"trailing comma in an array", "[1,2,]"},
        {"trailing comma in an object", "{\"a\":1,}"},
        {"doubled comma", "[1,,2]"},
        {"leading comma in an array", "[,1]"},
        {"leading comma in an object", "{,\"a\":1}"},
        {"line comment after the value", "[1]//c"},
        {"block comment before the value", "/*c*/[1]"},
        {"empty text", ""},
        {"spaces only", "   "},
        {"white space only", "«0009»«000a»«000d» "},
        {"garbage after the value", "[1] x"},
        {"extra closing bracket", "[1]]"},
        {"extra closing brace", "{\"a\":1}}"},
        {"two literals", "1 2"},
        {"two strings", "\"x\" \"y\""},
        {"escape x", "\"«005c»x\""},
        {"escape quote", "\"«005c»'\""},
        {"escape capital U", "\"«005c»U0041\""},
        {"short u escape", "\"«005c»u004\""},
        {"u escape with G", "\"«005c»u004G\""},
        {"unclosed string", "\"abc"},
        {"unclosed object", "{\"a\":1"},
        {"unclosed array", "[1,2"},
        {"lone [", "["},
        {"lone {", "{"},
        {"lone quote", "\""},
        {"BOM before the value", "«feff»[1]"},
        {"no-break space before the value", "«00a0»[1]"},
        {"line separator before the value", "«2028»[1]"},
        {"next line before the value", "«0085»[1]"},
        {"vertical tab before the value", "«000b»[1]"},
        {"form feed before the value", "«000c»[1]"},
        {"ideographic space before the value", "«3000»[1]"},
        {"form feed after a literal", "[1«000c»,2]"},
        {"literal with a space", "{\"a\":abc def}"},
        {"literal after a string", "{\"a\":\"x\"y}"},
        {"no colon", "{\"a\"\"b\"}"},
        {"space instead of colon", "{\"a\" 1}"},
        {"no value", "{\"a\":}"},
        {"no key", "{:1}"},
        {"number key", "{1:2}"},
        {"null key", "{null:1}"},
        {"true key", "{true:1}"},
        {"no comma between members", "{\"a\":1 \"b\":2}"},
        {"no comma between numbers", "[1 2]"},
        {"no comma between literals", "[true false]"},
        {"colon after a literal at the top", "x:1"},
        {"backslash at the end of a string", "\"«005c»\""},
        {"u at the end of a string", "\"«005c»u\""},
        {"NUL after the value", "[1]«0000»"},
        {"NUL before the value", "«0000»[1]"},
        {"NUL inside an array", "[«0000»1]"},
        {"backslash then end", "\"«005c»"},
        {"two-digit u then end", "\"«005c»u12"},
        {"colon in an array", "[a:b]"},
        {"literal after an array", "[a]b"},
        {"quote after a literal", "[a\"b\"]"},
        {"backslash in a literal", "[a«005c»b]"},
        {"garbage after an array of numbers", "[1,2,3]x"},
        {"short u escape in an object", "{\"a\":\"«005c»u00\"}"},
        {"key without value", "{\"a\"}"},
        {"two colons", "{\"a\":1:2}"},
        {"colon between numbers", "[1:2]"},
        {"comma instead of colon", "{\"a\",1}"},
        {"10000 nested arrays", "{rep:10000:[}{rep:10000:]}"},
        // where a `]` before a value does not separate items
        {"] then a value leaves the outer array open", "[[a]b]"},
        {"] then a value at the top leaves it open", "[[a]]b"},
        {"] then a comma", "[1],2]"},
        {"] after a trailing comma", "[1,]2]"},
        {"] after an empty array", "[]b]"},
        {"] after an empty array with a space", "[ ]b]"},
        {"] after an empty array in an object", "{\"a\":[]x]}"},
        {"] then ]", "[[a]]]"},
        {"] then ] then a comma", "[[a]],b]"},
        {"] then a colon", "[[a]]:b]"},
        {"] then spaces then ]", "[1]  ]"},
        {"} then a member", "{\"x\":[a]}\"y\":1}"},
        {"] separating, then the outer array open", "[[1,-].5,2]"},
    };
    for (const auto& row : rows) {
        QString notation = QString::fromUtf8(row[1]);
        // {rep:N:text} = text written N times
        while (notation.contains(u"{rep:"_s)) {
            const qsizetype at = notation.indexOf(u"{rep:"_s);
            const qsizetype colon = notation.indexOf(u':', at + 5);
            const qsizetype end = notation.indexOf(u'}', colon);
            int times = 0;
            for (qsizetype k = at + 5; k < colon; ++k) times = times * 10 + (notation[k].unicode() - u'0');
            notation = notation.left(at) + notation.mid(colon + 1, end - colon - 1).repeated(times) + notation.mid(end + 1);
        }
        QTest::newRow(row[0]) << notation;
    }
}

void JsonTest::rejectsWhatKotlinRejects() {
    QFETCH(QString, notation);
    QCOMPARE(answer(notation), u"ERR"_s);
}

void JsonTest::acceptsWhatKotlinAccepts_data() {
    QTest::addColumn<QString>("notation");
    QTest::addColumn<QString>("expected");
    const char* const rows[][3] = {
        {"object with every kind of value", "{\"a\":1,\"b\":\"x\",\"c\":null,\"d\":true,\"e\":[1,2],\"f\":{}}",
         "O{\"a\":L\"1\",\"b\":S\"x\",\"c\":N,\"d\":L\"true\",\"e\":A[L\"1\",L\"2\"],\"f\":O{}}"},
        {"NaN", "NaN", "L\"NaN\""},
        {"Infinity", "Infinity", "L\"Infinity\""},
        {"-Infinity", "-Infinity", "L\"-Infinity\""},
        {"abc", "abc", "L\"abc\""},
        {"1e", "1e", "L\"1e\""},
        {"01", "01", "L\"01\""},
        {"+1", "+1", "L\"+1\""},
        {".5", ".5", "L\".5\""},
        {"1.", "1.", "L\"1.\""},
        {"0x10", "0x10", "L\"0x10\""},
        {"true", "true", "L\"true\""},
        {"TRUE", "TRUE", "L\"TRUE\""},
        {"null", "null", "N"},
        {"NULL", "NULL", "L\"NULL\""},
        {"every odd literal in an array", "[NaN,Infinity,-Infinity,abc,1e,01,+1,.5,1.,0x10,TRUE,NULL]",
         "A[L\"NaN\",L\"Infinity\",L\"-Infinity\",L\"abc\",L\"1e\",L\"01\",L\"+1\",L\".5\",L\"1.\",L\"0x10\",L\"TRUE\",L\"NULL\"]"},
        {"NaN as a value", "{\"a\":NaN}", "O{\"a\":L\"NaN\"}"},
        {"single-quoted literal in an array", "['a']", "A[L\"'a'\"]"},
        {"apostrophe in a string", "\"a'b\"", "S\"a'b\""},
        {"comment-like literal", "[1/*c*/]", "A[L\"1/*c*/\"]"},
        {"string", "\"str\"", "S\"str\""},
        {"42", "42", "L\"42\""},
        {"-0", "-0", "L\"-0\""},
        {"1e999", "1e999", "L\"1e999\""},
        {"-1e999", "-1e999", "L\"-1e999\""},
        {"30 digits", "123456789012345678901234567890", "L\"123456789012345678901234567890\""},
        {"1.5e-400", "1.5e-400", "L\"1.5e-400\""},
        {"duplicate key", "{\"a\":1,\"a\":2}", "O{\"a\":L\"2\"}"},
        {"duplicate key around another", "{\"a\":1,\"b\":2,\"a\":3}", "O{\"a\":L\"3\",\"b\":L\"2\"}"},
        {"duplicate key of another type", "{\"a\":{\"x\":1},\"a\":[2]}", "O{\"a\":A[L\"2\"]}"},
        {"escaped NUL", "\"«005c»u0000\"", "S\"«0000»\""},
        {"escaped high surrogate", "\"«005c»uD83D\"", "S\"«d83d»\""},
        {"escaped surrogates reversed", "\"«005c»uDE00«005c»uD83D\"", "S\"«de00»«d83d»\""},
        {"raw emoji", "\"«d83d»«de00»\"", "S\"«d83d»«de00»\""},
        {"raw high surrogate", "\"«d83d»\"", "S\"«d83d»\""},
        {"raw low surrogate", "\"«de00»x\"", "S\"«de00»x\""},
        {"hex of both cases", "\"«005c»uabcd«005c»uABCD\"", "S\"«abcd»«abcd»\""},
        {"every short escape", "\"«005c»/«005c»b«005c»f«005c»n«005c»r«005c»t«005c»\"«005c»«005c»\"",
         "S\"/«0008»«000c»«000a»«000d»«0009»«0022»«005c»\""},
        {"raw NUL", "\"a«0000»b\"", "S\"a«0000»b\""},
        {"raw U+0001", "\"a«0001»b\"", "S\"a«0001»b\""},
        {"raw line feed", "\"a«000a»b\"", "S\"a«000a»b\""},
        {"raw tab", "\"a«0009»b\"", "S\"a«0009»b\""},
        {"raw carriage return", "\"a«000d»b\"", "S\"a«000d»b\""},
        {"raw U+001F", "\"a«001f»b\"", "S\"a«001f»b\""},
        {"raw DEL", "\"a«007f»b\"", "S\"a«007f»b\""},
        {"lone BOM", "«feff»", "L\"«feff»\""},
        {"no-break space in a literal", "[1«00a0»]", "A[L\"1«00a0»\"]"},
        {"tab, line feed, carriage return around tokens", "{\"a\"«0009»:«000a»1«000d»}", "O{\"a\":L\"1\"}"},
        {"single-quoted literal", "'abc'", "L\"'abc'\""},
        {"single-quoted value", "{\"a\":'b'}", "O{\"a\":L\"'b'\"}"},
        {"tru", "{\"a\":tru}", "O{\"a\":L\"tru\"}"},
        {"nul", "{\"a\":nul}", "O{\"a\":L\"nul\"}"},
        {"True", "{\"a\":True}", "O{\"a\":L\"True\"}"},
        {"minus", "{\"a\":-}", "O{\"a\":L\"-\"}"},
        {"1-2", "{\"a\":1-2}", "O{\"a\":L\"1-2\"}"},
        {"1e+", "{\"a\":1e+}", "O{\"a\":L\"1e+\"}"},
        {"--1", "{\"a\":--1}", "O{\"a\":L\"--1\"}"},
        {"1.2.3", "{\"a\":1.2.3}", "O{\"a\":L\"1.2.3\"}"},
        {"punctuated literal", "{\"a\":a-b_c$d}", "O{\"a\":L\"a-b_c$d\"}"},
        {"[null]", "[null]", "A[N]"},
        {"[\"a\"]", "[\"a\"]", "A[S\"a\"]"},
        {"[[]]", "[[]]", "A[A[]]"},
        {"empty key and value", "{\"\":\"\"}", "O{\"\":S\"\"}"},
        {"escaped key equal to a later key", "{\"«005c»u0061\":1,\"a\":2}", "O{\"a\":L\"2\"}"},
        {"lone minus", "-", "L\"-\""},
        {"e acute of both cases", "\"«005c»u00e9«005c»u00E9\"", "S\"«00e9»«00e9»\""},
        {"nullx", "[nullx]", "A[L\"nullx\"]"},
        {"null1", "[null1]", "A[L\"null1\"]"},
        {"Null", "[Null]", "A[L\"Null\"]"},
        {"spaces around", " [1] ", "A[L\"1\"]"},
        {"line feeds around", "«000a»[1]«000a»", "A[L\"1\"]"},
        {"{ }", "{ }", "O{}"},
        {"[ ]", "[ ]", "A[]"},
        {"{}", "{}", "O{}"},
        {"[]", "[]", "A[]"},
        {"duplicate key among others", "{\"a\":1,\"b\":2,\"a\":3,\"c\":4}", "O{\"a\":L\"3\",\"b\":L\"2\",\"c\":L\"4\"}"},
        {"null replaced by a string", "{\"a\":null,\"a\":\"x\"}", "O{\"a\":S\"x\"}"},
        {"duplicate key in a nested object", "{\"a\":[1],\"b\":{\"a\":1,\"a\":2}}", "O{\"a\":A[L\"1\"],\"b\":O{\"a\":L\"2\"}}"},
        {"mixed array", "[1,{\"a\":[]},\"x\",null,true]", "A[L\"1\",O{\"a\":A[]},S\"x\",N,L\"true\"]"},
        {"Hangul and emoji keys", "{\"«d55c»\":\"«ae00»\",\"«d83d»«de00»\":1}", "O{\"«d55c»\":S\"«ae00»\",\"«d83d»«de00»\":L\"1\"}"},
        {"lone surrogates as key and value", "{\"«d800»\":\"«dc00»\"}", "O{\"«d800»\":S\"«dc00»\"}"},
        {"escaped surrogate pair", "\"«005c»uD83D«005c»uDE00\"", "S\"«d83d»«de00»\""},
        {"tab after a string", "\"a\"«0009»", "S\"a\""},
        {"line feed after a literal", "1«000a»", "L\"1\""},
        {"n", "n", "L\"n\""},
        {"escaped quote", "\"«005c»u0022\"", "S\"«0022»\""},
        {"nested objects and arrays", "{\"a\":{\"b\":{\"c\":[1,[2,[3]]]}}}", "O{\"a\":O{\"b\":O{\"c\":A[L\"1\",A[L\"2\",A[L\"3\"]]]}}}"},
        {"white space inside an array", "[«0009»1«000d»,«000a»2 ]", "A[L\"1\",L\"2\"]"},
        {"spaces inside an object", "{\"a\" : 1 , \"b\" : 2}", "O{\"a\":L\"1\",\"b\":L\"2\"}"},
        {"numbers kept as written", "[1e5,-2.5E-3,0,-0.0]", "A[L\"1e5\",L\"-2.5E-3\",L\"0\",L\"-0.0\"]"},
        {"truefalse", "truefalse", "L\"truefalse\""},
        {"true false null", "[true,false,null]", "A[L\"true\",L\"false\",N]"},
        {"empty string", "\"\"", "S\"\""},
        {"array of objects", "[{\"a\":1},{\"a\":2}]", "A[O{\"a\":L\"1\"},O{\"a\":L\"2\"}]"},
        {"Python's NaN and -Infinity", "{\"x\":[{\"y\":NaN}],\"z\":-Infinity}", "O{\"x\":A[O{\"y\":L\"NaN\"}],\"z\":L\"-Infinity\"}"},
        {"BOM inside a string", "\"«feff»a\"", "S\"«feff»a\""},
        {"BOM as a literal in an array", "[«feff»]", "A[L\"«feff»\"]"},
        {"BOM inside a key", "{\"«feff»a\":1}", "O{\"«feff»a\":L\"1\"}"},
        // In an array, after an item, a `]` followed by the start of a value separates two items like a comma (found by
        // the differential run, then observed directly); `}` does not do this in an object, nor `]` after no item.
        {"] then a literal", "[1]b]", "A[L\"1\",L\"b\"]"},
        {"] then a string", "[1]\"x\"]", "A[L\"1\",S\"x\"]"},
        {"] then a no-break space literal", "[1]«00a0»]", "A[L\"1\",L\"«00a0»\"]"},
        {"] then a comment-like literal", "[1]/*c*/]", "A[L\"1\",L\"/*c*/\"]"},
        {"] then null, twice", "[null]null]", "A[N,N]"},
        {"] three times between items", "[1]x]y]z]", "A[L\"1\",L\"x\",L\"y\",L\"z\"]"},
        {"] then an object", "[{\"a\":1}]{\"b\":2}]", "A[O{\"a\":L\"1\"},O{\"b\":L\"2\"}]"},
        {"inner array closed, then ] then a literal", "[[a]]b]", "A[A[L\"a\"],L\"b\"]"},
        {"inner empty array closed, then ] then a literal", "[[]]b]", "A[A[],L\"b\"]"},
        {"] then white space then a literal", "[[1]]«000a»b]", "A[A[L\"1\"],L\"b\"]"},
        {"] separating twice", "[[a]]b]c]", "A[A[L\"a\"],L\"b\",L\"c\"]"},
        {"] separating in the middle array", "[[[a]]b]]", "A[A[A[L\"a\"],L\"b\"]]"},
        {"] separating inside an object's array", "{\"a\":[1]x]}", "O{\"a\":A[L\"1\",L\"x\"]}"},
        {"] separating after an object and a minus", "[[{\"b\":1},.5,-].7,-2.]]", "A[A[O{\"b\":L\"1\"},L\".5\",L\"-\",L\".7\",L\"-2.\"]]"},
    };
    for (const auto& row : rows) QTest::newRow(row[0]) << QString::fromUtf8(row[1]) << QString::fromUtf8(row[2]);
}

void JsonTest::acceptsWhatKotlinAccepts() {
    QFETCH(QString, notation);
    QFETCH(QString, expected);
    QCOMPARE(answer(notation), expected);
}

void JsonTest::deepNestingBehavesLikeKotlinAndDoesNotOverflowTheStack() {
    // Kotlin parses 1000 nested arrays (always, also on a cold JVM) and a chain of objects of any depth tried (10^6), and
    // overflows its stack on 60000 nested arrays (always). Between those, whether Kotlin's arrays fit its stack depends on
    // the JVM's state; xgs::json's line is at more than json::maxArrayDepth (1000) open arrays. Every parsed value below
    // also goes out of scope (an iterative destructor), is copied and is dumped.
    QCOMPARE(xgs::json::maxArrayDepth, 1000);
    {
        const std::optional<Value> value = parse(nested(u"["_s, QString(), u"]"_s, 1000));
        QVERIFY(value.has_value());
        QCOMPARE(dump(*value), nested(u"A["_s, QString(), u"]"_s, 1000));
        const Value copy = *value;
        QCOMPARE(dump(copy), dump(*value));
    }
    QVERIFY(parse(nested(u"[{\"a\":"_s, u"1"_s, u"}]"_s, 1000)).has_value());
    QVERIFY(!parse(nested(u"["_s, QString(), u"]"_s, 1001)).has_value());
    QVERIFY(!parse(nested(u"[{\"a\":"_s, u"1"_s, u"}]"_s, 1001)).has_value());
    QVERIFY(!parse(nested(u"["_s, QString(), u"]"_s, 60000)).has_value());
    QVERIFY(!parse(nested(u"{\"a\":"_s, QString(), u"}"_s, 20000) + nested(u"["_s, QString(), u"]"_s, 60000)).has_value());
    QVERIFY(!parse(nested(u"["_s, QString(), u"]"_s, 10000)).has_value());   // the Step 2 row: ERR on Kotlin's cold JVM
    // arrays that close again do not add up
    QVERIFY(parse(u"["_s + nested(u"["_s, QString(), u"]"_s, 999) + u","_s + nested(u"["_s, QString(), u"]"_s, 999) + u"]"_s).has_value());
    // objects do not count, above, between or below arrays
    QVERIFY(parse(nested(u"{\"a\":"_s, nested(u"["_s, QString(), u"]"_s, 1000), u"}"_s, 20000)).has_value());
    QVERIFY(parse(nested(u"["_s, nested(u"{\"a\":"_s, u"1"_s, u"}"_s, 20000), u"]"_s, 1000)).has_value());
    QVERIFY(parse(u"["_s + nested(u"{\"a\":"_s, u"[1]"_s, u"}"_s, 20000) + u"]"_s).has_value());
    {
        const int depth = 200000;
        const std::optional<Value> value = parse(nested(u"{\"a\":"_s, u"1"_s, u"}"_s, depth));
        QVERIFY(value.has_value());
        const Value* at = &*value;
        int levels = 0;
        while (const Object* object = std::get_if<Object>(&at->data)) {
            QCOMPARE(object->size(), std::size_t(1));
            at = find(*object, u"a");
            ++levels;
        }
        QCOMPARE(levels, depth);
        QCOMPARE(std::get<Primitive>(at->data).content, u"1"_s);
        Value copy = *value;
        const QString text = dump(copy);
        QVERIFY(text == nested(u"O{0061:"_s, u"L0031"_s, u"}"_s, depth));
        copy = Value{};
        Value moved = std::move(copy);
        QVERIFY(std::holds_alternative<Null>(moved.data));
    }
    // unclosed deep documents are errors, whatever the depth
    QVERIFY(!parse(u"{\"a\":"_s.repeated(200000) + u"1"_s).has_value());
    QVERIFY(!parse(u"["_s.repeated(500)).has_value());
    QVERIFY(!parse(u"["_s.repeated(60000)).has_value());
}

void JsonTest::parsesATenMegabyteDocumentQuickly() {
    // an info.json-like document of more than 10 MB (10 * 2^20 UTF-16 units): a long description with escapes and many
    // entries in a formats-like array, with Python's NaN among the numbers
    const QString description = u"\"첫 줄\\n두 번째 줄 \\\"인용\\\" \\u00e9\\ud83d\\ude00 line\""_s;
    QString text = u"{\"id\":\"abc\",\"title\":\"제목\",\"duration\":258.5,\"description\":"_s + description + u",\"formats\":["_s;
    const QString entry = u"{\"format_id\":\"251\",\"tbr\":NaN,\"filesize\":1234567,\"url\":\"https://example.invalid/videoplayback?a=1&b=2\",\"fragments\":[1,2,3],\"note\":null},"_s;
    int entries = 0;
    while (text.size() < 10 * 1024 * 1024) {
        text += entry;
        ++entries;
    }
    text += u"{}],\"album\":\"앨범\",\"album\":\"Album\"}"_s;

    QElapsedTimer timer;
    timer.start();
    std::optional<Value> value = parse(text);
    const qint64 elapsed = timer.elapsed();
    qInfo("parse of %lld UTF-16 units took %lld ms", static_cast<long long>(text.size()), static_cast<long long>(elapsed));
#ifdef NDEBUG
    // The time limit is about the optimized build; an unoptimized Debug build with Qt's checks on is many times slower.
    QVERIFY2(elapsed < 3000, qPrintable(u"took %1 ms"_s.arg(elapsed)));
#endif
    QVERIFY(value.has_value());
    const Object& root = std::get<Object>(value->data);
    QCOMPARE(root.size(), std::size_t(6));
    QCOMPARE(std::get<Primitive>(find(root, u"album")->data).content, u"Album"_s);
    QCOMPARE(root.back().first, u"album"_s);   // the duplicate kept the position of the first "album"
    QCOMPARE(std::get<Primitive>(find(root, u"duration")->data).content, u"258.5"_s);
    QCOMPARE(std::get<Primitive>(find(root, u"description")->data).content, u"첫 줄\n두 번째 줄 \"인용\" \x00e9\U0001F600 line"_s);
    const Array& formats = std::get<Array>(find(root, u"formats")->data);
    QCOMPARE(formats.size(), std::size_t(entries) + 1);
    const Object& first = std::get<Object>(formats.front().data);
    QCOMPARE(std::get<Primitive>(find(first, u"tbr")->data).content, u"NaN"_s);
    QVERIFY(!std::get<Primitive>(find(first, u"tbr")->data).isString);
    QVERIFY(std::holds_alternative<Null>(find(first, u"note")->data));
}

QTEST_APPLESS_MAIN(JsonTest)
#include "tst_json.moc"
