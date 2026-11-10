#include "Json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace zplus::json {
namespace {

void appendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7f) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else if (codePoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else if (codePoint <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else {
        throw std::invalid_argument("JSON string contains an invalid Unicode code point");
    }
}

std::uint32_t decodeUtf8(std::string_view input, std::size_t& offset) {
    if (offset >= input.size()) throw std::invalid_argument("invalid UTF-8 in JSON string");
    const auto first = static_cast<unsigned char>(input[offset++]);
    if (first < 0x80) return first;
    std::size_t continuationCount = 0;
    std::uint32_t result = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        continuationCount = 1;
        result = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
        continuationCount = 2;
        result = first & 0x0f;
    } else if (first >= 0xf0 && first <= 0xf4) {
        continuationCount = 3;
        result = first & 0x07;
    } else {
        throw std::invalid_argument("invalid UTF-8 in JSON string");
    }
    if (continuationCount > input.size() - offset) {
        throw std::invalid_argument("invalid UTF-8 in JSON string");
    }
    for (std::size_t i = 0; i < continuationCount; ++i) {
        const auto next = static_cast<unsigned char>(input[offset++]);
        if ((next & 0xc0) != 0x80) {
            throw std::invalid_argument("invalid UTF-8 in JSON string");
        }
        result = (result << 6) | (next & 0x3f);
    }
    if ((continuationCount == 1 && result < 0x80) ||
        (continuationCount == 2 && result < 0x800) ||
        (continuationCount == 3 && result < 0x10000) ||
        (result >= 0xd800 && result <= 0xdfff) || result > 0x10ffff) {
        throw std::invalid_argument("invalid UTF-8 in JSON string");
    }
    return result;
}

class Parser {
public:
    Parser(std::string_view source, std::size_t maximumDepth)
        : source_(source), maximumDepth_(maximumDepth) {
        if (maximumDepth_ == 0) {
            throw std::invalid_argument("JSON maximum nesting depth must be positive");
        }
    }

    Value run() {
        skipWhitespace();
        Value result = parseValue(0);
        skipWhitespace();
        if (offset_ != source_.size()) fail("unexpected trailing data");
        return result;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::invalid_argument(
            std::string("invalid JSON at byte ") + std::to_string(offset_) + ": " + message);
    }

    void skipWhitespace() {
        while (offset_ < source_.size()) {
            const char ch = source_[offset_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++offset_;
        }
    }

    bool consume(char expected) {
        if (offset_ < source_.size() && source_[offset_] == expected) {
            ++offset_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) fail("unexpected character");
    }

    Value parseValue(std::size_t depth) {
        if (depth > maximumDepth_) fail("maximum nesting depth exceeded");
        if (offset_ >= source_.size()) fail("expected a value");
        switch (source_[offset_]) {
            case 'n': return parseLiteral("null", nullptr);
            case 't': return parseLiteral("true", true);
            case 'f': return parseLiteral("false", false);
            case '"': return Value{parseString()};
            case '[': return parseArray(depth);
            case '{': return parseObject(depth);
            default:
                if (source_[offset_] == '-' ||
                    (source_[offset_] >= '0' && source_[offset_] <= '9')) {
                    return Value{parseNumber()};
                }
                fail("expected a value");
        }
    }

    template<typename T>
    Value parseLiteral(std::string_view literal, T result) {
        if (source_.substr(offset_, literal.size()) != literal) fail("invalid literal");
        offset_ += literal.size();
        return Value{result};
    }

    std::uint32_t parseHex4() {
        if (source_.size() - offset_ < 4) fail("incomplete Unicode escape");
        std::uint32_t result = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = source_[offset_++];
            result <<= 4;
            if (ch >= '0' && ch <= '9') result |= static_cast<std::uint32_t>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') result |= static_cast<std::uint32_t>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') result |= static_cast<std::uint32_t>(ch - 'A' + 10);
            else fail("invalid hexadecimal digit in Unicode escape");
        }
        return result;
    }

    std::string parseString() {
        expect('"');
        std::string result;
        while (offset_ < source_.size()) {
            const auto ch = static_cast<unsigned char>(source_[offset_++]);
            if (ch == '"') return result;
            if (ch < 0x20) fail("unescaped control character in string");
            if (ch == '\\') {
                if (offset_ >= source_.size()) fail("incomplete escape");
                switch (source_[offset_++]) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t codePoint = parseHex4();
                        if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
                            if (source_.size() - offset_ < 6 ||
                                source_[offset_] != '\\' || source_[offset_ + 1] != 'u') {
                                fail("high surrogate must be followed by a low surrogate");
                            }
                            offset_ += 2;
                            const std::uint32_t low = parseHex4();
                            if (low < 0xdc00 || low > 0xdfff) {
                                fail("invalid low surrogate");
                            }
                            codePoint = 0x10000 +
                                ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                        } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
                            fail("unexpected low surrogate");
                        }
                        appendUtf8(result, codePoint);
                        break;
                    }
                    default: fail("invalid string escape");
                }
            } else if (ch < 0x80) {
                result.push_back(static_cast<char>(ch));
            } else {
                --offset_;
                const std::uint32_t codePoint = decodeUtf8(source_, offset_);
                appendUtf8(result, codePoint);
            }
        }
        fail("unterminated string");
    }

    double parseNumber() {
        const std::size_t begin = offset_;
        consume('-');
        if (consume('0')) {
            if (offset_ < source_.size() && source_[offset_] >= '0' && source_[offset_] <= '9') {
                fail("leading zero in number");
            }
        } else {
            if (offset_ >= source_.size() || source_[offset_] < '1' || source_[offset_] > '9') {
                fail("invalid number");
            }
            while (offset_ < source_.size() && source_[offset_] >= '0' && source_[offset_] <= '9') {
                ++offset_;
            }
        }
        if (consume('.')) {
            const std::size_t fractionStart = offset_;
            while (offset_ < source_.size() && source_[offset_] >= '0' && source_[offset_] <= '9') {
                ++offset_;
            }
            if (fractionStart == offset_) fail("fraction requires at least one digit");
        }
        if (consume('e') || consume('E')) {
            if (!consume('+')) consume('-');
            const std::size_t exponentStart = offset_;
            while (offset_ < source_.size() && source_[offset_] >= '0' && source_[offset_] <= '9') {
                ++offset_;
            }
            if (exponentStart == offset_) fail("exponent requires at least one digit");
        }
        double result = 0;
        const auto text = source_.substr(begin, offset_ - begin);
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), result, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            !std::isfinite(result)) {
            fail("number is outside the supported range");
        }
        return result;
    }

    Value parseArray(std::size_t depth) {
        expect('[');
        skipWhitespace();
        Value::Array values;
        if (consume(']')) return Value{std::move(values)};
        for (;;) {
            skipWhitespace();
            values.push_back(parseValue(depth + 1));
            skipWhitespace();
            if (consume(']')) break;
            expect(',');
        }
        return Value{std::move(values)};
    }

    Value parseObject(std::size_t depth) {
        expect('{');
        skipWhitespace();
        Value::Object values;
        if (consume('}')) return Value{std::move(values)};
        for (;;) {
            skipWhitespace();
            if (offset_ >= source_.size() || source_[offset_] != '"') {
                fail("object keys must be strings");
            }
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            skipWhitespace();
            values.emplace_back(std::move(key), parseValue(depth + 1));
            skipWhitespace();
            if (consume('}')) break;
            expect(',');
        }
        return Value{std::move(values)};
    }

    std::string_view source_;
    std::size_t maximumDepth_;
    std::size_t offset_ = 0;
};

