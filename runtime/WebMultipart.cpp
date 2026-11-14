#include "Web.hpp"

#include "Base64.hpp"
#include "Json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace zplus::web {
namespace {

constexpr std::size_t maximumParts = 256;
constexpr std::size_t maximumPartHeaderBytes = 16 * 1024;
constexpr std::size_t maximumPartHeaderCount = 32;
constexpr std::size_t maximumMultipartBytes = 16 * 1024 * 1024;

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

const json::Value* member(const json::Value& value, std::string_view name) {
    const auto* object = std::get_if<json::Value::Object>(&value.data);
    if (!object) return nullptr;
    for (auto item = object->rbegin(); item != object->rend(); ++item) {
        if (item->first == name) return &item->second;
    }
    return nullptr;
}

std::string stringMember(const json::Value& value, std::string_view name) {
    const auto* item = member(value, name);
    if (!item) return {};
    const auto* text = std::get_if<std::string>(&item->data);
    if (!text) throw std::invalid_argument("multipart request field must be a string");
    return *text;
}

std::string fieldValue(std::string_view input, std::string_view wanted) {
    std::size_t offset = 0;
    bool found = false;
    std::string result;
    while (offset < input.size()) {
        while (offset < input.size() && (input[offset] == ';' ||
                input[offset] == ' ' || input[offset] == '\t')) ++offset;
        const auto keyStart = offset;
        while (offset < input.size() && input[offset] != '=' && input[offset] != ';') ++offset;
        const std::string key = lower(trim(input.substr(keyStart, offset - keyStart)));
        if (key.empty() || offset >= input.size() || input[offset] != '=') {
            throw std::invalid_argument("multipart parameter is malformed");
        }
        ++offset;
        std::string value;
        if (offset < input.size() && input[offset] == '"') {
            ++offset;
            bool closed = false;
            while (offset < input.size()) {
                const char ch = input[offset++];
                if (ch == '\\') {
                    if (offset == input.size()) {
                        throw std::invalid_argument("multipart parameter has an incomplete escape");
                    }
                    value.push_back(input[offset++]);
                } else if (ch == '"') {
                    closed = true;
                    break;
                } else {
                    value.push_back(ch);
                }
            }
            if (!closed) throw std::invalid_argument("multipart parameter is not quoted correctly");
            while (offset < input.size() && input[offset] != ';') {
                if (input[offset] != ' ' && input[offset] != '\t') {
                    throw std::invalid_argument("multipart parameter has trailing data");
                }
                ++offset;
            }
        } else {
            const auto valueStart = offset;
            while (offset < input.size() && input[offset] != ';') ++offset;
            value = trim(input.substr(valueStart, offset - valueStart));
        }
        if (key == wanted) {
            if (found) throw std::invalid_argument("multipart parameter is duplicated");
            result = std::move(value);
            found = true;
        }
    }
    return result;
}

std::string contentTypeBoundary(std::string_view contentType) {
    const auto separator = contentType.find(';');
    const auto mediaType = lower(trim(contentType.substr(0, separator)));
    if (mediaType != "multipart/form-data") {
        throw std::invalid_argument("request Content-Type must be multipart/form-data");
    }
    if (separator == std::string_view::npos) {
        throw std::invalid_argument("multipart Content-Type is missing its boundary");
    }
    const std::string boundary = fieldValue(contentType.substr(separator + 1), "boundary");
    if (boundary.empty() || boundary.size() > 70 ||
        std::any_of(boundary.begin(), boundary.end(), [](unsigned char ch) {
            return !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') || ch == '\'' || ch == '(' ||
                ch == ')' || ch == '+' || ch == '_' || ch == ',' || ch == '-' ||
                ch == '.' || ch == '/' || ch == ':' || ch == '=' || ch == '?');
        })) {
        throw std::invalid_argument("multipart boundary is invalid");
    }
    return boundary;
}

