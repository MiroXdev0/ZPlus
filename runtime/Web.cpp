#include "Web.hpp"

#include "Http.hpp"
#include "Json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace zplus::web {
namespace {

constexpr std::size_t maximumTextBytes = 16 * 1024 * 1024;
constexpr std::size_t maximumFormFields = 4096;

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

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::string decode(std::string_view value, bool plusAsSpace) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%') {
            if (index + 2 >= value.size()) {
                throw std::invalid_argument("malformed percent escape in web value");
            }
            const int high = hexValue(value[index + 1]);
            const int lowValue = hexValue(value[index + 2]);
            if (high < 0 || lowValue < 0) {
                throw std::invalid_argument("malformed percent escape in web value");
            }
            result.push_back(static_cast<char>((high << 4) | lowValue));
            index += 2;
        } else if (plusAsSpace && value[index] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(value[index]);
        }
    }
    return result;
}

bool unreserved(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

bool validToken(std::string_view value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        const bool alphaNumeric = (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        return alphaNumeric || ch == '!' || ch == '#' || ch == '$' ||
            ch == '%' || ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
            ch == '-' || ch == '.' || ch == '^' || ch == '_' || ch == '`' ||
            ch == '|' || ch == '~';
    });
}

bool hasControl(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch < 0x20 || ch == 0x7f;
    });
}

void requireWebSize(std::string_view value, const char* description) {
    if (value.size() > maximumTextBytes) {
        throw std::length_error(std::string(description) + " exceeds the 16 MiB limit");
    }
}

const json::Value::Object& requireObject(const json::Value& value, const char* context) {
    const auto* object = std::get_if<json::Value::Object>(&value.data);
    if (!object) throw std::invalid_argument(std::string(context) + " must be a JSON object");
    return *object;
}

const json::Value::Array& requireArray(const json::Value& value, const char* context) {
    const auto* array = std::get_if<json::Value::Array>(&value.data);
    if (!array) throw std::invalid_argument(std::string(context) + " must be a JSON array");
    return *array;
}

std::string requireStringField(
    const json::Value::Object& object,
    const std::string& name,
    const char* context
) {
    const auto found = std::find_if(object.begin(), object.end(), [&](const auto& field) {
        return field.first == name;
    });
    if (found == object.end()) {
        throw std::invalid_argument(std::string(context) + " is missing '" + name + "'");
    }
    const auto* value = std::get_if<std::string>(&found->second.data);
    if (!value) {
        throw std::invalid_argument(std::string(context) + " field '" + name +
            "' must be a string");
    }
    return *value;
}

std::string normalizePath(std::string value) {
    if (value.empty() || value.front() != '/') {
        throw std::invalid_argument("route paths must start with '/'");
    }
    if (value.size() > 1 && value.back() == '/') value.pop_back();
    return value.empty() ? "/" : value;
}

std::string normalizeMethod(std::string value) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        })) {
        throw std::invalid_argument("HTTP method must contain only letters");
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    static constexpr std::array<std::string_view, 7> methods = {
        "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"
    };
    if (std::find(methods.begin(), methods.end(), value) == methods.end()) {
        throw std::invalid_argument("unsupported HTTP route method '" + value + "'");
    }
    return value;
}

std::string prefixPath(std::string prefix, std::string path) {
    prefix = normalizePath(std::move(prefix));
    path = normalizePath(std::move(path));
    if (prefix == "/") return path;
    if (path == "/") return prefix;
    return prefix + path;
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string extension(std::string_view path) {
    const auto separator = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos ||
        (separator != std::string_view::npos && dot < separator)) {
        return {};
    }
    return lower(std::string(path.substr(dot)));
}

std::filesystem::path pathFromUtf8(const std::string& value) {
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

bool withinRoot(const std::filesystem::path& root, const std::filesystem::path& file) {
    const auto relative = file.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative) {
        if (component == "..") return false;
    }
    return true;
}