void appendEscaped(std::string& output, std::string_view value) {
    output.push_back('"');
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t offset = 0; offset < value.size();) {
        const auto ch = static_cast<unsigned char>(value[offset]);
        switch (ch) {
            case '"': output += "\\\""; ++offset; break;
            case '\\': output += "\\\\"; ++offset; break;
            case '\b': output += "\\b"; ++offset; break;
            case '\f': output += "\\f"; ++offset; break;
            case '\n': output += "\\n"; ++offset; break;
            case '\r': output += "\\r"; ++offset; break;
            case '\t': output += "\\t"; ++offset; break;
            default:
                if (ch < 0x20) {
                    output += "\\u00";
                    output.push_back(hex[ch >> 4]);
                    output.push_back(hex[ch & 0x0f]);
                    ++offset;
                } else if (ch < 0x80) {
                    output.push_back(static_cast<char>(ch));
                    ++offset;
                } else {
                    appendUtf8(output, decodeUtf8(value, offset));
                }
        }
    }
    output.push_back('"');
}

void appendSerialized(std::string& output, const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value.data)) {
        output += "null";
    } else if (const auto* boolean = std::get_if<bool>(&value.data)) {
        output += *boolean ? "true" : "false";
    } else if (const auto* number = std::get_if<double>(&value.data)) {
        if (!std::isfinite(*number)) {
            throw std::invalid_argument("JSON numbers must be finite");
        }
        char buffer[64];
        const auto result = std::to_chars(
            buffer, buffer + sizeof(buffer), *number, std::chars_format::general);
        if (result.ec != std::errc{}) throw std::runtime_error("cannot serialize JSON number");
        output.append(buffer, result.ptr);
    } else if (const auto* string = std::get_if<std::string>(&value.data)) {
        appendEscaped(output, *string);
    } else if (const auto* array = std::get_if<Value::Array>(&value.data)) {
        output.push_back('[');
        for (std::size_t i = 0; i < array->size(); ++i) {
            if (i) output.push_back(',');
            appendSerialized(output, (*array)[i]);
        }
        output.push_back(']');
    } else {
        const auto& object = std::get<Value::Object>(value.data);
        output.push_back('{');
        for (std::size_t i = 0; i < object.size(); ++i) {
            if (i) output.push_back(',');
            appendEscaped(output, object[i].first);
            output.push_back(':');
            appendSerialized(output, object[i].second);
        }
        output.push_back('}');
    }
}