std::string headerValue(const json::Value& headers, std::string_view wanted) {
    const auto* object = std::get_if<json::Value::Object>(&headers.data);
    if (!object) throw std::invalid_argument("request headers must be a JSON object");
    bool found = false;
    std::string result;
    for (const auto& [name, value] : *object) {
        if (lower(name) == wanted) {
            if (found) throw std::invalid_argument("multipart header is duplicated");
            const auto* text = std::get_if<std::string>(&value.data);
            if (!text) throw std::invalid_argument("request header values must be strings");
            if (std::any_of(text->begin(), text->end(), [](unsigned char ch) {
                    return (ch < 0x20 && ch != '\t') || ch == 0x7f;
                })) {
                throw std::invalid_argument("multipart header contains a control character");
            }
            result = *text;
            found = true;
        }
    }
    return result;
}

std::string dispositionParameter(std::string_view disposition, std::string_view name) {
    const auto separator = disposition.find(';');
    if (lower(trim(disposition.substr(0, separator))) != "form-data") {
        throw std::invalid_argument("multipart part must use form-data disposition");
    }
    if (separator == std::string_view::npos) return {};
    return fieldValue(disposition.substr(separator + 1), name);
}

bool hasDispositionParameter(std::string_view disposition, std::string_view name) {
    const auto separator = disposition.find(';');
    if (separator == std::string_view::npos) return false;
    std::size_t offset = separator + 1;
    while (offset < disposition.size()) {
        while (offset < disposition.size() &&
                (disposition[offset] == ';' || disposition[offset] == ' ' ||
                    disposition[offset] == '\t')) ++offset;
        const auto keyStart = offset;
        while (offset < disposition.size() && disposition[offset] != '=' &&
                disposition[offset] != ';') ++offset;
        const auto key = lower(trim(disposition.substr(keyStart, offset - keyStart)));
        if (offset >= disposition.size() || disposition[offset] != '=') continue;
        ++offset;
        const bool found = key == name;
        if (offset < disposition.size() && disposition[offset] == '"') {
            ++offset;
            while (offset < disposition.size()) {
                if (disposition[offset] == '\\') {
                    offset += std::min<std::size_t>(2, disposition.size() - offset);
                } else if (disposition[offset++] == '"') {
                    break;
                }
            }
        } else {
            while (offset < disposition.size() && disposition[offset] != ';') ++offset;
        }
        if (found) return true;
    }
    return false;
}

std::filesystem::path pathFromUtf8(const std::string& value) {
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

std::string pathToUtf8(const std::filesystem::path& value) {
    const auto text = value.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool withinRoot(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative) {
        if (component == "..") return false;
    }
    return true;
}

json::Value parsePartHeaders(std::string_view source) {
    json::Value::Object headers;
    std::size_t offset = 0;
    std::size_t count = 0;
    while (offset < source.size()) {
        if (++count > maximumPartHeaderCount) {
            throw std::length_error("multipart part contains too many headers");
        }
        const auto end = source.find("\r\n", offset);
        const auto line = source.substr(offset,
            end == std::string_view::npos ? source.size() - offset : end - offset);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            throw std::invalid_argument("multipart part contains an invalid header");
        }
        const std::string name = lower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        if (name.empty() || std::any_of(name.begin(), name.end(), [](unsigned char ch) {
                const bool alphaNumeric = (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
                return !(alphaNumeric || ch == '-');
            }) || std::any_of(value.begin(), value.end(), [](unsigned char ch) {
                return (ch < 0x20 && ch != '\t') || ch == 0x7f;
            })) {
            throw std::invalid_argument("multipart part contains an invalid header");
        }
        if (std::any_of(headers.begin(), headers.end(), [&](const auto& field) {
                return field.first == name;
            })) {
            throw std::invalid_argument("multipart part contains a duplicate header");
        }
        headers.emplace_back(name, json::Value{value});
        if (end == std::string_view::npos) break;
        offset = end + 2;
    }
    return json::Value{std::move(headers)};
}

void writeNewUpload(const std::filesystem::path& path, std::string_view data) {
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
            throw std::invalid_argument("upload destination already exists");
        }
        throw std::system_error(static_cast<int>(error), std::system_category(),
            "could not create upload destination");
    }
    bool complete = true;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const DWORD length = static_cast<DWORD>(std::min<std::size_t>(
            data.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(file, data.data() + offset, length, &written, nullptr) ||
            written == 0) {
            complete = false;
            break;
        }
        offset += written;
    }
    const bool closed = CloseHandle(file) != 0;
    if (!complete || !closed) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("could not write complete upload file");
    }
