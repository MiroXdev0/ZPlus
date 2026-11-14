#include "Web.hpp"

#include "Base64.hpp"
#include "Json.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <limits>
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

json::Value* member(json::Value& value, std::string_view name) {
    auto* object = std::get_if<json::Value::Object>(&value.data);
    if (!object) return nullptr;
    for (auto item = object->rbegin(); item != object->rend(); ++item) {
        if (item->first == name) return &item->second;
    }
    return nullptr;
}

std::string requiredString(const json::Value& value, std::string_view name) {
    const auto* item = member(value, name);
    const auto* text = item ? std::get_if<std::string>(&item->data) : nullptr;
    if (!text) throw std::invalid_argument("middleware option '" +
        std::string(name) + "' must be a string");
    return *text;
}

std::string optionalString(
    const json::Value& value,
    std::string_view name,
    std::string fallback = {}
) {
    const auto* item = member(value, name);
    if (!item) return fallback;
    const auto* text = std::get_if<std::string>(&item->data);
    if (!text) throw std::invalid_argument("middleware option '" +
        std::string(name) + "' must be a string");
    return *text;
}

std::int64_t integerOption(const json::Value& value, std::string_view name) {
    const auto* item = member(value, name);
    const auto* number = item ? std::get_if<double>(&item->data) : nullptr;
    if (!number || !std::isfinite(*number) || *number < 1 ||
        *number >= std::ldexp(1.0, 63) ||
        std::floor(*number) != *number) {
        throw std::invalid_argument("middleware option '" +
            std::string(name) + "' must be a positive integer");
    }
    return static_cast<std::int64_t>(*number);
}

bool supportedMiddleware(std::string_view name) {
    static constexpr std::string_view supported[] = {
        "securityHeaders", "cors", "requireHeader", "maxBodyBytes",
        "setRequestHeader", "setResponseHeader", "requestTiming", "requestLog",
        "jsonBody", "errorResponse"
    };
    return std::find(std::begin(supported), std::end(supported), name) !=
        std::end(supported);
}

bool booleanOption(const json::Value& value, std::string_view name) {
    const auto* item = member(value, name);
    if (!item) return false;
    const auto* boolean = std::get_if<bool>(&item->data);
    if (!boolean) throw std::invalid_argument("middleware option '" +
        std::string(name) + "' must be a boolean");
    return *boolean;
}

json::Value::Object& object(json::Value& value, const char* description) {
    auto* result = std::get_if<json::Value::Object>(&value.data);
    if (!result) throw std::invalid_argument(std::string(description) + " must be a JSON object");
    return *result;
}

const json::Value::Array& array(const json::Value& value, const char* description) {
    const auto* result = std::get_if<json::Value::Array>(&value.data);
    if (!result) throw std::invalid_argument(std::string(description) + " must be a JSON array");
    return *result;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return std::string(value.substr(first, last - first + 1));
}