std::filesystem::path resolveStaticFile(
    const std::string& rootValue,
    const std::string& pathValue,
    const std::string& indexFile
) {
    if (pathValue.empty() || pathValue.find('\\') != std::string::npos ||
        hasControl(pathValue)) {
        throw std::invalid_argument("static file path is invalid");
    }
    if (!indexFile.empty() &&
        (indexFile.find_first_of("/\\") != std::string::npos ||
            indexFile == "." || indexFile == ".." || hasControl(indexFile))) {
        throw std::invalid_argument("static index file name is invalid");
    }
    std::string relative = decode(pathValue, false);
    if (relative.find('\\') != std::string::npos || hasControl(relative)) {
        throw std::invalid_argument("decoded static file path is invalid");
    }
    while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
    const std::filesystem::path relativePath = pathFromUtf8(relative);
    if (relativePath.is_absolute() || relativePath.has_root_name()) {
        throw std::invalid_argument("static file path must be relative to the configured root");
    }

    std::error_code error;
    const auto root = std::filesystem::canonical(pathFromUtf8(rootValue), error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
        throw std::runtime_error("static file root is not an accessible directory");
    }
    auto file = std::filesystem::canonical(root / relativePath, error);
    if (error) throw std::runtime_error("static file does not exist inside the configured root");
    if (!withinRoot(root, file)) {
        throw std::invalid_argument("static file path escapes the configured root");
    }
    if (std::filesystem::is_directory(file, error) && !error) {
        if (indexFile.empty()) {
            throw std::invalid_argument("static file path names a directory without an index file");
        }
        file = std::filesystem::canonical(file / pathFromUtf8(indexFile), error);
        if (error) throw std::runtime_error("static index file does not exist");
    }
    if (!withinRoot(root, file)) {
        throw std::invalid_argument("static file path escapes the configured root");
    }
    if (!std::filesystem::is_regular_file(file, error) || error) {
        throw std::runtime_error("static file path does not name a regular file");
    }
    return file;
}

std::string readStaticFile(const std::filesystem::path& file, std::int64_t maximumBytes) {
    if (maximumBytes <= 0 ||
        maximumBytes > static_cast<std::int64_t>(maximumTextBytes)) {
        throw std::invalid_argument("static file size limit must be between 1 byte and 16 MiB");
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error) throw std::runtime_error("could not determine static file size");
    if (size > static_cast<std::uintmax_t>(maximumBytes)) {
        throw std::length_error("static file exceeds the configured size limit");
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("could not open static file");
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        input.read(contents.data(), static_cast<std::streamsize>(size));
        if (!input || input.gcount() != static_cast<std::streamsize>(size)) {
            throw std::runtime_error("could not read complete static file");
        }
    }
    return contents;
}

} // namespace

std::string urlEncode(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (unreserved(ch)) {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('%');
            result.push_back(hex[ch >> 4]);
            result.push_back(hex[ch & 0x0f]);
        }
    }
    return result;
}

std::string urlDecode(const std::string& value) {
    requireWebSize(value, "URL component");
    return decode(value, false);
}

