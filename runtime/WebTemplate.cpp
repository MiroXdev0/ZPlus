#include "Web.hpp"

#include "Json.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace zplus::web {
namespace {

constexpr std::size_t maximumTemplateDepth = 32;
constexpr std::size_t maximumTemplateOutputBytes = 16 * 1024 * 1024;

const json::Value* member(const json::Value& value, std::string_view name) {
    const auto* object = std::get_if<json::Value::Object>(&value.data);
    if (!object) return nullptr;
    for (auto item = object->rbegin(); item != object->rend(); ++item) {
        if (item->first == name) return &item->second;
    }
    return nullptr;
}

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

const json::Value* resolve(
    const json::Value& root,
    const json::Value* current,
    std::size_t index,
    std::string_view path
) {
    if (path == "this") return current;
    if (path == "@index") return nullptr;
    const json::Value* value = &root;
    const bool relativeToCurrent = path.starts_with("this.");
    if (relativeToCurrent) path.remove_prefix(5);
    std::size_t offset = 0;
    const auto firstSeparator = path.find('.');
    const auto firstPart = path.substr(0, firstSeparator);
    if (relativeToCurrent) {
        value = current;
    } else if (!member(root, firstPart) && current) {
        value = current;
    }
    while (value && offset < path.size()) {
        const auto separator = path.find('.', offset);
        const auto part = path.substr(offset,
            separator == std::string_view::npos ? path.size() - offset : separator - offset);
        value = member(*value, part);
        if (separator == std::string_view::npos) break;
        offset = separator + 1;
    }
    (void)index;
    return value;
}

bool truthy(const json::Value* value) {
    if (!value || std::holds_alternative<std::nullptr_t>(value->data)) return false;
    if (const auto* boolean = std::get_if<bool>(&value->data)) return *boolean;
    if (const auto* number = std::get_if<double>(&value->data)) return *number != 0;
    if (const auto* text = std::get_if<std::string>(&value->data)) return !text->empty();
    if (const auto* array = std::get_if<json::Value::Array>(&value->data)) return !array->empty();
    return !std::get<json::Value::Object>(value->data).empty();
}

std::string escapeHtml(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output.push_back(ch); break;
        }
    }
    return output;
}

std::string valueText(const json::Value* value) {
    if (!value || std::holds_alternative<std::nullptr_t>(value->data)) return {};
    if (const auto* text = std::get_if<std::string>(&value->data)) return *text;
    if (std::holds_alternative<json::Value::Array>(value->data) ||
        std::holds_alternative<json::Value::Object>(value->data)) {
        return json::serialize(*value);
    }
    return json::serialize(*value);
}

struct SectionBounds {
    std::size_t bodyEnd = 0;
    std::size_t next = 0;
};

SectionBounds findSectionEnd(
    std::string_view source,
    std::size_t offset,
    std::string_view expectedSection,
    std::size_t depth
) {
    if (depth > maximumTemplateDepth) {
        throw std::length_error("template nesting exceeds 32 levels");
    }
    std::vector<std::string_view> sections{expectedSection};
    while (offset < source.size()) {
        const auto opening = source.find("{{", offset);
        if (opening == std::string_view::npos) {
            break;
        }
        const auto closing = source.find("}}", opening + 2);
        if (closing == std::string_view::npos) {
            throw std::invalid_argument("template contains an unclosed expression");
        }
        const std::string directive = trim(source.substr(opening + 2, closing - opening - 2));
        const std::size_t afterDirective = closing + 2;
        if (directive.starts_with("#if ")) {
            sections.push_back("if");
        } else if (directive.starts_with("#each ")) {
            sections.push_back("each");
        }
        if (sections.size() > maximumTemplateDepth) {
            throw std::length_error("template nesting exceeds 32 levels");
        }
        if (directive == "/if" || directive == "/each") {
            const std::string_view closingSection =
                directive == "/if" ? "if" : "each";
            if (sections.empty() || sections.back() != closingSection) {
                throw std::invalid_argument("template contains an unmatched section close");
            }
            sections.pop_back();
            if (sections.empty()) return {opening, afterDirective};
        } else if (!directive.starts_with("#if ") &&
                !directive.starts_with("#each ") &&
                (directive.starts_with("#") || directive.starts_with("/"))) {
            throw std::invalid_argument("template contains an unsupported section");
        }
        offset = afterDirective;
    }
    throw std::invalid_argument("template section is not closed");
}

bool equalsAsciiCaseInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