void setHeader(json::Value::Object& headers, std::string name, std::string value) {
    if (name.empty() ||
        std::any_of(value.begin(), value.end(), [](unsigned char ch) {
            return (ch < 0x20 && ch != '\t') || ch == 0x7f;
        }) ||
        std::any_of(name.begin(), name.end(), [](unsigned char ch) {
            const bool alphaNumeric = (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
            return !(alphaNumeric || ch == '!' || ch == '#' || ch == '$' ||
                ch == '%' || ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
                ch == '-' || ch == '.' || ch == '^' || ch == '_' || ch == '`' ||
                ch == '|' || ch == '~');
        })) {
        throw std::invalid_argument("middleware generated an invalid HTTP header");
    }
    const std::string normalized = lower(name);
    for (auto& field : headers) {
        if (lower(field.first) == normalized) {
            field.first = std::move(name);
            field.second = json::Value{std::move(value)};
            return;
        }
    }
    headers.emplace_back(std::move(name), json::Value{std::move(value)});
}

void addVary(json::Value::Object& headers, std::string_view token) {
    for (auto& field : headers) {
        if (lower(field.first) != "vary") continue;
        const auto* value = std::get_if<std::string>(&field.second.data);
        if (!value) throw std::invalid_argument("Vary header must be a string");
        std::size_t start = 0;
        while (start <= value->size()) {
            const auto separator = value->find(',', start);
            const std::string item = lower(trim(std::string_view(*value).substr(
                start, separator == std::string::npos
                    ? value->size() - start : separator - start)));
            if (item == lower(std::string(token))) return;
            if (separator == std::string::npos) break;
            start = separator + 1;
        }
        field.second = json::Value{*value + ", " + std::string(token)};
        return;
    }
    headers.emplace_back("vary", json::Value{std::string(token)});
}

std::string stringOrEmpty(const json::Value& value, std::string_view key) {
    const auto* item = member(value, key);
    if (!item) return {};
    const auto* text = std::get_if<std::string>(&item->data);
    if (!text) throw std::invalid_argument("HTTP request field must be a string");
    return *text;
}

std::size_t bodySize(const json::Value& request) {
    const std::string base64 = stringOrEmpty(request, "bodyBase64");
    if (!base64.empty()) return encoding::base64Decode(base64).size();
    return stringOrEmpty(request, "body").size();
}

json::Value::Object responseObject(
    std::int64_t status,
    std::string body,
    std::string contentType = "text/plain; charset=utf-8"
) {
    json::Value::Object headers;
    headers.emplace_back("content-type", json::Value{std::move(contentType)});
    json::Value::Object result;
    result.emplace_back("status", json::Value{static_cast<double>(status)});
    result.emplace_back("headers", json::Value{std::move(headers)});
    result.emplace_back("body", json::Value{std::move(body)});
    return result;
}

std::int64_t statusCode(const json::Value& response) {
    const auto* item = member(response, "status");
    const auto* number = item ? std::get_if<double>(&item->data) : nullptr;
    if (!number || !std::isfinite(*number) || *number < 200 || *number > 599 ||
        std::floor(*number) != *number) {
        throw std::invalid_argument("middleware response status must be an HTTP status code");
    }
    return static_cast<std::int64_t>(*number);
}

} // namespace

std::string middlewareUse(
    const std::string& pipelineJson,
    const std::string& name,
    const std::string& optionsJson
) {
    if (!supportedMiddleware(name)) {
        throw std::invalid_argument("unsupported declarative middleware '" + name + "'");
    }
    auto pipeline = json::parse(pipelineJson);
    auto* steps = std::get_if<json::Value::Array>(&pipeline.data);
    if (!steps) throw std::invalid_argument("middleware pipeline must be a JSON array");
    const auto options = json::parse(optionsJson);
    if (!std::holds_alternative<json::Value::Object>(options.data)) {
        throw std::invalid_argument("middleware options must be a JSON object");
    }
    json::Value::Object step;
    step.emplace_back("name", json::Value{name});
    step.emplace_back("options", options);
    steps->emplace_back(std::move(step));
    return json::serialize(pipeline);
}