std::string parseUrl(const std::string& value) {
    requireWebSize(value, "URL");
    if (hasControl(value)) throw std::invalid_argument("URL must not contain control characters");
    const auto schemeEnd = value.find("://");
    if (schemeEnd == std::string::npos || schemeEnd == 0) {
        throw std::invalid_argument("URL must be absolute and include a scheme and authority");
    }
    const std::string scheme = lower(value.substr(0, schemeEnd));
    if (!((scheme.front() >= 'a' && scheme.front() <= 'z') ||
          (scheme.front() >= 'A' && scheme.front() <= 'Z')) ||
        !std::all_of(scheme.begin() + 1, scheme.end(), [](unsigned char ch) {
            return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9')) || ch == '+' || ch == '-' || ch == '.';
        })) {
        throw std::invalid_argument("URL scheme is invalid");
    }
    const std::size_t authorityStart = schemeEnd + 3;
    const auto authorityEnd = value.find_first_of("/?#", authorityStart);
    const std::string authority = value.substr(
        authorityStart, authorityEnd == std::string::npos
            ? std::string::npos : authorityEnd - authorityStart);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        throw std::invalid_argument("URL authority must contain a host and no user information");
    }
    std::string host;
    std::string port;
    bool portSpecified = false;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) throw std::invalid_argument("URL IPv6 host is malformed");
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                throw std::invalid_argument("URL authority is malformed");
            }
            portSpecified = true;
            port = authority.substr(close + 2);
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string::npos) {
            if (authority.find(':') != colon) {
                throw std::invalid_argument("IPv6 URL hosts must be enclosed in brackets");
            }
            portSpecified = true;
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
    }
    if (host.empty()) throw std::invalid_argument("URL host must not be empty");
    if (hasControl(host) || std::any_of(host.begin(), host.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        })) {
        throw std::invalid_argument("URL host must not contain whitespace or control characters");
    }
    if (portSpecified && port.empty()) {
        throw std::invalid_argument("URL port must not be empty");
    }
    if (!port.empty()) {
        unsigned int portNumber = 0;
        const auto [end, error] = std::from_chars(
            port.data(), port.data() + port.size(), portNumber);
        if (error != std::errc{} || end != port.data() + port.size() ||
            portNumber == 0 || portNumber > 65535) {
            throw std::invalid_argument("URL port must be between 1 and 65535");
        }
    } else if (scheme == "http") {
        port = "80";
    } else if (scheme == "https") {
        port = "443";
    }

    std::string path = "/";
    std::string query;
    std::string fragment;
    if (authorityEnd != std::string::npos) {
        std::size_t cursor = authorityEnd;
        if (value[cursor] == '/') {
            const auto pathEnd = value.find_first_of("?#", cursor);
            path = value.substr(cursor, pathEnd == std::string::npos
                ? std::string::npos : pathEnd - cursor);
            cursor = pathEnd == std::string::npos ? value.size() : pathEnd;
        }
        if (cursor < value.size() && value[cursor] == '?') {
            const auto queryEnd = value.find('#', cursor + 1);
            query = value.substr(cursor + 1, queryEnd == std::string::npos
                ? std::string::npos : queryEnd - cursor - 1);
            cursor = queryEnd == std::string::npos ? value.size() : queryEnd;
        }
        if (cursor < value.size() && value[cursor] == '#') {
            fragment = value.substr(cursor + 1);
        }
    }
    json::Value::Object fields;
    fields.emplace_back("scheme", json::Value{scheme});
    fields.emplace_back("host", json::Value{host});
    fields.emplace_back("port", json::Value{port});
    fields.emplace_back("path", json::Value{path});
    fields.emplace_back("query", json::Value{query});
    fields.emplace_back("fragment", json::Value{fragment});
    return json::serialize(json::Value{std::move(fields)});
}

std::string buildUrl(
    const std::string& schemeValue,
    const std::string& host,
    const std::string& port,
    const std::string& pathValue,
    const std::string& query,
    const std::string& fragment
) {
    const std::string scheme = lower(schemeValue);
    if (scheme.empty() ||
        !((scheme.front() >= 'a' && scheme.front() <= 'z') ||
          (scheme.front() >= 'A' && scheme.front() <= 'Z')) ||
        !std::all_of(scheme.begin() + 1, scheme.end(), [](unsigned char ch) {
            return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9')) || ch == '+' || ch == '-' || ch == '.';
        })) {
        throw std::invalid_argument("URL scheme is invalid");
    }
    if (host.empty() || host.find_first_of("/?#@") != std::string::npos ||
        hasControl(host) || std::any_of(host.begin(), host.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        })) {
        throw std::invalid_argument("URL host is invalid");
    }
    if (host.front() == '[' &&
        (host.size() < 3 || host.back() != ']' ||
         host.substr(1, host.size() - 2).find_first_of("[]") != std::string::npos)) {
        throw std::invalid_argument("bracketed URL host is malformed");
    }
    if (host.front() != '[' && host.find_first_of("[]") != std::string::npos) {
        throw std::invalid_argument("URL host brackets are malformed");
    }
    if (!port.empty()) {
        unsigned int portNumber = 0;
        const auto [end, error] = std::from_chars(
            port.data(), port.data() + port.size(), portNumber);
        if (error != std::errc{} || end != port.data() + port.size() ||
            portNumber == 0 || portNumber > 65535) {
            throw std::invalid_argument("URL port must be between 1 and 65535");
        }
    }
    if (hasControl(pathValue) || hasControl(query) || hasControl(fragment) ||
        pathValue.find_first_of("?#") != std::string::npos ||
        query.find('#') != std::string::npos ||
        fragment.find('#') != std::string::npos) {
        throw std::invalid_argument("URL components contain an invalid delimiter");
    }
    const std::string path = pathValue.empty() ? "/" : pathValue;
    if (path.front() != '/') throw std::invalid_argument("URL path must start with '/'");

    std::string result = scheme + "://";
    if (host.find(':') != std::string::npos && host.front() != '[') {
        result += "[" + host + "]";
    } else {
        result += host;
    }
    if (!port.empty() &&
        !((scheme == "http" && port == "80") ||
          (scheme == "https" && port == "443"))) {
        result += ":" + port;
    }
    result += path;
    if (!query.empty()) result += "?" + query;
    if (!fragment.empty()) result += "#" + fragment;
    return result;
}

