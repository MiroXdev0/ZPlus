#include "Web.hpp"

#include "Json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace zplus::web {
namespace {

const json::Value* member(const json::Value& value, std::string_view name) {
    const auto* object = std::get_if<json::Value::Object>(&value.data);
    if (!object) return nullptr;
    for (auto item = object->rbegin(); item != object->rend(); ++item) {
        if (item->first == name) return &item->second;
    }
    return nullptr;
}

bool flag(const json::Value& object, std::string_view name, bool fallback = false) {
    const auto* value = member(object, name);
    if (!value) return fallback;
    const auto* result = std::get_if<bool>(&value->data);
    if (!result) throw std::invalid_argument("form validation flags must be booleans");
    return *result;
}

std::int64_t limit(const json::Value& object, std::string_view name) {
    const auto* value = member(object, name);
    if (!value) return -1;
    const auto* number = std::get_if<double>(&value->data);
    if (!number || !std::isfinite(*number) || *number < 0 ||
        *number >= std::ldexp(1.0, 63) ||
        std::floor(*number) != *number) {
        throw std::invalid_argument("form validation length limits must be non-negative integers");
    }
    return static_cast<std::int64_t>(*number);
}

std::vector<std::string> values(const json::Value* value) {
    if (!value) return {};
    if (const auto* text = std::get_if<std::string>(&value->data)) return {*text};
    if (const auto* array = std::get_if<json::Value::Array>(&value->data)) {
        std::vector<std::string> result;
        result.reserve(array->size());
        for (const auto& item : *array) {
            const auto* text = std::get_if<std::string>(&item.data);
            if (!text) throw std::invalid_argument("form values must be strings");
            result.push_back(*text);
        }
        return result;
    }
    throw std::invalid_argument("form values must be strings or arrays of strings");
}

std::size_t utf8Length(std::string_view text) {
    (void)json::quote(text);
    std::size_t count = 0;
    for (const unsigned char ch : text) {
        if ((ch & 0xc0) != 0x80) ++count;
    }
    return count;
}

std::optional<double> finiteNumber(std::string_view text) {
    if (text.empty()) return std::nullopt;
    double value = 0;
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (error != std::errc{} || end != text.data() + text.size() ||
        !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<double> numberOption(const json::Value& object, std::string_view name) {
    const auto* value = member(object, name);
    if (!value) return std::nullopt;
    const auto* number = std::get_if<double>(&value->data);
    if (!number || !std::isfinite(*number)) {
        throw std::invalid_argument("form validation numeric bounds must be finite numbers");
    }
    return *number;
}

bool validEmail(std::string_view value) {
    if (value.empty() || value.size() > 254 ||
        std::any_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch <= 0x20 || ch >= 0x7f;
        })) return false;
    const auto at = value.find('@');
    if (at == std::string_view::npos || at == 0 || at > 64 ||
        at != value.rfind('@') || at + 1 == value.size()) return false;
    const auto local = value.substr(0, at);
    if (local.front() == '.' || local.back() == '.' ||
        local.find("..") != std::string_view::npos ||
        !std::all_of(local.begin(), local.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '.' || ch == '!' ||
                ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
                ch == '\'' || ch == '*' || ch == '+' || ch == '-' ||
                ch == '/' || ch == '=' || ch == '?' || ch == '^' ||
                ch == '_' || ch == '`' || ch == '{' || ch == '|' ||
                ch == '}' || ch == '~';
        })) return false;
    const auto domain = value.substr(at + 1);
    if (domain.size() < 3 || domain.front() == '.' || domain.back() == '.' ||
        domain.find('.') == std::string_view::npos) return false;
    std::size_t start = 0;
    while (start < domain.size()) {
        const auto dot = domain.find('.', start);
        const auto label = domain.substr(start,
            dot == std::string_view::npos ? domain.size() - start : dot - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' ||
            label.back() == '-' ||
            !std::all_of(label.begin(), label.end(), [](unsigned char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '-';
            })) return false;
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return true;
}

void addError(
    json::Value::Array& errors,
    const std::string& field,
    const std::string& rule,
    const std::string& message
) {
    json::Value::Object error;
    error.emplace_back("field", json::Value{field});
    error.emplace_back("rule", json::Value{rule});
    error.emplace_back("message", json::Value{message});
    errors.emplace_back(std::move(error));
}

} // namespace

bool formHas(const std::string& formJson, const std::string& name) {
    const auto form = json::parse(formJson);
    if (!std::holds_alternative<json::Value::Object>(form.data)) {
        throw std::invalid_argument("form data must be a JSON object");
    }
    return member(form, name) != nullptr;
}