void validateInterpolationContexts(std::string_view source) {
    enum class State { Text, Tag, Comment, Script, Style };
    State state = State::Text;
    std::string tagName;
    bool closingTag = false;
    char quote = 0;
    for (std::size_t offset = 0; offset < source.size();) {
        if (source.compare(offset, 2, "{{") == 0) {
            if (state != State::Text) {
                throw std::invalid_argument(
                    "template interpolation is supported only in HTML text nodes");
            }
            const auto closing = source.find("}}", offset + 2);
            if (closing == std::string_view::npos) {
                throw std::invalid_argument("template contains an unclosed expression");
            }
            offset = closing + 2;
            continue;
        }
        if (state == State::Comment) {
            if (source.compare(offset, 3, "-->") == 0) {
                state = State::Text;
                offset += 3;
            } else {
                ++offset;
            }
            continue;
        }
        if (state == State::Script || state == State::Style) {
            const std::string_view rawTag = state == State::Script ? "script" : "style";
            if (offset + rawTag.size() + 2 <= source.size() &&
                source[offset] == '<' && source[offset + 1] == '/' &&
                equalsAsciiCaseInsensitive(source.substr(offset + 2, rawTag.size()), rawTag)) {
                const std::size_t afterName = offset + rawTag.size() + 2;
                if (afterName == source.size() || source[afterName] == '>' ||
                    source[afterName] == '/' || std::isspace(
                        static_cast<unsigned char>(source[afterName]))) {
                    state = State::Tag;
                    tagName = std::string(rawTag);
                    closingTag = true;
                    quote = 0;
                    offset = afterName;
                    continue;
                }
            }
            ++offset;
            continue;
        }
        if (state == State::Tag) {
            const char ch = source[offset++];
            if (quote != 0) {
                if (ch == quote) quote = 0;
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '>') {
                if (!closingTag && quote == 0 && tagName == "script") {
                    state = State::Script;
                } else if (!closingTag && quote == 0 && tagName == "style") {
                    state = State::Style;
                } else {
                    state = State::Text;
                }
                tagName.clear();
                closingTag = false;
            }
            continue;
        }
        if (source.compare(offset, 4, "<!--") == 0) {
            state = State::Comment;
            offset += 4;
            continue;
        }
        if (source[offset] == '<') {
            state = State::Tag;
            ++offset;
            if (offset < source.size() && source[offset] == '/') {
                closingTag = true;
                ++offset;
            } else {
                closingTag = false;
            }
            while (offset < source.size() &&
                std::isspace(static_cast<unsigned char>(source[offset]))) ++offset;
            const std::size_t nameStart = offset;
            while (offset < source.size()) {
                const unsigned char ch = static_cast<unsigned char>(source[offset]);
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '-' || ch == ':')) break;
                ++offset;
            }
            tagName = source.substr(nameStart, offset - nameStart);
            std::transform(tagName.begin(), tagName.end(), tagName.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            continue;
        }
        ++offset;
    }
}

std::string renderRange(
    std::string_view source,
    const json::Value& root,
    const json::Value* current,
    std::size_t index,
    std::size_t depth
) {
    if (depth > maximumTemplateDepth) {
        throw std::length_error("template nesting exceeds 32 levels");
    }
    std::string output;
    std::size_t offset = 0;
    while (offset < source.size()) {
        const auto opening = source.find("{{", offset);
        if (opening == std::string_view::npos) {
            output.append(source.substr(offset));
            break;
        }
        output.append(source.substr(offset, opening - offset));
        const auto closing = source.find("}}", opening + 2);
        if (closing == std::string_view::npos) {
            throw std::invalid_argument("template contains an unclosed expression");
        }
        const std::string directive = trim(source.substr(opening + 2, closing - opening - 2));
        const std::size_t afterDirective = closing + 2;
        if (directive.starts_with("#if ")) {
            const std::string key = trim(std::string_view(directive).substr(4));
            const auto bounds = findSectionEnd(source, afterDirective, "if", depth + 1);
            if (truthy(resolve(root, current, index, key))) {
                output += renderRange(source.substr(
                    afterDirective, bounds.bodyEnd - afterDirective),
                    root, current, index, depth + 1);
            }
            offset = bounds.next;
        } else if (directive.starts_with("#each ")) {
            const std::string key = trim(std::string_view(directive).substr(6));
            const auto bounds = findSectionEnd(source, afterDirective, "each", depth + 1);
            const auto* items = resolve(root, current, index, key);
            if (items) {
                const auto* array = std::get_if<json::Value::Array>(&items->data);
                if (!array) throw std::invalid_argument("template each expression must be an array");
                const std::string_view body = source.substr(
                    afterDirective, bounds.bodyEnd - afterDirective);
                for (std::size_t itemIndex = 0; itemIndex < array->size(); ++itemIndex) {
                    output += renderRange(body, root, &(*array)[itemIndex],
                        itemIndex, depth + 1);
                    if (output.size() > maximumTemplateOutputBytes) {
                        throw std::length_error("rendered template exceeds 16 MiB");
                    }
                }
            }
            offset = bounds.next;
        } else if (directive.starts_with("!")) {
            offset = afterDirective;
        } else if (directive.starts_with("#") || directive.starts_with("/")) {
            throw std::invalid_argument("template contains an unsupported section");
        } else {
            std::string text;
            if (directive == "@index") {
                text = std::to_string(index);
            } else {
                text = valueText(resolve(root, current, index, directive));
            }
            output += escapeHtml(text);
            offset = afterDirective;
        }
        if (output.size() > maximumTemplateOutputBytes) {
            throw std::length_error("rendered template exceeds 16 MiB");
        }
    }
    if (output.size() > maximumTemplateOutputBytes) {
        throw std::length_error("rendered template exceeds 16 MiB");
    }
    return output;
}

} // namespace

std::string loadTemplate(
    const std::string& root,
    const std::string& path,
    std::int64_t maximumBytes
) {
    std::string source = staticFile(root, path, maximumBytes);
    (void)json::quote(source);
    return source;
}

std::string renderTemplate(const std::string& source, const std::string& contextJson) {
    if (source.size() > maximumTemplateOutputBytes) {
        throw std::length_error("template source exceeds 16 MiB");
    }
    validateInterpolationContexts(source);
    const auto context = json::parse(contextJson);
    return renderRange(source, context, nullptr, 0, 0);
}

} // namespace zplus::web