std::string parseForm(const std::string& value) {
    requireWebSize(value, "form body");
    json::Value::Object fields;
    std::size_t count = 0;
    std::size_t start = 0;
    while (start <= value.size() && !value.empty()) {
        const auto separator = value.find('&', start);
        const std::string_view pair(value.data() + start,
            (separator == std::string::npos ? value.size() : separator) - start);
        const auto equals = pair.find('=');
        if (pair.empty()) {
            if (separator == std::string::npos) break;
            start = separator + 1;
            continue;
        }
        const std::string key = decode(pair.substr(0, equals), true);
        const std::string fieldValue = decode(
            equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1),
            true);
        if (key.empty() || hasControl(key)) {
            throw std::invalid_argument("form field names must be non-empty and contain no control characters");
        }
        if (++count > maximumFormFields) {
            throw std::length_error("form contains more than 4096 fields");
        }
        const auto existing = std::find_if(fields.begin(), fields.end(), [&](const auto& field) {
            return field.first == key;
        });
        if (existing == fields.end()) {
            fields.emplace_back(key, json::Value{fieldValue});
        } else if (auto* values = std::get_if<json::Value::Array>(&existing->second.data)) {
            values->emplace_back(fieldValue);
        } else {
            json::Value::Array duplicateValues;
            duplicateValues.push_back(std::move(existing->second));
            duplicateValues.emplace_back(fieldValue);
            existing->second.data = std::move(duplicateValues);
        }
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return json::serialize(json::Value{std::move(fields)});
}

std::string parseCookies(const std::string& value) {
    if (value.size() > 16 * 1024) throw std::length_error("Cookie header exceeds 16 KiB");
    json::Value::Object fields;
    std::size_t start = 0;
    while (start < value.size()) {
        const auto separator = value.find(';', start);
        const auto part = std::string_view(value).substr(
            start, separator == std::string::npos
                ? value.size() - start : separator - start);
        const std::string trimmed = trim(part);
        if (!trimmed.empty()) {
            const auto equals = trimmed.find('=');
            if (equals == std::string::npos) {
                throw std::invalid_argument("malformed Cookie header pair");
            }
            const std::string name = trim(std::string_view(trimmed).substr(0, equals));
            const std::string cookieValue = decode(
                std::string_view(trimmed).substr(equals + 1), false);
            if (!validToken(name) || hasControl(cookieValue)) {
                throw std::invalid_argument("invalid cookie name or value");
            }
            const auto existing = std::find_if(fields.begin(), fields.end(), [&](const auto& field) {
                return field.first == name;
            });
            if (existing == fields.end()) fields.emplace_back(name, json::Value{cookieValue});
        }
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return json::serialize(json::Value{std::move(fields)});
}

std::string cookieHeader(
    const std::string& name,
    const std::string& value,
    std::int64_t maxAge,
    const std::string& path,
    const std::string& domain,
    bool secure,
    bool httpOnly,
    const std::string& sameSiteValue
) {
    if (!validToken(name)) throw std::invalid_argument("cookie name is invalid");
    if (hasControl(path) || path.find(';') != std::string::npos ||
        (!path.empty() && path.front() != '/') || hasControl(domain) ||
        std::any_of(domain.begin(), domain.end(), [](unsigned char ch) {
            return !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '.' || ch == '-');
        })) {
        throw std::invalid_argument("cookie path or domain is invalid");
    }
    if (maxAge < -1) throw std::invalid_argument("cookie Max-Age must be -1 or non-negative");
    std::string sameSite;
    if (!sameSiteValue.empty()) {
        sameSite = toUpper(sameSiteValue);
        if (sameSite == "LAX") sameSite = "Lax";
        else if (sameSite == "STRICT") sameSite = "Strict";
        else if (sameSite == "NONE") sameSite = "None";
        else throw std::invalid_argument("SameSite must be Lax, Strict, None, or empty");
        if (sameSite == "None" && !secure) {
            throw std::invalid_argument("SameSite=None cookies must also be Secure");
        }
    }
    std::string result = name + "=" + urlEncode(value);
    if (maxAge >= 0) result += "; Max-Age=" + std::to_string(maxAge);
    if (!path.empty()) result += "; Path=" + path;
    if (!domain.empty()) result += "; Domain=" + domain;
    if (secure) result += "; Secure";
    if (httpOnly) result += "; HttpOnly";
    if (!sameSite.empty()) result += "; SameSite=" + sameSite;
    return result;
}