std::string decodePointerToken(std::string_view token) {
    std::string result;
    result.reserve(token.size());
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] != '~') {
            result.push_back(token[i]);
            continue;
        }
        if (++i >= token.size()) throw std::invalid_argument("invalid JSON Pointer escape");
        if (token[i] == '0') result.push_back('~');
        else if (token[i] == '1') result.push_back('/');
        else throw std::invalid_argument("invalid JSON Pointer escape");
    }
    return result;
}

const Value* objectMember(const Value::Object& object, std::string_view name) {
    for (auto item = object.rbegin(); item != object.rend(); ++item) {
        if (item->first == name) return &item->second;
    }
    return nullptr;
}

const Value* resolvePointerToken(const Value& value, std::string_view token) {
    if (const auto* object = std::get_if<Value::Object>(&value.data)) {
        return objectMember(*object, decodePointerToken(token));
    }
    if (const auto* array = std::get_if<Value::Array>(&value.data)) {
        if (token.empty() || (token.size() > 1 && token.front() == '0')) return nullptr;
        std::size_t index = 0;
        for (const char digit : token) {
            if (digit < '0' || digit > '9' ||
                index > (std::numeric_limits<std::size_t>::max() - 9) / 10) {
                return nullptr;
            }
            index = index * 10 + static_cast<std::size_t>(digit - '0');
        }
        return index < array->size() ? &(*array)[index] : nullptr;
    }
    return nullptr;
}

const Value* requiredPointer(std::string_view source, std::string_view pointer, Value& root) {
    root = parse(source);
    const Value* result = atPointer(root, pointer);
    if (!result) throw std::out_of_range("JSON Pointer does not identify a value");
    return result;
}

} // namespace

Value parse(std::string_view source, std::size_t maximumDepth) {
    return Parser(source, maximumDepth).run();
}

std::string serialize(const Value& value) {
    std::string result;
    appendSerialized(result, value);
    return result;
}

std::string quote(std::string_view value) {
    std::string result;
    appendEscaped(result, value);
    return result;
}

const Value* atPointer(const Value& root, std::string_view pointer) {
    if (pointer.empty()) return &root;
    if (pointer.front() != '/') {
        throw std::invalid_argument("JSON Pointer must be empty or begin with '/'");
    }
    const Value* current = &root;
    std::size_t begin = 1;
    for (;;) {
        const std::size_t end = pointer.find('/', begin);
        current = resolvePointerToken(
            *current,
            pointer.substr(begin, end == std::string_view::npos
                ? pointer.size() - begin
                : end - begin));
        if (!current || end == std::string_view::npos) return current;
        begin = end + 1;
    }
}

std::string kind(const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value.data)) return "null";
    if (std::holds_alternative<bool>(value.data)) return "boolean";
    if (std::holds_alternative<double>(value.data)) return "number";
    if (std::holds_alternative<std::string>(value.data)) return "string";
    if (std::holds_alternative<Value::Array>(value.data)) return "array";
    return "object";
}

std::string compact(std::string_view source) {
    return serialize(parse(source));
}

bool valid(std::string_view source) {
    try {
        (void)parse(source);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

std::string getRaw(std::string_view source, std::string_view pointer) {
    Value root;
    return serialize(*requiredPointer(source, pointer, root));
}

std::string getString(std::string_view source, std::string_view pointer) {
    Value root;
    const Value* value = requiredPointer(source, pointer, root);
    if (const auto* result = std::get_if<std::string>(&value->data)) return *result;
    throw std::invalid_argument("JSON value at pointer is not a string");
}

double getNumber(std::string_view source, std::string_view pointer) {
    Value root;
    const Value* value = requiredPointer(source, pointer, root);
    if (const auto* result = std::get_if<double>(&value->data)) return *result;
    throw std::invalid_argument("JSON value at pointer is not a number");
}

bool getBoolean(std::string_view source, std::string_view pointer) {
    Value root;
    const Value* value = requiredPointer(source, pointer, root);
    if (const auto* result = std::get_if<bool>(&value->data)) return *result;
    throw std::invalid_argument("JSON value at pointer is not a boolean");
}

std::size_t arrayLength(std::string_view source, std::string_view pointer) {
    Value root;
    const Value* value = requiredPointer(source, pointer, root);
    if (const auto* result = std::get_if<Value::Array>(&value->data)) return result->size();
    throw std::invalid_argument("JSON value at pointer is not an array");
}

std::string arrayGet(std::string_view source, std::string_view pointer, std::size_t index) {
    Value root;
    const Value* value = requiredPointer(source, pointer, root);
    const auto* array = std::get_if<Value::Array>(&value->data);
    if (!array) throw std::invalid_argument("JSON value at pointer is not an array");
    if (index >= array->size()) throw std::out_of_range("JSON array index is out of bounds");
    return serialize((*array)[index]);
}

std::string valueKind(std::string_view source, std::string_view pointer) {
    Value root;
    return kind(*requiredPointer(source, pointer, root));
}

} // namespace zplus::json