#else
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int file = ::open(path.c_str(), flags, 0600);
    if (file < 0) {
        if (errno == EEXIST) {
            throw std::invalid_argument("upload destination already exists");
        }
        throw std::system_error(errno, std::generic_category(),
            "could not create upload destination");
    }
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto written = ::write(file, data.data() + offset, data.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            const int error = errno;
            ::close(file);
            ::unlink(path.c_str());
            throw std::system_error(error, std::generic_category(),
                "could not write complete upload file");
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::close(file) != 0) {
        const int error = errno;
        ::unlink(path.c_str());
        throw std::system_error(error, std::generic_category(),
            "could not close upload file");
    }
#endif
}

} // namespace

std::string parseMultipart(const std::string& requestJson, std::int64_t maximumBytes) {
    if (maximumBytes <= 0 ||
        maximumBytes > static_cast<std::int64_t>(maximumMultipartBytes)) {
        throw std::invalid_argument("multipart size limit must be between 1 byte and 16 MiB");
    }
    const auto request = json::parse(requestJson);
    const auto* headers = member(request, "headers");
    if (!headers) throw std::invalid_argument("request is missing its headers");
    const std::string boundary = contentTypeBoundary(headerValue(*headers, "content-type"));
    std::string body;
    const std::string encodedBody = stringMember(request, "bodyBase64");
    const auto maximumEncodedBytes =
        ((static_cast<std::size_t>(maximumBytes) + 2) / 3) * 4;
    if (encodedBody.size() > maximumEncodedBytes) {
        throw std::length_error("multipart request exceeds the configured size limit");
    }
    if (!encodedBody.empty()) body = encoding::base64Decode(encodedBody);
    else body = stringMember(request, "body");
    if (body.size() > static_cast<std::size_t>(maximumBytes)) {
        throw std::length_error("multipart request exceeds the configured size limit");
    }
    const std::string marker = "--" + boundary;
    if (body.rfind(marker, 0) != 0) {
        throw std::invalid_argument("multipart body does not begin with its boundary");
    }

    json::Value::Object fields;
    json::Value::Array files;
    std::size_t offset = marker.size();
    std::size_t partCount = 0;
    bool finished = false;
    while (!finished) {
        if (body.compare(offset, 2, "--") == 0) {
            offset += 2;
            if (offset < body.size() && body.compare(offset, 2, "\r\n") == 0) offset += 2;
            if (offset != body.size()) {
                throw std::invalid_argument("multipart body has data after its closing boundary");
            }
            finished = true;
            break;
        }
        if (body.compare(offset, 2, "\r\n") != 0) {
            throw std::invalid_argument("multipart boundary has an invalid separator");
        }
        offset += 2;
        const auto headerEnd = body.find("\r\n\r\n", offset);
        if (headerEnd == std::string::npos ||
            headerEnd - offset > maximumPartHeaderBytes) {
            throw std::invalid_argument("multipart part headers are missing or too large");
        }
        const auto headersValue = parsePartHeaders(
            std::string_view(body).substr(offset, headerEnd - offset));
        const std::string disposition = headerValue(headersValue, "content-disposition");
        const std::string name = dispositionParameter(disposition, "name");
        const std::string filename = dispositionParameter(disposition, "filename");
        const bool isFile = hasDispositionParameter(disposition, "filename");
        if (name.empty() || name.size() > 1024) {
            throw std::invalid_argument("multipart part field name is missing or too long");
        }
        if (filename.size() > 255) {
            throw std::invalid_argument("multipart filename exceeds the 255-byte limit");
        }
        offset = headerEnd + 4;

        const std::string delimiter = "\r\n" + marker;
        std::size_t contentEnd = body.find(delimiter, offset);
        while (contentEnd != std::string::npos) {
            const std::size_t suffix = contentEnd + delimiter.size();
            if (body.compare(suffix, 2, "--") == 0 ||
                body.compare(suffix, 2, "\r\n") == 0) break;
            contentEnd = body.find(delimiter, contentEnd + 2);
        }
        if (contentEnd == std::string::npos) {
            throw std::invalid_argument("multipart body is missing a closing boundary");
        }
        if (++partCount > maximumParts) {
            throw std::length_error("multipart request contains too many parts");
        }
        const std::string content = body.substr(offset, contentEnd - offset);
        offset = contentEnd + 2 + marker.size();
        const std::string contentType = headerValue(headersValue, "content-type");
        if (isFile) {
            json::Value::Object file;
            file.emplace_back("fieldName", json::Value{name});
            file.emplace_back("filename", json::Value{filename});
            file.emplace_back("contentType", json::Value{
                contentType.empty() ? "application/octet-stream" : contentType});
            file.emplace_back("size", json::Value{static_cast<double>(content.size())});
            file.emplace_back("dataBase64",
                json::Value{encoding::base64Encode(content)});
            files.emplace_back(std::move(file));
        } else {
            (void)json::quote(content);
            auto existing = std::find_if(fields.begin(), fields.end(),
                [&](const auto& field) { return field.first == name; });
            if (existing == fields.end()) {
                fields.emplace_back(name, json::Value{content});
            } else if (auto* repeated = std::get_if<json::Value::Array>(&existing->second.data)) {
                repeated->emplace_back(content);
            } else {
                json::Value::Array accumulated;
                accumulated.push_back(std::move(existing->second));
                accumulated.emplace_back(content);
                existing->second.data = std::move(accumulated);
            }
        }
    }
    if (!finished) throw std::invalid_argument("multipart body is incomplete");
    json::Value::Object result;
    result.emplace_back("fields", json::Value{std::move(fields)});
    result.emplace_back("files", json::Value{std::move(files)});
    return json::serialize(json::Value{std::move(result)});
}