std::string cookieHeaderExpires(
    const std::string& name,
    const std::string& value,
    std::int64_t maxAge,
    const std::string& expires,
    const std::string& path,
    const std::string& domain,
    bool secure,
    bool httpOnly,
    const std::string& sameSite
) {
    if (!expires.empty() &&
        (expires.size() != 29 || !expires.ends_with(" GMT") ||
            std::any_of(expires.begin(), expires.end(), [](unsigned char ch) {
                return !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == ',' || ch == ' ' || ch == ':');
            }))) {
        throw std::invalid_argument("cookie Expires must be an RFC 1123 GMT date");
    }
    std::string result = cookieHeader(
        name, value, maxAge, path, domain, secure, httpOnly, sameSite);
    if (!expires.empty()) result += "; Expires=" + expires;
    return result;
}

std::string securityHeaders(const std::string& headersJson) {
    const auto parsed = json::parse(headersJson);
    const auto& input = requireObject(parsed, "response headers");
    json::Value::Object output = input;
    for (const auto& [name, value] : input) {
        const auto* text = std::get_if<std::string>(&value.data);
        if (!validToken(name) || !text || hasControl(*text)) {
            throw std::invalid_argument("response headers must have valid names and string values");
        }
    }
    static const std::array<std::pair<std::string_view, std::string_view>, 4> defaults = {{
        {"x-content-type-options", "nosniff"},
        {"x-frame-options", "DENY"},
        {"referrer-policy", "strict-origin-when-cross-origin"},
        {"permissions-policy", "camera=(), microphone=(), geolocation=()"}
    }};
    for (const auto& [name, value] : defaults) {
        const bool present = std::any_of(output.begin(), output.end(), [&](const auto& field) {
            return lower(field.first) == name;
        });
        if (!present) output.emplace_back(std::string(name), json::Value{std::string(value)});
    }
    return json::serialize(json::Value{std::move(output)});
}

std::string routeAdd(
    const std::string& routesJson,
    const std::string& methodValue,
    const std::string& patternValue,
    const std::string& handler
) {
    return routeAddConfigured(routesJson, methodValue, patternValue, handler, "", "{}");
}

