#include "json/json.h"

#include <QHash>

// Built from the answers of the real Kotlin/JVM to observed inputs (qt/tools/differential kind J, the observation table of
// the Q2a Task 3 report) and from public documentation only; not from kotlinx.serialization's source.
namespace xgs::json {

namespace {

bool hasChildren(const Value& value) {
    if (const Object* object = std::get_if<Object>(&value.data)) return !object->empty();
    if (const Array* array = std::get_if<Array>(&value.data)) return !array->empty();
    return false;
}

// Moves the children of [value] that have children of their own to [pending] and drops the rest, so that destroying
// [value] afterwards recurses no further than one level of leaves.
void takeChildren(Value& value, std::vector<Value>& pending) {
    if (Object* object = std::get_if<Object>(&value.data)) {
        for (auto& member : *object) {
            if (hasChildren(member.second)) pending.push_back(std::move(member.second));
        }
        object->clear();
    } else if (Array* array = std::get_if<Array>(&value.data)) {
        for (Value& item : *array) {
            if (hasChildren(item)) pending.push_back(std::move(item));
        }
        array->clear();
    }
}

// ---- the reader ----

bool isWhite(char16_t c) {
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r';
}

// A unit that cannot be part of an unquoted literal (observed: U+0000..U+0020 and these eight).
bool endsLiteral(char16_t c) {
    return c <= 0x20 || c == u'"' || c == u',' || c == u':' || c == u'[' || c == u']' || c == u'{' || c == u'}' || c == u'\\';
}

// The first unit of a value: a string, an object, an array or a literal. (U+0000, which peek() gives at the end, is none.)
bool startsValue(char16_t c) {
    return c == u'"' || c == u'{' || c == u'[' || !endsLiteral(c);
}

int hexDigit(char16_t c) {
    if (c >= u'0' && c <= u'9') return c - u'0';
    if (c >= u'a' && c <= u'f') return c - u'a' + 10;
    if (c >= u'A' && c <= u'F') return c - u'A' + 10;
    return -1;
}

// An object while its members are read: the members in order, and (once it has a few) where each key is, so that a
// repeated key replaces its value in place without a linear search.
struct ObjectBuilder {
    static constexpr std::size_t indexFrom = 8;
    Object members;
    QHash<QString, std::size_t> index;

    void put(QString key, Value value) {
        if (members.size() < indexFrom) {
            for (auto& member : members) {
                if (member.first == key) {
                    member.second = std::move(value);
                    return;
                }
            }
            members.emplace_back(std::move(key), std::move(value));
            if (members.size() == indexFrom) {
                for (std::size_t i = 0; i < members.size(); ++i) index.insert(members[i].first, i);
            }
            return;
        }
        const auto found = index.constFind(key);
        if (found != index.constEnd()) {
            members[*found].second = std::move(value);
            return;
        }
        index.insert(key, members.size());
        members.emplace_back(std::move(key), std::move(value));
    }
};

// A container being read. Containers nest on this explicit stack, not on the C++ call stack.
struct Frame {
    bool isArray = false;
    Array items;              // isArray
    ObjectBuilder object;     // !isArray
    QString key;              // !isArray: the key whose value is being read
};

class Reader {
public:
    explicit Reader(const QString& text) : text_(text.constData()), size_(text.size()) {}