std::string saveUpload(
    const std::string& multipartJson,
    std::int64_t index,
    const std::string& rootValue,
    const std::string& destinationValue,
    std::int64_t maximumBytes
) {
    if (index < 0 || maximumBytes <= 0 ||
        maximumBytes > static_cast<std::int64_t>(maximumMultipartBytes)) {
        throw std::invalid_argument("upload index or size limit is invalid");
    }
    const auto parsed = json::parse(multipartJson);
    const auto* filesValue = member(parsed, "files");
    const auto* files = filesValue
        ? std::get_if<json::Value::Array>(&filesValue->data) : nullptr;
    if (!files || static_cast<std::uint64_t>(index) >= files->size()) {
        throw std::out_of_range("upload index is outside the file list");
    }
    const auto& file = (*files)[static_cast<std::size_t>(index)];
    const std::string encodedData = stringMember(file, "dataBase64");
    const auto maximumEncodedBytes =
        ((static_cast<std::size_t>(maximumBytes) + 2) / 3) * 4;
    if (encodedData.size() > maximumEncodedBytes) {
        throw std::length_error("uploaded file exceeds the configured size limit");
    }
    const std::string data = encoding::base64Decode(encodedData);
    if (data.size() > static_cast<std::size_t>(maximumBytes)) {
        throw std::length_error("uploaded file exceeds the configured size limit");
    }
    if (destinationValue.empty() || destinationValue.find('\\') != std::string::npos) {
        throw std::invalid_argument("upload destination must be a relative path");
    }
    const auto relative = pathFromUtf8(destinationValue);
    if (relative.is_absolute() || relative.has_root_name()) {
        throw std::invalid_argument("upload destination must be a relative path");
    }
    for (const auto& component : relative) {
        if (component == ".." || component == ".") {
            throw std::invalid_argument("upload destination may not contain dot segments");
        }
    }
    std::error_code error;
    const auto root = std::filesystem::canonical(pathFromUtf8(rootValue), error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
        throw std::runtime_error("upload root is not an accessible directory");
    }
    const auto target = root / relative;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) throw std::runtime_error("could not create upload destination directory");
    const auto parent = std::filesystem::canonical(target.parent_path(), error);
    if (error || !withinRoot(root, parent)) {
        throw std::invalid_argument("upload destination escapes its configured root");
    }
    writeNewUpload(target, data);
    return pathToUtf8(target);
}

} // namespace zplus::web