std::string routeAddConfigured(
    const std::string& routesJson,
    const std::string& methodValue,
    const std::string& patternValue,
    const std::string& handler,
    const std::string& name,
    const std::string& configurationJson
) {
    if (handler.empty() || hasControl(handler)) {
        throw std::invalid_argument("route handler identifier must be non-empty and contain no control characters");
    }
    if (hasControl(name)) throw std::invalid_argument("route name contains a control character");
    const std::string method = normalizeMethod(methodValue);
    const std::string pattern = normalizePath(patternValue);
    (void)http::routeParams(pattern, pattern);
    const auto configuration = json::parse(configurationJson);
    (void)requireObject(configuration, "route configuration");
    json::Value parsed = json::parse(routesJson);
    json::Value::Array routes = requireArray(parsed, "routes");
    for (const auto& route : routes) {
        const auto& fields = requireObject(route, "route");
        const auto existingMethod = requireStringField(fields, "method", "route");
        const auto existingPattern = requireStringField(fields, "pattern", "route");
        if (normalizeMethod(existingMethod) == method &&
            normalizePath(existingPattern) == pattern) {
            throw std::invalid_argument("route method and path pattern are already registered");
        }
        if (!name.empty()) {
            const auto existingName = std::find_if(
                fields.begin(), fields.end(), [](const auto& field) {
                    return field.first == "name";
                });
            if (existingName != fields.end()) {
                const auto* text = std::get_if<std::string>(&existingName->second.data);
                if (text && *text == name) {
                    throw std::invalid_argument("route name is already registered");
                }
            }
        }
    }
    json::Value::Object route;
    route.emplace_back("method", json::Value{method});
    route.emplace_back("pattern", json::Value{pattern});
    route.emplace_back("handler", json::Value{handler});
    if (!name.empty()) route.emplace_back("name", json::Value{name});
    route.emplace_back("configuration", configuration);
    routes.emplace_back(std::move(route));
    return json::serialize(json::Value{std::move(routes)});
}

std::string routeGroup(const std::string& prefix, const std::string& routesJson) {
    const std::string normalizedPrefix = normalizePath(prefix);
    json::Value parsed = json::parse(routesJson);
    const auto& routes = requireArray(parsed, "routes");
    std::string result = "[]";
    for (const auto& route : routes) {
        const auto& fields = requireObject(route, "route");
        const auto name = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
            return field.first == "name";
        });
        const auto configuration = std::find_if(
            fields.begin(), fields.end(), [](const auto& field) {
                return field.first == "configuration";
            });
        result = routeAddConfigured(
            result,
            requireStringField(fields, "method", "route"),
            prefixPath(normalizedPrefix, requireStringField(fields, "pattern", "route")),
            requireStringField(fields, "handler", "route"),
            name == fields.end() ? "" : std::get<std::string>(name->second.data),
            configuration == fields.end() ? "{}" : json::serialize(configuration->second));
    }
    return result;
}

std::string routeMatch(
    const std::string& routesJson,
    const std::string& methodValue,
    const std::string& path
) {
    const std::string method = normalizeMethod(methodValue);
    if (path.empty() || path.front() != '/') {
        throw std::invalid_argument("request path must start with '/'");
    }
    json::Value parsed = json::parse(routesJson);
    const auto& routes = requireArray(parsed, "routes");
    const json::Value::Object* matched = nullptr;
    std::string matchedParams;
    std::vector<std::string> allowed;
    for (const auto& route : routes) {
        const auto& fields = requireObject(route, "route");
        const std::string routeMethod = normalizeMethod(
            requireStringField(fields, "method", "route"));
        const std::string pattern = normalizePath(
            requireStringField(fields, "pattern", "route"));
        const std::string params = http::routeParams(path, pattern);
        if (params.empty()) continue;
        if (routeMethod == method ||
            (method == "HEAD" && routeMethod == "GET")) {
            if (!matched) {
                matched = &fields;
                matchedParams = params;
            }
        } else if (std::find(allowed.begin(), allowed.end(), routeMethod) == allowed.end()) {
            allowed.push_back(routeMethod);
        }
    }
    json::Value::Object result;
    if (matched) {
        result.emplace_back("status", json::Value{200.0});
        result.emplace_back("method", json::Value{requireStringField(*matched, "method", "route")});
        result.emplace_back("handler", json::Value{requireStringField(*matched, "handler", "route")});
        result.emplace_back("params", json::parse(matchedParams));
        const auto name = std::find_if(matched->begin(), matched->end(), [](const auto& field) {
            return field.first == "name";
        });
        if (name != matched->end()) result.emplace_back("name", name->second);
        const auto configuration = std::find_if(
            matched->begin(), matched->end(), [](const auto& field) {
                return field.first == "configuration";
            });
        if (configuration != matched->end()) {
            result.emplace_back("configuration", configuration->second);
        }
    } else if (!allowed.empty()) {
        if (std::find(allowed.begin(), allowed.end(), "GET") != allowed.end() &&
            std::find(allowed.begin(), allowed.end(), "HEAD") == allowed.end()) {
            allowed.push_back("HEAD");
        }
        std::sort(allowed.begin(), allowed.end());
        std::string allow;
        for (const auto& allowedMethod : allowed) {
            if (!allow.empty()) allow += ", ";
            allow += allowedMethod;
        }
        result.emplace_back("status", json::Value{405.0});
        result.emplace_back("allow", json::Value{allow});
    } else {
        result.emplace_back("status", json::Value{404.0});
    }
    return json::serialize(json::Value{std::move(result)});
}