    std::optional<Value> read() {
        skipWhite();
        while (true) {
            // a value starts at at_
            if (at_ >= size_) return std::nullopt;
            const char16_t c = text_[at_].unicode();
            Value value;
            if (c == u'{') {
                ++at_;
                skipWhite();
                if (peek() == u'}') {
                    ++at_;
                    value = Value(Object{});
                } else {
                    Frame frame;
                    if (!readKeyAndColon(frame.key)) return std::nullopt;
                    stack_.push_back(std::move(frame));
                    continue;
                }
            } else if (c == u'[') {
                if (openArrays_ == maxArrayDepth) return std::nullopt;
                ++at_;
                skipWhite();
                if (peek() == u']') {
                    ++at_;
                    value = Value(Array{});
                } else {
                    Frame frame;
                    frame.isArray = true;
                    stack_.push_back(std::move(frame));
                    ++openArrays_;
                    continue;
                }
            } else if (c == u'"') {
                QString content;
                if (!readString(content)) return std::nullopt;
                value = Value(Primitive{true, std::move(content)});
            } else if (!endsLiteral(c)) {
                const qsizetype start = at_;
                while (at_ < size_ && !endsLiteral(text_[at_].unicode())) ++at_;
                const QStringView literal(text_ + start, at_ - start);
                if (literal == u"null") value = Value(Null{});
                else value = Value(Primitive{false, literal.toString()});
            } else {
                return std::nullopt;
            }

            // [value] is complete: give it to the containers, closing those that end here
            while (true) {
                skipWhite();
                if (stack_.empty()) {
                    if (at_ != size_) return std::nullopt;
                    return value;
                }
                Frame& top = stack_.back();
                if (top.isArray) {
                    top.items.push_back(std::move(value));
                    const char16_t next = peek();
                    if (next == u',') {
                        ++at_;
                        skipWhite();
                        break;   // the next item
                    }
                    if (next != u']') return std::nullopt;
                    ++at_;
                    // Observed: after an item, a `]` followed (past white space) by the start of a value separates
                    // two items like a comma, and the array goes on: Kotlin reads `[1]x]` as [1, x].
                    skipWhite();
                    if (startsValue(peek())) break;
                    value = Value(std::move(top.items));
                    stack_.pop_back();
                    --openArrays_;
                } else {
                    top.object.put(std::move(top.key), std::move(value));
                    const char16_t next = peek();
                    if (next == u',') {
                        ++at_;
                        skipWhite();
                        if (!readKeyAndColon(top.key)) return std::nullopt;
                        break;   // the next member's value
                    }
                    if (next != u'}') return std::nullopt;
                    ++at_;
                    value = Value(std::move(top.object.members));
                    stack_.pop_back();
                }
            }
        }
    }

private:
    // the unit at at_, or U+0000 at the end (which no rule takes as a token of its own)
    char16_t peek() const { return at_ < size_ ? text_[at_].unicode() : char16_t(0); }

    void skipWhite() {
        while (at_ < size_ && isWhite(text_[at_].unicode())) ++at_;
    }

    // a key (a string, nothing else), white space, `:` and white space
    bool readKeyAndColon(QString& key) {
        if (peek() != u'"' || !readString(key)) return false;
        skipWhite();
        if (peek() != u':') return false;
        ++at_;
        skipWhite();
        return true;
    }

    // the string that starts at the `"` at at_; false when it is not closed or has an escape Kotlin rejects
    bool readString(QString& out) {
        out.clear();
        ++at_;
        qsizetype run = at_;
        while (at_ < size_) {
            const char16_t c = text_[at_].unicode();
            if (c == u'"') {
                if (out.isEmpty()) out = QString(text_ + run, at_ - run);
                else out.append(text_ + run, at_ - run);
                ++at_;
                return true;
            }
            if (c != u'\\') {
                ++at_;
                continue;
            }
            out.append(text_ + run, at_ - run);
            ++at_;
            if (at_ >= size_) return false;
            const char16_t e = text_[at_].unicode();
            ++at_;
            switch (e) {
            case u'"': out.append(QChar(u'"')); break;
            case u'\\': out.append(QChar(u'\\')); break;
            case u'/': out.append(QChar(u'/')); break;
            case u'b': out.append(QChar(u'\b')); break;
            case u'f': out.append(QChar(u'\f')); break;
            case u'n': out.append(QChar(u'\n')); break;
            case u'r': out.append(QChar(u'\r')); break;
            case u't': out.append(QChar(u'\t')); break;
            case u'u': {
                if (size_ - at_ < 4) return false;
                int unit = 0;
                for (int k = 0; k < 4; ++k) {
                    const int digit = hexDigit(text_[at_ + k].unicode());
                    if (digit < 0) return false;
                    unit = unit * 16 + digit;
                }
                at_ += 4;
                out.append(QChar(char16_t(unit)));
                break;
            }
            default:
                return false;
            }
            run = at_;
        }
        return false;
    }