std::string middlewareBefore(
    const std::string& pipelineJson,
    const std::string& requestJson
) {
    auto pipeline = json::parse(pipelineJson);
    const auto requestSource = json::parse(requestJson);
    auto request = requestSource;
    auto& requestFields = object(request, "request");
    for (const auto& step : array(pipeline, "middleware pipeline")) {
        const std::string name = requiredString(step, "name");
        if (!supportedMiddleware(name)) {
            throw std::invalid_argument("unsupported declarative middleware '" + name + "'");
        }
        const auto* optionsValue = member(step, "options");
        if (!optionsValue) throw std::invalid_argument("middleware step is missing options");
        const auto& options = *optionsValue;
        if (name == "requireHeader") {
            const std::string headerName = requiredString(options, "header");
            const auto* headersValue = member(request, "headers");
            if (!headersValue) throw std::invalid_argument("request is missing headers");
            bool found = false;
            std::string actual;
            const auto* headerObject = std::get_if<json::Value::Object>(&headersValue->data);
            if (!headerObject) throw std::invalid_argument("request headers must be a JSON object");
            for (const auto& [key, value] : *headerObject) {
                if (lower(key) == lower(headerName)) {
                    const auto* text = std::get_if<std::string>(&value.data);
                    if (!text) throw std::invalid_argument("request header values must be strings");
                    actual = *text;
                    found = true;
                }
            }
            const std::string expected = optionalString(options, "equals");
            if (!found || actual.empty() || (!expected.empty() && actual != expected)) {
                const std::int64_t status = member(options, "status")
                    ? integerOption(options, "status") : 401;
                if (status < 200 || status > 599) {
                    throw std::invalid_argument("middleware status must be an HTTP status code");
                }
                json::Value::Object result;
                result.emplace_back("proceed", json::Value{false});
                result.emplace_back("request", std::move(request));
                result.emplace_back("response", json::Value{responseObject(
                    status, optionalString(options, "message", "Unauthorized"))});
                return json::serialize(json::Value{std::move(result)});
            }
        } else if (name == "maxBodyBytes") {
            const auto maximum = integerOption(options, "bytes");
            if (bodySize(request) > static_cast<std::size_t>(maximum)) {
                json::Value::Object result;
                result.emplace_back("proceed", json::Value{false});
                result.emplace_back("request", std::move(request));
                result.emplace_back("response", json::Value{responseObject(
                    413, optionalString(options, "message", "Payload Too Large"))});
                return json::serialize(json::Value{std::move(result)});
            }
        } else if (name == "setRequestHeader") {
            auto* headers = member(request, "headers");
            if (!headers) {
                requestFields.emplace_back("headers",
                    json::Value{json::Value::Object{}});
                headers = &requestFields.back().second;
            }
            setHeader(object(*headers, "request headers"),
                requiredString(options, "name"), requiredString(options, "value"));
        } else if (name == "jsonBody") {
            const std::string contentType = lower([&] {
                const auto* headers = member(request, "headers");
                if (!headers) return std::string{};
                return stringOrEmpty(*headers, "content-type");
            }());
            if (contentType.find("application/json") != std::string::npos) {
                try {
                    (void)json::parse(stringOrEmpty(request, "body"));
                } catch (const std::invalid_argument&) {
                    json::Value::Object result;
                    result.emplace_back("proceed", json::Value{false});
                    result.emplace_back("request", std::move(request));
                    result.emplace_back("response", json::Value{responseObject(
                        400, optionalString(options, "message", "Malformed JSON"))});
                    return json::serialize(json::Value{std::move(result)});
                }
            }
        } else if (name == "requestTiming") {
            const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
            const double start = std::chrono::duration<double, std::milli>(elapsed).count();
            bool replaced = false;
            for (auto& field : requestFields) {
                if (field.first == "_zweb_started_ms") {
                    field.second = json::Value{start};
                    replaced = true;
                }
            }
            if (!replaced) requestFields.emplace_back("_zweb_started_ms", json::Value{start});
        }
    }
    json::Value::Object result;
    result.emplace_back("proceed", json::Value{true});
    result.emplace_back("request", std::move(request));
    result.emplace_back("response", json::Value{nullptr});
    return json::serialize(json::Value{std::move(result)});
}