std::string routeUrl(
    const std::string& routesJson,
    const std::string& name,
    const std::string& parametersJson
) {
    const auto routes = json::parse(routesJson);
    const auto& entries = requireArray(routes, "routes");
    const auto parameters = json::parse(parametersJson);
    const auto& parameterObject = requireObject(parameters, "route parameters");
    for (const auto& entry : entries) {
        const auto& fields = requireObject(entry, "route");
        const auto routeName = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
            return field.first == "name";
        });
        if (routeName == fields.end()) continue;
        const auto* routeNameText = std::get_if<std::string>(&routeName->second.data);
        if (!routeNameText || *routeNameText != name) continue;
        std::string pattern = requireStringField(fields, "pattern", "route");
        std::string path;
        for (std::size_t offset = 0; offset < pattern.size();) {
            if (pattern[offset] != '{') {
                path.push_back(pattern[offset++]);
                continue;
            }
            const auto close = pattern.find('}', offset + 1);
            if (close == std::string::npos) {
                throw std::invalid_argument("route pattern has an unclosed parameter");
            }
            const std::string parameterName = pattern.substr(offset + 1, close - offset - 1);
            const auto parameter = std::find_if(parameterObject.begin(), parameterObject.end(),
                [&](const auto& item) { return item.first == parameterName; });
            if (parameter == parameterObject.end()) {
                throw std::invalid_argument("route URL is missing parameter '" + parameterName + "'");
            }
            const auto* value = std::get_if<std::string>(&parameter->second.data);
            if (!value) throw std::invalid_argument("route URL parameters must be strings");
            path += urlEncode(*value);
            offset = close + 1;
        }
        return path;
    }
    throw std::out_of_range("named route was not found");
}

std::string response(
    std::int64_t status,
    const std::string& headersJson,
    const std::string& body
) {
    if (status < 200 || status > 599) {
        throw std::invalid_argument("HTTP response status must be between 200 and 599");
    }
    if ((status == 204 || status == 205 || status == 304) && !body.empty()) {
        throw std::invalid_argument("204, 205, and 304 HTTP responses cannot include a body");
    }
    const auto headers = json::parse(headersJson);
    const auto& values = requireObject(headers, "response headers");
    for (const auto& [name, value] : values) {
        const auto* text = std::get_if<std::string>(&value.data);
        if (!validToken(name) || !text || hasControl(*text)) {
            throw std::invalid_argument("response headers must have valid names and string values");
        }
    }
    json::Value::Object result;
    result.emplace_back("status", json::Value{static_cast<double>(status)});
    result.emplace_back("headers", headers);
    result.emplace_back("body", json::Value{body});
    return json::serialize(json::Value{std::move(result)});
}

std::string jsonResponse(
    std::int64_t status,
    const std::string& headersJson,
    const std::string& bodyJson
) {
    const auto parsedBody = json::parse(bodyJson);
    const auto parsedHeaders = json::parse(headersJson);
    auto headers = requireObject(parsedHeaders, "response headers");
    const bool hasContentType = std::any_of(headers.begin(), headers.end(), [](const auto& field) {
        return lower(field.first) == "content-type";
    });
    if (!hasContentType) {
        headers.emplace_back("content-type", json::Value{"application/json; charset=utf-8"});
    }
    const auto result = response(status, json::serialize(json::Value{std::move(headers)}),
        json::serialize(parsedBody));
    return result;
}