    const QChar* text_;
    qsizetype size_;
    qsizetype at_ = 0;
    int openArrays_ = 0;
    std::vector<Frame> stack_;
};

// ---- the dump ----

void appendHex(QString& out, QStringView text) {
    if (text.isEmpty()) {
        out.append(u'.');
        return;
    }
    static constexpr char16_t digits[] = u"0123456789abcdef";
    for (const QChar c : text) {
        const char16_t unit = c.unicode();
        const char16_t hex[4] = {digits[(unit >> 12) & 15], digits[(unit >> 8) & 15], digits[(unit >> 4) & 15], digits[unit & 15]};
        out.append(reinterpret_cast<const QChar*>(hex), 4);
    }
}

}  // namespace

// ---- Value: copy and destruction without recursion ----

Value::Value(const Value& other) {
    struct Task {
        const Value* from;
        Value* to;
    };
    std::vector<Task> tasks{{&other, this}};
    while (!tasks.empty()) {
        const Task task = tasks.back();
        tasks.pop_back();
        if (const Primitive* primitive = std::get_if<Primitive>(&task.from->data)) {
            task.to->data = *primitive;
        } else if (const Object* object = std::get_if<Object>(&task.from->data)) {
            Object& copy = task.to->data.emplace<Object>();
            copy.reserve(object->size());
            for (const auto& member : *object) copy.emplace_back(member.first, Value());
            // the copies stay where they are: `copy` does not grow any more
            for (std::size_t i = 0; i < object->size(); ++i) tasks.push_back({&(*object)[i].second, &copy[i].second});
        } else if (const Array* array = std::get_if<Array>(&task.from->data)) {
            Array& copy = task.to->data.emplace<Array>(array->size());
            for (std::size_t i = 0; i < array->size(); ++i) tasks.push_back({&(*array)[i], &copy[i]});
        } else {
            task.to->data = Null{};
        }
    }
}

Value& Value::operator=(const Value& other) {
    if (this != &other) {
        Value copy(other);
        *this = std::move(copy);
    }
    return *this;
}

// The value held before is destroyed element by element, each by ~Value, so this does not recurse either.
Value& Value::operator=(Value&& other) noexcept = default;

Value::~Value() {
    if (!hasChildren(*this)) return;
    std::vector<Value> pending;
    takeChildren(*this, pending);
    while (!pending.empty()) {
        Value value = std::move(pending.back());
        pending.pop_back();
        takeChildren(value, pending);
    }
}

std::optional<Value> parse(const QString& text) {
    return Reader(text).read();
}

const Value* find(const Object& object, QStringView key) {
    for (const auto& member : object) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

QString dump(const Value& value) {
    struct Open {
        const Value* container;
        std::size_t next;
    };
    QString out;
    std::vector<Open> stack;
    // writes [v] whole when it is a leaf, or its opening when it is a container (whose contents come from the loop)
    auto start = [&](const Value& v) {
        if (const Primitive* primitive = std::get_if<Primitive>(&v.data)) {
            out.append(primitive->isString ? u'S' : u'L');
            appendHex(out, primitive->content);
        } else if (std::holds_alternative<Object>(v.data)) {
            out.append(u"O{");
            stack.push_back({&v, 0});
        } else if (std::holds_alternative<Array>(v.data)) {
            out.append(u"A[");
            stack.push_back({&v, 0});
        } else {
            out.append(u'N');
        }
    };
    start(value);
    while (!stack.empty()) {
        const std::size_t index = stack.back().next++;
        const Value* container = stack.back().container;
        if (const Object* object = std::get_if<Object>(&container->data)) {
            if (index == object->size()) {
                out.append(u'}');
                stack.pop_back();
                continue;
            }
            if (index > 0) out.append(u',');
            appendHex(out, (*object)[index].first);
            out.append(u':');
            start((*object)[index].second);
        } else {
            const Array& array = std::get<Array>(container->data);
            if (index == array.size()) {
                out.append(u']');
                stack.pop_back();
                continue;
            }
            if (index > 0) out.append(u',');
            start(array[index]);
        }
    }
    return out;
}

}  // namespace xgs::json