double formNumber(const std::string& formJson, const std::string& name) {
    const auto form = json::parse(formJson);
    if (!std::holds_alternative<json::Value::Object>(form.data)) {
        throw std::invalid_argument("form data must be a JSON object");
    }
    const auto textValues = values(member(form, name));
    if (textValues.empty()) throw std::out_of_range("form field is missing");
    double result = 0;
    const auto& text = textValues.front();
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), result, std::chars_format::general);
    if (error != std::errc{} || end != text.data() + text.size() ||
        !std::isfinite(result)) {
        throw std::invalid_argument("form field is not a finite number");
    }
    return result;
}

std::string validateForm(const std::string& formJson, const std::string& rulesJson) {
    const auto form = json::parse(formJson);
    if (!std::holds_alternative<json::Value::Object>(form.data)) {
        throw std::invalid_argument("form data must be a JSON object");
    }
    const auto rules = json::parse(rulesJson);
    const auto* ruleArray = std::get_if<json::Value::Array>(&rules.data);
    if (!ruleArray) throw std::invalid_argument("form validation rules must be a JSON array");

    json::Value::Array errors;
    for (const auto& rule : *ruleArray) {
        const auto* nameValue = member(rule, "name");
        const auto* name = nameValue ? std::get_if<std::string>(&nameValue->data) : nullptr;
        if (!name || name->empty()) {
            throw std::invalid_argument("each form validation rule requires a non-empty name");
        }
        const auto fieldValues = values(member(form, *name));
        const bool required = flag(rule, "required");
        const bool numericRule = flag(rule, "numeric");
        const bool emailRule = flag(rule, "email");
        const std::int64_t minLength = limit(rule, "minLength");
        const std::int64_t maxLength = limit(rule, "maxLength");
        const auto minimum = numberOption(rule, "minimum");
        const auto maximum = numberOption(rule, "maximum");
        if (minLength >= 0 && maxLength >= 0 && minLength > maxLength) {
            throw std::invalid_argument("minimum form length cannot exceed maximum length");
        }
        if (minimum && maximum && *minimum > *maximum) {
            throw std::invalid_argument("minimum form number cannot exceed maximum number");
        }
        const auto* allowedValue = member(rule, "allowed");
        const json::Value::Array* allowed = nullptr;
        if (allowedValue) {
            allowed = std::get_if<json::Value::Array>(&allowedValue->data);
            if (!allowed) throw std::invalid_argument("allowed form values must be a JSON array");
            for (const auto& candidate : *allowed) {
                if (!std::holds_alternative<std::string>(candidate.data)) {
                    throw std::invalid_argument("allowed values must be strings");
                }
            }
        }
        const bool empty = fieldValues.empty() ||
            std::all_of(fieldValues.begin(), fieldValues.end(), [](const auto& value) {
                return value.empty();
            });
        if (empty) {
            if (required) {
                addError(errors, *name, "required", "This field is required.");
            }
            continue;
        }
        for (const auto& fieldValue : fieldValues) {
            const std::size_t length = utf8Length(fieldValue);
            if (minLength >= 0 && length < static_cast<std::size_t>(minLength)) {
                addError(errors, *name, "minLength",
                    "This field is shorter than the minimum length.");
            }
            if (maxLength >= 0 && length > static_cast<std::size_t>(maxLength)) {
                addError(errors, *name, "maxLength",
                    "This field exceeds the maximum length.");
            }
            const auto number = finiteNumber(fieldValue);
            const bool numberRequired = numericRule || minimum || maximum;
            if (numberRequired && !number) {
                addError(errors, *name, "numeric", "This field must be a finite number.");
            }
            if (number) {
                if (minimum && *number < *minimum) {
                    addError(errors, *name, "minimum",
                        "This field is below the minimum number.");
                }
                if (maximum && *number > *maximum) {
                    addError(errors, *name, "maximum",
                        "This field exceeds the maximum number.");
                }
            }
            if (emailRule && !validEmail(fieldValue)) {
                addError(errors, *name, "email", "This field must be a valid email address.");
            }
            if (allowed) {
                bool found = false;
                for (const auto& candidate : *allowed) {
                    const auto* text = std::get_if<std::string>(&candidate.data);
                    if (*text == fieldValue) found = true;
                }
                if (!found) {
                    addError(errors, *name, "allowed", "This field has an unsupported value.");
                }
            }
        }
    }
    json::Value::Object result;
    result.emplace_back("valid", json::Value{errors.empty()});
    result.emplace_back("errors", json::Value{std::move(errors)});
    return json::serialize(json::Value{std::move(result)});
}

} // namespace zplus::web