std::string parseJsonBody(const std::string& requestJson) {
    const auto request = json::parse(requestJson);
    const auto* encodingValue = json::atPointer(request, "/bodyEncoding");
    if (encodingValue) {
        const auto* encodingName = std::get_if<std::string>(&encodingValue->data);
        if (!encodingName) {
            throw std::invalid_argument("HTTP request body encoding must be a string");
        }
        if (*encodingName == "base64") {
            throw std::invalid_argument(
                "binary HTTP request bodies cannot be parsed as JSON text");
        }
    }
    const auto* bodyValue = json::atPointer(request, "/body");
    const auto* body = bodyValue ? std::get_if<std::string>(&bodyValue->data) : nullptr;
    if (!body) throw std::invalid_argument("HTTP request body must be a string");
    return json::compact(*body);
}

std::string notFoundResponse(const std::string& body) {
    return response(404, R"({"content-type":"text/plain; charset=utf-8"})", body);
}

std::string methodNotAllowedResponse(const std::string& allow, const std::string& body) {
    if (hasControl(allow)) throw std::invalid_argument("Allow header contains a control character");
    return response(405,
        json::serialize(json::Value{json::Value::Object{
            {"allow", json::Value{allow}},
            {"content-type", json::Value{"text/plain; charset=utf-8"}}
        }}), body);
}

std::string mimeType(const std::string& path) {
    static const std::map<std::string, std::string> types = {
        {".avif", "image/avif"}, {".css", "text/css; charset=utf-8"},
        {".gif", "image/gif"}, {".htm", "text/html; charset=utf-8"},
        {".html", "text/html; charset=utf-8"}, {".ico", "image/x-icon"},
        {".jpeg", "image/jpeg"}, {".jpg", "image/jpeg"},
        {".js", "text/javascript; charset=utf-8"}, {".json", "application/json"},
        {".map", "application/json"}, {".png", "image/png"},
        {".svg", "image/svg+xml"}, {".txt", "text/plain; charset=utf-8"},
        {".wasm", "application/wasm"}, {".webp", "image/webp"},
        {".woff", "font/woff"}, {".woff2", "font/woff2"},
        {".xml", "application/xml"}
    };
    const auto found = types.find(extension(path));
    return found == types.end() ? "application/octet-stream" : found->second;
}

std::string staticFile(
    const std::string& rootValue,
    const std::string& pathValue,
    std::int64_t maximumBytes
) {
    return readStaticFile(resolveStaticFile(rootValue, pathValue, {}), maximumBytes);
}

bool respondFile(
    std::int64_t serverHandle,
    std::int64_t requestId,
    std::int64_t status,
    const std::string& headersJson,
    const std::string& root,
    const std::string& path,
    std::int64_t maximumBytes
) {
    return http::respond(
        serverHandle, requestId, status, headersJson,
        staticFile(root, path, maximumBytes));
}

bool respondStatic(
    std::int64_t serverHandle,
    std::int64_t requestId,
    std::int64_t status,
    const std::string& headersJson,
    const std::string& root,
    const std::string& path,
    const std::string& indexFile,
    std::int64_t maximumBytes,
    const std::string& cacheControl
) {
    const auto file = resolveStaticFile(root, path, indexFile);
    auto parsedHeaders = json::parse(headersJson);
    auto headers = requireObject(parsedHeaders, "response headers");
    const auto setDefault = [&](const std::string& name, const std::string& value) {
        const bool present = std::any_of(headers.begin(), headers.end(), [&](const auto& field) {
            return lower(field.first) == lower(name);
        });
        if (!present) headers.emplace_back(name, json::Value{value});
    };
    setDefault("content-type", mimeType(file.string()));
    if (!cacheControl.empty()) {
        if (hasControl(cacheControl)) {
            throw std::invalid_argument("Cache-Control header contains a control character");
        }
        setDefault("cache-control", cacheControl);
    }
    return http::respond(serverHandle, requestId, status,
        json::serialize(json::Value{std::move(headers)}),
        readStaticFile(file, maximumBytes));
}

} // namespace zplus::web