std::string middlewareAfter(
    const std::string& pipelineJson,
    const std::string& requestJson,
    const std::string& responseJson
) {
    const auto pipeline = json::parse(pipelineJson);
    const auto request = json::parse(requestJson);
    auto response = json::parse(responseJson);
    auto& responseFields = object(response, "response");
    auto* headersValue = member(response, "headers");
    if (!headersValue) {
        responseFields.emplace_back("headers", json::Value{json::Value::Object{}});
        headersValue = &responseFields.back().second;
    }
    auto& headers = object(*headersValue, "response headers");
    json::Value::Array logs;
    for (const auto& step : array(pipeline, "middleware pipeline")) {
        const std::string name = requiredString(step, "name");
        if (!supportedMiddleware(name)) {
            throw std::invalid_argument("unsupported declarative middleware '" + name + "'");
        }
        const auto* optionsValue = member(step, "options");
        if (!optionsValue) throw std::invalid_argument("middleware step is missing options");
        const auto& options = *optionsValue;
        if (name == "securityHeaders") {
            const std::string serialized = json::serialize(json::Value{headers});
            headers = std::get<json::Value::Object>(
                json::parse(securityHeaders(serialized)).data);
        } else if (name == "cors") {
            const std::string origin = optionalString(options, "origin", "*");
            const bool credentials = booleanOption(options, "credentials");
            if (origin == "*" && credentials) {
                throw std::invalid_argument("CORS credentials cannot be enabled with wildcard origin");
            }
            const auto* requestHeadersValue = member(request, "headers");
            const auto* requestHeaders = requestHeadersValue
                ? std::get_if<json::Value::Object>(&requestHeadersValue->data) : nullptr;
            if (requestHeadersValue && !requestHeaders) {
                throw std::invalid_argument("request headers must be a JSON object");
            }
            std::string requestOrigin;
            if (requestHeaders) {
                for (const auto& [headerName, headerValue] : *requestHeaders) {
                    if (lower(headerName) != "origin") continue;
                    const auto* text = std::get_if<std::string>(&headerValue.data);
                    if (!text) throw std::invalid_argument("Origin header must be a string");
                    requestOrigin = *text;
                }
            }
            if (origin != "*") addVary(headers, "Origin");
            if (requestOrigin.empty() ||
                (origin != "*" && requestOrigin != origin)) continue;
            setHeader(headers, "access-control-allow-origin",
                origin == "*" ? "*" : requestOrigin);
            const std::string methods = optionalString(
                options, "methods", "GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS");
            const std::string allowedHeaders = optionalString(options, "headers");
            const std::string exposedHeaders = optionalString(options, "exposedHeaders");
            if (!methods.empty()) setHeader(headers, "access-control-allow-methods", methods);
            if (!allowedHeaders.empty()) {
                setHeader(headers, "access-control-allow-headers", allowedHeaders);
            }
            if (!exposedHeaders.empty()) {
                setHeader(headers, "access-control-expose-headers", exposedHeaders);
            }
            if (credentials) setHeader(headers, "access-control-allow-credentials", "true");
        } else if (name == "setResponseHeader") {
            setHeader(headers, requiredString(options, "name"), requiredString(options, "value"));
        } else if (name == "requestTiming") {
            const auto* started = member(request, "_zweb_started_ms");
            const auto* start = started ? std::get_if<double>(&started->data) : nullptr;
            if (start) {
                const auto now = std::chrono::steady_clock::now().time_since_epoch();
                const double elapsed =
                    std::chrono::duration<double, std::milli>(now).count() - *start;
                setHeader(headers, "server-timing",
                    "app;dur=" + std::to_string(std::max(0.0, elapsed)));
            }
        } else if (name == "errorResponse") {
            const std::int64_t status = statusCode(response);
            if (status >= 500) {
                const std::string message = optionalString(options, "message", "Internal Server Error");
                bool replaced = false;
                for (auto& field : responseFields) {
                    if (field.first == "body") {
                        field.second = json::Value{message};
                        replaced = true;
                    }
                }
                if (!replaced) responseFields.emplace_back("body", json::Value{message});
                setHeader(headers, "content-type", "text/plain; charset=utf-8");
            }
        } else if (name == "requestLog") {
            const std::int64_t status = statusCode(response);
            json::Value::Object entry;
            entry.emplace_back("method", json::Value{stringOrEmpty(request, "method")});
            entry.emplace_back("path", json::Value{stringOrEmpty(request, "path")});
            entry.emplace_back("status", json::Value{static_cast<double>(status)});
            const auto* startedValue = member(request, "_zweb_started_ms");
            const auto* started = startedValue
                ? std::get_if<double>(&startedValue->data) : nullptr;
            if (started) {
                const auto now = std::chrono::steady_clock::now().time_since_epoch();
                const double elapsed =
                    std::chrono::duration<double, std::milli>(now).count() - *started;
                entry.emplace_back("durationMs",
                    json::Value{std::max(0.0, elapsed)});
            } else {
                entry.emplace_back("durationMs", json::Value{nullptr});
            }
            entry.emplace_back("requestBytes",
                json::Value{static_cast<double>(bodySize(request))});
            entry.emplace_back("responseBytes",
                json::Value{static_cast<double>(bodySize(response))});
            entry.emplace_back("errorCategory", json::Value{
                status >= 500 ? "server" : (status >= 400 ? "client" : "none")});
            logs.emplace_back(std::move(entry));
        }
    }
    (void)statusCode(response);
    if (!logs.empty()) responseFields.emplace_back("logs", json::Value{std::move(logs)});
    return json::serialize(response);
}

} // namespace zplus::web
